"""USB Out channel choice (eurorack only, targets/euro/usb_m7.c) under Unicorn.
The M7 payload's descriptor edit, channel packing, ring setup, DataIn, fixed
sends, serial suffix and the disconnect/reconnect sequence, with the USB stack
around it stubbed. No USB hardware or host. On the desktop build there is no
payload and the test only says so.
"""
from firmware_harness import *
import firmware_harness as H

SYMS=ROOT/'build'/'usb7.symbols'
if not SYMS.exists():
    print('PASS USB Out is eurorack only: no payload in this build');print('1 USB Out checks passed');raise SystemExit(0)
U={f[2]:int(f[0],16) for line in SYMS.read_text().splitlines() if len(f:=line.split())==3}

u=machine();u.mem_map(0x40040000,0x1000)
DESC=0x240000a8;OUT=0x38800ee0;ST=0x38800ec0;DCTL=0x40040804
d0=patched.find(bytes.fromhex('09024601'))
check(d0>0 and patched[d0+0x58]==18 and patched[d0+0xc7]==18 and patched[d0+0xd7:d0+0xd9]==b'\x80\x01','stock config descriptor: 18 channels device->host, EP 0x82 max packet 384')
u.mem_write(DESC,patched[d0:d0+0x146])
clock=[1000];wd=[];ring_in=[];tx=[];stocks=[]
stub(u,0x0805e188,lambda u:u.reg_write(UC_ARM_REG_R0,clock[0]))
stub(u,0x080655dc,lambda u:wd.append(1))
stub(u,0x08066750,lambda u:(ring_in.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),u.reg_read(UC_ARM_REG_R2)))),u.reg_write(UC_ARM_REG_R0,0)))
stub(u,0x08066f48,lambda u:(tx.append((u.reg_read(UC_ARM_REG_R1),bytes(u.mem_read(u.reg_read(UC_ARM_REG_R2),u.reg_read(UC_ARM_REG_R3))))),u.reg_write(UC_ARM_REG_R0,0)))
stub(u,0x08065db0,lambda u:(stocks.append('datain'),u.reg_write(UC_ARM_REG_R0,0)))
stub(u,0x080658c0,lambda u:(stocks.append('init'),u.reg_write(UC_ARM_REG_R0,0)))
def tick(dt=5):clock[0]+=dt;call(u,U['usb_tick'],0,0,0,0)
def want(mask,fmt=0):w32(u,OUT,0x55534231);w32(u,OUT+4,mask);w32(u,OUT+8,fmt)
STARTED=0x2400b9b4
w32(u,STARTED,0);want(0x3ffff);tick()
check(u.mem_read(DESC+0x58,1)[0]==18 and wd==[1],'all 18 on: descriptor unchanged, the stock watchdog runs')
want(0xff);tick()
check(u.mem_read(DESC+0x58,1)[0]==8 and u.mem_read(DESC+0xc7,1)[0]==8 and bytes(u.mem_read(DESC+0xd7,2))==b'\xc0\x00','USB 1-8 before USB runs: the descriptor says 8 channels at once, max packet 192')
# Packing: two 54-byte frames, byte = channel number.
RING=0x24010000;RBUF=0x24011000;BLK=0x24014000
w32(u,0x24000200,RING);w32(u,RING,RBUF);w32(u,RING+4,0x2880);w32(u,RING+8,0x36);w32(u,RING+0x10,0);w32(u,RING+0x14,0)
frame=bytes(c for c in range(18) for _ in range(3))
u.mem_write(BLK,frame*2);del ring_in[:]
call(u,U['usb_push'],RING,BLK,108)
check(ring_in==[bytes(c for c in range(8) for _ in range(3))*2],'ring write: USB 1-8 kept, 24-byte frames')
want(1|1<<12|1<<17);call(u,U['usb_tick'],0,0,0,0);u.mem_write(BLK,frame*2);del ring_in[:];call(u,U['usb_push'],RING,BLK,108)
check(ring_in==[bytes([0,0,0,12,12,12,17,17,17])*2],'any set: USB 1, 13 and 18 in that order')
# 16 bit: the upper two bytes of each 24-bit sample.
want(0xff,1);call(u,U['usb_tick'],0,0,0,0)
check(u.mem_read(DESC+0x58,1)[0]==8 and u.mem_read(DESC+0xc7,1)[0]==8 and u.mem_read(DESC+0xd1,2)==b'\x02\x10' and bytes(u.mem_read(DESC+0xd7,2))==b'\x80\x00','USB 1-8 at 16 bit: 8 channels, subslot 2 bytes, 16 bit resolution, max packet 128')
f24=bytes(b for c in range(18) for b in (0x10+c,0x40+c,0x80+c))
u.mem_write(BLK,f24*2);del ring_in[:];call(u,U['usb_push'],RING,BLK,108)
check(ring_in==[bytes(b for c in range(8) for b in (0x40+c,0x80+c))*2],'16 bit packing: middle and high byte of each sample, 16-byte frames')
want(0x3ffff,1);call(u,U['usb_tick'],0,0,0,0)
check(u.mem_read(DESC+0x58,1)[0]==18 and u.mem_read(DESC+0xd1,2)==b'\x02\x10' and bytes(u.mem_read(DESC+0xd7,2))==b'\x20\x01','all 18 at 16 bit: not stock, max packet 288')
del tx[:];u.mem_write(0x24019000,bytes(324));call(u,U['usb_tx82'],0x24020000,0x82,0x24019000,324)
check(tx==[(0x82,bytes(216))],'fixed sends at 16 bit, 18 channels: 6 frames of 36 bytes')
want(0x3ffff,0);call(u,U['usb_tick'],0,0,0,0)
check(u.mem_read(DESC+0xd1,2)==b'\x03\x18' and bytes(u.mem_read(DESC+0xd7,2))==b'\x80\x01','back to 24 bit and all 18: the stock descriptor')
want(0xff);tick()
call(u,U['usb_init'],0x24020000,1)
check(stocks==['init'] and u32(u,RING+4)==192*24 and u32(u,RING+8)==24 and u32(u,RING+0x10)==0,'class Init (stock first): the ring holds 192 frames of 24 bytes')
# DataIn: 6 frames (+ADJ) from the ring at 48 kHz.
u.mem_write(RBUF,bytes(range(256))*4);w32(u,RING+0x10,500);w32(u,RING+0x14,0)
w32(u,0x240001f8,48000);w32(u,0x2400bc94,1);w32(u,0x2400bd00,0x24018000);w32(u,0x2400bd44,7);del tx[:]
call(u,U['usb_datain'],0x24020000,0x82)
check(tx==[(0x82,(bytes(range(256))*4)[:168])] and u32(u,RING+0x14)==168 and u32(u,0x2400bc94)==0 and u32(u,0x2400bd44)==0 and u32(u,RING+0x1c)==1,'DataIn: 7 frames (6 + ADJ 1) of 24 bytes from the ring, ADJ and the IN watchdog count cleared, streaming')
w32(u,0x240001f8,44100);del tx[:];call(u,U['usb_datain'],0x24020000,0x82)
check(tx==[(0x82,bytes(144))],'host not at 48 kHz: 6 frames of silence')
call(u,U['usb_datain'],0x24020000,0x01)
check(stocks[-1]=='datain','other endpoints: the stock DataIn')
del tx[:];u.mem_write(0x24019000,bytes(324));call(u,U['usb_tx82'],0x24020000,0x82,0x24019000,324)
check(tx==[(0x82,bytes(144))],'the fixed 324-byte sends become 6 frames of 24 bytes')
# Serial: "-000FF" appended (UTF-16).
SER=0x2401a000
stub(u,0x08066fec,lambda u:(u.mem_write(SER,b'\x0a\x03A\0B\0C\0D\0'),w32(u,u.reg_read(UC_ARM_REG_R1),10),u.reg_write(UC_ARM_REG_R0,SER)))
LEN=0x2401b000;w32(u,LEN,0);call(u,U['usb_serial'],0,LEN)
n=u32(u,LEN)&0xffff
check(n==22 and bytes(u.mem_read(SER,n))[2:].decode('utf-16-le')=='ABCD-000FF' and u.mem_read(SER,1)[0]==22,'serial number: "-000FF" appended for USB 1-8')
want(0xff,1);call(u,U['usb_tick'],0,0,0,0);w32(u,LEN,0);call(u,U['usb_serial'],0,LEN);n=u32(u,LEN)&0xffff
check(bytes(u.mem_read(SER,n))[2:].decode('utf-16-le')=='ABCD-000FF-16' and u.mem_read(SER,1)[0]==n,'serial number at 16 bit: "-000FF-16"')
r16=0x24030000;w32(u,0x24000200,r16);w32(u,r16,RBUF);call(u,U['usb_init'],0x24020000,1)
check(u32(u,r16+4)==192*16 and u32(u,r16+8)==16,'class Init at 16 bit, USB 1-8: 192 frames of 16 bytes')
u.mem_write(RBUF,bytes(range(256)));w32(u,r16+0x10,200);w32(u,r16+0x14,0);w32(u,0x240001f8,48000);w32(u,0x2400bc94,0);del tx[:]
call(u,U['usb_datain'],0x24020000,0x82)
check(tx==[(0x82,bytes(range(96)))],'DataIn at 16 bit: 6 frames of 16 bytes')
w32(u,0x24000200,RING);want(0xff);call(u,U['usb_tick'],0,0,0,0)
# A change while USB runs: disconnect, edit after 20 ms, reconnect at 250 ms.
w32(u,STARTED,1);w32(u,DCTL,0);del wd[:];want(0xffff);tick()
check(u32(u,DCTL)&2 and u.mem_read(DESC+0x58,1)[0]==8 and not wd,'USB 1-16 while running: soft disconnect first, descriptor not yet changed')
tick(10);check(u.mem_read(DESC+0x58,1)[0]==8,'10 ms later still disconnected, unchanged')
tick(15);check(u.mem_read(DESC+0x58,1)[0]==16 and u32(u,DCTL)&2,'after 20 ms: 16 channels, still disconnected')
w32(u,0x2400b9a8,1234);w32(u,0x2400b9a0,5);tick(250)
check(not u32(u,DCTL)&2 and u32(u,0x2400b9a0)==0 and u32(u,0x2400b9a4)==1234 and not wd,'at 250 ms: reconnect with the watchdog state reset')
tick();check(wd==[1],'then the stock watchdog runs again')
want(0);tick();tick(25);tick(250)
check(u.mem_read(DESC+0x58,1)[0]==18 and bytes(u.mem_read(DESC+0xd7,2))==b'\x80\x01','none chosen counts as all: back to the stock 18 channels and 384')
u.mem_write(BLK,frame*2);del ring_in[:];call(u,U['usb_push'],RING,BLK,108)
check(ring_in==[frame*2],'stock again: frames pass through')
# DEV port audio (dev_ring.h): the block's 18 channels as 16 bit into the M4's ring while its heartbeat runs.
DR=0x30030000
try:u.mem_map(0x30000000,0x48000)
except Exception:pass
u.mem_write(DR,bytes(64+256*20));w32(u,DR,0x52564544);w32(u,DR+4,0x3002f000);w32(u,DR+8,256)
u.mem_write(DR+12,bytes([1,0,18,13,14,2,3,4,5,6]))   # DEV 1 USB 1, DEV 2 Off, DEV 3 USB 18, DEV 4/5 Main
f24=bytes(b for c in range(18) for b in (0x10+c,0x40+c,0x80+c))
def routed(rt=(1,0,18,13,14,2,3,4,5,6)):return b''.join(bytes((0x40+c-1,0x80+c-1)) if c else b'\0\0' for c in rt)
def push(hb,dt=8):
    clock[0]+=dt;w32(u,OUT+0x18,DR|hb);u.mem_write(BLK,f24*2);call(u,U['usb_push'],RING,BLK,108)
