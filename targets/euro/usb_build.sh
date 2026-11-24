# USB Out (eurorack only), run by build.sh in build/euro (targets/euro.json): the M7 payload, then the M4 DEV port payload.
"$CC" -mcpu=cortex-m7 -mfpu=fpv5-d16 -mthumb -mfloat-abi=hard -Os \
  -ffreestanding -nostdlib -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -Wall -Wextra -Werror -Isrc -Wl,--build-id=none,-e,0 -T ../../targets/euro/usb7.ld \
  -o build/usb7.elf ../../targets/euro/usb_m7.c
"$OBJCOPY" -O binary -j .cave build/usb7.elf build/usb7.bin
"$NM" build/usb7.elf > build/usb7.symbols
# DEV port audio (eurorack only): the M4 host payload (targets/euro/usbh_m4.c).
"$CC" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mthumb -mfloat-abi=hard -Os \
  -ffreestanding -nostdlib -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -Wall -Wextra -Werror -Isrc -Wl,--build-id=none,-e,0 -T ../../targets/euro/usbh4.ld \
  -o build/usbh4.elf ../../targets/euro/usbh_m4.c
"$OBJCOPY" -O binary -j .cave build/usbh4.elf build/usbh4.bin
"$NM" build/usbh4.elf > build/usbh4.symbols
