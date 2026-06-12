#!/usr/bin/env python3
"""Boot a Previous binary headless, OCR-poll until 'File Viewer' appears,
record boot time, then measure VNC bytes/sec + emulator CPU% over a
fixed window (idle, then scripted cursor activity).  Returns one CSV row."""
import argparse, json, os, signal, socket, struct, subprocess, sys, time, shutil

p = argparse.ArgumentParser()
p.add_argument('--label', required=True)
p.add_argument('--binary', required=True)
p.add_argument('--display', required=True)      # e.g. :199
p.add_argument('--vnc-port', type=int, required=True)
p.add_argument('--rundir', required=True)
p.add_argument('--jit', choices=['on', 'off'], required=True)
p.add_argument('--asset-root', default='/workspace/assets/previous')
p.add_argument('--boot-timeout', type=int, default=600)
p.add_argument('--idle-secs', type=int, default=20)
p.add_argument('--motion-secs', type=int, default=20)
p.add_argument('--motion-rate-hz', type=int, default=10)
args = p.parse_args()

ROOT = '/workspace/projects/previous'
RUNDIR = args.rundir
os.makedirs(f'{RUNDIR}/home/.previous', exist_ok=True)

src_imgs = sorted([f for f in os.listdir(f'{args.asset_root}/images') if f.startswith('nextstep33-system-en-backup-') and f.endswith('.img')], reverse=True)
if not src_imgs:
    sys.exit('no backup image')
src_img = f'{args.asset_root}/images/{src_imgs[0]}'
run_img = f'{RUNDIR}/nextstep33-system-en-run.img'
if not os.path.exists(run_img):
    subprocess.run(['cp', '--sparse=always', '--reflink=auto', src_img, run_img], check=True)

cfg = f"""[Log]
sLogFileName = stderr
nTextLogLevel = 5
nAlertDlgLogLevel = 1
bConfirmQuit = FALSE
[ConfigDialog]
bShowConfigDialogAtStartup = FALSE
[Screen]
bFullScreen = FALSE
bShowStatusbar = FALSE
bShowDriveLed = FALSE
[Keyboard]
bSwapCmdAlt = FALSE
nKeymapType = 0
[Sound]
bEnableSound = FALSE
bEnableMicrophone = FALSE
[ROM]
szRom030FileName = {args.asset_root}/roms/Rev_1.0_v41.BIN
szRom040FileName = {args.asset_root}/roms/Rev_2.5_v66.BIN
szRomTurboFileName = {args.asset_root}/roms/Rev_3.3_v74.BIN
[Boot]
nBootDevice = 1
bEnableDRAMTest = FALSE
bEnablePot = TRUE
bExtendedPot = FALSE
bEnableSoundTest = TRUE
bEnableSCSITest = TRUE
bLoopPot = FALSE
bVerbose = TRUE
[HardDisk]
szImageName0 = {run_img}
nDeviceType0 = 1
bDiskInserted0 = TRUE
bWriteProtected0 = FALSE
[Floppy]
bDriveConnected0 = FALSE
[System]
nMachineType = 1
bColor = FALSE
bTurbo = FALSE
bNBIC = TRUE
nRTC = TRUE
nCpuLevel = 4
nCpuFreq = 25
bCompatibleCpu = TRUE
bRealtime = FALSE
nDSPType = 2
bDSPMemoryExpansion = TRUE
bRealTimeClock = TRUE
n_FPUType = 68040
bCompatibleFPU = TRUE
bMMU = TRUE
[Dimension]
bEnabled = FALSE
"""
with open(f'{RUNDIR}/home/.previous/previous.cfg', 'w') as f:
    f.write(cfg)

# Spawn Xvfb
subprocess.run(['pkill', '-9', '-f', f'Xvfb {args.display}'])
time.sleep(0.5)
xvfb = subprocess.Popen(['Xvfb', args.display, '-screen', '0', '1280x900x24'],
                       stdout=open(f'{RUNDIR}/xvfb.log', 'w'),
                       stderr=subprocess.STDOUT, preexec_fn=os.setsid)
