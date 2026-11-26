"""Guest-level checks using real PS/2 input, a disposable disk, and VGA captures."""
from qemu_install import boot, open_terminal, type_text, check_tree
from pathlib import Path
import tempfile, time, shutil, http.server, threading


def frame(g, work):
    path = work / 'screen.ppm'
    g.command(f'screendump {path}')
    with path.open('rb') as f:
        assert f.readline().strip()==b'P6'
        return tuple(map(int,f.readline().split()))

with tempfile.TemporaryDirectory() as tmp:
    work=Path(tmp); disk=work/'disposable.img'; shutil.copyfile('install-build/disk.img',disk)
    with boot(work,disk) as g:
        open_terminal(g); original=frame(g,work)
        type_text(g,'video 1920x1080'); g.wait(b'[video] mode=1920x1080',8)
        assert frame(g,work)==(1920,1080)
        g.wait(f'[video] mode={original[0]}x{original[1]}'.encode(),22)
        assert frame(g,work)==original
        type_text(g,'video 1920x1080'); type_text(g,'video keep')
        type_text(g,'spkg install ide');g.keys('f2-g');g.wait(b'[wm] floating')
        type_text(g,'edit marker.txt');g.keys(*'abcdefghij');time.sleep(.5)
        # Second floating window starts at (70,60). Drag characters 2..5.
        # Pointer starts at (26,105) after open_terminal.
        g.command('mouse_move 96 4');g.command('mouse_button 1');g.command('mouse_move 18 0');g.command('mouse_button 0');
        g.keys('ctrl-c','ctrl-a','ctrl-v','ctrl-s');time.sleep(1)
    tree,_=check_tree(disk); v=tree['/sharkuser/marker.txt'];assert v==b'cde', v
    with boot(work,disk,efi=True) as g:
        open_terminal(g);assert frame(g,work)==(1920,1080)
        # Select the unwanted suffix of a pending command and delete just it.
        cmd='touch /Downloads/onezzz'
        g.keys(*[{' ':'spc','/':'slash','D':'shift-d'}.get(c,c) for c in cmd]);
        g.command('mouse_move 206 -39');g.command('mouse_button 1');g.command('mouse_move 24 0');g.command('mouse_button 0');g.keys('backspace','ret')
        type_text(g,'touch /Downloads/two');type_text(g,'explorer');
        g.keys(*(['down']*5),'ret','ctrl-a','ctrl-c','ctrl-v');time.sleep(1)
    tree,_=check_tree(disk)
    assert '/Downloads/Copy1-one' in tree and '/Downloads/Copy1-two' in tree, tree.keys()
    class Page(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body=(f'<html><body>http://10.0.2.2:{self.server.server_port}/done</body></html>' if self.path=='/sample' else '<html><body><script>console.log("selection-copied");</script></body></html>').encode()
            self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    with http.server.HTTPServer(('127.0.0.1',0),Page) as web:
        threading.Thread(target=web.serve_forever,daemon=True).start()
        with boot(work,disk) as g:
            open_terminal(g);type_text(g,f'browser http://10.0.2.2:{web.server_port}/sample');time.sleep(2)
            g.keys('ctrl-a','ctrl-c','ctrl-l','ctrl-v','ret');g.wait(b'js: selection-copied',15)
        web.shutdown()
    print('1080p hardware capture, timed revert, saved mode, IDE mouse selection/paste and Explorer batch copy passed')
