/* PC map M4 payload: upper half of the M4 code cave, after Cobalt's cm4 code
 * (0x080B8000, checked < 0x4000 bytes in make_release.py); its read-only data
 * (strings, tables, font) after the cue icon code (0x080D8000). */
MEMORY {
 PCMAP (rx) : ORIGIN = 0x080BC000, LENGTH = 0x4000
 PCDATA (r) : ORIGIN = 0x080DC000, LENGTH = 0x4000
}
SECTIONS {
  .cave : { *(.text .text.*) } > PCMAP
  .cavedata : { *(.rodata .rodata.*) } > PCDATA
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
