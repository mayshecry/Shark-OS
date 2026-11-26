"""Bounded random-access WAD: header, absolute block offsets, zlib blocks."""
import struct, sys, zlib
from pathlib import Path
src, dst = map(Path,sys.argv[1:]); data=src.read_bytes(); block=65536
parts=[zlib.compress(data[p:p+block],9) for p in range(0,len(data),block)]
off=[8+4*(len(parts)+1)]
for part in parts: off.append(off[-1]+len(part))
dst.write_bytes(b'WZ01'+struct.pack('<I',len(data))+struct.pack('<%dI'%len(off),*off)+b''.join(parts))
