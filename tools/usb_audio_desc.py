"""Print the USB audio descriptors of every audio device on this computer.

Needs libusb and pyusb:  brew install libusb && python3 -m pip install pyusb
Run:                     python3 tools/usb_audio_desc.py
Shows per streaming interface: UAC1 or UAC2, channels, bytes per sample
(subslot), bit resolution, sample rates (UAC1), endpoint, sync type, max
packet and interval. Only reads descriptors; changes nothing on the device.
"""
import sys
try:
    import usb.core, usb.util
except ImportError:
    sys.exit('pyusb missing: brew install libusb && python3 -m pip install pyusb')

SYNC = {0: 'none', 1: 'async', 2: 'adaptive', 3: 'sync'}
AC2 = {1: 'HEADER', 2: 'INPUT_TERMINAL', 3: 'OUTPUT_TERMINAL', 4: 'MIXER_UNIT', 5: 'SELECTOR_UNIT',
       6: 'FEATURE_UNIT', 10: 'CLOCK_SOURCE', 11: 'CLOCK_SELECTOR', 12: 'CLOCK_MULTIPLIER'}


def parse(cfg):
    i, sub, proto, out = 0, None, None, []
    while i + 1 < len(cfg) and cfg[i] >= 2:
        L, t = cfg[i], cfg[i + 1]
        b = cfg[i:i + L]
        if t == 4:
            sub, proto = (b[6], b[7]) if b[5] == 1 else (None, None)
            if b[5] == 1:
                kind = {1: 'control', 2: 'streaming', 3: 'MIDI'}.get(b[6], b[6])
                out.append(f'  Interface {b[2]} alt {b[3]}: audio {kind}, '
                           f'{"UAC2" if proto == 0x20 else "UAC1"}, {b[4]} endpoint(s)')
        elif t == 5 and sub is not None:
            mp = b[4] | b[5] << 8
            ival = b[6]
            out.append(f'    Endpoint {b[2]:#04x} ({"IN, device->host" if b[2] & 0x80 else "OUT, host->device"}): '
                       f'{"iso" if b[3] & 3 == 1 else "bulk/int"}, sync {SYNC[(b[3] >> 2) & 3]}'
                       f'{", feedback" if (b[3] >> 4) & 3 == 1 else ""}, max packet {mp & 0x7ff}'
                       f'{" x" + str(((mp >> 11) & 3) + 1) if mp >> 11 else ""}, bInterval {ival}')
        elif t == 0x24 and sub == 2:
            st = b[2]
            if st == 1 and proto == 0x20:
                out.append(f'    AS general: {b[10]} channels, formats {int.from_bytes(b[6:10], "little"):#x}')
            elif st == 1:
                out.append(f'    AS general: format tag {b[5] | b[6] << 8:#x}')
            elif st == 2 and proto == 0x20:
                out.append(f'    FORMAT: {b[4]} bytes per sample, {b[5]} bit')
            elif st == 2:
                n = b[7]
                rates = ([int.from_bytes(b[8 + 3 * k:11 + 3 * k], 'little') for k in range(n)] if n
                         else ['continuous %d-%d' % (int.from_bytes(b[8:11], 'little'), int.from_bytes(b[11:14], 'little'))])
                out.append(f'    FORMAT: {b[4]} channels, {b[5]} bytes per sample, {b[6]} bit, rates {rates}')
        elif t == 0x24 and sub == 1 and proto == 0x20:
            name = AC2.get(b[2], b[2])
            extra = f', {b[8]} channels' if b[2] == 2 else ''
            if b[2] == 10:
                extra = f', attributes {b[4]:#x}, controls {b[5]:#x}'
            out.append(f'    AC {name} id {b[3]}{extra}')
        i += L
    return out


found = False
for dev in usb.core.find(find_all=True):
    try:
        raw = bytes(dev.ctrl_transfer(0x80, 6, 0x0200, 0, 9))
        total = raw[2] | raw[3] << 8
        cfg = bytes(dev.ctrl_transfer(0x80, 6, 0x0200, 0, total))
    except Exception as e:
        continue
    if not any(cfg[i + 1] == 4 and cfg[i + 5] == 1 for i in range(len(cfg) - 5) if cfg[i] == 9):
        continue
    found = True
    try:
        name = f'{usb.util.get_string(dev, dev.iManufacturer)} {usb.util.get_string(dev, dev.iProduct)}'
    except Exception:
        name = '(name not readable)'
    speed = {1: 'low', 2: 'full', 3: 'high', 4: 'super'}.get(dev.speed, dev.speed)
    print(f'\n{name}  VID {dev.idVendor:#06x} PID {dev.idProduct:#06x}, USB {dev.bcdUSB >> 8}.{(dev.bcdUSB >> 4) & 15}, '
          f'{speed} speed, class {dev.bDeviceClass:#x}')
    print('\n'.join(parse(cfg)))
    print('  raw:', cfg.hex())
if not found:
    print('No USB audio device found (is it plugged in, and is libusb installed?)')
