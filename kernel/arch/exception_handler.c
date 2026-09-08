#include "idt.h"
#include "io.h"
#include "printk.h"
#include "sched.h"

static char	*exception_msgs[32] = 
	{
		"Division Error",
		"Debug",
		"Non-maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"Bound Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack-Segment Fault",
		"General Protection Fault",
		"Page Fault",
		"Reserved",
		"x87 Floating-Point Exception",
		"Alignment Check",
		"Machine Check",
		"SIMD Floating-Point Exception",
		"Virtualization Exception",
		"Control Protection Exception",
		"Reserved",
		"Hypervisor Injection Exception",
		"VMM Communication Exception",
		"Security Exception",
		"Reserved"
	};

void	exception_handler(regs_t *re)
{
	if (re->int_no >= 32)
	{
		/* A stack or hardware vector misdispatched to the exception
		 * stub: say which one so the IDT wiring bug is obvious. */
		printk("\nUnexpected vector %u via exception stub (eip=0x%x) - halting\n",
			re->int_no, re->eip);
		__asm__ volatile ("cli; hlt");
	}
	if (re->int_no < 32)
	{
		/* A fault in ring 3 only kills the offending task; we return
		 * to isr_common_stub which honours the requested switch. */
		if (re->cs & 3)
		{
			printk("\n[user pid %d] %s fault (eip=0x%x err=0x%x) - killing task\n",
				sched_current_pid(), exception_msgs[re->int_no], re->eip,
				re->err_code);
			sched_exit(1);
			return;
		}
		printk("\n%s Exception (eip=0x%x err=0x%x esp=0x%x eflags=0x%x cs=0x%x)\n", exception_msgs[re->int_no], re->eip, re->err_code, re->esp, re->eflags, re->cs);
		printk("eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x esi=0x%x edi=0x%x ebp=0x%x\n", re->eax, re->ebx, re->ecx, re->edx, re->esi, re->edi, re->ebp);
		{
			uint32_t	*fp = (uint32_t*)re->ebp;
			int		i;
			for (i = 0; i < 8 && fp > (uint32_t*)0x100000 && fp < (uint32_t*)0x2000000; i++)
			{
				printk("fp=%p ret=%p\n", (void*)fp, (void*)fp[1]);
				fp = (uint32_t*)fp[0];
			}
		}
		printk("System Halted");
		__asm__ volatile ("cli; hlt");
	}
	while (1);
}
