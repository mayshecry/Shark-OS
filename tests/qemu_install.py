import argparse
import contextlib
import mmap
import hashlib
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import threading
import struct
import zlib
import http.server

parser = argparse.ArgumentParser()
parser.add_argument("--firmware-dir", type=Path, default=Path("/usr/share/OVMF"))
parser.add_argument("--efi-ram",default=512,type=int)
parser.add_argument("--iso-ram",default=512,type=int)
parser.add_argument("--boot-timeout",default=180,type=int)
parser.add_argument("--bios-ram", default=128, type=int)
parser.add_argument('--ahci',action='store_true')
args = parser.parse_args()


class Guest:
    def __init__(self, proc, monitor, log):
        self.proc, self.monitor, self.log = proc, monitor, log

    def wait(self, text, timeout=180):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and self.proc.poll() is None:
            if self.log.exists() and text in self.log.read_bytes():
                return
            time.sleep(0.1)
        import os
        if os.getenv("FAIL_FRAME"): self.command("screendump "+os.environ["FAIL_FRAME"])
        raise RuntimeError(f"{text!r} not found: {self.log.read_bytes()[-2000:]!r}")

    def command(self, cmd):
        self.monitor.sendall((cmd + "\n").encode())
        output = b""
        while b"(qemu)" not in output:
            piece = self.monitor.recv(8192)
            if not piece:
                raise RuntimeError("monitor closed")
            output += piece
        time.sleep(0.12)

    def keys(self, *keys):
        for key in keys:
            self.command("sendkey " + key + " 45")


@contextlib.contextmanager
def boot(work, disk, efi=False, live=False):
    label = ("UEFI" if efi else "BIOS") + ("-ISO" if live else "-disk")
    log = work / (label + ".log")
    if log.exists():
        log.unlink()
    mon = work / (label + ".sock")
    cmd = [
        "qemu-system-x86_64",
        "-machine", "q35" if args.ahci else "pc",
        "-m",
        str(args.iso_ram if live else args.efi_ram if efi else args.bios_ram),
        "-cpu", "qemu64", "-smp", "1",
        "-nic",
        "none" if live else "user,model=rtl8139",
        "-display",
        "none",
        "-no-reboot",
        "-drive",
        f"if=ide,index=0,format=raw,file={disk}",
        "-serial",
        f"file:{log}",
        "-monitor",
        f"unix:{mon},server=on,wait=off",
    ]
    if efi:
        varfile = work / (label + ".fd")
        shutil.copyfile(args.firmware_dir / "OVMF_VARS_4M.fd", varfile)
        cmd.extend(
            [
                "-drive",
                f"if=pflash,format=raw,readonly=on,file={args.firmware_dir}/OVMF_CODE_4M.fd",
                "-drive",
                f"if=pflash,format=raw,file={varfile}",
            ]
        )
    if live:
        cmd.extend(["-boot", "order=d", "-cdrom", "sharkos.iso"])
    with subprocess.Popen(cmd) as proc, socket.socket(socket.AF_UNIX) as sock:
        try:
            for _ in range(100):
                if mon.exists() or proc.poll() is not None:
                    break
                time.sleep(0.05)
            sock.settimeout(5)
            sock.connect(str(mon))
            sock.recv(8192)
            guest = Guest(proc, sock, log)
            guest.wait(b"[setup] ready" if live else b"[installed] desktop boot",timeout=args.boot_timeout)
            print(label, "reached setup" if live else "skipped setup", flush=True)
            yield guest
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


def chunk(kind, data):
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data))
    )


png = (
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", struct.pack(">IIBBBBB", 12, 6, 8, 2, 0, 0, 0))
    + chunk(b"IDAT", zlib.compress((b"\0" + b"\x10\xbc\xc9" * 12) * 6))
    + chunk(b"IEND", b"")
)


