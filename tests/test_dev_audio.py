"""DEV port audio (eurorack only, targets/euro/usbh_m4.c) under Unicorn.
The wrapped MIDI host class with ST's USB host library stubbed: Init/DeInit,
the Requests steps (config descriptor, SET_INTERFACE, UAC2 SET_CUR 48 kHz,
periodic FIFO, isochronous pipe), the 1 ms SOF packets and the pass-through to
MIDI. A full-speed device like the Torso S-4 (UAC2, 10 channels of 16 bit
host -> device) is modelled by its config descriptor. No USB hardware. On the
desktop build there is no payload and the test only says so.
"""
from firmware_harness import *
import firmware_harness as H

SYMS=ROOT/'build'/'usbh4.symbols'
if not SYMS.exists():
    print('PASS DEV port audio is eurorack only: no payload in this build');print('1 DEV port audio checks passed');raise SystemExit(0)
F={f[2]:int(f[0],16) for line in SYMS.read_text().splitlines() if len(f:=line.split())==3}

u=machine(cpu_model=UC_CPU_ARM_CORTEX_M4);u.mem_map(0x40080000,0x1000)
PHOST=0x3000ac28;LINE=0x38800ee0;PDATA=0x300000b4;OTG=0x40080000;HEAP=0x30030000
CLASS=0x30000098
u.mem_write(CLASS,patched[0x0814fdc8-0x08040000:][:32])   # the .data image of the MIDI class
reg=[];stub(u,0x0811d184,lambda u:(reg.append((u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1))),u.reg_write(UC_ARM_REG_R0,0)))
stub(u,0x0811d100,lambda u:u.reg_write(UC_ARM_REG_R0,0));stub(u,0x0811d204,lambda u:u.reg_write(UC_ARM_REG_R0,0))   # USBH_Init, USBH_Start
call(u,0x081056b4)
check(reg==[(PHOST,CLASS)] and [u32(u,CLASS+o) for o in (8,0xc,0x10,0x14,0x18)]==[F[n]|1 for n in ('host_init','host_deinit','host_requests','host_bgnd','host_sof')] and u32(u,CLASS+4)==1,'MX_USB_HOST_Init (stock, USBH_Init/Start stubbed): the MIDI class (code 1) is registered with Init, DeInit, Requests, Bgnd and SOF going through the wrappers')

def d(*b):return bytes([len(b)+1,*b])
def cfg(nrch=10,slot=2,bits=16,maxp=980,midi=True,audio=True,fu=True,fb=3):
    ac=d(0x24,1,0,2,8,0,0,0)+d(0x24,0x0a,8,1,3,0,0)+d(0x24,2,1,0x01,0x01,0,8,nrch,0,0,0,0,0,0,0,0)
    if fu:ac+=d(0x24,6,5,1,*([0x0f,0,0,0]+[0]*(4*nrch)),0)   # feature unit 5 on input terminal 1: master mute+volume
    body=d(4,3,0,0,1,1,0x20,0)+ac
    if audio:
        body+=d(4,4,0,0,1,2,0x20,0)+d(4,4,1,2 if fb else 1,1,2,0x20,0)+d(0x24,1,1,0,1,1,0,0,0,nrch,0,0,0,0,0)+d(0x24,2,1,slot,bits)
        body+=d(5,0x03,0x05,maxp&0xff,maxp>>8,1)+d(0x25,1,0,0,0,0)+(d(5,0x85,0x11,fb,0,1) if fb else b'')
    if midi:body+=d(4,7,0,2,1,3,0,0)+d(5,0x04,2,64,0,0)+d(5,0x87,2,64,0,0)
    return bytes([9,2,(9+len(body))&0xff,(9+len(body))>>8,8,1,0,0xc0,0])+body
