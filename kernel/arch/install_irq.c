#include "irq.h"
#include "idt.h"
#include "io.h"

#include "term.h"

extern void		*irq_stub_table[];
void			timer_int(regs_t *re);
void			keyboard_handler(regs_t *re);

void	*irq_routines[16] =
{
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

void	irq_register(int irq, void (*handler)(regs_t *re))
{
	irq_routines[irq] = handler;
}

void	irq_unregister(int irq)
{
	irq_routines[irq] = 0;
}

static void	irq_remap(void)
{
	//Seding the init command to Master and Slave PIC
	outb(PIC_MASTER_0, 0x11);
	outb(PIC_SLAVE_0, 0x11);

	//Setting up Master and Slave with their beginning vectors
	outb(PIC_MASTER_1, 0x20);
	outb(PIC_SLAVE_1, 0x28);

	//Setting up slave PIC on IRQ2 for master PIC
	outb(PIC_MASTER_1, 0x04);
	outb(PIC_SLAVE_1, 0x02);

	//Setting up IRQ0 (sys timer) and IRQ1 (keyboard)
	outb(PIC_MASTER_1, 0x01);
	outb(PIC_SLAVE_1, 0x01);

	//Enable only the IRQs that have a handler (IRQ0 timer, IRQ1 keyboard).
	//Everything else is masked: the polling ATA driver keeps raising
	//IRQ14/IRQ15 from the disk INTRQ lines during boot, and no handler
	//registers for them.
	outb(PIC_MASTER_1, 0xFC);
	outb(PIC_SLAVE_1, 0xFF);
}

void	irq_init(void)
{
	irq_remap();
	for (int i = 0; i < 16; i++)
	{
		idt_set(IRQ_VECTOR_OFFSET + i, irq_stub_table[i], 0x8E);
	}
	irq_register(0, timer_int);
	irq_register(1, keyboard_handler);
}

