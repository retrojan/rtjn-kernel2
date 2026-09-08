#ifndef IDT_H
# define IDT_H

# include <stdint.h>

typedef struct
{
	uint16_t	isr_low;
	uint16_t	kernel_cs;
	uint8_t		reserved;
	uint8_t		attributes;
	uint16_t	isr_high;
} __attribute__((packed)) idt_entry_t;

typedef struct
{
	uint16_t	limit;
	uint32_t	base;
} __attribute__((packed)) idtr_t;


void		idt_init(void);
void		idt_set(uint8_t vector, void *isr, uint8_t flags);
//extern void	load_idt(void);

#endif
