#include "printk.h"
#include "term.h"
#include "io.h"
#include "sched.h"

volatile uint32_t	timer_ticks = 0;

void	timer_int(regs_t *re)
{
	timer_ticks++;
	sched_tick();
	(void)re;
}

uint32_t	sleep(uint32_t seconds)
{
	extern volatile uint8_t	cancel_input;
	uint64_t	time_target = (uint64_t)timer_ticks + (uint64_t)seconds * 18;

	while (timer_ticks < time_target)
	{
		if (cancel_input)
			break;
	}
	return (0);
}
