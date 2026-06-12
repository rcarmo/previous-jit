#!/usr/bin/env python3
"""Connect to a running Previous VNC server, perform RFB handshake, sample
encoding negotiation and (optionally) VNC bytes/sec under scripted cursor
motion.  Intended for quick smoke checks of the VNC server tunings."""
import argparse, json, socket, struct, sys, time

p = argparse.ArgumentParser()
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=5901)
p.add_argument('--motion', type=int, default=0,
               help='If >0, send PointerEvent sweeps for N seconds and report '
                    'bytes/sec on the wire.')
args = p.parse_args()

s = socket.socket(); s.connect((args.host, args.port)); s.settimeout(5)

def recvn(n):
    b = b''
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError(f'got {len(b)}/{n}')
        b += c
    return b

ver = recvn(12); s.sendall(b'RFB 003.008\n')
n = recvn(1)[0]; sectypes = recvn(n)
print(f'server: {ver.decode().strip()} sectypes={list(sectypes)}')
s.sendall(b'\x01')  # security type: None
status = struct.unpack('>I', recvn(4))[0]
if status:
    print('auth failed', status); sys.exit(1)
s.sendall(b'\x01')  # ClientInit shared
hdr = recvn(24)
w, h = struct.unpack('>HH', hdr[:4]); bpp = hdr[4]
rmax, gmax, bmax = struct.unpack('>HHH', hdr[8:14])
rsh, gsh, bsh = hdr[14], hdr[15], hdr[16]
nlen = struct.unpack('>I', hdr[20:24])[0]; name = recvn(nlen)
print(f'fb: {w}x{h} bpp={bpp} name={name.decode()!r}')
print(f'  R(max={rmax} shift={rsh})  G(max={gmax} shift={gsh})  B(max={bmax} shift={bsh})')

# Use Raw-only so we can size every rect deterministically.
encs = [0, -223, -239]
s.sendall(struct.pack('>BBH', 2, 0, len(encs)) +
          b''.join(struct.pack('>i', e) for e in encs))

def drain():
    hdr = recvn(4)
    _, _, nrects = struct.unpack('>BBH', hdr)
    bytes_total = 4
    for _ in range(nrects):
        rhdr = recvn(12); bytes_total += 12
        x, y, rw, rh, enc = struct.unpack('>HHHHi', rhdr)
        if enc == 0:
            nb = rw*rh*(bpp//8); recvn(nb); bytes_total += nb
        elif enc == -223:
            pass
        elif enc == -239:
            nb = rw*rh*(bpp//8) + ((rw+7)//8)*rh; recvn(nb); bytes_total += nb
        else:
            raise RuntimeError(f'unexpected enc {enc}')
    return bytes_total, nrects

s.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, w, h))
b, r = drain()
print(f'initial full update: {b} bytes, {r} rects')

if args.motion > 0:
    print(f'sampling motion for {args.motion}s ...')
    s.settimeout(0.05)
    total_b = total_r = updates = 0
    t0 = time.time()
    s.sendall(struct.pack('>BBHHHH', 3, 1, 0, 0, w, h))
    while time.time() - t0 < args.motion:
        # Slowly sweep cursor diagonally to keep dirty rects coming.
        t = time.time() - t0
        px = int(200 + 600 * (0.5 + 0.5 * (t % 4 - 2) / 2))
        py = int(200 + 400 * (0.5 + 0.5 * ((t * 1.3) % 4 - 2) / 2))
        s.sendall(struct.pack('>BBHH', 5, 0,
                              max(10, min(w-10, px)),
                              max(10, min(h-10, py))))
        try:
            b, r = drain()
            total_b += b; total_r += r; updates += 1
            s.sendall(struct.pack('>BBHHHH', 3, 1, 0, 0, w, h))
        except (socket.timeout, TimeoutError):
            time.sleep(0.01)
    dt = time.time() - t0
    print(json.dumps({
        'motion_secs': dt,
        'bytes_total': total_b,
        'rects_total': total_r,
        'updates': updates,
        'bytes_per_sec': total_b/dt,
        'updates_per_sec': updates/dt,
    }, indent=2))

s.close()
