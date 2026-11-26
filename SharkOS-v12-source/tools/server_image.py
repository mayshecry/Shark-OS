"""Build a non-recursive server disk image, then configure the live ISO."""
from pathlib import Path
import secrets
import subprocess
import sys

config, stage, limine, work = map(Path, sys.argv[1:])
work.mkdir(exist_ok=True)
token = secrets.token_hex(16)
(work / 'volume-id').write_text(token)
pack = stage / 'boot/install.pack'
pack.unlink(missing_ok=True)
compressed = pack.with_suffix(".pack.gz")
compressed.unlink(missing_ok=True)
options, entry = config.read_text().strip().split('\n\n', 1)
name, kernel = entry.split('\n', 1)

def entries(title, extra):
    bios = title + '\n' + kernel + extra
    if '32' in config.name:
        return bios
    efi = bios.replace('/sharkos-server.bin', '/sharkos-server-uefi.bin')
    return bios + '\n    if_fw_type: BIOS\n\n' + efi + '\n    if_fw_type: UEFI'

def write(body):
    for f in ('limine.conf', 'limine.cfg', 'boot/limine.cfg', 'boot/limine/limine.conf', 'boot/limine/limine.cfg'):
        (stage / f).write_text(options + '\n\n' + body + '\n')

write(entries(name, '\n    cmdline: sharkfs=' + token))
subprocess.run([sys.executable, 'tools/install_image.py', stage, limine, work / 'install.pack'], check=True)
with compressed.open('wb') as f:
    subprocess.run(['gzip','-n','-6','-c',work / 'install.pack'], stdout=f, check=True)
write(entries(name + ' - Live + CLI installer', '\n    module_path: $boot():/boot/install.pack.gz') + '\n\n' +
      entries(name + ' - Live only (lower RAM)', ''))
