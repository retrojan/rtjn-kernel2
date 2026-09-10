/* ring-3 microkernel shell. Runs entirely in user space; talks to the kernel
 * through syscalls for terminal I/O. FS/net commands will talk to the
 * respective servers over IPC once those exist. */
#include <stdint.h>
#include <stddef.h>
#include "ipc.h"

#define SHELL_BUFF	255

static char		g_history[16][64];
static size_t	g_hist_count = 0;
static size_t	g_hist_pos = 0;

/* minimal freestanding string helpers (no libc in ring 3) */
static int	ustrcmp(const char *a, const char *b)
{
	while (*a && *b && *a == *b)
	{
		a++;
		b++;
	}
	return ((unsigned char)*a - (unsigned char)*b);
}

static void	umemcpy(void *dst, const void *src, size_t n)
{
	unsigned char	*d = dst;
	const unsigned char	*s = src;

	while (n--)
		*d++ = *s++;
}

static void	umemmove(void *dst, const void *src, size_t n)
{
	unsigned char	*d = dst;
	const unsigned char	*s = src;

	if (d < s)
		umemcpy(dst, src, n);
	else
	{
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
}

static int	umemcmp(const void *a, const void *b, size_t n)
{
	const unsigned char	*x = a;
	const unsigned char	*y = b;

	while (n--)
	{
		if (*x != *y)
			return (*x - *y);
		x++;
		y++;
	}
	return (0);
}

static void	putch(char c)
{
	syscall3(SYS_WRITE, 1, (int)&c, 1);
}

static void	putstr(const char *s)
{
	int	len = 0;

	while (s[len])
		len++;
	syscall3(SYS_WRITE, 1, (int)s, len);
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
		putch(b[--i]);
}

static void	record_history(const char *line)
{
	if (g_hist_count == 0 ||
		umemcmp(g_history[g_hist_count - 1], line, 64) != 0)
	{
		size_t	i = 0;

		if (g_hist_count < 16)
			g_hist_count++;
		for (i = 0; g_hist_count - 1 < 16 && line[i] && i < 63; i++)
			g_history[g_hist_count - 1][i] = line[i];
		g_history[g_hist_count - 1][i] = 0;
	}
	g_hist_pos = g_hist_count;
}

static int	readline(char *buf, size_t size)
{
	size_t	i = 0;
	size_t	pos = 0;

	while (i < size)
	{
		char	ch = 0;
		int		n = syscall3(SYS_READ_KEY, (int)&ch, 1, 0);

		if (n <= 0)
		{
			syscall3(SYS_YIELD, 0, 0, 0);
			continue;
		}
		switch (ch)
		{
			case '\n':
				putch('\n');
				buf[i] = 0;
				return ((int)i);
			case '\b':
				if (pos > 0)
				{
					umemmove(buf + pos - 1, buf + pos,
						i - pos + 1);
					i--;
					pos--;
					putstr("\b \b");
				}
				break;
			case 0x10: /* up */
				if (g_hist_count)
				{
					if (g_hist_pos > 0)
						g_hist_pos--;
					i = pos = 0;
					while (g_history[g_hist_pos][i] && i < size - 1)
					{
						buf[i] = g_history[g_hist_pos][i];
						i++;
					}
					buf[i] = 0;
					putstr("\r\033[K");
					putstr(buf);
					pos = i;
				}
				break;
			case 0x12: /* down */
				if (g_hist_count)
				{
					if (g_hist_pos < g_hist_count - 1)
						g_hist_pos++;
					i = pos = 0;
					if (g_hist_pos < g_hist_count)
					{
						while (g_history[g_hist_pos][i] && i < size - 1)
						{
							buf[i] = g_history[g_hist_pos][i];
							i++;
						}
					}
					buf[i] = 0;
					putstr("\r\033[K");
					putstr(buf);
					pos = i;
				}
				break;
			default:
				if (ch >= 0x20 && ch < 0x7F && i < size - 1)
				{
					umemmove(buf + pos + 1, buf + pos,
						i - pos + 1);
					buf[pos] = ch;
					i++;
					pos++;
					putch(ch);
				}
				break;
		}
	}
	buf[i] = 0;
	return ((int)i);
}

static int	parse(char *buf, char **argv, int max)
{
	int	argc = 0;
	char	*cur = buf;

	while (*cur != 0 && argc < max)
	{
		while (*cur == ' ' || *cur == '\t')
			cur++;
		if (*cur == 0)
			break;
		argv[argc++] = cur;
		while (*cur != 0 && *cur != ' ' && *cur != '\t')
			cur++;
		if (*cur != 0)
		{
			*cur = 0;
			cur++;
		}
	}
	return (argc);
}

static void	cmd_help(void)
{
	putstr("shell commands:\n"
		"  help       - show this list\n"
		"  clear      - clear the terminal\n"
		"  echo <t>   - echo text\n"
		"  ps         - list tasks\n"
		"  uptime     - show uptime\n"
		"  pid        - print my pid\n"
		"  exec       - spawn a child user task\n"
		"  ipc_demo   - spawn two tasks that talk over IPC\n"
		"  version    - kernel version\n"
		"  shutdown   - power off\n");
}

static uint32_t	ticks(void)
{
	return ((uint32_t)syscall3(SYS_TICKS, 0, 0, 0));
}

static void	cmd_ps(void)
{
	putstr("pid state   name\n");
	putstr("--- -----   ----\n");
	putstr(" 0  running  kernel\n");
}

static void	cmd_exec(void)
{
	putstr("spawning user task...\n");
}

static void	cmd_dispatch(char *buf)
{
	char	*argv[16];
	int		argc = parse(buf, argv, 16);

	if (argc == 0)
		return;
	if (ustrcmp(argv[0], "help") == 0)
		cmd_help();
	else if (ustrcmp(argv[0], "clear") == 0)
		syscall3(SYS_WRITE, 1, (int)"\033[2J\033[H", 7);
	else if (ustrcmp(argv[0], "echo") == 0)
	{
		for (int i = 1; i < argc; i++)
		{
			if (i > 1)
				putch(' ');
			putstr(argv[i]);
		}
		putch('\n');
	}
	else if (ustrcmp(argv[0], "ps") == 0)
		cmd_ps();
	else if (ustrcmp(argv[0], "uptime") == 0)
	{
		uint32_t	t = ticks();

		putstr("Up time is ");
		putdec(t / 18 / 60);
		putstr(" minutes ");
		putdec(t / 18 % 60);
		putstr(" seconds.\n");
	}
	else if (ustrcmp(argv[0], "pid") == 0)
	{
		putstr("pid ");
		putdec((uint32_t)syscall3(SYS_GETPID, 0, 0, 0));
		putch('\n');
	}
	else if (ustrcmp(argv[0], "exec") == 0)
		cmd_exec();
	else if (ustrcmp(argv[0], "version") == 0)
		putstr("rtjn-kernel-i686 v0.5.14 (microkernel)\n");
	else if (ustrcmp(argv[0], "shutdown") == 0)
		syscall3(SYS_EXIT, 0, 0, 0);
	else
	{
		putstr("rtjn-shell: Command not found. (try 'help')\n");
	}
}

__attribute__((section(".text._start")))
void	_start(void)
{
	char	buf[SHELL_BUFF + 1];

	putstr("rtjn-kernel microkernel shell\n");
	putstr("type 'help' for commands\n\n");
	for (;;)
	{
		putstr("> ");
		if (readline(buf, SHELL_BUFF) < 0)
			continue;
		if (buf[0] == 0)
		{
			putch('\n');
			continue;
		}
		record_history(buf);
		cmd_dispatch(buf);
	}
}