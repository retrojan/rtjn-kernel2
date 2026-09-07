#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "string.h"
#include "printk.h"
#include "term.h"
#include "vga.h"
#include "io.h"
#include "irq.h"
#include "memory.h"
#include "time.h"
#include "gdt.h"
#include "idt.h"
#include "readline.h"
#include "commands.h"
#include "version.h"
#include "sched.h"
#include "usertask.h"


extern volatile uint32_t	timer_ticks;
extern volatile uint8_t		in_read;
extern volatile char		read_key;
extern uint32_t				_pmmngr_max_blocks;
extern uint32_t				_pmmngr_used_blocks;
extern void					*irq_routines[];

//  GLOBAL SHELL STATE 

static char	s_hist[64][64];
static size_t	s_hist_count = 0;
static size_t	s_hist_pos = 0;

const char	*shell_history_get(size_t idx)
{
	if (idx < s_hist_count)
		return (s_hist[idx]);
	return (0);
}

size_t	shell_history_count(void)
{
	return (s_hist_count);
}

const char	*shell_history_current(void)
{
	return (s_hist_pos < s_hist_count ? s_hist[s_hist_pos] : 0);
}

void	shell_history_pos_set(size_t p)
{
	s_hist_pos = p;
}

void	shell_record(char *line)
{
	size_t	i;
	size_t	len;

	len = strlen(line);
	if (len == 0)
		return;
	if (s_hist_count == 0 || strcmp(s_hist[s_hist_count - 1], line) != 0)
	{
		if (s_hist_count < 64)
		{
			for (i = 0; line[i] != 0 && i < 63; i++)
				s_hist[s_hist_count][i] = line[i];
			s_hist[s_hist_count][i] = 0;
			s_hist_count++;
		}
	}
	s_hist_pos = s_hist_count;
}

void	shell_history_prev(void)
{
	if (s_hist_count == 0)
		return;
	if (s_hist_pos > 0)
		s_hist_pos--;
}

void	shell_history_next(void)
{
	if (s_hist_count == 0)
		return;
	if (s_hist_pos < s_hist_count - 1)
		s_hist_pos++;
}

/* ============================================================
**  COMMAND IMPLEMENTATIONS
** ============================================================ */

int	cmd_help(int argc, char **argv)
{
	if (argc > 1)
	{
		for (size_t i = 0; i < g_command_count; i++)
		{
			if (strcmp(argv[1], g_commands[i].name) == 0)
			{
				printk("%s - %s\n", g_commands[i].name, g_commands[i].help);
				return (0);
			}
		}
		printk("help: no such command '%s' (see 'help')\n", argv[1]);
		return (0);
	}

	size_t	maxlen = 1;
	size_t	i;

	for (i = 0; i < g_command_count; i++)
	{
		size_t	l = strlen(g_commands[i].name);
		if (l > maxlen)
			maxlen = l;
	}

	size_t	colwidth = maxlen + 3;
	size_t	cols = (size_t)term_width / colwidth;
	if (cols < 1)
		cols = 1;

	printk("retrojan shell commands (%u) - 'help <cmd>' for details:\n",
		(unsigned)g_command_count);
	for (i = 0; i < g_command_count; i++)
	{
		printk("%s", g_commands[i].name);
		size_t	pad = colwidth - strlen(g_commands[i].name);
		for (size_t k = 0; k < pad; k++)
			printk(" ");
		if ((i + 1) % cols == 0)
			printk("\n");
	}
	printk("\n");
	return (0);
}

int	cmd_clear(int argc, char **argv)
{
	(void)argc; (void)argv;
	terminal_clear();
	return (0);
}

int	cmd_uname(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "-a") == 0)
		printk("%s %s %s %s\n", RTJN_RELEASENAME, RTJN_VERSION);
	else
		printk("%s\n", RTJN_RELEASENAME);
	return (0);
}

int	cmd_whoami(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("root\n");
	return (0);
}

int	cmd_version(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("rtjn-kernel %s \n", RTJN_VERSION);
	return (0);
}

