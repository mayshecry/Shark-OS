"""Installed guest: switch via CLI, reboot, exercise both menus and switch via Settings."""
from qemu_install import boot, open_terminal, type_text, check_tree
from pathlib import Path
import tempfile, shutil, time

with tempfile.TemporaryDirectory() as tmp:
    w=Path(tmp); disk=w/'test.img'; shutil.copyfile('install-build/disk.img',disk)
    with boot(w,disk) as g:
        open_terminal(g);g.wait(b'[wm] restored NemoWM');type_text(g,'appearance 1');type_text(g,'wmprefer JawsWM');time.sleep(.6)
    fs,_=check_tree(disk);assert fs['/System/wm.pref']==bytes([1,1,1,0])
    nemo_prefs=fs["/System/desktop.prefs"]
    with boot(w,disk,efi=True) as g:
        g.wait(b'[wm] restored JawsWM');g.wait(b'[boot] boot screen hide complete');time.sleep(.8)
        def shot():
            g.command('screendump '+str(w/'screen.ppm'));f=(w/'screen.ppm').read_bytes().split(b'\n',3);a,b=map(int,f[1].split());return a,b,f[3]
        sw,sh,rgb=shot();pixel=lambda x,y:rgb[3*(y*sw+x):3*(y*sw+x)+3]; assert pixel(70,sh-3)==bytes([80,80,117]) and pixel(sw-30,30)!=pixel(70,sh-3)
        px=py=100
        def move(x,y):
            global px,py
            while (px,py)!=(x,y):
                dx=min(90,max(-90,x-px));dy=min(90,max(-90,y-py));g.command(f'mouse_move {dx} {dy}');px+=dx;py+=dy
        def click(n=1):g.command(f'mouse_button {n}');g.command('mouse_button 0')
        move(210,115);click(2);move(245,245) # Appearance submenu
        sw,sh,rgb=shot();assert pixel(216,120)!=bytes([80,80,117]) and pixel(350,240)!=bytes([80,80,117]), 'cascade not painted'
        # Optional real framebuffer capture for the release preview.
        import os
        if os.getenv("JAWS_CAPTURE"):shutil.copyfile(w/"screen.ppm",os.environ["JAWS_CAPTURE"])
        move(365,245);move(365,265);click() # Appearance > Window manager
        # First floating window starts at (40,30); client starts at (46,55).
        move(46+200,55+38);click()    # Backdrop page
        move(150,55+78+23+9);click()  # Deep harbor
        sw,sh,rgb=shot();assert pixel(70,sh-3)==bytes([53,73,89])
        move(150,55+78+4*23+9);click()  # SharkOS Classic wallpaper
        time.sleep(.7);sw,sh,rgb=shot()
        def sample_strip():return tuple(pixel(x,sh-2) for x in range(0,sw,7))
        assert len(set(sample_strip()))>5, 'wallpaper must extend below the old taskbar area'
        # The fit options follow the six images, on the next list page.
        move(426,55+345-15);click();move(150,55+78+8*23+9);click() # Fit, index 11
        move(46+283,55+38);click()   # Video page
        move(160,55+78+4*23+9);click();assert shot()[:2]==(1920,1080)
        move(160,55+78+6*23+9);click();assert shot()[:2]==(sw,sh)
    fs,_=check_tree(disk);assert fs['/System/wm.pref'][3]==10,fs['/System/wm.pref'];assert fs['/System/desktop.prefs']==nemo_prefs
    with boot(w,disk) as g:
        g.wait(b'[wm] restored JawsWM');g.wait(b'[desktop] free KiB=');sw,sh,rgb=shot()
        # Fit can letterbox the bottom; verify the image in the main field.
        assert len({pixel(x,sh//2) for x in range(80,sw-80,7)})>10
        px=py=100;move(sw-30,154);click()  # third dock tile: Preferences, Manager page
        move(75,55+38);click()       # Manager page
        move(150,142);click();time.sleep(.5)
    fs,_=check_tree(disk);assert fs['/System/wm.pref'][1]==0,fs.get('/System/wm.pref')
    with boot(w,disk) as g:
        g.wait(b'[wm] restored NemoWM');g.wait(b'[prefs] packages=0 theme=1');open_terminal(g)
        type_text(g,'wmprefer unknown');time.sleep(.3)
    fs,_=check_tree(disk);assert fs['/System/wm.pref'][1]==0
    print('Jaws wallpaper, fit, reboot persistence and independent Nemo preferences passed')
