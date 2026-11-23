/* USB Out payload (eurorack only): free cave between the gate code (0x080C0000) and the phones cue code (0x080D0000). */
SECTIONS { . = 0x080c8000; .cave : { *(.text*) *(.rodata*) } /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*) *(.data*) *(.bss*) } }