int	cmd_uptime(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("Up time is %d minutes %d seconds.\n", (timer_ticks / 18) / 60, (timer_ticks / 18) % 60);
	return (0);
}

int	cmd_meminfo(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("Physical memory:\n");
	printk("  max blocks : %d\n", _pmmngr_max_blocks);
	printk("  used blocks: %d\n", _pmmngr_used_blocks);
	printk("  free blocks: %d\n", _pmmngr_max_blocks - _pmmngr_used_blocks);
	printk("  total      : %d KB\n", (_pmmngr_max_blocks * 4096) / 1024);
	printk("  used       : %d KB\n", (_pmmngr_used_blocks * 4096) / 1024);
	printk("  free       : %d KB\n", ((_pmmngr_max_blocks - _pmmngr_used_blocks) * 4096) / 1024);
	return (0);
}

int	cmd_history(int argc, char **argv)
{
	(void)argc; (void)argv;
	size_t	n = shell_history_count();

	if (n == 0)
	{
		printk("No history.\n");
		return (0);
	}
	for (size_t i = 0; i < n; i++)
		printk("  %d  %s\n", (int)(n - 1 - i), shell_history_get(n - 1 - i));
	return (0);
}

int	cmd_logo(int argc, char **argv)
{	
    printk("  .............\n");
    printk(" `/..@@@@@@@@.\\\\.\n");
    printk("``@`/......\\\\@.\\\\\n");
    printk("\\\\\\\\\\      \\`@```\n");
    printk(" ``@``     .//@//	" "	rtjn-kernel-%s\n",RTJN_VERSION);
    printk(" `\\.`\\....//.@///\n");
    printk("  \\`@\\@@@@@\\@```\n");
    printk(" ``@`/....\\\\@.\\.\n");
    printk(" `\\@`\\     .\\\\@\\\\.\n");
    printk("  \\`@\\`      \\\\.@``\n");
    printk("  `\\./`       \\...\n");
    printk("	...\n\n");
	return 0;
}


int	cmd_echo(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
	{
		if (i > 1)
			printk(" ");
		printk("%s", argv[i]);
	}
	printk("\n");
	return (0);
}

int	cmd_color(int argc, char **argv)
{
	static const uint8_t palette[16] = {
		VGA_COLOR_BLACK, VGA_COLOR_BLUE, VGA_COLOR_GREEN, VGA_COLOR_CYAN,
		VGA_COLOR_RED, VGA_COLOR_MAGENTA, VGA_COLOR_BROWN, VGA_COLOR_LIGHT_GREY,
		VGA_COLOR_DARK_GREY, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_LIGHT_GREEN,
		VGA_COLOR_LIGHT_CYAN, VGA_COLOR_LIGHT_RED, VGA_COLOR_LIGHT_MAGENTA,
		VGA_COLOR_LIGHT_BROWN, VGA_COLOR_WHITE
	};
	static const char *names[16] = {
		"black", "blue", "green", "cyan", "red", "magenta", "brown", "grey",
		"darkgrey", "lightblue", "lightgreen", "lightcyan", "lightred",
		"lightmagenta", "lightbrown", "white"
	};

	if (argc < 2)
	{
		printk("Usage: color <name> [0-15]\n");
		return (0);
	}
	for (int i = 0; i < 16; i++)
	{
		if (strcmp(argv[1], names[i]) == 0)
		{
			uint8_t bg = VGA_COLOR_BLACK;
			if (argc > 2)
				bg = (uint8_t)atoi(argv[2]) & 0xF;
			terminal_setcolor(vga_entry_color((enum vga_color)palette[i], (enum vga_color)bg));
			return (0);
		}
	}
	printk("Unknown color '%s'\n", argv[1]);
	return (0);
}

int	cmd_ctest(int argc, char **argv)
{
	(void)argc; (void)argv;
	uint8_t	saved = terminal_color;

	for (int i = 0; i < 16; i++)
	{
		terminal_setcolor(vga_entry_color((enum vga_color)i, VGA_COLOR_BLACK));
		printk("Color %2d  ABCDEF ", i);
		printk("\n");
	}
	terminal_setcolor(saved);
	printk("Color test complete.\n");
	return (0);
}

