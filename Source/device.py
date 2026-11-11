#!/usr/bin/env python3
"""RGDS calibrated physical keys and volume -> World of Goo SDL mailbox."""
import os,sys,pathlib,glob,mmap,struct,select,time,fcntl
# RGDS+ measured Alpha 7b user calibration (SDL button indices), GUID 1900b655010000000100000000010000.
CALIBRATION={'a':0,'b':1,'x':3,'y':2,'l1':4,'r1':5,'select':6,'start':7,'l2':10,'r2':11}
# Captured ANBERNIC key capability bitmap from the original device report.
CAPTURED_KEY_WORDS='400000000 1fff000000000000 0 0 c000000000000 2'
def bitmap_words(text):
 return sum(int(word,16)<<(64*i) for i,word in enumerate(reversed(text.split())))
def sdl_button_codes(bitmap):
 # SDL2 Linux evdev numbering: BTN_JOYSTICK..KEY_MAX, then keys below BTN_JOYSTICK.
 return [code for code in list(range(0x120,0x2ff))+list(range(0x120)) if bitmap>>code&1]
def calibrated_keys(codes):
 return {name:codes[index] for name,index in CALIBRATION.items()}
physical=calibrated_keys(sdl_button_codes(bitmap_words(CAPTURED_KEY_WORDS)))
state=pathlib.Path(sys.argv[1]);node=pathlib.Path('/sys/class/anbernic_misc/openbor_volume');exitfile=pathlib.Path(sys.argv[2]);state.write_bytes(bytes(4096));f=state.open('r+b');mem=mmap.mmap(f.fileno(),4096);
try:initial_level=max(0,min(10,int(node.read_text().strip())))
except (OSError,ValueError):initial_level=2
struct.pack_into('<I',mem,4,initial_level)
fds=[];keysets={};axes={};ranges={};start=None
for p in sorted(pathlib.Path('/sys/class/input').glob('event*')):
 try:
  name=(p/'device/name').read_text().strip()
  if 'anbernic' not in name.lower() and name!='adc-keys':continue
  fd=os.open('/dev/input/'+p.name,os.O_RDONLY|os.O_NONBLOCK|os.O_CLOEXEC);fds.append(fd);keysets[fd]=set();axes[fd]={};ranges[fd]={}
  for axis in [2,3,16,17]:
   try:
    b=bytearray(24);fcntl.ioctl(fd,0x80184540+axis,b,True);v,lo,hi,*_=struct.unpack('iiiiii',b);ranges[fd][axis]=(lo,hi);axes[fd][axis]=v
   except OSError:pass
  print('Input:',p.name,name,flush=True)
  if 'anbernic' in name.lower():
   try:
    keybits=bytearray(96);fcntl.ioctl(fd,0x80604521,keybits,True)
    codes=sdl_button_codes(int.from_bytes(keybits,'little'))
    if len(codes)!=17:raise ValueError('Unexpected SDL button count: '+str(len(codes)))
    physical=calibrated_keys(codes)
    print('Measured calibration resolved from device capabilities',flush=True)
   except (OSError,ValueError,IndexError):
    print('Using captured RGDS+ device capabilities with Measured calibration',flush=True)
   for button,index in CALIBRATION.items():print('PHYSICAL',button.upper(),'= b'+str(index),'= evdev',physical[button],flush=True)
 except OSError:pass
# Bits 16..29 carry physical state for the managed controller layer.
mapping={physical['a']:(5,6,16),physical['b']:(9,17),physical['x']:(18,),physical['y']:(19,),
 physical['l1']:(24,),physical['r1']:(25,),physical['select']:(26,),physical['start']:(9,27),
 physical['l2']:(28,),physical['r2']:(29,),544:(22,),545:(23,),546:(20,),547:(21,),103:(22,),108:(23,),105:(20,),106:(21,)}
def controls_bits(keys,axes):
 bits=0
 for code,outputs in mapping.items():
  if code in keys:
   for i in outputs:bits|=1<<i
 for fd,values in axes.items():
  # Stock firmware reports min=0, max=1, but emits signed -1/0/+1.
  x=values.get(16,0);y=values.get(17,0)
  if x<0:bits|=1<<20
  elif x>0:bits|=1<<21
  if y<0:bits|=1<<22
  elif y>0:bits|=1<<23
 return bits
if not fds:raise SystemExit('No readable RGDS+ controls found')
lastpress=0;lastcode=0;lastfd=-1
try:
 while fds:
  ready,_,_=select.select(fds,[],[],.02)
  now=time.monotonic()
  for fd in ready:
   try:data=os.read(fd,24*64)
   except OSError:continue
   for off in range(0,len(data)-23,24):
    _,_,typ,code,value=struct.unpack_from('llHHi',data,off)
    if typ==1:
     if value:keysets[fd].add(code)
     else:keysets[fd].discard(code)
     if code in [114,115] and value in [1,2]:
      if (value==2 and now-lastpress<.18) or (fd!=lastfd and code==lastcode and now-lastpress<.03):continue
      try:level=int(node.read_text().strip());node.write_text(str(max(0,min(10,level+(1 if code==115 else -1))))+'\n')
      except (OSError,ValueError):pass
      lastpress=now;lastcode=code;lastfd=fd
    elif typ==3:axes[fd][code]=value
  keys=set().union(*keysets.values());bits=controls_bits(keys,axes)
  if physical['select'] in keys and physical['start'] in keys:
   if start is None:start=now
   elif now-start>=1.5:exitfile.write_text('START+SELECT\n')
  else:start=None
  try:level=max(0,min(10,int(node.read_text().strip())))
  except (OSError,ValueError):level=initial_level
  ax=ay=0.0
  for fd,values in axes.items():
   for axis in (2,3):
    if axis not in ranges[fd]:continue
    lo,hi=ranges[fd][axis];mid=(lo+hi)/2
    v=(values.get(axis,mid)-mid)/max(1,(hi-lo)/2)
    v=0 if abs(v)<.18 else max(-1,min(1,v))
    if axis==2 and abs(v)>abs(ax):ax=v
    if axis==3 and abs(v)>abs(ay):ay=v
  struct.pack_into('<IIff',mem,0,bits,level,ax,ay)
finally:
 struct.pack_into('<I',mem,0,0);mem.close();f.close()
 for fd in fds:os.close(fd)
