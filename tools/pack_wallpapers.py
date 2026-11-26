"""Lossless 8-row strips, with 32-bit horizontal differences for compression."""
import re,struct,sys,zlib
from pathlib import Path
out=Path(sys.argv[1]);tmp=out.with_suffix('.tmp'); maxw=0
with tmp.open('w') as f:
 for arg in sys.argv[2:]:
    text=Path(arg).read_text();(wn,w),(hn,h)=re.findall(r'#define (\w+_(?:WIDTH|HEIGHT)) (\d+)',text)[:2];w,h=int(w),int(h);maxw=max(maxw,w)
    name=re.search(r'unsigned int (\w+)\[',text)[1];pixels=[int(x,16) for x in re.findall(r'0x([a-fA-F0-9]{8})[uU]?',text)];assert len(pixels)==w*h
    diff=[(c-(pixels[i-1] if i%w else 0))&0xffffffff for i,c in enumerate(pixels)]
    chunks=[]
    for start in range(0,len(diff),8*w):
        row=diff[start:start+8*w];chunks.append(zlib.compress(struct.pack('<%dI'%len(row),*row),9))
    off=[4*(len(chunks)+1)];
    for chunk in chunks:off.append(off[-1]+len(chunk))
    blob=struct.pack('<%dI'%len(off),*off)+b''.join(chunks)
    f.write(f'#define {wn} {w}\n#define {hn} {h}\nstatic const unsigned char {name}[] __attribute__((aligned(4)))=\n');
    for pos in range(0,len(blob),64):f.write('"'+''.join('\\x%02x'%b for b in blob[pos:pos+64])+'"\n');
    f.write(';\n')
 f.write(f'#define WP_STRIDE_MAX {maxw}\n')
tmp.replace(out)
