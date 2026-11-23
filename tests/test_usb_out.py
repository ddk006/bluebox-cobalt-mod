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
def want(mask):w32(u,OUT,0x55534231);w32(u,OUT+4,mask)
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
print(H.checks,'USB Out checks passed')
