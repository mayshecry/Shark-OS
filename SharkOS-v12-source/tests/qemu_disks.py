"""Boot the desktop ISO with disposable or absent disks; never attach a host device."""
from pathlib import Path
import subprocess
import tempfile
import time


def check(work, machine, size, count, ahci):
    serial = work / 'serial'
    serial.unlink(missing_ok=True)
    cmd = ['qemu-system-x86_64', '-machine', machine, '-m', '512', '-display', 'none',
           '-nic', 'none', '-monitor', 'none', '-serial', f'file:{serial}', '-no-reboot',
           '-boot', 'order=d', '-cdrom', 'sharkos.iso']
    if size:
        disk = work / 'blank.raw'
        with disk.open('wb') as f:
            f.truncate(size)
        cmd += ['-drive', f'format=raw,if=ide,index=0,snapshot=on,file={disk}']
    with subprocess.Popen(cmd) as p:
        try:
            end = time.monotonic() + 100
            while p.poll() is None and time.monotonic() < end:
                log = serial.read_text(errors='replace') if serial.exists() else ''
                if '[setup] ready' in log:
                    break
                time.sleep(.1)
            assert '[setup] ready' in log, log[-1000:]
            assert f'[disks] verified={count} ahci={ahci}' in log, log[-2000:]
            entries = [x for x in log.splitlines() if x.startswith('[disk]')]
            assert len(entries) == count, entries
            if count:
                assert entries[0].endswith(f'bytes={size}'), entries
            print(machine, size, entries, flush=True)
        finally:
            p.terminate()


with tempfile.TemporaryDirectory() as tmp:
    for args in [('pc', 0, 0, 0), ('q35', 0, 0, 1), ('pc', 128 << 20, 1, 0), ('pc', 1 << 40, 1, 0), ('q35', 1 << 40, 1, 1)]:
        check(Path(tmp), *args)
