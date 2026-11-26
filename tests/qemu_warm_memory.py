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
            time.sleep(.5);g.command('screendump '+str(w/'svg.ppm'))
            assert (w/'svg.ppm').read_bytes().count(bytes([27,237,135]))>=100, 'SVG fill missing from real framebuffer'
            click(120,110);during=sample();click(1610,42);click(120,110);after=sample()
            print(f'Navigator cycle {n}: {start} KiB free before / {during} during / {after} after',flush=True);assert after-during>25000 and start-after<2048;start=after
        # Keep Terminal open at (40,30); Preferences opens next at (70,60).
        click(1890,154);click(276,123);click(455,264)  # Backdrop -> first wallpaper
        time.sleep(.6);click(55,115);image=sample()
        click(455,85+78+9);time.sleep(.4);click(55,115);solid=sample()
        assert solid-image==1920*1080*4//1024,(image,solid)
        print(f'1080p wallpaper cache returned {solid-image} KiB on selecting solid',flush=True)
