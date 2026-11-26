"""Run one point per VM; configure RAM with qemu_install's --*-ram flags.
Profile via MEM_PROFILE=bios|efi|iso-bios|iso-efi, MEM_WORK=basic|doom.
"""
import os,re,tempfile,time,shutil
from pathlib import Path
from qemu_install import args,boot,type_text,open_terminal,check_tree
profile=os.getenv('MEM_PROFILE','bios'); live=profile.startswith('iso');efi=profile.endswith('efi')
ram=args.iso_ram if live else args.efi_ram if efi else args.bios_ram
with tempfile.TemporaryDirectory() as td:
 w=Path(td);disk=w/'scratch.img';shutil.copyfile('install-build/disk.img',disk)
 with boot(w,disk,efi=efi,live=live) as g:
    if live:g.keys('esc')
    g.wait(b'[desktop] free KiB=',20);time.sleep(1)
    free=int(re.search(rb'\[desktop\] free KiB=(\d+)',g.log.read_bytes())[1]);print(f'{profile} {ram} MiB -- first frame, {free} KiB free, {(ram*1024-free)/1024:.2f} MiB nonfree',flush=True)
    steady=b'[idle] free KiB='
    if os.getenv('MEM_STEADY'):g.wait(steady,25);free=int(re.search(rb'\[idle\] free KiB=(\d+)',g.log.read_bytes())[1]);print(f'10s steady {free} KiB free, {(ram*1024-free)/1024:.2f} MiB nonfree',flush=True)
    open_terminal(g);type_text(g,'cc hello.c');g.wait(b'[cc] Linked application: hello',20);type_text(g,'./hello');g.wait(b'[run] ./hello returned 0x00000000',15)
    type_text(g,'wmprefer JawsWM');time.sleep(.5)
    if os.getenv('MEM_WORK')=='doom':
        type_text(g,'spkg install doom')
        # Root menu at (26,105); Applications submenu, eleventh installed icon.
        g.command('mouse_button 2');g.command('mouse_button 0');g.command('mouse_move 55 90');g.command('mouse_move 98 0');
        g.command('mouse_move 0 100');g.command('mouse_move 0 100');g.command('mouse_move 0 20');g.command('mouse_button 1');g.command('mouse_button 0');g.wait(b'[doom] engine ready',55);print('FreeDoom engine initialized',flush=True)
    print('Interactive Terminal, C compilation and linked hello passed',flush=True)
 if not live: assert check_tree(disk)[0]['/System/wm.pref'][1]==1
