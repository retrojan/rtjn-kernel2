#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "term.h"
#include "sched.h"
#include "syscall.h"

extern volatile uint32_t	timer_ticks;

static int	sys_write(int fd, const char *buf, uint32_t len)
{
	(void)fd;
	term_write(buf, len);
	return ((int)len);
}

void	syscall_handler(regs_t *re)
{
	uint32_t	n = re->eax;

	sched_force_switch = 0;
	re->eax = 0;

	switch (n)
	{
		case SYS_WRITE:
			re->eax = (uint32_t)sys_write((int)re->ebx,
				(const char*)re->ecx, re->edx);
			break;
		case SYS_EXIT:
			sched_exit((int)re->ebx);
			break;
		case SYS_GETPID:
			re->eax = sched_current_pid();
			break;
		case SYS_YIELD:
			sched_yield();
			break;
		case SYS_TICKS:
			re->eax = timer_ticks;
			break;
		default:
			re->eax = 0xFFFFFFFF;
			break;
	}
}