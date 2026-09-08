#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "gdt.h"

/* 32-bit Task State Segment. Layout matches IA-32 TSS (offset 0x00-0x67). */
typedef struct
{
	uint32_t	prev_tss;
	uint32_t	esp0;
	uint32_t	ss0;
	uint32_t	esp1;
	uint32_t	ss1;
	uint32_t	esp2;
	uint32_t	ss2;
	uint32_t	cr3;
	uint32_t	eip;
	uint32_t	eflags;
	uint32_t	eax;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	ebx;
	uint32_t	esp;
	uint32_t	ebp;
	uint32_t	esi;
	uint32_t	edi;
	uint32_t	es;
	uint32_t	cs;
	uint32_t	ss;
	uint32_t	ds;
	uint32_t	fs;
	uint32_t	gs;
	uint32_t	ldt;
	uint16_t	trap;
	uint16_t	iomap_base;
} __attribute__((packed)) tss_t;

static tss_t	g_tss __attribute__((aligned(16)));

uint32_t	tss_get_base(void)
{
	return ((uint32_t)&g_tss);
}

void	tss_init(void)
{
	bzero(&g_tss, sizeof(g_tss));

	g_tss.ss0 = GDT_SEL_KERN_DATA;
	g_tss.esp0 = 0;
	/* No I/O permission bitmap (iomap_base == sizeof TSS):
	 * all port I/O is denied in ring 3. */
	g_tss.trap = 0;
	g_tss.iomap_base = sizeof(tss_t);
}

void	tss_set_kernel_esp0(uint32_t esp)
{
	g_tss.esp0 = esp;
}

void	tss_load(void)
{
	__asm__ volatile ("ltr %0" :: "r"((uint16_t)GDT_SEL_TSS));
	__asm__ volatile ("" ::: "memory");
}