int	cmd_sleep(int argc, char **argv)
{
	uint32_t secs = 1;

	if (argc > 1)
		secs = (uint32_t)atoi(argv[1]);
	printk("Sleeping %d second(s)...\n", secs);
	sleep(secs);
	printk("Done.\n");
	return (0);
}

static int	apply_operator(int acc, char op, int val)
{
	switch (op)
	{
		case '+': return (acc + val);
		case '-': return (acc - val);
		case '*': return (acc * val);
		case '/': return (val != 0 ? acc / val : acc);
		case '%': return (val != 0 ? acc % val : acc);
		default: return (acc);
	}
}

int	cmd_calc(int argc, char **argv)
{
	if (argc < 2)
	{
		printk("Usage: calc <num> <op> <num> [op num]...  (op: + - * / %%)\n");
		return (0);
	}
	int acc = atoi(argv[1]);
	char op = 0;
	for (int i = 2; i < argc; i++)
	{
		if (argv[i][0] == 0)
			continue;
		if (argv[i][1] == 0 && (argv[i][0] == '+' || argv[i][0] == '-' ||
			argv[i][0] == '*' || argv[i][0] == '/' || argv[i][0] == '%'))
		{
			op = argv[i][0];
		}
		else
		{
			if (op == 0)
			{
				printk("Expected operator before '%s'\n", argv[i]);
				return (0);
			}
			acc = apply_operator(acc, op, atoi(argv[i]));
			op = 0;
		}
	}
	printk("= %d\n", acc);
	return (0);
}

int	cmd_hexdump(int argc, char **argv)
{
	uint32_t addr = 0;
	uint32_t len = 64;

	if (argc > 1)
		addr = (uint32_t)atoi(argv[1]);
	if (argc > 2)
		len = (uint32_t)atoi(argv[2]);
	uint8_t *p = (uint8_t*)addr;
	int row = 0;
	for (uint32_t i = 0; i < len; i += 16)
	{
		printk("%08x  ", (uint32_t)(p + i));
		for (uint32_t j = 0; j < 16 && i + j < len; j++)
		{
			printk("%02x ", p[i + j]);
			if (j == 7)
				printk(" ");
		}
		printk("\n");
		row++;
		if (row >= 64)
		{
			printk("... truncated\n");
			break;
		}
	}
	return (0);
}

int	cmd_cpuid(int argc, char **argv)
{
	(void)argc; (void)argv;
	uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

	asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
	printk("Vendor: %c%c%c%c%c%c%c%c%c%c%c%c\n",
		(char)(ebx & 0xff), (char)((ebx >> 8) & 0xff), (char)((ebx >> 16) & 0xff),
		(char)((ebx >> 24) & 0xff), (char)(edx & 0xff), (char)((edx >> 8) & 0xff),
		(char)((edx >> 16) & 0xff), (char)((edx >> 24) & 0xff), (char)(ecx & 0xff),
		(char)((ecx >> 8) & 0xff), (char)((ecx >> 16) & 0xff), (char)((ecx >> 24) & 0xff));
	asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
	printk("Max EAX: %x\n", (unsigned)eax);
	uint32_t flags = edx;
	if (flags & (1 << 15)) printk("  CMOV supported\n");
	if (flags & (1 << 23)) printk("  MMX supported\n");
	if (flags & (1 << 25)) printk("  SSE supported\n");
	if (flags & (1 << 26)) printk("  SSE2 supported\n");
	return (0);
}

