#include "printk.h"
# include "commands.h"
#include "string.h"
#include "stack.h"
#include "readline.h"
#include "io.h"
#include "version.h"
#include "acpi.h"
#include "term.h"
#include "vga.h"
#include "memory.h"
#define TERM_BUFF	255

extern  volatile uint32_t	timer_ticks;
void	print_physical_memory(void);
void	demo_vmalloc(void);
void	demo_kmalloc(void);
void	demo_page_panic(void);
void	demo_div_panic(void);
void	demo_paging(void);

static void reboot(void)
{
	uint8_t	kbrd_status = 0x02;

	while (kbrd_status & 0x02)
		kbrd_status = inb(0x64);

	asm volatile ("cli");
	outb(0x64, 0xFE);
loop:
	asm volatile ("hlt");
	goto loop;

}

static void	shutdown(void)
{
	printk("Powering off...\n");
	acpi_shutdown();
}

static void	print_info_label(const char *label)
{
	uint8_t	saved = t_color;
	size_t	len = strlen(label);

	printk(" %s", label);
	while (len++ < 8)
		printk(" ");	/* pad the label so values align */
	term_setcolor(saved);
	printk(": ");
}

static void	print_cpu_brand(char *out, size_t out_size)
{
	char	brand[49];
	uint32_t eax = 0;

	out[0] = 0;
	asm volatile("cpuid" : "=a"(eax) : "a"(0x80000000));
	if (eax < 0x80000004)
	{
		strncpy(out, "unknown", out_size - 1);
		out[out_size - 1] = 0;
		return ;
	}
	for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++)
	{
		uint32_t ebx = 0, ecx = 0, edx = 0;
		asm volatile("cpuid"
			: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf));
		uint32_t	*word = (uint32_t*)(brand + (leaf - 0x80000002) * 16);
		word[0] = eax; word[1] = ebx; word[2] = ecx; word[3] = edx;
	}
	brand[48] = 0;
	strncpy(out, brand, out_size - 1);
	out[out_size - 1] = 0;
}

#define BANNER_LOGO_W	26

static void	print_logo_line(const char *line)
{
	size_t	len = strlen(line);
	uint8_t	saved = t_color;

	term_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
	printk("%s", line);
	while (len++ < BANNER_LOGO_W)
		printk(" ");
	term_setcolor(saved);
}

void	print_banner(void)
{
	const char	*logo[] = {
		"  ..............",
		" `/..@@@@@@@@.\\.",
		"``@`/......\\\\@.\\",
		"\\\\\\      \\`@```",
		" ``@``     .//@//",
		" `\\.`\\....//.@///",
		"  \\`@\\@@@@@\\@```",
		" ``@`/....\\\\@.\\.",
		" `\\@`\\     .\\\\@\\",
		"  \\`@\\`      \\\\.@``",
		"  `\\./`       \\...",
		"    ...",
	};
	char		cpu[64];
	uint32_t	total_kb = (_pm_blocks * 4096) / 1024;
	uint32_t	used_kb = (_pm_used * 4096) / 1024;

	print_cpu_brand(cpu, sizeof(cpu));

	print_logo_line(logo[0]);
	printk("\n");

	print_logo_line(logo[1]);
	print_info_label("OS");
	printk("rtjn-kernel ");
	term_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
	printk("%s\n", RTJN_VERSION);
	term_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

	print_logo_line(logo[2]);
	print_info_label("Host");
	printk("%s\n", RTJN_AUTHOR);

	print_logo_line(logo[3]);
	print_info_label("Kernel");
	printk("rtjn-kernel-i686-");
	printk("%s\n", RTJN_VERSION);


	print_logo_line(logo[4]);
	print_info_label("CPU");
	printk("%s\n", cpu);


	print_logo_line(logo[5]);
	print_info_label("Memory");
	printk("%u", used_kb / 1024);
	printk(" MiB / ");
	printk("%u", total_kb / 1024);
	term_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
	printk(" MiB\n");

	print_logo_line(logo[6]);
	print_info_label("Uptime");
	printk("%d minutes %d seconds\n", (timer_ticks / 18) / 60, (timer_ticks / 18) % 60);

	for (size_t i = 8; i < 12; i++)
	{
		print_logo_line(logo[i]);
		printk("\n");
	}
	printk("\n");
}

static void	dispatch(char *buf)
{
	char	*argv[16];
	int		argc = 0;
	size_t	i;

	char *cur = buf;
	while (*cur != 0 && argc < 16)
	{
		while (*cur == ' ' || *cur == '\t')
			cur++;
		if (*cur == 0)
			break ;
		argv[argc++] = cur;
		while (*cur != 0 && *cur != ' ' && *cur != '\t')
			cur++;
		if (*cur != 0)
		{
			*cur = 0;
			cur++;
		}
	}
	if (argc == 0)
		return ;
	if (strcmp(argv[0], "help") == 0)
		cmd_help(argc, argv);
	else if (strcmp(argv[0], "clear") == 0)
		cmd_clear(0, 0);
	else if (strcmp(argv[0], "time") == 0)
		printk("Up time is %d minutes %d seconds.\n", (timer_ticks / 18) / 60, (timer_ticks / 18) % 60);
	else if (strcmp(argv[0], "shutdown") == 0)
		shutdown();
	else if (strcmp(argv[0], "reboot") == 0)
		reboot();
	else if (strcmp(argv[0], "stacktrace") == 0)
		print_stack(1);
	else if (strcmp(argv[0], "map") == 0)
		print_physical_memory();
	else if (strcmp(argv[0], "kmalloc") == 0)
		demo_kmalloc();
	else if (strcmp(argv[0], "vmalloc") == 0)
		demo_vmalloc();
	else if (strcmp(argv[0], "paging") == 0)
		demo_paging();
	else if (strcmp(argv[0], "page_panic") == 0)
		demo_page_panic();
	else if (strcmp(argv[0], "div_panic") == 0)
		demo_div_panic();
	else
	{
		int	found = 0;
		for (i = 0; i < g_command_count; i++)
		{
			if (strcmp(argv[0], g_commands[i].name) == 0)
			{
				((int (*)(int, char**))g_commands[i].func)(argc, argv);
				found = 1;
				break ;
			}
		}
		if (!found)
			printk("rtjn-kernel2: Command not found.\n");
	}
}

void	shell(void)
{
	char	buf[TERM_BUFF + 1];

	print_banner();
	while (1)
	{
		bzero(buf, TERM_BUFF + 1);
		printk("%s> ", g_cwd);
		readline(buf, TERM_BUFF);
		printk("\n");
		if (strlen(buf) == 0)
			continue ;
		shell_record(buf);
		buf[TERM_BUFF] = 0;
		dispatch(buf);
	}
}
