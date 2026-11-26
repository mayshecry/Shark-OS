import argparse
import binascii
from pathlib import Path
import shutil
import struct
import subprocess

p = argparse.ArgumentParser()
p.add_argument('stage', type=Path)
p.add_argument('limine', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
work = a.output.parent
work.mkdir(exist_ok=True)
volume = work / 'esp.fat'
disk = work / 'disk.img'
used = sum(f.stat().st_size for f in a.stage.rglob('*') if f.is_file())
size = max(64, (used >> 20) + 16) << 20
with volume.open('wb') as f:
    f.truncate(size)
subprocess.run(['mformat', '-i', volume, '-F', '-H', '2048', '-v', 'SHARKOS', '::'], check=True)
# Do not duplicate the ISO-only El Torito images in the installed ESP.
import tempfile
with tempfile.TemporaryDirectory(dir=work) as tmp:
    tree = Path(tmp) / 'esp'
    shutil.copytree(a.stage, tree, ignore=shutil.ignore_patterns('limine-*-cd.bin'))
    subprocess.run(['mcopy', '-s', '-i', volume, *sorted(tree.iterdir()), '::/'], check=True)
with disk.open('wb') as f, volume.open('rb') as v:
    mbr = bytearray(512)
    struct.pack_into('<B3sB3sII', mbr, 446, 128, b'\xfe\xff\xff', 239, b'\xfe\xff\xff', 2048, size // 512)
    base = 2048 + size // 512
    sectors = 1 + 2 * 8192
    struct.pack_into('<B3sB3sII', mbr, 462, 0, b'\xfe\xff\xff', 0xda, b'\xfe\xff\xff', base, sectors)
    mbr[510:] = b'\x55\xaa'
    f.write(mbr)
    f.seek(1 << 20)
    shutil.copyfileobj(v, f)
    f.write(b'SHARKFS2' + (work / 'volume-id').read_bytes())
    f.truncate((base + sectors) * 512)
subprocess.run([a.limine.resolve(), 'bios-install', disk], check=True)
crc = 0
with disk.open('rb') as f:
    while chunk := f.read(1 << 20):
        crc = binascii.crc32(chunk, crc)
tmp = a.output.with_suffix('.tmp')
with tmp.open('wb') as f, disk.open('rb') as src:
    f.write(struct.pack('<8sII', b'SHARKIMG', disk.stat().st_size, crc))
    shutil.copyfileobj(src, f)
tmp.replace(a.output)
