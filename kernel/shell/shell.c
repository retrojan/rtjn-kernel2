#include "printk.h"
# include "commands.h"
#include "string.h"
#include "stack.h"
#include "readline.h"
#include "io.h"
#include "version.h"
#include "acpi.h"
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

static void	print_banner(void)
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
			printk("retrojan: Command not found.\n");
	}
}

void	shell(void)
{
	char	buf[TERM_BUFF + 1];

	print_banner();
	while (1)
	{
		bzero(buf, TERM_BUFF + 1);
		printk("# %s> ", g_cwd);
		readline(buf, TERM_BUFF);
		printk("\n");
		if (strlen(buf) == 0)
			continue ;
		shell_record(buf);
		buf[TERM_BUFF] = 0;
		dispatch(buf);
	}
}
