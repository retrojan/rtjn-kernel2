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
#include "sched.h"
#include "usertask.h"
#include "net.h"
#include "dns.h"
#include "icmp.h"
#include "http.h"
#include "vfs.h"
#include "ata.h"
#include "ext2.h"
#include "usb.h"


extern volatile uint8_t		cancel_input;
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

	printk("shell commands(%u) - '", (unsigned)g_command_count);
	term_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
	printk("help <cmd>");
	term_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
	printk("' for details:\n\n");
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
	term_clear();
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

int	cmd_fetch(int argc, char **argv)
{
	(void)argc; (void)argv;
	print_banner();
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
	gdt_ptr_t	ptr;
	gdt_entry_t *e;
	int		entries;

	asm volatile("sgdt %0" : "=m"(ptr) : : "memory");
	printk("GDT base=0x%x limit=%d\n", ptr.base, ptr.limit);
	entries = (ptr.limit + 1) / 8;
	if (entries > 16)
		entries = 16;
	e = (gdt_entry_t*)ptr.base;
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
	sched_list();
	return (0);
}

int	cmd_exec(int argc, char **argv)
{
	int	pid;

	pid = utask_spawn(argc > 1 ? argv[1] : "usertask");
	if (pid < 0)
	{
		printk("exec: could not start a user task\n");
		return (0);
	}
	printk("started ring3 user task pid %d\n", pid);
	return (0);
}

/* ============================================================
**  FILESYSTEM COMMANDS
** ============================================================ */

char	g_cwd[VFS_MAX_PATH] = "/";

static void	strtrim_right(char *s, char c)
{
	size_t	len = strlen(s);

	while (len > 0 && s[len - 1] == c)
		s[--len] = 0;
}

static void	canonicalize(char *path)
{
	char	parts[32][48];
	int	nparts = 0;
	char	*p = path;
	char	comp[64];
	char	out[VFS_MAX_PATH];

	if (path[0] != '/')
		return;
	p++;
	while (*p)
	{
		int	k = 0;

		while (*p && *p != '/')
		{
			if (k < 47)
			{
				comp[k++] = *p;
				comp[k] = 0;
			}
			p++;
		}
		if (*p == '/')
			p++;
		if (k == 0)
			continue;
		if (strcmp(comp, ".") == 0)
			continue;
		if (strcmp(comp, "..") == 0)
		{
			if (nparts > 0)
				nparts--;
			continue;
		}
		if (nparts < 32)
			strcpy(parts[nparts++], comp);
	}
	if (nparts == 0)
	{
		strcpy(path, "/");
		return;
	}
	out[0] = '/';
	out[1] = 0;
	for (int i = 0; i < nparts; i++)
	{
		size_t	off = strlen(out);
		size_t	plen = strlen(parts[i]);

		memcpy(out + off, parts[i], plen);
		out[off + plen] = '/';
		out[off + plen + 1] = 0;
	}
	strtrim_right(out, '/');
	if (out[0] == 0)
		strcpy(out, "/");
	strcpy(path, out);
}

void	vfs_cmd_resolve(const char *path, char *out, size_t out_sz)
{
	char	joined[VFS_MAX_PATH];

	if (!path || out_sz == 0)
		return;
	if (path[0] == '/')
	{
		strncpy(joined, path, sizeof(joined) - 1);
		joined[sizeof(joined) - 1] = 0;
	}
	else if (strcmp(path, ".") == 0 || strcmp(path, "") == 0)
	{
		strncpy(joined, g_cwd, sizeof(joined) - 1);
		joined[sizeof(joined) - 1] = 0;
	}
	else
	{
		if (g_cwd[strlen(g_cwd) - 1] == '/')
			snprintf(joined, sizeof(joined), "%s%s", g_cwd, path);
		else
			snprintf(joined, sizeof(joined), "%s/%s", g_cwd, path);
	}
	canonicalize(joined);
	strncpy(out, joined, out_sz - 1);
	out[out_sz - 1] = 0;
}

