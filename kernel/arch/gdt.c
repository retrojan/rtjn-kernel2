#include "gdt.h"
#include "string.h"

gdt_entry_t	gdt_e[GDT_SIZE];
gdt_ptr_t		gdt;

extern void	gdt_flush(void);

static void	gdt_set_entry(gdt_entry_t *entry, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
	entry->base_low = base & 0xFFFF;
	entry->base_middle = (base >> 16) & 0xFF;
	entry->base_high = (base >> 24) & 0xFF;

	entry->limit_low = limit & 0xFFFF;
	entry->limit_high = (limit >> 16) & 0x0F;

	entry->access = access;
	entry->flags = flags & 0xF;
}

void	gdt_init(void)
{
	gdt.limit = (sizeof(gdt_entry_t) * GDT_SIZE) - 1;
	gdt.base = GDT_BASE;

	//Null Descriptor
	gdt_set_entry(&(gdt_e[0]), 0, 0, 0, 0);
	//Kernel Code - Data - Stack
	gdt_set_entry(&(gdt_e[1]), 0, 0xFFFFF, GDT_KERN_ACCESS | GDT_SEG_CODE, GDT_FLAGS);
	gdt_set_entry(&(gdt_e[2]), 0, 0xFFFFF, GDT_KERN_ACCESS | GDT_SEG_DATA, GDT_FLAGS);
	gdt_set_entry(&(gdt_e[3]), 0, 0xFFFFF, GDT_KERN_ACCESS | GDT_SEG_STACK, GDT_FLAGS);
	//User Code - Data - Stack
	gdt_set_entry(&(gdt_e[4]), 0, 0xFFFFF, GDT_USER_ACCESS | GDT_SEG_CODE, GDT_FLAGS);
	gdt_set_entry(&(gdt_e[5]), 0, 0xFFFFF, GDT_USER_ACCESS | GDT_SEG_DATA, GDT_FLAGS);
	gdt_set_entry(&(gdt_e[6]), 0, 0xFFFFF, GDT_USER_ACCESS | GDT_SEG_STACK, GDT_FLAGS);
	//TSS (32-bit available, limit 0x67+1 = 104 bytes)
	gdt_set_entry(&(gdt_e[7]), tss_get_base(), 0x68 - 1, 0x89, 0);

	memcpy((void*)GDT_BASE, gdt_e, sizeof(gdt_entry_t) * GDT_SIZE);

	gdt_flush();
}
