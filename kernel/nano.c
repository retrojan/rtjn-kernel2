#include <stddef.h>
#include <stdint.h>
#include "string.h"
#include "printk.h"
#include "term.h"
#include "vga.h"
#include "cursor.h"
#include "vfs.h"
#include "commands.h"

/* Keyboard state consumed by the editor */
extern volatile uint8_t	in_read;
extern volatile char	read_key;
extern volatile int8_t	nav_key;
extern volatile uint8_t	ctrl_char;
extern volatile uint8_t	cancel_input;
extern volatile uint8_t	do_clear_screen;

#define NANO_MAX_LINES	512
#define NANO_MAX_COL	160
#define NANO_STATUS_SZ	80

/* Editor event encodings */
#define EV_NAV(c)	(0x100 | (c))	/* arrow: 1 up, 2 down, 3 left, 4 right */
#define EV_CTRL(c)	(0x200 | (c))	/* Ctrl+letter */
#define EV_CANCEL	0x300
#define EV_REDRAW	0x301

static char	nano_lines[NANO_MAX_LINES][NANO_MAX_COL];
static int	nano_lcount = 0;
static int	nano_row = 0;
static int	nano_col = 0;
static int	nano_scroll = 0;
static int	nano_scroll_x = 0;
static int	nano_modified = 0;
static char	nano_fname[128];
static char	nano_status[NANO_STATUS_SZ];

static int	nano_tab_w(void)
{
	int	col_in_tab = (nano_col % 4);

	return (4 - col_in_tab);
}

/* ---- line helpers ---- */

static int	nano_line_name_len(int r)
{
	return ((int)strlen(nano_lines[r]));
}

static void	nano_insert_char(int ch)
{
	char	*line = nano_lines[nano_row];
	int	len = nano_line_name_len(nano_row);

	if (len >= NANO_MAX_COL - 1)
		return;
	for (int i = len; i >= nano_col; i--)
		line[i + 1] = line[i];
	line[nano_col] = (char)ch;
	nano_col++;
	if (nano_col >= NANO_MAX_COL)
		nano_col = NANO_MAX_COL - 1;
	nano_modified = 1;
}

static void	nano_backspace(void)
{
	char	*line = nano_lines[nano_row];
	int	len = nano_line_name_len(nano_row);

	if (nano_col > 0)
	{
		for (int i = nano_col - 1; i < len; i++)
			line[i] = line[i + 1];
		nano_col--;
		nano_modified = 1;
	}
	else if (nano_row > 0)
	{
		/* Merge with the previous line. */
		char	*prev = nano_lines[nano_row - 1];
		int	plen = nano_line_name_len(nano_row - 1);
		int	room = NANO_MAX_COL - 1 - plen;

		if (room > len)
			room = len;
		if (room > 0)
		{
			for (int i = 0; i < room; i++)
				prev[plen + i] = line[i];
			prev[plen + room] = 0;
		}
		for (int i = nano_row; i < nano_lcount - 1; i++)
			for (int k = 0; k < NANO_MAX_COL; k++)
				nano_lines[i][k] = nano_lines[i + 1][k];
		nano_lcount--;
		nano_row--;
		nano_col = plen + room;
		if (nano_col > NANO_MAX_COL - 1)
			nano_col = NANO_MAX_COL - 1;
		nano_modified = 1;
	}
}

static void	nano_newline(void)
{
	char	*line = nano_lines[nano_row];
	int	len = nano_line_name_len(nano_row);
	int	tail;

	if (nano_lcount >= NANO_MAX_LINES)
		return;

	/* Make room for one more line below. */
	for (int i = nano_lcount; i > nano_row + 1; i--)
		for (int k = 0; k < NANO_MAX_COL; k++)
			nano_lines[i][k] = nano_lines[i - 1][k];
	bzero(nano_lines[nano_row + 1], NANO_MAX_COL);

	/* Split the current line. */
	tail = len - nano_col;
	if (tail < 0)
		tail = 0;
	if (tail > NANO_MAX_COL - 1)
		tail = NANO_MAX_COL - 1;
	for (int i = 0; i < tail; i++)
		nano_lines[nano_row + 1][i] = line[nano_col + i];
	nano_lines[nano_row + 1][tail] = 0;
	line[nano_col] = 0;

	nano_lcount++;
	nano_row++;
	nano_col = 0;
	nano_modified = 1;
}

