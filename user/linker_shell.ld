ENTRY(_start)

/* Ring-3 shell: linked at 0x40100000 and mapped there by kernel/usertask.c.
   _start is placed first in .text so it sits at file offset 0 of the flat
   binary, letting the kernel jump straight to the binary base. */
SECTIONS
{
	. = 0x40100000;

	.text : { KEEP(*(.text._start)) *(.text*) }
	.rodata : { *(.rodata*) *(.rodata.*) }
	.data : { *(.data*) }
	.bss : { *(.bss*) *(COMMON) }
}