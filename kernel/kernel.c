#include <stdint.h>
#include "term.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "printk.h"
#include "memory.h"
#include "sched.h"
#include "multiboot.h"

#include "ata.h"
#include "vfs.h"
#include "ext2.h"
#include "usb.h"
#include "net.h"

void		shell(void);

void	map_ident(uintptr_t base, uint32_t len)
{
	uintptr_t	page;

	page = base & ~0xFFF;
	for (; page < base + len; page += PAGE_SIZE)
	{
		vm_map_page((void*)page, (void*)page);
		__native_flush_tlb_single((virtual_addr)page);
	}
}

void	kernel_main(multiboot_info_t* mbd, uint32_t magic)
{
	gdt_init();
	idt_init();
	irq_init();

	pm_init(mbd, magic);
	vm_init();

	tss_init();
	tss_load();
	sched_init();
	__asm__ volatile ("sti"); //Enable interrupts after scheduler init

	ata_init();
	vfs_init();
	ext2_init();

	usb_init();

	net_init();
	net_driver_init();

	term_init();
	shell();
}