/* ---- file I/O ---- */

static void	nano_load_file(const char *path)
{
	uint8_t	buf[1024];
	int	fd;
	int	n;
	char	tmp_line[4096];
	int	tpos = 0;

	nano_lcount = 1;
	bzero(nano_lines[0], NANO_MAX_COL);
	bzero(tmp_line, sizeof(tmp_line));

	fd = vfs_open(path, VFS_O_READ);
	if (fd < 0)
		return;

	while ((n = vfs_read(fd, buf, sizeof(buf))) > 0)
	{
		for (int i = 0; i < n; i++)
		{
			char	c = (char)buf[i];

			if (c == '\n')
			{
				/* Strip trailing \r (CRLF files). */
				if (tpos > 0 && tmp_line[tpos - 1] == '\r')
					tmp_line[tpos - 1] = 0;
				else
					tmp_line[tpos] = 0;
				/* Store the line (truncated). */
				if (tpos > NANO_MAX_COL - 1)
					tpos = NANO_MAX_COL - 1;
				strncpy(nano_lines[nano_lcount - 1], tmp_line, tpos);
				nano_lines[nano_lcount - 1][tpos] = 0;
				if (nano_lcount < NANO_MAX_LINES)
				{
					nano_lcount++;
					bzero(nano_lines[nano_lcount - 1], NANO_MAX_COL);
				}
				tpos = 0;
			}
			else if (tpos < (int)sizeof(tmp_line) - 1)
				tmp_line[tpos++] = c;
		}
	}
	vfs_close(fd);

	/* Flush the final (possibly empty) line. */
	if (nano_lcount > 0)
	{
		if (tpos > NANO_MAX_COL - 1)
			tpos = NANO_MAX_COL - 1;
		strncpy(nano_lines[nano_lcount - 1], tmp_line, tpos);
		nano_lines[nano_lcount - 1][tpos] = 0;
	}
	nano_modified = 0;
}

static int	nano_save_file(const char *path)
{
	int	fd = vfs_open(path, VFS_O_READ | VFS_O_WRITE | VFS_O_CREATE);
	int	total = 0;

	if (fd < 0)
	{
		snprintf(nano_status, sizeof(nano_status), "Save FAILED: open(%d)", fd);
		return (-1);
	}
	if (vfs_truncate(path, 0) != VFS_OK)
	{
		snprintf(nano_status, sizeof(nano_status), "Save FAILED: truncate");
		vfs_close(fd);
		return (-1);
	}
	for (int i = 0; i < nano_lcount; i++)
	{
		uint32_t	len = (uint32_t)strlen(nano_lines[i]);

		if (vfs_write(fd, (const uint8_t*)nano_lines[i], len) != (int)len)
		{
			snprintf(nano_status, sizeof(nano_status), "Save FAILED: write(%d)", i);
			vfs_close(fd);
			return (-1);
		}
		vfs_write(fd, (const uint8_t*)"\n", 1);
		total += (int)len + 1;
	}
	vfs_close(fd);
	nano_modified = 0;
	return (total);
}

/* ---- rendering ---- */

