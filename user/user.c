/* ring-3 user program, linked at 0x40000000 and copied into a user-mapped
 * page at that address by kernel/usertask.c. Runs in ring 3, talks to the
 * kernel through int 0x80. Safe to run several instances: the code/rodata
 * page is shared read-only, each task uses its own stack, and the binary has
 * no writable globals. */
#include <stdint.h>
#include "syscall.h"

static int	syscall3(int n, int a, int b, int c)
{
	int	r;

	__asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c));
	return (r);
}

static void	putdec(uint32_t v)
{
	char	b[12];
	int		i = 0;

	do
	{
		b[i++] = '0' + (v % 10);
		v /= 10;
	}
	while (v);
	while (i > 0)
		syscall3(SYS_WRITE, 1, (int)&b[--i], 1);
}

static void	putstr(const char *s)
{
	int	len = 0;

	while (s[len])
		len++;
	syscall3(SYS_WRITE, 1, (int)s, len);
}

static uint32_t	ticks(void)
{
	return ((uint32_t)syscall3(SYS_TICKS, 0, 0, 0));
}

void	_start(void)
{
	uint32_t	pid = (uint32_t)syscall3(SYS_GETPID, 0, 0, 0);

	for (int i = 0; i < 25; i++)
	{
		putstr("USER pid ");
		putdec(pid);
		putstr(" iteration ");
		putdec((uint32_t)i);
		putstr("\n");
		uint32_t	dl = ticks() + 10;

		while (ticks() < dl)
			syscall3(SYS_YIELD, 0, 0, 0);
	}
	putstr("USER pid ");
	putdec(pid);
	putstr(" exiting\n");
	syscall3(SYS_EXIT, 0, 0, 0);
	for (;;)
		__asm__ volatile ("");
}