int	cmd_gdt(int argc, char **argv)
{
	(void)argc; (void)argv;
	gdt_ptr	ptr;
	gdt_entry *e;
	int		entries;

	asm volatile("sgdt %0" : "=m"(ptr) : : "memory");
	printk("GDT base=0x%x limit=%d\n", ptr.base, ptr.limit);
	entries = (ptr.limit + 1) / 8;
	if (entries > 16)
		entries = 16;
	e = (gdt_entry*)ptr.base;
	for (int i = 0; i < entries; i++)
	{
		printk("  [%d] base=0x%x access=0x%x flags=0x%x\n", i,
			(uint32_t)((e[i].base_low) | (e[i].base_middle << 16) | ((uint32_t)e[i].base_high << 24)),
			e[i].access, e[i].flags);
	}
	return (0);
}

int	cmd_idt(int argc, char **argv)
{
	(void)argc; (void)argv;
	idtr_t	ptr;

	asm volatile("sidt %0" : "=m"(ptr) : : "memory");
	printk("IDT base=0x%x limit=%d\n", ptr.base, ptr.limit);
	return (0);
}

int	cmd_irq(int argc, char **argv)
{
	(void)argc; (void)argv;
	for (int i = 0; i < 16; i++)
	{
		if (irq_routines[i])
			printk("  IRQ%d: registered\n", i);
	}
	return (0);
}

int	cmd_hlt(int argc, char **argv)
{
	(void)argc; (void)argv;
	asm volatile("hlt");
	return (0);
}

int	cmd_tfault(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("Triggering triple fault...\n");
	uint8_t *zerodiv = (uint8_t*)0x0;
	*zerodiv = 0;
	return (0);
}

int	cmd_stkp(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("Recursing to overflow the stack...\n");
	volatile uint8_t big[8192];
	(void)big;
	cmd_stkp(0, 0);
	return (0);
}

/* ============================================================
**  CMOS real-time clock helpers
** ============================================================ */
static uint8_t	rtc_read(uint8_t reg)
{
	outb(0x70, reg);
	return (inb(0x71));
}

static uint8_t	rtc_conv(uint8_t v, int bcd)
{
	if (bcd)
		return ((v & 0x0F) + (v >> 4) * 10);
	return (v);
}

/* ============================================================
**  System / info commands
** ============================================================ */
int	cmd_info(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("retrojan kernel\n");
	printk("  author   : retrojan\n");
	printk("  arch     : i686\n");
	printk("  boot     : BIOS\n");
	printk("  console  : VGA text\n");
	return (0);
}

int	cmd_arch(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("i686\n");
	return (0);
}

int	cmd_pit(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("Timer ticks: %u\n", (unsigned)timer_ticks);
	return (0);
}

int	cmd_date(int argc, char **argv)
{
	(void)argc; (void)argv;
	uint8_t	status = rtc_read(0x0B);
	int		bcd = !(status & 0x04);
	uint8_t	sec  = rtc_conv(rtc_read(0x00), bcd);
	uint8_t	min  = rtc_conv(rtc_read(0x02), bcd);
	uint8_t	hour = rtc_conv(rtc_read(0x04), bcd);
	uint8_t	day  = rtc_conv(rtc_read(0x07), bcd);
	uint8_t	mon  = rtc_conv(rtc_read(0x08), bcd);
	uint8_t	year = rtc_conv(rtc_read(0x09), bcd);
	uint8_t	cent = rtc_conv(rtc_read(0x32), bcd);
	uint16_t	yy = cent ? (uint16_t)(cent * 100 + year) : (uint16_t)(2000 + year);
	printk("%04d-%02d-%02d %02d:%02d:%02d\n", yy, mon, day, hour, min, sec);
	return (0);
}

int	cmd_cpu(int argc, char **argv)
{
	(void)argc; (void)argv;
	char	brand[49];

	uint32_t	eax = 0, ebx = 0, ecx = 0, edx = 0;
	asm volatile("cpuid" : "=a"(eax) : "a"(0x80000000));
	if (eax < 0x80000004)
	{
		printk("No brand string available.\n");
		return (0);
	}
	for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++)
	{
		asm volatile("cpuid"
			: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf));
		uint32_t	*word = (uint32_t*)(brand + (leaf - 0x80000002) * 16);
		word[0] = eax; word[1] = ebx; word[2] = ecx; word[3] = edx;
	}
	brand[48] = 0;
	printk("CPU: %s\n", brand);
	return (0);
}


