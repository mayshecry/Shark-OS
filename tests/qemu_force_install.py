"""Retry a refused reinstall on a disposable used disk; never use host devices."""
import hashlib, tempfile, os
from pathlib import Path
from qemu_install import boot, open_terminal, type_text
source=Path('install-build/disk.img');size=source.stat().st_size;efi=os.getenv('MEM_PROFILE')=='efi'
with tempfile.TemporaryDirectory() as td:
    w=Path(td);disk=w/'used.img'
    with disk.open('wb') as f:
        f.truncate(size+8192);f.write(b'previous partition table');f.seek(size);f.write(b'outside install area');f.seek(-512,2);f.write(b'EFI PART')
    before=hashlib.sha256(disk.read_bytes()).digest()
    with boot(w,disk,efi=efi,live=True) as g:
        g.keys(*(['down']*5),'ret','0');type_text(g,'install 0');g.wait(b'Erase and reinstall?',20)
        type_text(g,'erase 1');g.wait(b'Cancelled. No data written.',8);assert hashlib.sha256(disk.read_bytes()).digest()==before
        g.keys('ret','ret','0');type_text(g,'install 0');g.keys('esc');g.keys('ret','ret','0');type_text(g,'install 0')
        assert hashlib.sha256(disk.read_bytes()).digest()==before
        type_text(g,'erase 0');g.wait(b'Installed. Remove the live media',100)
    with disk.open('rb') as f:
        assert f.read(size)==source.read_bytes();assert f.read(20)==b'outside install area';f.seek(-512,2);assert f.read()==bytes(512)
    with boot(w,disk,efi=efi) as g:
        open_terminal(g);type_text(g,'cc hello.c');type_text(g,'./hello');g.wait(b'[run] ./hello returned 0x00000000',20)
    print('Wrong disk phrase and Esc made zero changes. Confirmed replacement verified and booted.')
