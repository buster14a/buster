"""Map an already linked Mach-O image unchanged and call LC_MAIN under Linux.
The original image's segment bytes and addresses are preserved. Only the ELF
container and a Linux exit trampoline are added; dyld and unwind are untested.
"""
import struct,sys
from pathlib import Path
src,dst=map(Path,sys.argv[1:]);data=src.read_bytes();assert struct.unpack_from('<I',data)[0]==0xfeedfacf
cpu=struct.unpack_from('<I',data,4)[0];machine=183 if cpu==0x100000c else 62;count=struct.unpack_from('<I',data,16)[0];offset=32;segments=[];entry_file=None;text_base=None
for i in range(count):
 cmd,size=struct.unpack_from('<II',data,offset)
 if cmd==0x19:
  name=data[offset+8:offset+24].rstrip(b'\0');va,mem,fileoff,filesize=struct.unpack_from('<QQQQ',data,offset+24);prot=struct.unpack_from('<I',data,offset+60)[0]
  if name==b'__TEXT':text_base=va-fileoff
  if mem and name!=b'__PAGEZERO':segments.append((va,mem,data[fileoff:fileoff+filesize],(4 if prot&1 else 0)|(2 if prot&2 else 0)|(1 if prot&4 else 0)))
 if cmd==0x80000028:entry_file=struct.unpack_from('<Q',data,offset+8)[0]
 offset+=size
assert entry_file is not None and text_base is not None;main=text_base+entry_file
stubva=(max(va+mem for va,mem,_,_ in segments)+4095)&~4095
if machine==183:
 words=[0xd2800000|(((main>>(16*i))&65535)<<5)|16|(i<<21)|(0x20000000 if i else 0) for i in range(4)]
 stub=struct.pack('<7I',*words,0xd63f0200,0xd2800ba8,0xd4000001)
else:stub=b'\x48\x83\xec\x20\x48\xb8'+struct.pack('<Q',main)+b'\xff\xd0\x89\xc7\xb8\x3c\0\0\0\x0f\x05'
segments.append((stubva,4096,stub,5));result=bytearray(4096);ph=[]
for va,mem,blob,flags in segments:
 if not flags&2 and len(blob)<mem:blob+=bytes(mem-len(blob))
 fileoff=len(result);assert fileoff%4096==va%4096;ph.append(struct.pack('<IIQQQQQQ',1,flags,fileoff,va,va,len(blob),mem,4096));result.extend(blob);result.extend(bytes((-len(result))%4096))
header=struct.pack('<16sHHIQQQIHHHHHH',b'\x7fELF\x02\x01\x01'+bytes(9),2,machine,1,stubva,64,0,0,64,56,len(ph),0,0,0);result[:64]=header
for i,row in enumerate(ph):result[64+56*i:64+56*(i+1)]=row
dst.write_bytes(result);dst.chmod(0o755)
