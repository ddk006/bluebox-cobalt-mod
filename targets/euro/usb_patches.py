# USB Out (eurorack only): run inside tools/make_release.py of build/euro
# (targets/euro.json); euro 1.5.1 addresses, not mapped. See usb_m7.c.
usb_code=(ROOT/'build'/'usb7.bin').read_bytes()
if len(usb_code)>0x8000:raise ValueError('USB Out code cave too large')
add('USB Out code',0x080c8000,bytes(len(usb_code)),usb_code)
USB={f[2]:int(f[0],16) for line in (ROOT/'build'/'usb7.symbols').read_text().splitlines() if len(f:=line.split())==3}
for at,stock_fn,ours in ((0x08076c2c,0x080658c0,'usb_init'),(0x08076c40,0x08065db0,'usb_datain'),(0x08076c88,0x08066fec,'usb_serial')):
    add('USB Out class slot '+hex(at),at,struct.pack('<I',stock_fn|1),struct.pack('<I',USB[ours]|1))
add('USB Out ring write',0x0804115a,branch(0x0804115a,0x08066750),branch(0x0804115a,USB['usb_push']))
add('USB Out watchdog',0x08041166,branch(0x08041166,0x080655dc),branch(0x08041166,USB['usb_tick']))
for at in (0x08065ab4,0x08065d1a,0x08066640):
    add('USB Out fixed send '+hex(at),at,branch(at,0x08066f48),branch(at,USB['usb_tx82']))
# DEV port audio (M4, targets/euro/usbh_m4.c): the code, and the class
# registration in MX_USB_HOST_Init (bl USBH_RegisterClass at 0x081056c6) first
# puts the wrappers into the MIDI class (RAM 0x30000098; .data image unchanged).
host_code=(ROOT/'build'/'usbh4.bin').read_bytes()
if len(host_code)>0x4000:raise ValueError('DEV port audio code cave too large')
add('USB Out host code',0x080cc000,bytes(len(host_code)),host_code)
HOST={f[2]:int(f[0],16) for line in (ROOT/'build'/'usbh4.symbols').read_text().splitlines() if len(f:=line.split())==3}
add('USB Out host class registration',0x081056c6,branch(0x081056c6,0x0811d184),branch(0x081056c6,HOST['host_register']))