class Fixture(http.server.BaseHTTPRequestHandler):
    wrong_fallback = False
    def do_GET(self):
        body, mime = png, "image/png"
        if self.path.startswith("/pool"):
            number = "2" if self.path.startswith("/pool2") else "1"
            script = "var obj={}; var sum=0;" + "".join(
                f"obj.k{i}={i};sum+=obj.k{i};" for i in range(600)
            )
            body = (
                "<html><body>Pool test<script>"
                + script
                + f"if(sum===179700)console.log('pool-pass-{number}');</script></body></html>"
            ).encode()
            mime = "text/html"
        if self.path.startswith('/noscript'):
            body = b'<html><head><noscript><meta http-equiv="refresh" content="0;url=/wrong-fallback"></noscript></head><body><script>console.log("fallback-test-loaded");</script></body></html>'
            mime = 'text/html'
        if self.path.startswith('/wrong-fallback'): Fixture.wrong_fallback = True
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def type_text(g, text):
    mapping = {
        "/": "slash",
        ":": "shift-semicolon",
        ".": "dot",
        " ": "spc",
        "-": "minus",
    }
    g.keys(*(mapping.get(c, "shift-"+c.lower() if c.isupper() else c) for c in text), "ret")


def open_terminal(guest):
    guest.wait(b"[boot] boot screen hide complete")
    time.sleep(0.6)
    guest.command("mouse_move -74 5")
    for _ in range(2):
        guest.command("mouse_button 1")
        guest.command("mouse_button 0")


def check_tree(disk):
    with disk.open("rb") as f:
        f.seek(470)
        (base,) = struct.unpack("<I", f.read(4))
        states = []
        for slot in range(2):
            f.seek((base + 1 + slot * 8192) * 512)
            header = f.read(512)
            magic, gen, n, crc, count, tile, seal = struct.unpack_from("<7I", header)
            if magic != 0x32534653:
                continue
            data = f.read(n)
            assert zlib.crc32(header[:24]) == seal and zlib.crc32(data) == crc
            paths = []
            tree = {}
            pos = 0
            for _ in range(count):
                parent, kind, perm, length, name = struct.unpack_from(
                    "<BB2xII32s", data, pos
                )
                pos += 44
                path = (
                    ""
                    if parent == 255
                    else paths[parent] + "/" + name.split(b"\0")[0].decode()
                )
                paths.append(path)
                tree[path] = data[pos : pos + length]
                pos += length
            assert pos == n
            states.append((gen, tree, tile))
        return max(states, key=lambda state: state[0])[1:]


