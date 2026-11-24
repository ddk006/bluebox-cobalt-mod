/* DEV port audio (eurorack only, M4): free flash after the USB Out M7 payload (0x080C8000) and before the phones cue code (0x080D0000).
 * 0x080E0000-0x080FFFFF is the storage application (tests/test_patch.py keeps it unchanged). */
SECTIONS { . = 0x080cc000; .cave : { *(.text*) *(.rodata*) } /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*) *(.data*) *(.bss*) } }