push(0);push(0);push(0)
check(u32(u,DR+0x20)==0 and u32(u,DR+0x24)==0,'DEV: a ring address without a heartbeat (stale, from before a reset): nothing written')
push(1);push(2);push(3)
check(u32(u,DR+0x20)==0,'three beats: not yet')
push(4)
check(u32(u,DR+0x20)==2 and u32(u,DR+0x24)==1 and bytes(u.mem_read(DR+64,40))==routed()*2,'four beats in a row: the 2 frames written as routed (DEV 1 USB 1, DEV 2 silent, DEV 3 USB 18, ...), 16 bit (middle and high byte), write position 2, one block')
push(4,dt=1);push(4,dt=1)
check(u32(u,DR+0x20)==6,'between beats (within 50 ms) it keeps writing')
push(4,dt=60)
check(u32(u,DR+0x20)==6,'no beat for 60 ms (the M4 stopped streaming): stops')
push(5);push(6);push(7);push(0)
check(u32(u,DR+0x20)==8,'beats again: writes again')
w32(u,DR,0);push(1)
check(u32(u,DR+0x20)==8,'no ring magic: nothing written')
w32(u,DR,0x52564544);w32(u,DR+8,20);push(2)
check(u32(u,DR+0x20)==8,'a ring size out of range (20 frames): nothing written')
w32(u,DR+8,256);clock[0]=100;push(3,dt=0)
check(u32(u,DR+0x20)==8,'the clock went back (this core restarted): beats start over, nothing written')
w32(u,DR+0x20,255);push(4);push(5);push(6)   # beats 2-4 since the restart
check(u32(u,DR+0x20)==1 and bytes(u.mem_read(DR+64+255*20,20))==routed(),'the write position wraps at the ring size (256 frames)')
w32(u,OUT+0xc,5);push(7)   # DEV Audio Ring test
v=[struct.unpack_from('<10h',u.mem_read(DR+64+((1+j)%256)*20,20)) for j in range(2)]
check(v[0][0]==v[0][2] and v[0][1]==0 and abs(v[1][0]-v[0][0])<1100 and v[0][0]!=0x4100 and u32(u,DR+0x20)==3,'Ring test: the M7 writes a 1 kHz sine on every routed channel (DEV 2 Off stays silent), phase running on')
w32(u,OUT+0xc,0)
w32(u,OUT+0x18,0)
print(H.checks,'USB Out checks passed')