static void	vfs_cwd_parent(char *out, size_t out_sz)
{
	size_t	l = strlen(g_cwd);

	if (l <= 1)
	{
		snprintf(out, out_sz, "/");
		return;
	}
	while (l > 1 && g_cwd[l - 1] == '/')
		l--;
	while (l > 1 && g_cwd[l - 1] != '/')
		l--;
	if (l <= 1)
		snprintf(out, out_sz, "/");
	else
	{
		memcpy(out, g_cwd, l - 1);
		out[l - 1] = 0;
	}
}

static int	vfs_is_dir(const char *path)
{
	vfs_node_t	*node = vfs_open_node(path);

	if (!node)
		return (0);
	return (node->flags == VFS_FT_DIR);
}

int	cmd_cd(int argc, char **argv)
{
	char	full[VFS_MAX_PATH];
	const char	*target;

	if (argc <= 1)
	{
		printk("cd: usage: cd <dir>\n");
		return (0);
	}
	target = argv[1];

	if (strcmp(target, "~") == 0)
		target = "/";

	if (strcmp(target, "..") == 0)
		vfs_cwd_parent(full, sizeof(full));
	else
		vfs_cmd_resolve(target, full, sizeof(full));

	if (!vfs_is_dir(full) && strcmp(full, "/") != 0)
	{
		printk("cd: %s: No such directory\n", target);
		return (0);
	}
	strncpy(g_cwd, full, sizeof(g_cwd) - 1);
	g_cwd[sizeof(g_cwd) - 1] = 0;
	return (0);
}

int	cmd_ls(int argc, char **argv)
{
	char	full[VFS_MAX_PATH];
	const char	*path = ".";
	vfs_node_t	node;
	uint32_t	index = 0;

	if (argc > 1)
		path = argv[1];
	vfs_cmd_resolve(path, full, sizeof(full));

	while (vfs_readdir(full, index, &node) == VFS_OK)
	{
		if (node.flags == VFS_FT_DIR)
			printk("%s/\n", node.name);
		else
			printk("%s\n", node.name);
		index++;
	}
	if (index == 0)
		printk("(empty)\n");
	return (0);
}

int	cmd_cat(int argc, char **argv)
{
	uint8_t	buf[1024];
	int	fd;
	int	n;
	char	full[VFS_MAX_PATH];

	if (argc < 2)
	{
		printk("Usage: cat <path>\n");
		return (0);
	}
	vfs_cmd_resolve(argv[1], full, sizeof(full));
	fd = vfs_open(full, VFS_O_READ);
	if (fd < 0)
	{
		printk("cat: cannot open %s\n", argv[1]);
		return (0);
	}
	while ((n = vfs_read(fd, buf, sizeof(buf))) > 0)
	{
		for (int i = 0; i < n; i++)
		{
			if (buf[i] == '\n')
				printk("\n");
			else if (buf[i] >= 0x20 && buf[i] < 0x7F)
				printk("%c", buf[i]);
		}
	}
	vfs_close(fd);
	return (0);
}

int	cmd_mount(int argc, char **argv)
{
	int	ret;

	if (argc < 2)
	{
		printk("Usage: mount <path>   (mounts ext2 on device 0 at path)\n");
		return (0);
	}
	ret = vfs_mount("hd0", argv[1], "ext2");
	if (ret != VFS_OK)
	{
		printk("mount: failed (%d)\n", ret);
		return (0);
	}
	return (0);
}

int	cmd_pwd(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("%s\n", g_cwd);
	return (0);
}

int	cmd_df(int argc, char **argv)
{
	ata_device_t	*dev = ata_get_device(EXT2_DRIVE);

	(void)argc; (void)argv;
	if (!dev || !dev->present)
	{
		printk("df: no disk\n");
		return (0);
	}
	printk("device %d: %s\n", EXT2_DRIVE, dev->model);
	printk("  size   : %u MB\n", (uint32_t)(dev->capacity_sectors / 2048));
	printk("  sectors: %u\n", dev->capacity_sectors);
	return (0);
}

