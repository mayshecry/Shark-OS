"""Install to an exactly-sized disposable target, comparing every sector."""
import tempfile,hashlib,os
from pathlib import Path
from qemu_install import boot,open_terminal,type_text,args
src=Path('install-build/disk.img');efi=os.environ.get('MEM_PROFILE')=='efi'
with tempfile.TemporaryDirectory() as td:
 w=Path(td);disk=w/'target.img';size=src.stat().st_size
 with disk.open('wb') as f:f.truncate(size)
 with boot(w,disk,efi=efi,live=True) as g:
    g.keys(*(['down']*5),'ret','0',*'install','spc','0','ret');g.wait(b'Installed. Remove the live media',100)
 def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while data:=f.read(1<<20):h.update(data)
    return h.digest()
 assert digest(src)==digest(disk);print(f'Install verified: {size} bytes; {args.iso_ram} MiB RAM',flush=True)
 with boot(w,disk,efi=efi) as g:
    open_terminal(g);type_text(g,'cc hello.c');type_text(g,'./hello');g.wait(b'[run] ./hello returned 0x00000000',20)