time.sleep(1)

env = os.environ.copy()
env.update({
    'HOME': f'{RUNDIR}/home',
    'SDL_AUDIODRIVER': 'dummy',
    'DISPLAY': args.display,
    'PREVIOUS_VNC': '1',
    'PREVIOUS_VNC_PORT': str(args.vnc_port),
    'PREVIOUS_RTC_UNIX_TIME': '0x2ec46472',
})
if args.jit == 'on':
    env['PREVIOUS_UAE2026_JIT'] = '1'
    env['PREVIOUS_UAE2026_JIT_RAM'] = '1'
    env['B2_JIT_RTE_FAULT_HANDOFF'] = '1'

t0 = time.time()
prev = subprocess.Popen([args.binary], env=env,
                        stdout=open(f'{RUNDIR}/previous.log', 'w'),
                        stderr=subprocess.STDOUT, preexec_fn=os.setsid)

# Poll OCR for "File Viewer" every 10s
boot_time = None
deadline = t0 + args.boot_timeout
while time.time() < deadline:
    time.sleep(10)
    try:
        png = subprocess.check_output(['env', f'DISPLAY={args.display}', 'import', '-window', 'root', 'png:-'],
                                       timeout=5, stderr=subprocess.DEVNULL)
        text = subprocess.check_output(['tesseract', '-', '-'], input=png, timeout=10,
                                       stderr=subprocess.DEVNULL).decode(errors='replace')
        # Desktop title bar shows '25MHz / 68040 / 64MB / NeXT cube'.
        # Match on at least two non-trivial tokens to avoid kernel/boot
        # text accidentally hitting a single keyword.
        tokens = sum(t in text for t in ('25MHz', '68040', '64MB', 'NeXT', 'File Viewer', 'Fle Viewer'))
        if tokens >= 2:
            boot_time = time.time() - t0
            break
    except Exception as e:
        pass
if boot_time is None:
    print(json.dumps({'label': args.label, 'boot_sec': None, 'error': 'boot_timeout'}))
    os.killpg(prev.pid, signal.SIGKILL); os.killpg(xvfb.pid, signal.SIGKILL)
    sys.exit(1)

# Settle 5s
time.sleep(5)

# Connect minimal RFB client to measure VNC bytes
def rfb_connect():
    s = socket.socket(); s.connect(('127.0.0.1', args.vnc_port))
    def recvn(n):
        b = b''
        while len(b) < n:
            c = s.recv(n - len(b))
            if not c: raise EOFError
            b += c
        return b
    recvn(12); s.sendall(b'RFB 003.008\n')
    n = recvn(1)[0]; recvn(n); s.sendall(b'\x01'); recvn(4)
    s.sendall(b'\x01'); hdr = recvn(24)
    w, h = struct.unpack('>HH', hdr[:4]); bpp = hdr[4]
    nlen = struct.unpack('>I', hdr[20:24])[0]; recvn(nlen)
    # Request Raw + NewFBSize so we can size every payload deterministically.
    encs = [0, -223, -239]
    s.sendall(struct.pack('>BBH', 2, 0, len(encs)) + b''.join(struct.pack('>i', e) for e in encs))
    return s, w, h, bpp, recvn