int	cmd_touch(int argc, char **argv)
{
	char	leaf[64];
	char	full[VFS_MAX_PATH];
	vfs_node_t	*parent;
	uint32_t	child_ino;

	if (argc < 2)
	{
		printk("Usage: touch <path>\n");
		return (0);
	}

	vfs_cmd_resolve(argv[1], full, sizeof(full));
	parent = vfs_parent(full, leaf, sizeof(leaf));
	if (!parent || leaf[0] == 0)
	{
		printk("touch: cannot create '%s'\n", argv[1]);
		return (0);
	}

	if (ext2_vfs_create_file(parent, leaf, &child_ino) != VFS_OK)
	{
		printk("touch: failed to create '%s'\n", argv[1]);
		return (0);
	}
	printk("created: %s (inode %d)\n", full, child_ino);
	return (0);
}

int	cmd_mkdir(int argc, char **argv)
{
	char	leaf[64];
	char	full[VFS_MAX_PATH];
	vfs_node_t	*parent;
	uint32_t	child_ino;

	if (argc < 2)
	{
		printk("Usage: mkdir <path>\n");
		return (0);
	}

	vfs_cmd_resolve(argv[1], full, sizeof(full));
	parent = vfs_parent(full, leaf, sizeof(leaf));
	if (!parent || leaf[0] == 0)
	{
		printk("mkdir: cannot create '%s'\n", argv[1]);
		return (0);
	}

	if (ext2_vfs_create_dir(parent, leaf, &child_ino) != VFS_OK)
	{
		printk("mkdir: failed to create '%s'\n", argv[1]);
		return (0);
	}
	printk("created dir: %s (inode %d)\n", full, child_ino);
	return (0);
}

int	cmd_echo_write(int argc, char **argv)
{
	const char	*path;
	char	leaf[64];
	char	full[VFS_MAX_PATH];
	vfs_node_t	*parent;
	uint32_t	child_ino;
	char	content[512];
	size_t	pos = 0;

	if (argc < 2)
	{
		printk("Usage: echo_write <text> <path>\n");
		printk("       echo_write <path>       (create empty file)\n");
		return (0);
	}

	if (argc == 2)
	{
		/* Just touch the file */
		vfs_cmd_resolve(argv[1], full, sizeof(full));
		parent = vfs_parent(full, leaf, sizeof(leaf));
		if (!parent || leaf[0] == 0)
		{
			printk("echo: cannot create '%s'\n", argv[1]);
			return (0);
		}
		if (ext2_vfs_create_file(parent, leaf, &child_ino) != VFS_OK)
		{
			printk("echo: failed to create '%s'\n", argv[1]);
			return (0);
		}
		printk("created: %s\n", full);
		return (0);
	}

	/* argc >= 3: last arg is path, the rest is text */
	path = argv[argc - 1];
	vfs_cmd_resolve(path, full, sizeof(full));

	for (int i = 1; i < argc - 1; i++)
	{
		if (i > 1 && pos < sizeof(content) - 1)
			content[pos++] = ' ';
		for (size_t k = 0; argv[i][k] && pos < sizeof(content) - 1; k++)
			content[pos++] = argv[i][k];
	}
	content[pos] = 0;

	parent = vfs_parent(full, leaf, sizeof(leaf));
	if (!parent || leaf[0] == 0)
	{
		printk("echo: cannot create '%s'\n", path);
		return (0);
	}
	if (ext2_vfs_create_file(parent, leaf, &child_ino) != VFS_OK)
	{
		printk("echo: failed to create '%s'\n", path);
		return (0);
	}

	int	fd = vfs_open(full, VFS_O_READ | VFS_O_WRITE);
	if (fd < 0)
	{
		printk("echo: cannot open '%s'\n", path);
		return (0);
	}
	int	n = vfs_write(fd, (const uint8_t*)content, (uint32_t)pos);
	vfs_close(fd);
	printk("wrote %d bytes to %s\n", n, full);
	return (0);
}

