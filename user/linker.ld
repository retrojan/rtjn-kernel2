ENTRY(_start)

/* Ring-3 user program: linked at 0x40000000 and copied into a user-mapped
   page at that virtual address by kernel/usertask.c. */
SECTIONS
{
	. = 0x40000000;

	.text : { *(.text*) }
	.rodata : { *(.rodata*) *(.rodata.*) }
	.data : { *(.data*) }
	.bss : { *(.bss*) *(COMMON) }
}