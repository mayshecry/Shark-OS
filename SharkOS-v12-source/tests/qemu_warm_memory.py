"""Persist JawsWM at 1080p, measure idle after cold reboot and after closing Navigator."""
from qemu_install import args,boot,open_terminal,type_text,Fixture
from pathlib import Path
import http.server,tempfile,threading,shutil,re,time
with tempfile.TemporaryDirectory() as tmp,http.server.HTTPServer(('127.0.0.1',0),Fixture) as web:
    threading.Thread(target=web.serve_forever,daemon=True).start();w=Path(tmp);disk=w/'disk.img';shutil.copyfile('install-build/disk.img',disk)
    with boot(w,disk) as g:
        open_terminal(g);type_text(g,'wmprefer JawsWM');type_text(g,'video 1920x1080');type_text(g,'video keep');time.sleep(.5)
    with boot(w,disk,efi=True) as g:
        g.wait(b'[wm] restored JawsWM');g.wait(b'[video] mode=1920x1080');g.wait(b'[idle] free KiB=',30)
        log=lambda:g.log.read_bytes();idle=int(re.search(rb'\[idle\] free KiB=(\d+)',log())[1]);print('UEFI 1080p Jaws at idle: %.2f MiB nonfree'%(args.efi_ram-idle/1024),flush=True)
        px=py=100
        def move(x,y):
            global px,py
            while (px,py)!=(x,y):
                dx=max(-100,min(100,x-px));dy=max(-100,min(100,y-py));g.command(f'mouse_move {dx} {dy}');px+=dx;py+=dy
        def click(x,y):move(x,y);g.command('mouse_button 1');g.command('mouse_button 0')
        def sample():
            before=log().count(b'[memstat]');type_text(g,'memstat');time.sleep(.3);vals=re.findall(rb'\[memstat\] free KiB=(\d+)',log());assert len(vals)>before;return int(vals[-1])
        click(1890,96);start=sample()
        for n in [1,2]:
            type_text(g,f'browser http://10.0.2.2:{web.server_port}/pool{n}');g.wait(f'js: pool-pass-{n}'.encode(),25)
            click(120,110);during=sample();click(1610,42);click(120,110);after=sample()
            print(f'Navigator cycle {n}: {start} KiB free before / {during} during / {after} after',flush=True);assert after-during>25000 and start-after<2048;start=after