int	cmd_rm(int argc, char **argv)
{
	int	recursive = 0;
	int	start = 1;

	if (argc < 2)
	{
		printk("Usage: rm <path>...      (remove files)\n");
		printk("       rm -r <path>...   (remove directories recursively)\n");
		return (0);
	}
	if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rf") == 0 ||
		strcmp(argv[1], "-fr") == 0)
	{
		recursive = 1;
		start = 2;
	}
	for (int i = start; i < argc; i++)
	{
		char	full[VFS_MAX_PATH];
		vfs_node_t	*node;

		vfs_cmd_resolve(argv[i], full, sizeof(full));
		node = vfs_open_node(full);

		if (!node)
		{
			printk("rm: %s: no such file\n", argv[i]);
			continue;
		}
		if (node->flags == VFS_FT_DIR && !recursive)
		{
			printk("rm: %s: is a directory (use rm -r)\n", argv[i]);
			continue;
		}
		int	ret = recursive ? vfs_rmtree(full) : vfs_unlink(full);
		if (ret == VFS_OK)
			printk("removed: %s\n", full);
		else
			printk("rm: %s: failed (%d)\n", argv[i], ret);
	}
	return (0);
}

int	cmd_rmdir(int argc, char **argv)
{
	if (argc < 2)
	{
		printk("Usage: rmdir <path>...   (remove empty directories)\n");
		return (0);
	}
	for (int i = 1; i < argc; i++)
	{
		char	full[VFS_MAX_PATH];

		vfs_cmd_resolve(argv[i], full, sizeof(full));
		int	ret = vfs_rmdir(full);
		if (ret == VFS_OK)
			printk("removed: %s\n", full);
		else if (ret == VFS_ERR_IS_FILE)
			printk("rmdir: %s: not a directory\n", argv[i]);
		else
			printk("rmdir: %s: failed (%d)\n", argv[i], ret);
	}
	return (0);
}

int	cmd_mv(int argc, char **argv)
{
	char	full1[VFS_MAX_PATH];
	char	full2[VFS_MAX_PATH];

	if (argc < 3)
	{
		printk("Usage: mv <source> <dest>\n");
		printk("       (if <dest> is a directory, <source> is moved into it)\n");
		return (0);
	}
	vfs_cmd_resolve(argv[1], full1, sizeof(full1));
	vfs_cmd_resolve(argv[2], full2, sizeof(full2));
	if (vfs_open_node(full1) == 0)
	{
		printk("mv: %s: no such file\n", argv[1]);
		return (0);
	}
	int	ret = vfs_rename(full1, full2);
	if (ret != VFS_OK)
		printk("mv: %s -> %s failed (%d)\n", full1, full2, ret);
	else
		printk("%s -> %s\n", full1, full2);
	return (0);
}

/* ============================================================
**  NETWORK COMMANDS
** ============================================================ */

static void	print_ip(net_ip4_t ip)
{
	char	buf[16];
	ip4_to_string(ip, buf, sizeof(buf));
	printk("%s", buf);
}

int	cmd_ping(int argc, char **argv)
{
	if (argc < 2)
	{
		printk("Usage: ping <host> [-c count]\n");
		return (0);
	}

	const char	*host = 0;
	int			count = 4;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--count") == 0)
		{
			if (i + 1 >= argc)
			{
				printk("ping: option '%s' requires an argument\n", argv[i]);
				return (0);
			}
			count = atoi(argv[++i]);
			continue;
		}
		if (!host)
			host = argv[i];
	}
	if (!host)
	{
		printk("Usage: ping <host> [-c count]\n");
		return (0);
	}
	if (count < 1)
		count = 1;

	net_ip4_t	ip;
	if (dns_resolve(host, &ip) != 0)
	{
		printk("ping: cannot resolve '%s'\n", host);
		return (0);
	}
	char	ipstr[16];
	ip4_to_string(ip, ipstr, sizeof(ipstr));
	printk("PING %s (%s)\n", host, ipstr);

	int		sent = 0, lost = 0;
	for (int i = 0; i < count; i++)
	{
		uint32_t	rtt = 0;
		if (icmp_ping(ip, &rtt) == 0)
			printk("%d bytes from %s: icmp_seq=%d ttl=64 time=%d ticks\n",
				56, ipstr, i, rtt);
		else
		{
			if (cancel_input)
			{
				printk("^C\n");
				break;
			}
			printk("Request timeout for icmp_seq=%d\n", i);
			lost++;
		}
		sent++;
		if (i + 1 < count)
			sleep(1);
	}
	printk("--- %s ping statistics ---\n", host);
	printk("%d packets transmitted, %d received, %d%% packet loss\n",
		sent, sent - lost, sent ? (lost * 100) / sent : 0);
	return (0);
}