DEV=[cfg()]
log=[];sent=[];setups=[];urb=[0];fbst=[1];fbval=[48<<14];frees=[];heap=[HEAP]
def arg(u,n):return u32(u,u.reg_read(UC_ARM_REG_SP)+4*(n-4))
def r0(u,v):u.reg_write(UC_ARM_REG_R0,v)
def data():return [x for x in sent if x[0]==3]
def fbreads():return [x for x in sent if x[0]==4]
midi_ret=[0]
def midi_init(u):log.append('midi_init');(w32(u,PDATA,0x3000b494) if midi_ret[0]==0 else None);r0(u,midi_ret[0])
stub(u,0x08105990,midi_init)
stub(u,0x08105944,lambda u:(log.append('midi_deinit'),w32(u,PDATA,0),r0(u,0)))
stub(u,0x08105936,lambda u:(log.append('midi_req'),r0(u,0)))
stub(u,0x08105b5a,lambda u:(log.append('midi_bgnd'),r0(u,0)))
stub(u,0x08105aec,lambda u:(log.append('midi_sof'),r0(u,0)))
def get_desc(u):
    b=DEV[0][:arg(u,4)&0xffff]
    log.append(('get_desc',u.reg_read(UC_ARM_REG_R2),arg(u,4)&0xffff));u.mem_write(u.reg_read(UC_ARM_REG_R3),b);r0(u,0)
stub(u,0x0811dbda,get_desc)
def ctl(u):
    n=u.reg_read(UC_ARM_REG_R2);setups.append((bytes(u.mem_read(PHOST+0x10,8)),bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),n)) if n else b''));r0(u,0)
stub(u,0x0811d8f2,ctl)
stub(u,0x0811dfd2,lambda u:(log.append(('alloc_pipe',u.reg_read(UC_ARM_REG_R1))),r0(u,3 if u.reg_read(UC_ARM_REG_R1)==3 else 4)))
stub(u,0x0811dfa2,lambda u:(log.append(('open_pipe',u.reg_read(UC_ARM_REG_R1),u.reg_read(UC_ARM_REG_R2),u.reg_read(UC_ARM_REG_R3),arg(u,4)&0xff,arg(u,5)&0xff,arg(u,6)&0xffff)),r0(u,0)))
stub(u,0x0811dfc8,lambda u:(log.append(('close_pipe',u.reg_read(UC_ARM_REG_R1))),r0(u,0)))
stub(u,0x0811e004,lambda u:(log.append(('free_pipe',u.reg_read(UC_ARM_REG_R1))),r0(u,0)))
def submit(u):
    pipe,buf,n=u.reg_read(UC_ARM_REG_R1),arg(u,5),arg(u,6)&0xffff
    if u.reg_read(UC_ARM_REG_R2)==1:u.mem_write(buf,struct.pack('<I',fbval[0])[:n])   # the device answers the feedback read
    sent.append((pipe,u.reg_read(UC_ARM_REG_R2),u.reg_read(UC_ARM_REG_R3),arg(u,4)&0xff,bytes(u.mem_read(buf,n))));r0(u,0)
stub(u,0x08105894,submit)
stub(u,0x081058ce,lambda u:r0(u,urb[0] if u.reg_read(UC_ARM_REG_R1)==3 else fbst[0]))
stub(u,0x0811b94a,lambda u:r0(u,0))
stub(u,0x0811ba90,lambda u:r0(u,heap[0]))
stub(u,0x0811bbc8,lambda u:(frees.append(u.reg_read(UC_ARM_REG_R0))))
def reset(mode,**kw):
    DEV[0]=cfg(**kw);del log[:],sent[:],setups[:],frees[:];w32(u,PDATA,0);fbval[0]=48<<14;fbst[0]=1
    w32(u,LINE+0xc,mode);w32(u,LINE+0x10,0);w32(u,LINE+0x14,0)
    u.mem_write(PHOST+2,b'\1');u.mem_write(PHOST+0x31c,b'\x05\x01')
    u.mem_write(PHOST+0x33a,struct.pack('<H',len(DEV[0])))
    w32(u,OTG+0x24,0x200);w32(u,OTG+0x28,0x01000200);w32(u,OTG+0x100,0x00e00300);w32(u,OTG+0x10,0x80000000)
def requests(n=12):
    for i in range(n):
        r=call(u,F['host_requests'],PHOST)
        if r!=1:return r,i+1
    return r,n
