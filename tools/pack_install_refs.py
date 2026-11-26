"""Lossless disk sector references: zero, literal, or immutable loaded ELF bytes.
Mutable segments are never referenced. Verify the reconstructed stream at build time.
"""
from pathlib import Path
import struct,sys,zlib,gzip
image,kernel,out=map(Path,sys.argv[1:]);elf=kernel.read_bytes();src=image.read_bytes();magic,size,crc=struct.unpack_from('<8sII',src)
assert magic==b'SHARKIMG' and size==len(src)-16 and size%512==0
phoff=struct.unpack_from('<Q',elf,32)[0];ents,n=struct.unpack_from('<HH',elf,54)
segments=[struct.unpack_from('<II6Q',elf,phoff+i*ents) for i in range(n)];base=min(p[4] for p in segments if p[0]==1);rw=min(p[4] for p in segments if p[0]==1 and p[1]&2)
by_content={};rom={}
for kind,flags,off,va,pa,fs,ms,align in segments:
 if kind!=1 or flags&2 or pa<base or pa+fs>rw:continue
 assert (pa-base)%512==0
 # Leave the final page literal: a linker build-id note can share the
 # final ELF sector but sits after the runtime immutable-data boundary.
 for i in range(0,max(0,(fs // 4096 - 1)*4096),512):
    sector=elf[off+i:off+i+512];idx=(pa+i-base)//512;by_content[sector]=0x80000000|idx;rom[idx]=sector
zero=bytes(512);by_content[zero]=0;literal=[];refs=bytearray();checksum=0
for pos in range(16,len(src),512):
 sector=src[pos:pos+512];ref=by_content.get(sector)
 if ref is None:literal.append(sector);ref=len(literal);by_content[sector]=ref
 refs.extend(struct.pack('<I',ref));checksum=zlib.crc32((rom[ref&0x7fffffff] if ref>>31 else literal[ref-1] if ref else zero),checksum)
assert checksum==crc
payload=struct.pack('<8sII',b'SHARKRF1',size,crc)+refs+b''.join(literal)
out.write_bytes(gzip.compress(payload,compresslevel=6,mtime=0));print(f'Installer RAM module {len(payload)} bytes; disk {size} bytes')