def install_regression():
    with tempfile.TemporaryDirectory() as temp, http.server.HTTPServer(
        ("127.0.0.1", 0), Fixture
    ) as fixture:
        threading.Thread(target=fixture.serve_forever, daemon=True).start()
        work = Path(temp)
        disk = work / "disposable.img"
        with disk.open("wb") as f:
            f.truncate(128 << 20)
        with boot(work, disk, efi=True, live=True) as guest:
            guest.keys(*(["down"] * 5), "ret", "0", "x", "ret")
            guest.wait(b"Confirmation did not match")
            with disk.open("rb") as f, mmap.mmap(
                f.fileno(), 0, access=mmap.ACCESS_READ
            ) as image:
                for i in range(0, len(image), 1 << 20):
                    assert image[i : i + (1 << 20)] == bytes(1 << 20)
            guest.keys("ret", "ret", "0", *"install", "spc", "0", "ret")
            guest.wait(b"Installed. Remove the live media", timeout=300)
        with disk.open("rb") as dst, Path("install-build/disk.img").open("rb") as src:
            while data := src.read(1 << 20):
                assert data == dst.read(len(data))
            while data := dst.read(1 << 20):
                assert data == bytes(len(data))
        print("Verified installed bytes and untouched disk tail", flush=True)
        for efi in (False, True):
            with boot(work, disk, efi=efi) as guest:
                assert b"[setup] ready" not in guest.log.read_bytes()
                open_terminal(guest)
                guest.wait(b"[desktop] free KiB=")
                print(
                    guest.log.read_bytes()
                    .split(b"[desktop] free KiB=")[-1]
                    .splitlines()[0]
                    .decode(),
                    "KiB free after first render",
                    flush=True,
                )
                guest.wait(b"[prefs] packages=17 theme=1" if efi else b"[prefs] packages=0 theme=0")
                if not efi:
                    type_text(guest,"spkg install ide")
                    type_text(guest,"spkg install pong")
                    type_text(guest,"appearance 1")
                # Copy a full command, erase it, paste and explicitly submit.
                command="cc -c square.c"
                guest.keys(*[{' ':'spc','-':'minus','.':'dot'}.get(c,c) for c in command], 'ctrl-c', *(['backspace']*len(command)), 'ctrl-v', 'ret')
                guest.wait(b"[cc] ELF64 object: square.o", timeout=20)
                if not efi:
                    type_text(guest, "cc hello.c")
                    guest.wait(b"[cc] Linked application: hello", timeout=10)
                    type_text(guest, "cc square.c -o hello")
                type_text(guest, "./hello")
                guest.wait(b"[run] ./hello returned 0x00000000", timeout=10)
                print(
                    "Ran linked hello" + (" after firmware change" if efi else ""),
                    flush=True,
                )
                guest.keys("f2-g")
                guest.wait(b"[wm] tiling" if efi else b"[wm] floating", timeout=8)
                guest.keys(*"sync", "ret")
                time.sleep(0.6)
                if not efi:
                    type_text(
                        guest,
                        "browser http://10.0.2.2:"
                        + str(fixture.server_port)
                        + "/ocean.png",
                    )
                else:
                    type_text(guest, "explorer")
                    guest.keys(*(["down"] * 5), "ret", "down", "ctrl-c", "ctrl-v", "up", "ret")
                guest.wait(b"[picture] decoded 12x6", timeout=35)
                guest.keys("ctrl-s")
                guest.wait(
                    b"Saved /Downloads/ocean" + (b"-1" if efi else b"") + b".png",
                    timeout=10,
                )
                print("Guest decoded and saved image", flush=True)
                if not efi:
                    for n in (1, 2):
                        guest.keys("ctrl-l")
                        type_text(guest, f"http://10.0.2.2:{fixture.server_port}/pool{n}")
                        guest.wait(f"js: pool-pass-{n}".encode(), timeout=25)
                    guest.keys("ctrl-l")
                    type_text(guest, f"http://10.0.2.2:{fixture.server_port}/noscript")
                    guest.wait(b"js: fallback-test-loaded")
                    time.sleep(2); assert not Fixture.wrong_fallback
                    print("Guest executed two cross-block JavaScript pages", flush=True)
            tree, tile = check_tree(disk)
            assert (
                tile == int(efi)
                and tree["/Downloads/ocean.png"] == png
                and tree["/sharkuser/square.o"].startswith(b"\x7fELF")
            )
            if efi:
                assert tree["/Downloads/ocean-1.png"] == png
                assert tree["/Downloads/Copy1-ocean.png"] == png

        def digest():
            h = hashlib.sha256()
            with disk.open("rb") as f:
                while data := f.read(1 << 20):
                    h.update(data)
            return h.digest()

        before = digest()
        with boot(work, disk, live=True) as guest:
            guest.wait(b"Live session (RAM only)")
            guest.keys("esc")
            open_terminal(guest)
            type_text(guest, "cc -c square.c")
            guest.wait(b"[cc] ELF64 object: square.o", timeout=10)
            type_text(guest, "sync")
            time.sleep(2.5)
        assert digest() == before
        print("Live session left attached installed disk byte-identical", flush=True)
        with disk.open("r+b") as f:
            f.seek(470)
            (base,) = struct.unpack("<I", f.read(4))
            banks = []
            for slot in range(2):
                offset = (base + 1 + slot * 8192) * 512
                f.seek(offset + 4)
                banks.append((struct.unpack("<I", f.read(4))[0], offset))
            offset = max(banks)[1] + 512
            f.seek(offset)
            b = f.read(1)
            f.seek(offset)
            f.write(bytes([b[0] ^ 1]))
        with boot(work, disk) as guest:
            guest.wait(b"Installed: saved", timeout=5)
        with disk.open("r+b") as f:
            for _, offset in banks:
                f.seek(offset)
                f.write(b"bad!")
        before = digest()
        with boot(work, disk) as guest:
            guest.wait(b"Data volume unreadable (RAM only)", timeout=5)
            guest.wait(b"[boot] boot screen hide complete")
            time.sleep(2.5)
        assert digest() == before
        print("Recovered older bank; two invalid banks left untouched", flush=True)
        fixture.shutdown()

if __name__ == "__main__":
    install_regression()