int	cmd_ps(int argc, char **argv)
{
	(void)argc; (void)argv;
	sched_list_tasks();
	return (0);
}

int	cmd_exec(int argc, char **argv)
{
	int	pid;

	pid = usertask_spawn(argc > 1 ? argv[1] : "usertask");
	if (pid < 0)
	{
		printk("exec: could not start a user task\n");
		return (0);
	}
	printk("started ring3 user task pid %d\n", pid);
	return (0);
}

/* ============================================================
**  COMMAND DESCRIPTOR TABLE
** ============================================================ */

t_command	g_commands[] = {
	{"logo",		"Print the kernel ASCII art banner",		cmd_logo},
	{"uname",		"Print system name (-a for details)",		cmd_uname},
	{"whoami",		"Print current user",						cmd_whoami},
	{"version",		"Print kernel version",						cmd_version},
	{"uptime",		"Print system uptime",						cmd_uptime},
	{"meminfo",		"Print physical memory usage",			cmd_meminfo},
	{"history",		"Print command history",					cmd_history},
	{"echo",		"Echo the given arguments",					cmd_echo},
	{"color",		"Set terminal color",						cmd_color},
	{"ctest",		"Show all available colors",				cmd_ctest},
	{"sleep",		"Sleep for N seconds",						cmd_sleep},
	{"calc",		"Simple arithmetic calculator",			cmd_calc},
	{"hexdump",		"Dump memory at an address",				cmd_hexdump},
	{"cpuid",		"Print CPU info",							cmd_cpuid},
	{"gdt",			"Dump the Global Descriptor Table",		cmd_gdt},
	{"idt",			"Dump the Interrupt Descriptor Table",	cmd_idt},
	{"irq",			"List registered IRQ handlers",			cmd_irq},
	{"hlt",			"Pause the CPU (halt)",						cmd_hlt},
	{"tfault",		"Trigger a triple fault",					cmd_tfault},
	{"stkp",		"Overflow the stack on purpose",			cmd_stkp},
	{"info",		"Show kernel information",					cmd_info},
	{"arch",		"Print the machine architecture",			cmd_arch},
	{"pit",			"Show PIT timer ticks",						cmd_pit},
	{"date",		"Read the CMOS real-time clock",			cmd_date},
	{"cpu",			"Print the CPU brand string",				cmd_cpu},
	{"ls",			"List a directory",							cmd_ls},
	{"cat",			"Print a file",								cmd_cat},
	{"cd",			"Change working directory",					cmd_cd},
	{"mount",		"Mount a filesystem",						cmd_mount},
	{"pwd",			"Print working directory",					cmd_pwd},
	{"df",			"Show disk usage",							cmd_df},
	{"touch",		"Create an empty file",						cmd_touch},
	{"mkdir",		"Create a new directory",					cmd_mkdir},
	{"echo_write",	"Write text to a file",						cmd_echo_write},
	{"rm",			"Remove a file (-r for directories)",		cmd_rm},
	{"rmdir",		"Remove an empty directory",				cmd_rmdir},
	{"mv",			"Rename or move a file/directory",			cmd_mv},
	{"nano",		"Simple text editor",						cmd_nano},
	{"vi",			"Minimal vi editor",						cmd_vi},
	{"usb",			"List USB devices",							cmd_usb},
	{"uhci",		"Show UHCI host controller info",			cmd_uhci},
	{"ps",			"List running kernel tasks",				cmd_ps},
	{"exec",		"Spawn a ring3 user task",					cmd_exec},
	{"ifconfig",	"Show the network interface config",		cmd_ifconfig},
	{"netstat",		"Show network link/packet state",			cmd_netstat},
	{"ping",		"ICMP echo (default 4, or -c count)",		cmd_ping},
	{"curl",		"Fetch a URL over the network",				cmd_curl},
};
const size_t	g_command_count = sizeof(g_commands) / sizeof(g_commands[0]);