static void	nano_draw(void)
{
	uint8_t	ed_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	uint8_t	bar_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
	int	h = (int)term_height;
	int	w = (int)term_width;
	char	bar[128];
	size_t	i;
	int	r;
	int	x;

	terminal_clear();

	/* Text area (everything above the status bar). */
	for (r = 0; r < h - 1; r++)
	{
		int	lineno = nano_scroll + r;
		int	x;

		if (lineno >= nano_lcount)
			break;
		for (x = 0; x < w; x++)
		{
			int	ci = nano_scroll_x + x;

			if (ci < NANO_MAX_COL && nano_lines[lineno][ci])
				terminal_putentryat(nano_lines[lineno][ci], ed_color, (size_t)x, (size_t)r);
			else
				terminal_putentryat(' ', ed_color, (size_t)x, (size_t)r);
		}
	}
	/* Blank remaining rows. */
	for (; r < h - 1; r++)
		for (int x = 0; x < w; x++)
			terminal_putentryat(' ', ed_color, (size_t)x, (size_t)r);

	/* Status bar. */
	for (x = 0; x < w; x++)
		terminal_putentryat(' ', bar_color, (size_t)x, (size_t)(h - 1));
	if (nano_status[0])
		for (i = 0; nano_status[i] && i < (size_t)w; i++)
			terminal_putentryat(nano_status[i], bar_color, i, (size_t)(h - 1));
	else
	{
		snprintf(bar, sizeof(bar), "%s  [Ln %d, Col %d]%s  ^O save  ^X exit  ^G help",
			nano_fname, nano_row + 1, nano_col + 1,
			nano_modified ? "  [modified]" : "");
		for (i = 0; bar[i] && i < (size_t)w; i++)
			terminal_putentryat(bar[i], bar_color, i, (size_t)(h - 1));
	}

	/* Place the hardware cursor. */
	{
		int	cx = nano_col - nano_scroll_x;
		int	cy = nano_row - nano_scroll;

		if (cx < 0)
			cx = 0;
		if (cx >= w)
			cx = w - 1;
		if (cx > 127)
			cx = 127;
		if (cy < 0)
			cy = 0;
		if (cy >= h - 1)
			cy = h - 1;
		update_cursor((int8_t)cx, (int8_t)cy);
	}
}

static void	nano_clamp_cursor(void)
{
	int	len = nano_line_name_len(nano_row);

	if (nano_col > len)
		nano_col = len;
	if (nano_row < 0)
		nano_row = 0;
	if (nano_row >= nano_lcount)
		nano_row = nano_lcount - 1;
	if (nano_scroll > nano_row)
		nano_scroll = nano_row;
	if (nano_row >= nano_scroll + (int)term_height - 1)
		nano_scroll = nano_row - ((int)term_height - 2);
	if (nano_scroll_x > nano_col)
		nano_scroll_x = nano_col;
	if (nano_col >= nano_scroll_x + (int)term_width)
		nano_scroll_x = nano_col - (int)term_width + 1;
	if (nano_scroll_x < 0)
		nano_scroll_x = 0;
}

/* ---- input ---- */

static int	nano_event(void)
{
	while (1)
	{
		if (ctrl_char)
		{
			uint8_t	c = ctrl_char;

			ctrl_char = 0;
			in_read = 0;
			return (EV_CTRL(c));
		}
		if (cancel_input)
		{
			cancel_input = 0;
			in_read = 0;
			return (EV_CANCEL);
		}
		if (do_clear_screen)
		{
			do_clear_screen = 0;
			return (EV_REDRAW);
		}
		if (in_read == 1 && nav_key != 0)
		{
			int8_t	k = nav_key;

			nav_key = 0;
			in_read = 0;
			return (EV_NAV(k));
		}
		if (in_read == 1 && read_key != 0)
		{
			char	c = read_key;

			read_key = 0;
			in_read = 0;
			return ((int)(uint8_t)c);
		}
	}
}

static int	nano_confirm_exit(void)
{
	strcpy(nano_status, "Save modified buffer? [Y]es [N]o [C]ancel");
	nano_draw();
	while (1)
	{
		int	ev = nano_event();

		if (ev == 'y' || ev == 'Y')
			return (1);
		if (ev == 'n' || ev == 'N')
			return (0);
		if (ev == EV_CANCEL || ev == 'c' || ev == 'C')
			return (-1);
	}
}