def state():return u32(u,LINE+0x10),u32(u,LINE+0x14)
SINE=list(struct.unpack('<48h',patched[F['sine48']-0x08040000:][:96]))   # the driver's table
check(all(abs(SINE[k]-8192*math.sin(2*math.pi*k/48))<1.5 for k in range(48)),'sine table: one cycle of 1 kHz at 48 kHz, -12 dBFS')

# Off: the stock MIDI class, nothing else.
w32(u,LINE+0x18,HEAP);w32(u,HEAP,0x48535431)   # a context left from an earlier boot
reset(0);midi_ret[0]=2
check(call(u,F['host_init'],PHOST)==2 and log==['midi_init'] and u32(u,LINE+0x18)==0,'DEV Audio Off: Init is MIDI Init (its result, here no MIDI), the old context pointer cleared')
midi_ret[0]=0;reset(0);call(u,F['host_init'],PHOST);del log[:]
check(call(u,F['host_requests'],PHOST)==0 and call(u,F['host_sof'],PHOST)==0 and call(u,F['host_bgnd'],PHOST)==0 and call(u,F['host_deinit'],PHOST)==0 and log==['midi_req','midi_sof','midi_bgnd','midi_deinit'] and not sent,'DEV Audio Off: Requests, SOF, Bgnd and DeInit are the MIDI ones, nothing sent')

