ENTRY(Reset_Handler)
MEMORY {
 FLASH (rx) : ORIGIN = 0x14000, LENGTH = 0x67000
 SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
 SDRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 8M
}
__StackTop = ORIGIN(SRAM)+LENGTH(SRAM);
SECTIONS {
 .vectors : { KEEP(*(.isr_vector)) } > FLASH
 .text : { *(.text*) *(.rodata*) KEEP(*(.firmware_identity)) } > FLASH
 .ARM.extab : { *(.ARM.extab*) } > FLASH
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
 .data : { . = ALIGN(4); __data_start__ = .; *(.data*) . = ALIGN(4); __data_end__ = .; } > SRAM AT > FLASH
 __data_load__ = LOADADDR(.data);
 .iram.bss (NOLOAD) : { . = ALIGN(8); __iram_start__ = .; *(.iram.bss*) . = ALIGN(8); __iram_end__ = .; } > SRAM
 /* The reviewed factory scatter loader initializes [0x20000000,0x20006230).
  * An ordinary low .noinit is therefore NOT retained across its reset path.
  * Reserve 64 bytes above that loader and below our separate 16 KiB stack. */
 .noinit 0x2000bfc0 (NOLOAD) : { __retained_start__ = .; KEEP(*(.noinit*)) __retained_end__ = .; } > SRAM
 .framebuffer (NOLOAD) : { . = ALIGN(64); *(.framebuffer*) } > SDRAM
 .bss (NOLOAD) : { . = ALIGN(8); __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end__ = .; } > SDRAM
 .heap (NOLOAD) : { __heap_start__ = .; . += 0x10000; __heap_end__ = .; } > SDRAM
 __image_end__ = LOADADDR(.data)+SIZEOF(.data);
 /DISCARD/ : { *(.comment*) *(.note*) }
}
ASSERT(__image_end__ <= 0x7b000, "application overlaps reserved flash")
ASSERT(__iram_end__ <= __retained_start__, "internal SRAM overlaps retained recovery state")
ASSERT(__retained_end__ <= __StackTop-0x4000, "retained state overlaps 16KB stack")
ASSERT(__heap_end__ + 0x100000 <= ORIGIN(SDRAM)+LENGTH(SDRAM), "media requires 1 MiB SDRAM headroom")