/* ---- command ---- */

int	cmd_nano(int argc, char **argv)
{
	int	done = 0;

	if (argc < 2)
	{
		printk("Usage: nano <path>\n");
		printk("  arrows: move   ^O: save   ^X: exit   ^G: help\n");
		return (0);
	}
	vfs_cmd_resolve(argv[1], nano_fname, sizeof(nano_fname));
	nano_fname[sizeof(nano_fname) - 1] = 0;

	nano_lcount = 1;
	bzero(nano_lines[0], NANO_MAX_COL);
	nano_row = 0;
	nano_col = 0;
	nano_scroll = 0;
	nano_scroll_x = 0;
	nano_modified = 0;
	nano_status[0] = 0;
	nano_load_file(nano_fname);
	/* Keep at least one editable line. */
	if (nano_lcount < 1)
	{
		nano_lcount = 1;
		bzero(nano_lines[0], NANO_MAX_COL);
	}

	while (!done)
	{
		int	ev;

		nano_clamp_cursor();
		nano_draw();
		ev = nano_event();

		if (ev == EV_CTRL('o') || ev == EV_CTRL('O'))
		{
			int	n = nano_save_file(nano_fname);

			if (n < 0)
				strcpy(nano_status, "Save FAILED");
			else
			{
				snprintf(nano_status, sizeof(nano_status),
					"Saved %d bytes to %s%s", n, nano_fname, " ");
				nano_status[NANO_STATUS_SZ - 1] = 0;
			}
		}
		else if (ev == EV_CTRL('x') || ev == EV_CTRL('X'))
		{
			if (nano_modified)
			{
				int	r = nano_confirm_exit();

				if (r == 1)
				{
					nano_save_file(nano_fname);
					done = 1;
				}
				else if (r == 0)
					done = 1;
				else
					nano_status[0] = 0;
			}
			else
				done = 1;
		}
		else if (ev == EV_CTRL('g') || ev == EV_CTRL('G'))
		{
			strcpy(nano_status,
				"^O save  ^X exit  ^G help  arrows move  Backspace delete");
		}
		else if (ev == EV_REDRAW)
		{
			;
		}
		else if (ev == EV_CANCEL)
		{
			done = 1;
		}
		else if (ev >= EV_NAV(1) && ev <= EV_NAV(4))
		{
			int	dir = ev & 0xFF;

			switch (dir)
			{
				case 1: /* up */
					if (nano_row > 0)
					{
						nano_row--;
						nano_clamp_cursor();
					}
					break;
				case 2: /* down */
					if (nano_row < nano_lcount - 1)
					{
						nano_row++;
						nano_clamp_cursor();
					}
					break;
				case 3: /* left */
					if (nano_col > 0)
						nano_col--;
					else if (nano_row > 0)
					{
						nano_row--;
						nano_col = nano_line_name_len(nano_row);
					}
					break;
				case 4: /* right */
					if (nano_col < nano_line_name_len(nano_row))
						nano_col++;
					else if (nano_row < nano_lcount - 1)
					{
						nano_row++;
						nano_col = 0;
					}
					break;
			}
		}
		else if (ev == '\b')
		{
			nano_backspace();
		}
		else if (ev == '\n')
		{
			nano_newline();
		}
		else if (ev == '\t')
		{
			int	n = nano_tab_w();

			for (int i = 0; i < n; i++)
				nano_insert_char(' ');
		}
		else if (ev >= 0x20 && ev < 0x7F)
		{
			nano_insert_char(ev);
		}
		else if (ev == EV_CTRL('c') || ev == EV_CTRL('C'))
		{
			done = 1;	/* abandon */
		}
	}

	/* Return the terminal to normal and print a fresh prompt line. */
	terminal_clear();
	terminal_row = 0;
	terminal_column = 0;
	update_cursor(0, 0);
	return (0);
}