# On, a device like the S-4 at full speed: MIDI, 10 channels of 16 bit host -> device,
# feature unit 5 on the stream, feedback endpoint 0x85 (3 bytes, 10.14).
reset(1)
check(call(u,F['host_init'],PHOST)==0 and log==['midi_init'] and u32(u,LINE+0x18)==HEAP and state()==(3,0),'DEV Audio On: MIDI Init, then a context from the FreeRTOS heap; state Starting')
r,n=requests()
check(r==0 and ('get_desc',0x0200,len(DEV[0])) in log,'Requests: reads the whole config descriptor ('+str(len(DEV[0]))+' bytes) and ends with OK after '+str(n)+' calls')
check(setups[0]==(bytes.fromhex('010b010004000000'),b''),'SET_INTERFACE: interface 4, alternate 1')
check(setups[1]==(bytes.fromhex('2101000103080400'),bytes.fromhex('80bb0000')),'UAC2 SET_CUR sampling frequency 48000 on clock 8 (its input terminal), AC interface 3')
check(setups[2:]==[(bytes.fromhex('2101000103050100'),b'\0'),(bytes.fromhex('2101000203050200'),b'\0\0')],'feature unit 5 (on the stream terminal): SET_CUR mute off, then volume 0 dB, master channel')
check(('alloc_pipe',3) in log and ('open_pipe',3,3,5,1,1,980) in log,'isochronous OUT pipe on EP 0x03, device address 5, full speed, max packet 980')
check(('alloc_pipe',0x85) in log and ('open_pipe',4,0x85,5,1,1,3) in log,'isochronous IN pipe on the feedback endpoint 0x85, 3 bytes')
check(u32(u,OTG+0x28)==0x00800200 and u32(u,OTG+0x100)==0x01000280 and u32(u,OTG+0x10)==0x420,'up to 49 frames (980 bytes): periodic TX FIFO 0x100 words at 0x280 (non-periodic 0x80 words), TX FIFOs flushed')
check(log[-1]=='midi_req' and state()==(4,10),'then the MIDI Requests; state Sending, 10 channels')
# SOF: one packet per ms, the sine on channels 1 and 2.
del log[:];urb[0]=0;call(u,F['host_sof'],PHOST)
p=data()[0][4] if data() else b''
def sample(f,ch):return struct.unpack_from('<h',p,(f*10+ch)*2)[0]
check(len(data())==1 and data()[0][:4]==(3,0,1,1) and len(p)==960 and len(fbreads())==1 and fbreads()[0][:4]==(4,1,1,1),'SOF: a feedback read on pipe 4 and one isochronous OUT packet of 48 frames x 10 channels x 2 bytes on pipe 3')
check([sample(f,0) for f in (0,6,12,18,24,36)]==[0,5793,8192,5793,0,-8192] and all(sample(f,1)==sample(f,0) for f in range(48)) and all(sample(f,c)==0 for f in range(48) for c in range(2,10)),'packet: 1 kHz sine at -12 dBFS on channels 1 and 2, channels 3-10 silent')
check(log==['midi_sof'],'SOF: MIDI SOF still runs')
call(u,F['host_sof'],PHOST)
check(len(data())==1 and u32(u,HEAP+0x18)==1,'next SOF with the last packet not out yet (URB idle): skipped and counted')
urb[0]=1;call(u,F['host_sof'],PHOST);call(u,F['host_sof'],PHOST)
check(len(data())==3 and u32(u,HEAP+0x14)==3,'URB done: a packet every SOF')
check(u32(u,LINE+0x1c)==2,'DEV Pkts: No SOF until the first second is counted')
for i in range(996):call(u,F['host_sof'],PHOST)
check(u32(u,LINE+0x1c)==1,'after 1000 SOFs with the packets done: DEV Pkts OK')
# Feedback 48.5 frames per ms: 48 and 49 frames in turn, the sine runs on across packets.
fbval[0]=97<<13;del sent[:]
for i in range(12):call(u,F['host_sof'],PHOST)
lens=[len(x[4]) for x in data()][2:]
check(sorted(set(lens))==[960,980] and abs(lens.count(980)-lens.count(960))<=1,'feedback 48.5 frames (10.14): packets of 48 and 49 frames in turn ('+','.join(str(n//20) for n in lens)+')')
seq=[struct.unpack_from('<h',x[4],k*20)[0] for x in data() for k in range(len(x[4])//20)]
o=next((k for k in range(48) if SINE[k]==seq[0] and SINE[(k+1)%48]==seq[1]),-1)
check(o>=0 and all(seq[k]==SINE[(o+k)%48] for k in range(len(seq))),'the sine runs on without a jump across packets of different size ('+str(len(seq))+' samples)')
fbval[0]=0;del sent[:]
for i in range(4):call(u,F['host_sof'],PHOST)
check(len(set(len(x[4]) for x in data()[2:]))>=1 and all(len(x[4]) in (960,980) for x in data()),'a feedback value far from 48 (0) is ignored: still 48/49 frames')
fbval[0]=48<<14
for i in range(4):call(u,F['host_sof'],PHOST)
del sent[:]
for i in range(4):call(u,F['host_sof'],PHOST)
check([len(x[4]) for x in data()]==[960]*4,'feedback back at 48.0: 48 frames')
w32(u,LINE+0xc,2);del sent[:];call(u,F['host_sof'],PHOST);p=data()[0][4]
check(all(sample(f,c)==0 for f in range(48) for c in (0,1,4,5,6,7,8,9)) and all(sample(f,3)==sample(f,2) for f in range(48)) and sorted(set(abs(sample(f,2)) for f in range(48)))[-1]==8192,'DEV Audio Sine 3-4: the sine moves to channels 3 and 4 (the S-4 track 1), the rest silent')
w32(u,LINE+0xc,3);del sent[:];call(u,F['host_sof'],PHOST);p=data()[0][4]
check(all(sample(f,c)==sample(f,0) for f in range(48) for c in range(10)) and any(sample(f,9)==8192 for f in range(48)),'DEV Audio Sine all: the sine on all 10 channels')
w32(u,LINE+0xc,1)
fbval[0]=0
for i in range(2000):call(u,F['host_sof'],PHOST)   # a whole window of feedback far from 48
check(u32(u,LINE+0x1c)==9,'packets out, feedback answers far from 48 (or empty): DEV Pkts OK, FB odd')
fbst[0]=0
for i in range(2000):call(u,F['host_sof'],PHOST)
check(u32(u,LINE+0x1c)==8,'packets out, no feedback answer: DEV Pkts OK, no FB')
fbst[0]=1;fbval[0]=48<<14
for i in range(2000):call(u,F['host_sof'],PHOST)
check(u32(u,LINE+0x1c)==1,'feedback near 48 again: DEV Pkts OK')
urb[0]=4
for i in range(1000):call(u,F['host_sof'],PHOST)
check(u32(u,LINE+0x1c)==6,'packets ending in URB_ERROR: DEV Pkts Error')
urb[0]=0
for i in range(2000):call(u,F['host_sof'],PHOST)   # a whole window without a finished packet
check(u32(u,LINE+0x1c)==3,'packets never done (URB idle): DEV Pkts Stuck')
urb[0]=1;call(u,F['host_sof'],PHOST)
del log[:];call(u,F['host_bgnd'],PHOST)
check(log==['midi_bgnd'],'Bgnd: MIDI')
n=len(sent);w32(u,LINE+0xc,0);call(u,F['host_sof'],PHOST);w32(u,LINE+0xc,1)
check(len(sent)==n,'DEV Audio switched Off while sending: no more packets or feedback reads')
del log[:];call(u,F['host_deinit'],PHOST)
check(('close_pipe',3) in log and ('free_pipe',3) in log and ('close_pipe',4) in log and ('free_pipe',4) in log and frees==[HEAP] and log[-1]=='midi_deinit' and u32(u,LINE+0x18)==0 and state()==(1,0) and u32(u,LINE+0x1c)==0,'DeInit: both pipes closed and freed, context back to the heap, MIDI DeInit; state Waiting')
call(u,F['host_sof'],PHOST)
check(len(sent)==n,'after DeInit the SOF sends nothing')

# No feature unit, no feedback endpoint: no mute/volume requests, always 48 frames.
reset(1,fu=False,fb=0);call(u,F['host_init'],PHOST);r,n=requests();urb[0]=1
for i in range(5):call(u,F['host_sof'],PHOST)
check(r==0 and len(setups)==2 and not fbreads() and [len(x[4]) for x in data()]==[960]*5,'no feature unit and no feedback: only SET_INTERFACE and the rate, 48 frames every SOF')
call(u,F['host_deinit'],PHOST)

# No MIDI interface: audio only, MIDI functions left out.
reset(1,midi=False);midi_ret[0]=2
check(call(u,F['host_init'],PHOST)==0,'no MIDI on the device: Init still OK for the audio stream')
r,n=requests();del log[:];urb[0]=1;call(u,F['host_sof'],PHOST);call(u,F['host_bgnd'],PHOST)
check(r==0 and state()==(4,10) and len(data())==1 and log==[],'audio only: sending, MIDI Requests/SOF/Bgnd not called')
del log[:];call(u,F['host_deinit'],PHOST)
check('midi_deinit' not in log and frees==[HEAP],'audio only: DeInit leaves MIDI DeInit out')
midi_ret[0]=0

# MIDI controller without audio OUT.
reset(1,audio=False);call(u,F['host_init'],PHOST);r,n=requests()
check(r==0 and state()==(2,0) and not setups and log[-1]=='midi_req','MIDI only: state No audio, no requests sent, MIDI as before')
call(u,F['host_sof'],PHOST);check(not sent,'MIDI only: nothing sent, no feedback read')
call(u,F['host_deinit'],PHOST)

# 24-bit (3-byte subslots), 2 channels: the sine in the top two bytes.
reset(1,nrch=2,slot=3,bits=24,maxp=294);call(u,F['host_init'],PHOST);requests();urb[0]=1;call(u,F['host_sof'],PHOST)
p=data()[0][4]
check(len(p)==288 and p[12*6:12*6+3]==bytes([0])+struct.pack('<h',8192) and u32(u,OTG+0x100)==0x00e00300,'3-byte subslots, 2 channels: 288-byte packets, sample in the upper two bytes; the stock FIFO is big enough')
call(u,F['host_deinit'],PHOST)

# Too big for one full-speed packet, and no memory.
reset(1,nrch=24,maxp=1023);call(u,F['host_init'],PHOST);r,n=requests()
check(r==0 and state()==(7,24) and not setups,'24 channels: 2304 bytes per ms do not fit, state Too big, nothing selected')
call(u,F['host_deinit'],PHOST)
reset(1);heap[0]=0
check(call(u,F['host_init'],PHOST)==0 and state()==(6,0) and u32(u,LINE+0x18)==0,'no heap: state No memory, MIDI Init result returned')
heap[0]=HEAP
print(H.checks,'DEV port audio checks passed')