int	cmd_curl(int argc, char **argv)
{
	if (argc < 2)
	{
		printk("Usage: curl <url>\n");
		return (0);
	}
	char	body[4096];
	int		status = -1;
	if (http_get(argv[1], body, sizeof(body), &status) < 0)
	{
		printk("curl: request failed\n");
		return (0);
	}
	printk("HTTP/%d\n", status >= 0 ? status : 0);
	printk("%s\n", body);
	return (0);
}

int	cmd_ifconfig(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("%s: flags=%d<UP BROADCAST RUNNING MULTICAST>\n",
		net_iface.name, (net_iface.up ? 1 : 0));
	printk("        MAC %x:%x:%x:%x:%x:%x\n",
		net_iface.mac.b[0], net_iface.mac.b[1], net_iface.mac.b[2],
		net_iface.mac.b[3], net_iface.mac.b[4], net_iface.mac.b[5]);
	printk("        inet ");
	print_ip(net_iface.ip);
	printk("  netmask ");
	print_ip(net_iface.netmask);
	printk("  gateway ");
	print_ip(net_iface.gateway);
	printk("  dns ");
	print_ip(net_iface.dns);
	printk("\n");
	return (0);
}

int	cmd_netstat(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("Active Internet connections\n");
	printk("Proto  Local Address        Remote Address       State\n");
	printk("tcp    :%-5u                %-17s ESTABLISHED\n",
		(unsigned)0, "(active)");
	return (0);
}

/* ============================================================
**  USB COMMANDS
** ============================================================ */

int	cmd_usb(int argc, char **argv)
{
	(void)argc; (void)argv;
	int	count = usb_get_device_count();

	printk("USB devices: %d\n", count);
	for (int i = 0; i < count; i++)
	{
		usb_device_t	*d = usb_get_device((uint8_t)i);
		if (!d || !d->in_use)
			continue;
		printk("  %d: port %d  speed %s  addr %d\n",
			i, d->port,
			d->speed == USB_SPEED_LOW ? "low" :
			(d->speed == USB_SPEED_HIGH ? "high" : "full"),
			d->address);
		printk("     vid:prod %04x:%04x  class %d\n",
			d->vendor_id, d->product_id, d->device_class);
		for (int j = 0; j < 4 && j < d->num_interfaces; j++)
		{
			printk("     iface %d: class %d sub %d proto %d\n",
				j, d->interfaces[j].class,
				d->interfaces[j].subclass, d->interfaces[j].protocol);
		}
	}
	return (0);
}

/* ============================================================
**  COMMAND DESCRIPTOR TABLE
** ============================================================ */

t_command	g_commands[] = {
	{"fetch",		"Print the kernel fastfetch-style banner",		cmd_fetch},
	{"history",		"Print command history",					cmd_history},
	{"echo",		"Echo the given arguments",					cmd_echo},
	{"sleep",		"Sleep for N seconds",						cmd_sleep},
	{"calc",		"Simple arithmetic calculator",			cmd_calc},
	{"hexdump",		"Dump memory at an address",				cmd_hexdump},
	{"cpuid",		"Print CPU info",							cmd_cpuid},
	{"gdt",			"Dump the Global Descriptor Table",		cmd_gdt},
	{"idt",			"Dump the Interrupt Descriptor Table",	cmd_idt},
	{"irq",			"List registered IRQ handlers",			cmd_irq},
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
	{"ps",			"List running kernel tasks",				cmd_ps},
	{"exec",		"Spawn a ring3 user task",					cmd_exec},
	{"ifconfig",	"Show the network interface config",		cmd_ifconfig},
	{"netstat",		"Show network link/packet state",			cmd_netstat},
	{"ping",		"ICMP echo (default 4, or -c count)",		cmd_ping},
	{"curl",		"Fetch a URL over the network",				cmd_curl},
};
const size_t	g_command_count = sizeof(g_commands) / sizeof(g_commands[0]);
