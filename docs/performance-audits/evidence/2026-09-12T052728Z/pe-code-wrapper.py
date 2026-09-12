"""Map a linked PE at its preferred base, preserving all section bytes.
Find main from the input object's symbol and unique first-function code image.
Only an ELF container and Linux exit trampoline are added; PE loader, imports
and unwind behavior are not exercised. The fixture calls no OS functions.
"""
import struct,subprocess,sys
from pathlib import Path
src,obj,dst=map(Path,sys.argv[1:]);data=src.read_bytes();assert data[:2]==b'MZ'
pe=struct.unpack_from('<I',data,60)[0];cpu,count=struct.unpack_from('<HH',data,pe+4);machine=183 if cpu==0xaa64 else 62
opt=pe+24;base=struct.unpack_from('<Q',data,opt+24)[0];table=opt+struct.unpack_from('<H',data,pe+20)[0];segments=[]
subject=obj.read_bytes();otable=20+struct.unpack_from('<H',subject,16)[0];prefix=None
for i in range(struct.unpack_from('<H',subject,2)[0]):
 pos=otable+40*i
 if subject[pos:pos+8].rstrip(b'\0')==b'.text':
  size,off=struct.unpack_from('<II',subject,pos+16);prefix=subject[off:off+64]
assert prefix and len(prefix)==64
nm=subprocess.check_output(['llvm-nm-18','--format=posix',str(obj)],text=True);main_offset=int(next(line.split()[2] for line in nm.splitlines() if line.startswith('main ')),16)
main=None
for i in range(count):
 pos=table+40*i;vsize,va,size,off=struct.unpack_from('<IIII',data,pos+8);flags=struct.unpack_from('<I',data,pos+36)[0];blob=data[off:off+size]
 if flags&0x20000000:
  match=blob.find(prefix)
  if match>=0:
   assert blob.find(prefix,match+1)<0 and main is None;main=base+va+match+main_offset
 segments.append((base+va,max(vsize,size),blob,(4 if flags&0x40000000 else 0)|(2 if flags&0x80000000 else 0)|(1 if flags&0x20000000 else 0)))
assert main is not None
stubva=(max(va+mem for va,mem,_,_ in segments)+4095)&~4095
if machine==183:
 words=[0xd2800000|(((main>>(16*i))&65535)<<5)|16|(i<<21)|(0x20000000 if i else 0) for i in range(4)];stub=struct.pack('<7I',*words,0xd63f0200,0xd2800ba8,0xd4000001)
else:stub=b'\x48\x83\xec\x20\x48\xb8'+struct.pack('<Q',main)+b'\xff\xd0\x89\xc7\xb8\x3c\0\0\0\x0f\x05'
segments.append((stubva,4096,stub,5));result=bytearray(4096);ph=[]
for va,mem,blob,flags in segments:
 if not flags&2 and len(blob)<mem:blob+=bytes(mem-len(blob))
 off=len(result);assert off%4096==va%4096;ph.append(struct.pack('<IIQQQQQQ',1,flags,off,va,va,len(blob),mem,4096));result.extend(blob);result.extend(bytes((-len(result))%4096))
result[:64]=struct.pack('<16sHHIQQQIHHHHHH',b'\x7fELF\x02\x01\x01'+bytes(9),2,machine,1,stubva,64,0,0,64,56,len(ph),0,0,0)
for i,row in enumerate(ph):result[64+56*i:64+56*(i+1)]=row
dst.write_bytes(result);dst.chmod(0o755)
