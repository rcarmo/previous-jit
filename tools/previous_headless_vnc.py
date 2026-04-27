#!/usr/bin/env python3
import argparse
import os
import re
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageOps


RETURN_KEYSYM = 0xFF0D


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError(f"short read: wanted {size}, got {len(data)}")
        data.extend(chunk)
    return bytes(data)


class VNCClient:
    def __init__(self, host: str, port: int, outdir: Path, timeout: int = 180):
        self.host = host
        self.port = port
        self.outdir = outdir
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self.width = 0
        self.height = 0

    def connect(self) -> None:
        deadline = time.time() + self.timeout
        last_error = None
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=2)
                self.sock.settimeout(self.timeout)
                break
            except OSError as exc:
                last_error = exc
                time.sleep(1)
        if self.sock is None:
            raise RuntimeError(f"failed to connect to VNC {self.host}:{self.port}: {last_error}")

        proto = recv_exact(self.sock, 12)
        self.sock.sendall(proto)
        sec_count = recv_exact(self.sock, 1)[0]
        sec_types = recv_exact(self.sock, sec_count)
        if 1 not in sec_types:
            raise RuntimeError(f"VNC server does not offer security type 1: {list(sec_types)}")
        self.sock.sendall(b"\x01")
        sec_result = recv_exact(self.sock, 4)
        if sec_result != b"\x00\x00\x00\x00":
            raise RuntimeError(f"VNC security failed: {sec_result!r}")
        self.sock.sendall(b"\x01")
        init = recv_exact(self.sock, 24)
        self.width, self.height = struct.unpack(">HH", init[:4])
        name_len = struct.unpack(">I", init[20:24])[0]
        recv_exact(self.sock, name_len)
        self.sock.sendall(struct.pack(">BBH", 2, 0, 1) + struct.pack(">i", 0))

    def _ensure_sock(self) -> socket.socket:
        if self.sock is None:
            raise RuntimeError("VNC client not connected")
        return self.sock

    def capture(self, tag: str) -> str:
        sock = self._ensure_sock()
        sock.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, self.width, self.height))
        while True:
            msg_type = recv_exact(sock, 1)[0]
            if msg_type == 0:
                break
            if msg_type == 2:
                count = recv_exact(sock, 1)[0]
                recv_exact(sock, 2)
                recv_exact(sock, 4 * count)
                continue
            if msg_type == 3:
                recv_exact(sock, 7)
                continue
            raise RuntimeError(f"unsupported VNC server message: {msg_type}")

        recv_exact(sock, 1)  # padding
        rect_count = struct.unpack(">H", recv_exact(sock, 2))[0]
        canvas = Image.new("RGBA", (self.width, self.height))
        for _ in range(rect_count):
            x, y, w, h, enc = struct.unpack(">HHHHi", recv_exact(sock, 12))
            if enc != 0:
                raise RuntimeError(f"unsupported VNC encoding: {enc}")
            raw = recv_exact(sock, w * h * 4)
            piece = Image.frombytes("RGBA", (w, h), raw, "raw", "BGRA")
            canvas.paste(piece, (x, y))

        png_path = self.outdir / f"{tag}.png"
        prep_path = self.outdir / f"{tag}-ocr-prep.png"
        txt_base = self.outdir / f"{tag}-ocr"
        txt_path = self.outdir / f"{tag}-ocr.txt"
        canvas.save(png_path)
        grey = ImageOps.autocontrast(canvas.convert("L").resize((canvas.width * 2, canvas.height * 2)))
        bw = grey.point(lambda px: 255 if px > 170 else 0)
        bw.save(prep_path)
        subprocess.run([
            "/usr/bin/tesseract",
            str(prep_path),
            str(txt_base),
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        return txt_path.read_text(errors="replace") if txt_path.exists() else ""

    def press(self, keysym: int, delay: float = 0.05) -> None:
        sock = self._ensure_sock()
        sock.sendall(struct.pack(">BBHI", 4, 1, 0, keysym))
        time.sleep(delay)
        sock.sendall(struct.pack(">BBHI", 4, 0, 0, keysym))
        time.sleep(delay)

    def type_ascii(self, text: str, delay: float = 0.05) -> None:
        for ch in text:
            self.press(ord(ch), delay)

    def command(self, text: str | None = None) -> None:
        if text:
            self.type_ascii(text)
        self.press(RETURN_KEYSYM)


DESKTOP_RE = re.compile(r"Workspace|File Viewer", re.IGNORECASE)
INSTALL_RE = re.compile(r"Install\s+NEXTSTEP|Restart", re.IGNORECASE)


def write_result(outdir: Path, values: dict[str, str]) -> None:
    result = outdir / "result.env"
    with result.open("w", encoding="utf-8") as f:
        for key, value in values.items():
            f.write(f"{key}={value}\n")



def main() -> int:
    parser = argparse.ArgumentParser(description="Drive Previous headless over VNC until desktop or failure")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--boot-wait", type=int, default=130)
    parser.add_argument("--shell-wait", type=int, default=5)
    parser.add_argument("--fsck-wait", type=int, default=90)
    parser.add_argument("--desktop-timeout", type=int, default=1200)
    parser.add_argument("--desktop-poll", type=int, default=30)
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    client = VNCClient(args.host, args.port, outdir)
    client.connect()

    status: dict[str, str] = {
        "desktop_reached": "0",
        "install_activated": "0",
        "final_tag": "",
    }

    time.sleep(args.boot_wait)
    client.capture("boot_prompt")
    client.command("")
    time.sleep(args.shell_wait)
    client.capture("shell_prompt")
    client.command("fsck -y /dev/rsd0a")
    time.sleep(args.fsck_wait)
    client.capture("after_fsck")
    client.command("exit")

    elapsed = 0
    poll_index = 0
    install_activated = False
    while elapsed < args.desktop_timeout:
        time.sleep(args.desktop_poll)
        elapsed += args.desktop_poll
        poll_index += 1
        tag = f"desktop_{poll_index:02d}"
        text = client.capture(tag)
        status["final_tag"] = tag
        if DESKTOP_RE.search(text):
            status["desktop_reached"] = "1"
            write_result(outdir, status)
            print(f"METRIC desktop_reached=1")
            print(f"METRIC desktop_tag={tag}")
            return 0
        if INSTALL_RE.search(text) and not install_activated:
            client.command("")
            install_activated = True
            status["install_activated"] = "1"

    write_result(outdir, status)
    print("METRIC desktop_reached=0")
    if status["final_tag"]:
        print(f"METRIC desktop_tag={status['final_tag']}")
    return 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