def drain_one_update(s, recvn, bpp):
    """Read one FramebufferUpdate message, return total payload bytes counted."""
    hdr = recvn(4)
    _, _, nrects = struct.unpack('>BBH', hdr)
    bytes_total = 4
    for _ in range(nrects):
        rhdr = recvn(12); bytes_total += 12
        x, y, rw, rh, enc = struct.unpack('>HHHHi', rhdr)
        if enc == 0:
            n = rw*rh*(bpp//8); recvn(n); bytes_total += n
        elif enc == -223:
            pass
        elif enc == -239:
            n = rw*rh*(bpp//8) + ((rw+7)//8)*rh; recvn(n); bytes_total += n
        else:
            raise RuntimeError(f'unexpected enc {enc}')
    return bytes_total, nrects

def cpu_percent_for(pid, secs):
    """Sample /proc/<pid>/stat utime+stime over secs; return CPU% (single-core
    100% basis, can exceed 100 on multi-thread)."""
    def jiff():
        with open(f'/proc/{pid}/stat') as f:
            parts = f.read().split()
        return int(parts[13]) + int(parts[14])
    clk = os.sysconf('SC_CLK_TCK')
    j0 = jiff(); t0 = time.time()
    time.sleep(secs)
    j1 = jiff(); t1 = time.time()
    return 100.0 * (j1 - j0) / clk / (t1 - t0)

# Phase 1: Idle measurement (no client input). Request incremental updates
# at 60 Hz for idle_secs, count bytes.
s, w, h, bpp, recvn = rfb_connect()
s.settimeout(0.05)
# Initial full update — drain it to clear state.
s.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, w, h))
s.settimeout(15)
initial_bytes, initial_rects = drain_one_update(s, recvn, bpp)
print(json.dumps({'label': args.label, 'phase': 'initial', 'bytes': initial_bytes, 'rects': initial_rects, 'fb_w': w, 'fb_h': h}), flush=True)

def measure_phase(name, duration, motion_callback=None):
    s.settimeout(0.05)
    total_bytes = 0
    total_rects = 0
    updates = 0
    t_start = time.time()
    # Send first incremental
    s.sendall(struct.pack('>BBHHHH', 3, 1, 0, 0, w, h))
    while time.time() - t_start < duration:
        if motion_callback:
            motion_callback(time.time() - t_start)
        # Drain any pending updates
        try:
            b, r = drain_one_update(s, recvn, bpp)
            total_bytes += b
            total_rects += r
            updates += 1
            # Request next incremental
            s.sendall(struct.pack('>BBHHHH', 3, 1, 0, 0, w, h))
        except (socket.timeout, TimeoutError):
            time.sleep(0.01)
    elapsed = time.time() - t_start
    return {'bytes_total': total_bytes, 'rects_total': total_rects,
            'updates': updates, 'elapsed': elapsed,
            'bytes_per_sec': total_bytes/elapsed, 'updates_per_sec': updates/elapsed}

# Idle phase + concurrent CPU sample
import threading
cpu_results = {}
def sample_cpu(label, secs):
    cpu_results[label] = cpu_percent_for(prev.pid, secs)
ct = threading.Thread(target=sample_cpu, args=('idle', args.idle_secs))
ct.start()
idle = measure_phase('idle', args.idle_secs)
ct.join()
idle['cpu_pct'] = cpu_results['idle']
print(json.dumps({'label': args.label, 'phase': 'idle', **idle}), flush=True)

# Motion phase: send PointerEvent sweeps
def pointer_motion(t):
    px = int(200 + 600 * (0.5 + 0.5 * (t % 4 - 2) / 2))
    py = int(200 + 400 * (0.5 + 0.5 * ((t * 1.3) % 4 - 2) / 2))
    s.sendall(struct.pack('>BBHH', 5, 0, max(10, min(w-10, px)), max(10, min(h-10, py))))

ct = threading.Thread(target=sample_cpu, args=('motion', args.motion_secs))
ct.start()
motion = measure_phase('motion', args.motion_secs, pointer_motion)
ct.join()
motion['cpu_pct'] = cpu_results['motion']
print(json.dumps({'label': args.label, 'phase': 'motion', **motion}), flush=True)

print(json.dumps({'label': args.label, 'boot_sec': boot_time, 'fb_w': w, 'fb_h': h}), flush=True)

# Teardown
s.close()
os.killpg(prev.pid, signal.SIGTERM); time.sleep(1)
try: os.killpg(prev.pid, signal.SIGKILL)
except: pass
os.killpg(xvfb.pid, signal.SIGKILL)
