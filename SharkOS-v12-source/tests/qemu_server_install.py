"""Install onto disposable sparse files, then boot without the ISO."""
import contextlib
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import argparse
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--ahci', action='store_true')
parser.add_argument('flavors', nargs='*')
args = parser.parse_args()
prompt = b'root@shark:/# '

@contextlib.contextmanager
def boot(work, disk, image=None, efi=False):
    cmd = ['qemu-system-x86_64', '-machine', 'q35' if args.ahci else 'pc', '-m', '512', '-display', 'none', '-nic', 'none',
           '-serial', 'stdio', '-monitor', 'none', '-no-reboot']
    if efi:
        nv = work / 'vars.fd'
        shutil.copyfile('/usr/share/OVMF/OVMF_VARS_4M.fd', nv)
        cmd += ['-drive', 'if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd',
                '-drive', f'if=pflash,format=raw,file={nv}']
    if disk:
        cmd += ['-drive', f'if=ide,index=0,format=raw,file={disk}']
    if image:
        cmd += ['-boot', 'order=d', '-cdrom', image]
    with subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE) as p:
        def read():
            data = bytearray()
            deadline = time.monotonic() + 160
            while prompt not in data and time.monotonic() < deadline and p.poll() is None:
                if select.select([p.stdout], [], [], 1)[0]:
                    data += os.read(p.stdout.fileno(), 8192)
            assert prompt in data, data[-2000:]
            return data
        def send(cmd):
            for c in cmd.encode() + b'\r':
                p.stdin.write(bytes([c])); p.stdin.flush(); time.sleep(.012)
            return read()
        try:
            yield read(), send
        finally:
            p.terminate()


def run(flavor):
    image = 'sharkos-' + flavor + '.iso'
    efi = flavor == 'server'
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        disk = work / 'disk.raw'
        with disk.open('wb') as f: f.truncate(128 << 20)
        with boot(work, None, image) as (_, send):
            assert b'no verified' in send('install').lower()
            assert b'No data written' in send('install 0')
        with boot(work, disk, image, efi) as (intro, send):
            assert b'Type install' in intro
            listing = send('install')
            assert b'134217728' in listing, listing
            if args.ahci: assert b'AHCI 0:' in listing and b'SATA DMA48' in listing, listing
            assert b'Type INSTALL 0' in send('install 0')
            assert b'Cancelled' in send('no')
            assert not any(disk.read_bytes())
            assert b'Type INSTALL 0' in send('install 0')
            result = send('INSTALL 0'); assert b'Installed. Remove' in result, result[-1200:]
        with boot(work, disk) as (intro, send):
            assert b'Installed: new volume' in intro, intro[-2000:]
            assert b'Type install' not in intro
            send('touch /persist-marker')
            result = send('sync'); assert b'Installed: saved' in result, result
        with boot(work, disk, efi=efi) as (intro, send):
            assert b'Installed: saved' in intro, intro[-2000:]
            assert b'persist-marker' in send('ls /')
            assert b'Boot the server ISO' in send('install')
        if args.ahci:
            # A nonzero sector beyond the 32-bit LBA boundary must block installation.
            with disk.open('wb') as f:
                f.seek((2 << 40) + 511); f.write(b'X')
            with boot(work, disk, image) as (_, send):
                assert b'Type INSTALL 0' in send('install 0')
                result = send('INSTALL 0'); assert b'Disk tail unreadable or in use' in result, result
            with disk.open('rb') as f:
                assert f.read(512) == bytes(512)
                f.seek(-1, 2); assert f.read() == b'X'
        print(flavor, 'no disk, cancellation, install, ISO-free boot, sync and persistence passed', flush=True)

for flavor in args.flavors or ['server', 'server32']:
    run(flavor)
