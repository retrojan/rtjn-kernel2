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

	term_clear();

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
				term_putch_at(nano_lines[lineno][ci], ed_color, (size_t)x, (size_t)r);
			else
				term_putch_at(' ', ed_color, (size_t)x, (size_t)r);
		}
	}
	/* Blank remaining rows. */
	for (; r < h - 1; r++)
		for (int x = 0; x < w; x++)
			term_putch_at(' ', ed_color, (size_t)x, (size_t)r);

	/* Status bar. */
	for (x = 0; x < w; x++)
		term_putch_at(' ', bar_color, (size_t)x, (size_t)(h - 1));
	if (nano_status[0])
		for (i = 0; nano_status[i] && i < (size_t)w; i++)
			term_putch_at(nano_status[i], bar_color, i, (size_t)(h - 1));
	else
	{
		snprintf(bar, sizeof(bar), "%s  [Ln %d, Col %d]%s  ^O save  ^X exit  ^G help",
			nano_fname, nano_row + 1, nano_col + 1,
			nano_modified ? "  [modified]" : "");
		for (i = 0; bar[i] && i < (size_t)w; i++)
			term_putch_at(bar[i], bar_color, i, (size_t)(h - 1));
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
	term_clear();
	t_row = 0;
	t_col = 0;
	update_cursor(0, 0);
	return (0);
}

#define VI_MAX_LINES	512
#define VI_MAX_COL		160
#define VI_STATUS_SZ	80
#define VI_CMD_SZ		64
#define VI_SEARCH_SZ	64
#define VI_YANK_MAX		4096

#define EV_NAV(c)	(0x100 | (c))
#define EV_CTRL(c)	(0x200 | (c))
#define EV_CANCEL	0x300
#define EV_REDRAW	0x301
#define EV_ESC		0x400

#define VI_M_NORM	0
#define VI_M_INS	1
#define VI_M_CMD	2
#define VI_M_VIS	3
#define VI_M_VISL	4
#define VI_M_SEARCH	5

#define VIS_NONE	0
#define VIS_CHAR	1
#define VIS_LINE	2

#define OP_NONE		0
#define OP_DELETE	'd'
#define OP_YANK		'y'
#define OP_CHANGE	'c'

static char		vi_lines[VI_MAX_LINES][VI_MAX_COL];
static int		vi_lcount;
static int		vi_row;
static int		vi_col;
static int		vi_scroll;
static int		vi_scroll_x;
static int		vi_modified;
static int		vi_mode;
static int		vi_done;
static int		vi_pending;
static char		vi_fname[128];
static char		vi_status[VI_STATUS_SZ];
static char		vi_msg[VI_STATUS_SZ];
static char		vi_cmd[VI_CMD_SZ];
static int		vi_cmd_len;

static int		vi_show_rnu;
static int		vi_show_nu;

static char		vi_yank_buf[VI_YANK_MAX];
static int		vi_yank_len;
static int		vi_yank_line;

static int		vi_op;
static int		vi_op_linewise;
static int		vi_op_pending;

static int		vi_vis_start_row;
static int		vi_vis_start_col;
static int		vi_vis_type;

static char		vi_search_pat[VI_SEARCH_SZ];
static int		vi_search_len;
static int		vi_search_dir;
static int		vi_search_highlight;

static char		vi_search_prompt[VI_SEARCH_SZ];
static int		vi_search_prompt_len;

static char		vi_undo_line[VI_MAX_COL];
static int		vi_undo_row;
static int		vi_undo_lcount;
static char		vi_redo_line[VI_MAX_COL];
static int		vi_redo_row;
static int		vi_redo_lcount;
static int		vi_undo_valid;

static int		vi_dot_op;
static int		vi_dot_linewise;
static int		vi_dot_valid;

static int		vi_llen(int r)
{
	return ((int)strlen(vi_lines[r]));
}

static int	vi_is_word(char c)
{
	return (c != 0 && c != ' ' && c != '\t');
}

static int	vi_is_alpha(char c)
{
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
		|| (c >= '0' && c <= '9') || c == '_');
}

static void	vi_set_msg(const char *s)
{
	strncpy(vi_msg, s, sizeof(vi_msg) - 1);
	vi_msg[sizeof(vi_msg) - 1] = 0;
}

static void	vi_clamp(void);

static void	vi_save_undo(void)
{
	strncpy(vi_undo_line, vi_lines[vi_row], VI_MAX_COL);
	vi_undo_row = vi_row;
	vi_undo_lcount = vi_lcount;
	vi_undo_valid = 1;
}

static void	vi_insert_char(int ch)
{
	char	*line = vi_lines[vi_row];
	int	len = vi_llen(vi_row);

	if (len >= VI_MAX_COL - 1)
		return;
	for (int i = len; i >= vi_col; i--)
		line[i + 1] = line[i];
	line[vi_col] = (char)ch;
	vi_col++;
	if (vi_col >= VI_MAX_COL)
		vi_col = VI_MAX_COL - 1;
	vi_modified = 1;
}

static void	vi_backspace(void)
{
	char	*line = vi_lines[vi_row];
	int	len = vi_llen(vi_row);

	if (vi_col > 0)
	{
		for (int i = vi_col - 1; i < len; i++)
			line[i] = line[i + 1];
		vi_col--;
		vi_modified = 1;
	}
	else if (vi_row > 0)
	{
		char	*prev = vi_lines[vi_row - 1];
		int	plen = vi_llen(vi_row - 1);
		int	room = VI_MAX_COL - 1 - plen;

		if (room > len)
			room = len;
		if (room > 0)
		{
			for (int i = 0; i < room; i++)
				prev[plen + i] = line[i];
			prev[plen + room] = 0;
		}
		for (int i = vi_row; i < vi_lcount - 1; i++)
			for (int k = 0; k < VI_MAX_COL; k++)
				vi_lines[i][k] = vi_lines[i + 1][k];
		vi_lcount--;
		vi_row--;
		vi_col = plen + room;
		if (vi_col > VI_MAX_COL - 1)
			vi_col = VI_MAX_COL - 1;
		vi_modified = 1;
	}
}

static void	vi_newline(void)
{
	char	*line = vi_lines[vi_row];
	int	len = vi_llen(vi_row);
	int	tail;

	if (vi_lcount >= VI_MAX_LINES)
		return;
	for (int i = vi_lcount; i > vi_row + 1; i--)
		for (int k = 0; k < VI_MAX_COL; k++)
			vi_lines[i][k] = vi_lines[i - 1][k];
	bzero(vi_lines[vi_row + 1], VI_MAX_COL);

	tail = len - vi_col;
	if (tail < 0)
		tail = 0;
	if (tail > VI_MAX_COL - 1)
		tail = VI_MAX_COL - 1;
	for (int i = 0; i < tail; i++)
		vi_lines[vi_row + 1][i] = line[vi_col + i];
	vi_lines[vi_row + 1][tail] = 0;
	line[vi_col] = 0;

	vi_lcount++;
	vi_row++;
	vi_col = 0;
	vi_modified = 1;
}

static void	vi_open_line(int above)
{
	if (vi_lcount >= VI_MAX_LINES)
		return;
	if (above)
	{
		for (int i = vi_lcount; i > vi_row; i--)
			for (int k = 0; k < VI_MAX_COL; k++)
				vi_lines[i][k] = vi_lines[i - 1][k];
		bzero(vi_lines[vi_row], VI_MAX_COL);
		vi_lcount++;
		vi_col = 0;
	}
	else
	{
		for (int i = vi_lcount; i > vi_row + 1; i--)
			for (int k = 0; k < VI_MAX_COL; k++)
				vi_lines[i][k] = vi_lines[i - 1][k];
		bzero(vi_lines[vi_row + 1], VI_MAX_COL);
		vi_lcount++;
		vi_row++;
		vi_col = 0;
	}
	vi_modified = 1;
}

static void	vi_yank_text(const char *text, int len, int linewise)
{
	if (len >= VI_YANK_MAX)
		len = VI_YANK_MAX - 1;
	memcpy(vi_yank_buf, text, (size_t)len);
	vi_yank_buf[len] = 0;
	vi_yank_len = len;
	vi_yank_line = linewise;
}

static void	vi_delete_line_at(int row)
{
	if (vi_lcount <= 1)
	{
		bzero(vi_lines[0], VI_MAX_COL);
		return;
	}
	for (int i = row; i < vi_lcount - 1; i++)
		for (int k = 0; k < VI_MAX_COL; k++)
			vi_lines[i][k] = vi_lines[i + 1][k];
	vi_lcount--;
}

static void	vi_delete_line(void)
{
	vi_save_undo();
	vi_yank_text(vi_lines[vi_row], vi_llen(vi_row), 1);
	vi_delete_line_at(vi_row);
	if (vi_row >= vi_lcount)
		vi_row = vi_lcount - 1;
	if (vi_row < 0)
		vi_row = 0;
	vi_col = 0;
	vi_modified = 1;
}

static void	vi_delete_char(void)
{
	char	*line = vi_lines[vi_row];
	int	len = vi_llen(vi_row);

	if (vi_col >= len)
		return;
	for (int i = vi_col; i < len; i++)
		line[i] = line[i + 1];
	vi_modified = 1;
}

static void	vi_delete_to_eol(void)
{
	int	len = vi_llen(vi_row);

	if (vi_col < len)
	{
		vi_yank_text(&vi_lines[vi_row][vi_col], len - vi_col, 0);
		vi_lines[vi_row][vi_col] = 0;
	}
	vi_modified = 1;
}

static void	vi_delete_range(int r1, int c1, int r2, int c2, int linewise)
{
	vi_save_undo();
	if (linewise)
	{
		int	start = r1 < r2 ? r1 : r2;
		int	end = r1 < r2 ? r2 : r1;
		int	total = 0;
		char	tmp[VI_YANK_MAX];

		tmp[0] = 0;
		for (int i = start; i <= end && i < vi_lcount; i++)
		{
			int	ll = vi_llen(i);

			if (total + ll + 1 < VI_YANK_MAX)
			{
				if (total > 0)
					tmp[total++] = '\n';
				memcpy(tmp + total, vi_lines[i], (size_t)ll);
				total += ll;
			}
		}
		tmp[total] = 0;
		vi_yank_text(tmp, total, 1);
		for (int i = start; i <= end && vi_lcount > 1; i++)
			vi_delete_line_at(start);
		if (vi_lcount < 1)
		{
			vi_lcount = 1;
			bzero(vi_lines[0], VI_MAX_COL);
		}
		vi_row = start;
		if (vi_row >= vi_lcount)
			vi_row = vi_lcount - 1;
		vi_col = 0;
	}
	else
	{
		int	sr = r1 < r2 ? r1 : r2;
		int	er = r1 < r2 ? r2 : r1;
		int	sc = r1 < r2 ? c1 : c2;
		int	ec = r1 < r2 ? c2 : c1;
		char	tmp[VI_YANK_MAX];
		int	tlen = 0;

		if (sr == er)
		{
			int	ll = vi_llen(sr);
			int	end = ec < ll ? ec : ll;
			int	st = sc < ll ? sc : ll;

			if (st < end)
			{
				vi_yank_text(&vi_lines[sr][st], end - st, 0);
				for (int i = end; i <= ll; i++)
					vi_lines[sr][i - (end - st)] = vi_lines[sr][i];
			}
			vi_col = sc;
		}
		else
		{
			int	ll0 = vi_llen(sr);

			if (sc < ll0)
			{
				memcpy(tmp, &vi_lines[sr][sc], (size_t)(ll0 - sc));
				tlen += ll0 - sc;
				vi_lines[sr][sc] = 0;
			}
			for (int i = sr + 1; i < er && i < vi_lcount; i++)
			{
				int	ll = vi_llen(i);

				if (tlen + ll + 1 < VI_YANK_MAX)
				{
					tmp[tlen++] = '\n';
					memcpy(tmp + tlen, vi_lines[i], (size_t)ll);
					tlen += ll;
				}
				vi_lines[i][0] = 0;
			}
			if (er < vi_lcount)
			{
				int	ll2 = vi_llen(er);
				int	ec2 = ec < ll2 ? ec : ll2;

				if (ec2 > 0)
				{
					if (tlen + 1 < VI_YANK_MAX)
						tmp[tlen++] = '\n';
					memcpy(tmp + tlen, vi_lines[er], (size_t)ec2);
					tlen += ec2;
				}
				for (int i = ec2; i <= ll2; i++)
					vi_lines[er][i - ec2] = vi_lines[er][i];
			}
			tmp[tlen] = 0;
			vi_yank_text(tmp, tlen, 0);
			for (int i = sr + 1; i <= er && vi_lcount > 1; i++)
				vi_delete_line_at(sr + 1);
			if (vi_lcount < 1)
			{
				vi_lcount = 1;
				bzero(vi_lines[0], VI_MAX_COL);
			}
			vi_row = sr;
			if (vi_row >= vi_lcount)
				vi_row = vi_lcount - 1;
			vi_col = sc;
		}
	}
	vi_modified = 1;
}

static void	vi_change_range(int r1, int c1, int r2, int c2, int linewise)
{
	vi_delete_range(r1, c1, r2, c2, linewise);
	if (!linewise)
	{
		if (vi_row < vi_lcount)
		{
			int	ll = vi_llen(vi_row);

			if (vi_col > ll)
				vi_col = ll;
		}
	}
	else
		vi_col = 0;
	vi_mode = VI_M_INS;
	vi_msg[0] = 0;
}

static int	vi_word_end(int row, int col)
{
	char	*line = vi_lines[row];
	int	len = vi_llen(row);
	int	c = col;

	if (c < len && vi_is_alpha(line[c]))
	{
		while (c < len && vi_is_alpha(line[c]))
			c++;
	}
	else if (c < len)
	{
		while (c < len && !vi_is_alpha(line[c]) && line[c] != ' ' && line[c] != '\t')
			c++;
	}
	if (c < len && c == col)
		c++;
	return (c);
}

static void	vi_word_fwd(void)
{
	char	*line = vi_lines[vi_row];
	int	len = vi_llen(vi_row);

	if (vi_col >= len)
	{
		if (vi_row < vi_lcount - 1)
		{
			vi_row++;
			vi_col = 0;
		}
		return;
	}
	if (vi_is_word(line[vi_col]))
		while (vi_col < len && vi_is_word(line[vi_col]))
			vi_col++;
	while (vi_col < len && !vi_is_word(line[vi_col]))
		vi_col++;
}

static void	vi_word_back(void)
{
	char	*line = vi_lines[vi_row];

	if (vi_col > 0)
	{
		vi_col--;
		while (vi_col > 0 && !vi_is_word(line[vi_col]))
			vi_col--;
		while (vi_col > 0 && vi_is_word(line[vi_col - 1]))
			vi_col--;
	}
	else if (vi_row > 0)
	{
		vi_row--;
		vi_col = vi_llen(vi_row);
		if (vi_col > 0)
			vi_col--;
		while (vi_col > 0 && vi_is_word(vi_lines[vi_row][vi_col - 1]))
			vi_col--;
	}
}

static void	vi_move_to_matching(void)
{
	char	*line = vi_lines[vi_row];
	int	len = vi_llen(vi_row);
	int	c = vi_col;
	char	openers[] = "({[";
	char	closers[] = ")}]";

	if (c >= len)
		return;
	for (int i = 0; i < 3; i++)
	{
		int	depth;

		if (line[c] == openers[i])
		{
			depth = 1;
			c++;
			while (depth > 0 && c < len)
			{
				if (line[c] == openers[i])
					depth++;
				else if (line[c] == closers[i])
					depth--;
				if (depth > 0)
					c++;
			}
			if (depth == 0)
				vi_col = c;
			return;
		}
		if (line[c] == closers[i])
		{
			depth = 1;
			c--;
			while (depth > 0 && c >= 0)
			{
				if (line[c] == closers[i])
					depth++;
				else if (line[c] == openers[i])
					depth--;
				if (depth > 0)
					c--;
			}
			if (depth == 0)
				vi_col = c;
			return;
		}
	}
}

static void	vi_move_to_paragraph(int forward)
{
	if (forward)
	{
		int	r = vi_row + 1;

		while (r < vi_lcount && vi_lines[r][0] != 0)
			r++;
		if (r < vi_lcount)
			r++;
		while (r < vi_lcount && vi_lines[r][0] == 0)
			r++;
		if (r < vi_lcount)
			vi_row = r;
		else
			vi_row = vi_lcount - 1;
	}
	else
	{
		int	r = vi_row - 1;

		if (r > 0 && vi_lines[r][0] == 0)
			r--;
		while (r > 0 && vi_lines[r][0] != 0)
			r--;
		vi_row = r > 0 ? r : 0;
	}
	vi_col = 0;
}

static int	vi_search_forward(const char *pat, int plen, int sr, int sc, int *orow, int *ocol)
{
	int	r = sr;
	int	c = sc + 1;

	if (plen == 0)
		return (0);
	while (r < vi_lcount)
	{
		int	ll = vi_llen(r);

		while (c <= ll - plen)
		{
			if (strncmp(&vi_lines[r][c], pat, (size_t)plen) == 0)
			{
				*orow = r;
				*ocol = c;
				return (1);
			}
			c++;
		}
		r++;
		c = 0;
	}
	return (0);
}

static int	vi_search_backward(const char *pat, int plen, int sr, int sc, int *orow, int *ocol)
{
	int	r = sr;
	int	c = sc - 1;

	if (plen == 0)
		return (0);
	while (r >= 0)
	{
		int	ll = vi_llen(r);

		if (c > ll - plen)
			c = ll - plen;
		while (c >= 0)
		{
			if (strncmp(&vi_lines[r][c], pat, (size_t)plen) == 0)
			{
				*orow = r;
				*ocol = c;
				return (1);
			}
			c--;
		}
		r--;
		if (r >= 0)
			c = vi_llen(r) - plen;
	}
	return (0);
}

static void	vi_search_next(void)
{
	int	rr, cc;

	if (vi_search_len == 0)
		return;
	if (vi_search_dir > 0)
	{
		if (vi_search_forward(vi_search_pat, vi_search_len, vi_row, vi_col, &rr, &cc))
		{
			vi_row = rr;
			vi_col = cc;
			vi_search_highlight = 1;
		}
		else
			vi_set_msg("Pattern not found");
	}
	else
	{
		if (vi_search_backward(vi_search_pat, vi_search_len, vi_row, vi_col, &rr, &cc))
		{
			vi_row = rr;
			vi_col = cc;
			vi_search_highlight = 1;
		}
		else
			vi_set_msg("Pattern not found");
	}
}

static void	vi_search_prev(void)
{
	int	rr, cc;

	if (vi_search_len == 0)
		return;
	if (vi_search_dir > 0)
	{
		if (vi_search_backward(vi_search_pat, vi_search_len, vi_row, vi_col, &rr, &cc))
		{
			vi_row = rr;
			vi_col = cc;
			vi_search_highlight = 1;
		}
		else
			vi_set_msg("Pattern not found");
	}
	else
	{
		if (vi_search_forward(vi_search_pat, vi_search_len, vi_row, vi_col, &rr, &cc))
		{
			vi_row = rr;
			vi_col = cc;
			vi_search_highlight = 1;
		}
		else
			vi_set_msg("Pattern not found");
	}
}

static void	vi_paste_after(void)
{
	int	len;

	if (vi_yank_len == 0)
		return;
	vi_save_undo();
	if (vi_yank_line)
	{
		if (vi_lcount >= VI_MAX_LINES)
			return;
		for (int i = vi_lcount; i > vi_row + 1; i--)
			for (int k = 0; k < VI_MAX_COL; k++)
				vi_lines[i][k] = vi_lines[i - 1][k];
		vi_lcount++;
		strncpy(vi_lines[vi_row + 1], vi_yank_buf, (size_t)vi_yank_len);
		vi_lines[vi_row + 1][vi_yank_len] = 0;
		vi_row++;
		vi_col = 0;
	}
	else
	{
		len = vi_llen(vi_row);
		int	ins = vi_col + 1;

		if (ins > len)
			ins = len;
		if (vi_yank_len + len >= VI_MAX_COL - 1)
			return;
		for (int i = len; i >= ins; i--)
			vi_lines[vi_row][i + vi_yank_len] = vi_lines[vi_row][i];
		for (int i = 0; i < vi_yank_len; i++)
			vi_lines[vi_row][ins + i] = vi_yank_buf[i];
		vi_col = ins + vi_yank_len - 1;
		if (vi_col < 0)
			vi_col = 0;
	}
	vi_modified = 1;
}

static void	vi_paste_before(void)
{
	int	len;

	if (vi_yank_len == 0)
		return;
	vi_save_undo();
	if (vi_yank_line)
	{
		if (vi_lcount >= VI_MAX_LINES)
			return;
		for (int i = vi_lcount; i > vi_row; i--)
			for (int k = 0; k < VI_MAX_COL; k++)
				vi_lines[i][k] = vi_lines[i - 1][k];
		vi_lcount++;
		strncpy(vi_lines[vi_row], vi_yank_buf, (size_t)vi_yank_len);
		vi_lines[vi_row][vi_yank_len] = 0;
		vi_col = 0;
	}
	else
	{
		len = vi_llen(vi_row);
		if (vi_yank_len + len >= VI_MAX_COL - 1)
			return;
		for (int i = len; i >= vi_col; i--)
			vi_lines[vi_row][i + vi_yank_len] = vi_lines[vi_row][i];
		for (int i = 0; i < vi_yank_len; i++)
			vi_lines[vi_row][vi_col + i] = vi_yank_buf[i];
	}
	vi_modified = 1;
}

static void	vi_toggle_case(void)
{
	char	*line = vi_lines[vi_row];
	int	len = vi_llen(vi_row);

	if (vi_col >= len)
		return;
	vi_save_undo();
	if (line[vi_col] >= 'a' && line[vi_col] <= 'z')
		line[vi_col] -= 32;
	else if (line[vi_col] >= 'A' && line[vi_col] <= 'Z')
		line[vi_col] += 32;
	vi_col++;
	if (vi_col >= len)
		vi_col = len > 0 ? len - 1 : 0;
	vi_modified = 1;
}

static void	vi_join_lines(void)
{
	char	*cur;
	char	*next;
	int	clen;
	int	nlen;

	if (vi_row >= vi_lcount - 1)
		return;
	vi_save_undo();
	cur = vi_lines[vi_row];
	next = vi_lines[vi_row + 1];
	clen = vi_llen(vi_row);
	nlen = vi_llen(vi_row + 1);
	while (clen > 0 && (cur[clen - 1] == ' ' || cur[clen - 1] == '\t'))
		clen--;
	cur[clen] = ' ';
	clen++;
	if (clen + nlen < VI_MAX_COL - 1)
	{
		memcpy(cur + clen, next, (size_t)nlen);
		cur[clen + nlen] = 0;
	}
	for (int i = vi_row + 1; i < vi_lcount - 1; i++)
		for (int k = 0; k < VI_MAX_COL; k++)
			vi_lines[i][k] = vi_lines[i + 1][k];
	vi_lcount--;
	vi_modified = 1;
}

static void	vi_indent(int dir)
{
	vi_save_undo();
	if (dir > 0)
	{
		char	*line = vi_lines[vi_row];
		int	len = vi_llen(vi_row);

		if (len >= VI_MAX_COL - 2)
			return;
		for (int i = len; i >= 0; i--)
			line[i + 2] = line[i];
		line[0] = ' ';
		line[1] = ' ';
		vi_col += 2;
	}
	else
	{
		char	*line = vi_lines[vi_row];

		if (line[0] == ' ')
		{
			int	rm = 1;
			int	ll;

			if (line[1] == ' ')
				rm = 2;
			ll = vi_llen(vi_row);
			for (int i = rm; i <= ll; i++)
				line[i - rm] = line[i];
			if (vi_col >= rm)
				vi_col -= rm;
			else
				vi_col = 0;
		}
	}
	vi_modified = 1;
}

static void	vi_do_undo(void)
{
	if (!vi_undo_valid)
	{
		vi_set_msg("[Already at oldest change]");
		return;
	}
	strncpy(vi_redo_line, vi_lines[vi_undo_row], VI_MAX_COL);
	vi_redo_row = vi_undo_row;
	vi_redo_lcount = vi_lcount;
	strncpy(vi_lines[vi_undo_row], vi_undo_line, VI_MAX_COL);
	vi_lcount = vi_undo_lcount;
	vi_row = vi_undo_row;
	vi_col = 0;
	vi_undo_valid = 0;
	vi_modified = 1;
	vi_set_msg("");
}

static void	vi_do_redo(void)
{
	strncpy(vi_undo_line, vi_lines[vi_redo_row], VI_MAX_COL);
	vi_undo_row = vi_redo_row;
	vi_undo_lcount = vi_lcount;
	strncpy(vi_lines[vi_redo_row], vi_redo_line, VI_MAX_COL);
	vi_lcount = vi_redo_lcount;
	vi_row = vi_redo_row;
	vi_col = 0;
	vi_modified = 1;
	vi_set_msg("");
}

static void	vi_visual_yank(void)
{
	int	sr = vi_vis_start_row < vi_row ? vi_vis_start_row : vi_row;
	int	er = vi_vis_start_row < vi_row ? vi_row : vi_vis_start_row;
	int	sc = vi_vis_start_row < vi_row ? vi_vis_start_col : vi_col;
	int	ec = vi_vis_start_row < vi_row ? vi_col : vi_vis_start_col;
	int	linewise = (vi_vis_type == VIS_LINE);
	char	tmp[VI_YANK_MAX];
	int	tlen = 0;

	if (linewise)
	{
		for (int i = sr; i <= er && i < vi_lcount; i++)
		{
			int	ll = vi_llen(i);

			if (tlen + ll + 1 < VI_YANK_MAX)
			{
				if (tlen > 0)
					tmp[tlen++] = '\n';
				memcpy(tmp + tlen, vi_lines[i], (size_t)ll);
				tlen += ll;
			}
		}
	}
	else if (sr == er)
	{
		int	ll = vi_llen(sr);
		int	end = ec < ll ? ec : ll;

		if (sc < end)
		{
			memcpy(tmp, &vi_lines[sr][sc], (size_t)(end - sc));
			tlen = end - sc;
		}
	}
	else
	{
		int	ll0 = vi_llen(sr);

		if (sc < ll0)
		{
			memcpy(tmp + tlen, &vi_lines[sr][sc], (size_t)(ll0 - sc));
			tlen += ll0 - sc;
		}
		for (int i = sr + 1; i < er && i < vi_lcount; i++)
		{
			int	ll = vi_llen(i);

			if (tlen + ll + 1 < VI_YANK_MAX)
			{
				tmp[tlen++] = '\n';
				memcpy(tmp + tlen, vi_lines[i], (size_t)ll);
				tlen += ll;
			}
		}
		if (er < vi_lcount)
		{
			int	ll2 = vi_llen(er);
			int	end2 = ec < ll2 ? ec : ll2;

			if (end2 > 0)
			{
				if (tlen + 1 < VI_YANK_MAX)
					tmp[tlen++] = '\n';
				memcpy(tmp + tlen, vi_lines[er], (size_t)end2);
				tlen += end2;
			}
		}
	}
	tmp[tlen] = 0;
	vi_yank_text(tmp, tlen, linewise);
	char	msg_buf[32];

	snprintf(msg_buf, sizeof(msg_buf), "%d lines yanked", er - sr + 1);
	vi_set_msg(msg_buf);
}

static void	vi_visual_delete(void)
{
	vi_delete_range(vi_vis_start_row, vi_vis_start_col, vi_row, vi_col,
		vi_vis_type == VIS_LINE);
	vi_vis_type = VIS_NONE;
	vi_mode = VI_M_NORM;
}

static void	vi_substitute_line(int row, const char *pat, int plen,
	const char *rep, int rlen)
{
	char	*line = vi_lines[row];
	int	ll = vi_llen(row);
	char	tmp[VI_MAX_COL];
	int	t = 0;
	int	c = 0;

	while (c < ll && t < VI_MAX_COL - 1)
	{
		if (c <= ll - plen && strncmp(line + c, pat, (size_t)plen) == 0)
		{
			for (int i = 0; i < rlen && t < VI_MAX_COL - 1; i++)
				tmp[t++] = rep[i];
			c += plen;
		}
		else
			tmp[t++] = line[c++];
	}
	tmp[t] = 0;
	memcpy(line, tmp, (size_t)(t + 1));
}

static void	vi_op_exec(int op, int linewise, int r1, int c1, int r2, int c2)
{
	int	sr = r1 < r2 ? r1 : r2;
	int	er = r1 < r2 ? r2 : r1;
	int	sc = r1 < r2 ? c1 : c2;
	int	ec = r1 < r2 ? c2 : c1;

	if (linewise)
	{
		sc = 0;
		ec = 0;
	}
	if (op == OP_DELETE)
	{
		vi_delete_range(r1, c1, r2, c2, linewise);
		vi_clamp();
	}
	else if (op == OP_YANK)
	{
		char	tmp[VI_YANK_MAX];
		int	tlen = 0;

		if (linewise)
		{
			for (int i = sr; i <= er && i < vi_lcount; i++)
			{
				int	ll = vi_llen(i);

				if (tlen + ll + 1 < VI_YANK_MAX)
				{
					if (tlen > 0)
						tmp[tlen++] = '\n';
					memcpy(tmp + tlen, vi_lines[i], (size_t)ll);
					tlen += ll;
				}
			}
		}
		else if (sr == er)
		{
			int	ll = vi_llen(sr);
			int	end = ec < ll ? ec : ll;

			if (sc < end)
			{
				memcpy(tmp, &vi_lines[sr][sc], (size_t)(end - sc));
				tlen = end - sc;
			}
		}
		else
		{
			int	ll0 = vi_llen(sr);

			if (sc < ll0)
			{
				memcpy(tmp + tlen, &vi_lines[sr][sc], (size_t)(ll0 - sc));
				tlen += ll0 - sc;
			}
			for (int i = sr + 1; i < er && i < vi_lcount; i++)
			{
				int	ll = vi_llen(i);

				if (tlen + ll + 1 < VI_YANK_MAX)
				{
					tmp[tlen++] = '\n';
					memcpy(tmp + tlen, vi_lines[i], (size_t)ll);
					tlen += ll;
				}
			}
			if (er < vi_lcount)
			{
				int	ll2 = vi_llen(er);
				int	end2 = ec < ll2 ? ec : ll2;

				if (end2 > 0)
				{
					if (tlen + 1 < VI_YANK_MAX)
						tmp[tlen++] = '\n';
					memcpy(tmp + tlen, vi_lines[er], (size_t)end2);
					tlen += end2;
				}
			}
		}
		tmp[tlen] = 0;
		vi_yank_text(tmp, tlen, linewise);
		char	msg_buf[32];

		snprintf(msg_buf, sizeof(msg_buf), "%d lines yanked", er - sr + 1);
		vi_set_msg(msg_buf);
	}
	else if (op == OP_CHANGE)
	{
		vi_change_range(r1, c1, r2, c2, linewise);
	}
}

static void	vi_apply_op_motion(int op, int linewise, int motion)
{
	int	sr = vi_row;
	int	sc = vi_col;

	if (motion == 'w')
		vi_word_fwd();
	else if (motion == 'b')
		vi_word_back();
	else if (motion == 'e')
	{
		int	end = vi_word_end(vi_row, vi_col);

		if (end == vi_col && vi_col < vi_llen(vi_row))
			end = vi_col + 1;
		vi_col = end;
		if (vi_col >= vi_llen(vi_row) && vi_row < vi_lcount - 1)
		{
			vi_row++;
			vi_col = 0;
		}
	}
	else if (motion == '$')
	{
		vi_col = vi_llen(vi_row);
		if (vi_col > 0)
			vi_col--;
	}
	else if (motion == '0')
		vi_col = 0;
	else if (motion == '^')
	{
		vi_col = 0;
		while (vi_col < vi_llen(vi_row)
			&& (vi_lines[vi_row][vi_col] == ' ' || vi_lines[vi_row][vi_col] == '\t'))
			vi_col++;
	}
	else if (motion == 'h')
	{
		if (vi_col > 0)
			vi_col--;
	}
	else if (motion == 'l')
	{
		if (vi_col < vi_llen(vi_row))
			vi_col++;
	}
	else if (motion == 'j')
	{
		if (vi_row < vi_lcount - 1)
			vi_row++;
	}
	else if (motion == 'k')
	{
		if (vi_row > 0)
			vi_row--;
	}
	else if (motion == 'G')
	{
		vi_row = vi_lcount - 1;
		vi_col = 0;
	}
	else if (motion == 'H')
	{
		vi_row = vi_scroll;
		vi_col = 0;
	}
	else if (motion == 'M')
	{
		vi_row = vi_scroll + ((int)term_height - 2) / 2;
		if (vi_row >= vi_lcount)
			vi_row = vi_lcount - 1;
		vi_col = 0;
	}
	else if (motion == 'L')
	{
		vi_row = vi_scroll + (int)term_height - 3;
		if (vi_row >= vi_lcount)
			vi_row = vi_lcount - 1;
		vi_col = 0;
	}
	else
		return;
	vi_op_exec(op, linewise, sr, sc, vi_row, vi_col);
	vi_clamp();
}

static void	vi_load_file(const char *path)
{
	uint8_t	buf[1024];
	int	fd;
	int	n;
	char	tmp_line[4096];
	int	tpos = 0;

	vi_lcount = 1;
	bzero(vi_lines[0], VI_MAX_COL);
	bzero(tmp_line, sizeof(tmp_line));

	fd = vfs_open(path, VFS_O_READ);
	if (fd < 0)
	{
		strcpy(vi_msg, "[New file]");
		return;
	}
	while ((n = vfs_read(fd, buf, sizeof(buf))) > 0)
	{
		for (int i = 0; i < n; i++)
		{
			char	c = (char)buf[i];

			if (c == '\n')
			{
				if (tpos > 0 && tmp_line[tpos - 1] == '\r')
					tmp_line[tpos - 1] = 0;
				else
					tmp_line[tpos] = 0;
				if (tpos > VI_MAX_COL - 1)
					tpos = VI_MAX_COL - 1;
				strncpy(vi_lines[vi_lcount - 1], tmp_line, (size_t)tpos);
				vi_lines[vi_lcount - 1][tpos] = 0;
				if (vi_lcount < VI_MAX_LINES)
				{
					vi_lcount++;
					bzero(vi_lines[vi_lcount - 1], VI_MAX_COL);
				}
				tpos = 0;
			}
			else if (tpos < (int)sizeof(tmp_line) - 1)
				tmp_line[tpos++] = c;
		}
	}
	vfs_close(fd);
	if (vi_lcount > 0)
	{
		if (tpos > VI_MAX_COL - 1)
			tpos = VI_MAX_COL - 1;
		strncpy(vi_lines[vi_lcount - 1], tmp_line, (size_t)tpos);
		vi_lines[vi_lcount - 1][tpos] = 0;
	}
	vi_modified = 0;
}

static int	vi_save_file(const char *path)
{
	int	fd = vfs_open(path, VFS_O_READ | VFS_O_WRITE | VFS_O_CREATE);
	int	total = 0;

	if (fd < 0)
	{
		snprintf(vi_status, sizeof(vi_status), "Save FAILED: open(%d)", fd);
		return (-1);
	}
	if (vfs_truncate(path, 0) != VFS_OK)
	{
		snprintf(vi_status, sizeof(vi_status), "Save FAILED: truncate");
		vfs_close(fd);
		return (-1);
	}
	for (int i = 0; i < vi_lcount; i++)
	{
		uint32_t	len = (uint32_t)strlen(vi_lines[i]);

		if (vfs_write(fd, (const uint8_t *)vi_lines[i], (int)len) != (int)len)
		{
			snprintf(vi_status, sizeof(vi_status), "Save FAILED: write(%d)", i);
			vfs_close(fd);
			return (-1);
		}
		if (i == vi_lcount - 1 && len == 0)
			continue;
		if (vfs_write(fd, (const uint8_t *)"\n", 1) != 1)
		{
			snprintf(vi_status, sizeof(vi_status), "Save FAILED: write(%d)", i);
			vfs_close(fd);
			return (-1);
		}
		total += (int)len + 1;
	}
	vfs_close(fd);
	vi_modified = 0;
	snprintf(vi_msg, sizeof(vi_msg), "\"%s\" %d bytes written", path, total);
	vi_msg[VI_STATUS_SZ - 1] = 0;
	return (0);
}

static void	vi_exec_cmd(const char *cmd)
{
	if (strcmp(cmd, "w") == 0)
		vi_save_file(vi_fname);
	else if (strcmp(cmd, "q") == 0)
	{
		if (vi_modified)
			vi_set_msg("No write since last change (add ! to override)");
		else
			vi_done = 1;
	}
	else if (strcmp(cmd, "q!") == 0)
		vi_done = 1;
	else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "wq!") == 0
		|| strcmp(cmd, "x") == 0)
	{
		if (vi_save_file(vi_fname) == 0)
			vi_done = 1;
	}
	else if (cmd[0] == 'e' && cmd[1] == ' ')
	{
		const char	*p = cmd + 2;

		while (*p == ' ')
			p++;
		vfs_cmd_resolve(p, vi_fname, sizeof(vi_fname));
		vi_fname[sizeof(vi_fname) - 1] = 0;
		vi_lcount = 1;
		bzero(vi_lines[0], VI_MAX_COL);
		vi_row = 0;
		vi_col = 0;
		vi_scroll = 0;
		vi_scroll_x = 0;
		vi_modified = 0;
		vi_load_file(vi_fname);
		if (vi_lcount < 1)
		{
			vi_lcount = 1;
			bzero(vi_lines[0], VI_MAX_COL);
		}
		vi_set_msg("");
	}
	else if (cmd[0] == 'r' && (cmd[1] == ' ' || cmd[1] == '!'))
	{
		const char	*p = cmd + 2;
		uint8_t	rbuf[1024];
		int	fd;
		int	n;
		char	tline[4096];
		int	tpos = 0;
		int	ins_row = vi_row + 1;

		while (*p == ' ')
			p++;
		fd = vfs_open(p, VFS_O_READ);
		if (fd < 0)
		{
			vi_set_msg("File not found");
			return;
		}
		if (vi_lcount >= VI_MAX_LINES)
		{
			vfs_close(fd);
			return;
		}
		for (int i = vi_lcount; i > ins_row; i--)
			for (int k = 0; k < VI_MAX_COL; k++)
				vi_lines[i][k] = vi_lines[i - 1][k];
		vi_lcount++;
		bzero(vi_lines[ins_row], VI_MAX_COL);
		while ((n = vfs_read(fd, rbuf, sizeof(rbuf))) > 0)
		{
			for (int i = 0; i < n; i++)
			{
				char	c = (char)rbuf[i];

				if (c == '\n')
				{
					if (tpos > VI_MAX_COL - 1)
						tpos = VI_MAX_COL - 1;
					strncpy(vi_lines[ins_row], tline, (size_t)tpos);
					vi_lines[ins_row][tpos] = 0;
					ins_row++;
					if (ins_row >= VI_MAX_LINES)
						break;
					for (int j = vi_lcount; j > ins_row; j--)
						for (int k = 0; k < VI_MAX_COL; k++)
							vi_lines[j][k] = vi_lines[j - 1][k];
					vi_lcount++;
					bzero(vi_lines[ins_row], VI_MAX_COL);
					tpos = 0;
				}
				else if (tpos < (int)sizeof(tline) - 1)
					tline[tpos++] = c;
			}
		}
		vfs_close(fd);
		if (tpos > 0 && ins_row < VI_MAX_LINES)
		{
			if (tpos > VI_MAX_COL - 1)
				tpos = VI_MAX_COL - 1;
			strncpy(vi_lines[ins_row], tline, (size_t)tpos);
			vi_lines[ins_row][tpos] = 0;
		}
		vi_modified = 1;
	}
	else if (strcmp(cmd, "set nu") == 0)
	{
		vi_show_nu = 1;
		vi_show_rnu = 0;
		vi_set_msg("");
	}
	else if (strcmp(cmd, "set rnu") == 0)
	{
		vi_show_rnu = 1;
		vi_show_nu = 0;
		vi_set_msg("");
	}
	else if (strcmp(cmd, "set nonu") == 0 || strcmp(cmd, "set nornu") == 0)
	{
		vi_show_nu = 0;
		vi_show_rnu = 0;
		vi_set_msg("");
	}
	else if (strcmp(cmd, "noh") == 0 || strcmp(cmd, "nohlsearch") == 0)
	{
		vi_search_highlight = 0;
		vi_set_msg("");
	}
	else if (cmd[0] == 's' && cmd[1] == '/')
	{
		char	pat[VI_SEARCH_SZ];
		char	rep[VI_SEARCH_SZ];
		int	plen = 0;
		int	rlen = 0;
		const char	*p = cmd + 2;
		int	phase = 0;

		bzero(pat, sizeof(pat));
		bzero(rep, sizeof(rep));
		while (*p && phase < 2)
		{
			if (phase == 0)
			{
				if (*p == '/')
				{
					phase = 1;
					p++;
				}
				else
				{
					if (plen < VI_SEARCH_SZ - 1)
						pat[plen++] = *p;
					p++;
				}
			}
			else
			{
				if (*p == '/' || *p == 'g')
				{
					p++;
					if (*p == 0 || *(p - 1) == '/')
						phase = 2;
				}
				else
				{
					if (rlen < VI_SEARCH_SZ - 1)
						rep[rlen++] = *p;
					p++;
				}
			}
		}
		vi_substitute_line(vi_row, pat, plen, rep, rlen);
		vi_modified = 1;
		vi_set_msg("");
	}
	else if (cmd[0] == '%' && cmd[1] == 's' && cmd[2] == '/')
	{
		char	pat[VI_SEARCH_SZ];
		char	rep[VI_SEARCH_SZ];
		int	plen = 0;
		int	rlen = 0;
		const char	*p = cmd + 3;
		int	phase = 0;

		bzero(pat, sizeof(pat));
		bzero(rep, sizeof(rep));
		while (*p && phase < 2)
		{
			if (phase == 0)
			{
				if (*p == '/')
				{
					phase = 1;
					p++;
				}
				else
				{
					if (plen < VI_SEARCH_SZ - 1)
						pat[plen++] = *p;
					p++;
				}
			}
			else
			{
				if (*p == '/' || *p == 'g')
				{
					p++;
					if (*p == 0 || *(p - 1) == '/')
						phase = 2;
				}
				else
				{
					if (rlen < VI_SEARCH_SZ - 1)
						rep[rlen++] = *p;
					p++;
				}
			}
		}
		for (int i = 0; i < vi_lcount; i++)
			vi_substitute_line(i, pat, plen, rep, rlen);
		vi_modified = 1;
		vi_set_msg("");
	}
	else if (cmd[0] == '>' && cmd[1] == '>')
	{
		vi_indent(1);
		vi_set_msg("");
	}
	else if (cmd[0] == '<' && cmd[1] == '<')
	{
		vi_indent(-1);
		vi_set_msg("");
	}
	else if (strcmp(cmd, "help") == 0)
	{
		vi_set_msg(":w :q :wq :x :! :e f :r f :%s//g :s// :set nu/rnu :noh :d :>> :<<");
	}
	else
		vi_set_msg("Not an editor command");
}

static void	vi_draw(void)
{
	uint8_t	ed_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	uint8_t	tilde_color = vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
	uint8_t	bar_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
	uint8_t	sel_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
	uint8_t	nu_color = vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
	uint8_t	search_hl_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_BROWN);
	int	h = (int)term_height;
	int	w = (int)term_width;
	char	bar[128];
	int	r;
	int	x;
	int	gnu_w = 0;

	term_clear();
	if (vi_show_nu || vi_show_rnu)
		gnu_w = 4;
	for (r = 0; r < h - 1; r++)
	{
		int	lineno = vi_scroll + r;

		if (lineno >= vi_lcount)
		{
			term_putch_at('~', tilde_color, 0, (size_t)r);
			for (x = 1; x < w; x++)
				term_putch_at(' ', tilde_color, (size_t)x, (size_t)r);
			continue;
		}
		if (gnu_w > 0)
		{
			char	nbuf[5];
			int	n;

			if (vi_show_nu)
				n = lineno + 1;
			else
				n = vi_row - lineno > 0 ? vi_row - lineno : lineno - vi_row + 1;
			if (n > 9999)
				n = 9999;
			snprintf(nbuf, sizeof(nbuf), "%4d", n);
			for (x = 0; x < gnu_w; x++)
				term_putch_at(nbuf[x], nu_color, (size_t)x, (size_t)r);
		}
		for (x = gnu_w; x < w; x++)
		{
			int	ci = vi_scroll_x + (x - gnu_w);
			char	ch = ' ';
			uint8_t	color = ed_color;
			int	in_sel = 0;

			if (ci < VI_MAX_COL && ci >= 0 && vi_lines[lineno][ci])
				ch = vi_lines[lineno][ci];
			if (vi_vis_type != VIS_NONE && vi_mode == VI_M_VIS)
			{
				int	vsr = vi_vis_start_row < vi_row ? vi_vis_start_row : vi_row;
				int	ver = vi_vis_start_row < vi_row ? vi_row : vi_vis_start_row;
				int	vsc = vi_vis_start_row < vi_row ? vi_vis_start_col : vi_col;
				int	VEC = vi_vis_start_row < vi_row ? vi_col : vi_vis_start_col;

				if (vi_vis_type == VIS_LINE)
				{
					if (lineno >= vsr && lineno <= ver)
						in_sel = 1;
				}
				else
				{
					if (lineno > vsr && lineno < ver)
						in_sel = 1;
					else if (lineno == vsr && lineno == ver)
						in_sel = (ci >= vsc && ci <= VEC);
					else if (lineno == vsr)
						in_sel = (ci >= vsc);
					else if (lineno == ver)
						in_sel = (ci <= VEC);
				}
			}
			if (in_sel)
				color = sel_color;
			else if (vi_search_highlight && vi_search_len > 0
				&& ci >= 0 && ci <= vi_llen(lineno) - vi_search_len
				&& strncmp(&vi_lines[lineno][ci], vi_search_pat,
					(size_t)vi_search_len) == 0)
				color = search_hl_color;
			term_putch_at(ch, color, (size_t)x, (size_t)r);
		}
	}

	for (x = 0; x < w; x++)
		term_putch_at(' ', bar_color, (size_t)x, (size_t)(h - 1));

	if (vi_mode == VI_M_CMD)
	{
		bar[0] = ':';
		for (int i = 0; i < vi_cmd_len && i < w - 2; i++)
			bar[1 + i] = vi_cmd[i];
		bar[1 + (vi_cmd_len < w - 2 ? vi_cmd_len : w - 2)] = 0;
		for (x = 0; bar[x] && x < w; x++)
			term_putch_at(bar[x], bar_color, (size_t)x, (size_t)(h - 1));
	}
	else if (vi_mode == VI_M_SEARCH)
	{
		bar[0] = vi_search_dir > 0 ? '/' : '?';
		for (int i = 0; i < vi_search_prompt_len && i < w - 2; i++)
			bar[1 + i] = vi_search_prompt[i];
		bar[1 + (vi_search_prompt_len < w - 2 ? vi_search_prompt_len : w - 2)] = 0;
		for (x = 0; bar[x] && x < w; x++)
			term_putch_at(bar[x], bar_color, (size_t)x, (size_t)(h - 1));
	}
	else if (vi_msg[0])
	{
		for (int i = 0; vi_msg[i] && i < w; i++)
			term_putch_at(vi_msg[i], bar_color, (size_t)i, (size_t)(h - 1));
	}
	else
	{
		const char	*mode_str = "-- NORMAL --";
		char	flags[16];

		flags[0] = 0;
		if (vi_mode == VI_M_INS)
			mode_str = "-- INSERT --";
		else if (vi_mode == VI_M_VIS)
			mode_str = vi_vis_type == VIS_LINE ? "-- V-LINE --" : "-- VISUAL --";
		if (vi_modified)
			strcat(flags, "+");
		if (vi_show_nu)
			strcat(flags, "nu ");
		if (vi_show_rnu)
			strcat(flags, "rnu ");
		snprintf(bar, sizeof(bar), "%s  [Ln %d, Col %d]%s  %s",
			vi_fname, vi_row + 1, vi_col + 1, flags, mode_str);
		for (int i = 0; bar[i] && i < w; i++)
			term_putch_at(bar[i], bar_color, (size_t)i, (size_t)(h - 1));
	}

	{
		int	cx = vi_col - vi_scroll_x + gnu_w;
		int	cy = vi_row - vi_scroll;

		if (cx < gnu_w)
			cx = gnu_w;
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

static void	vi_clamp(void)
{
	int	len = vi_llen(vi_row);

	if (vi_col > len)
		vi_col = len;
	if (vi_row < 0)
		vi_row = 0;
	if (vi_row >= vi_lcount)
		vi_row = vi_lcount - 1;
	if (len < 0)
		len = 0;
	if (vi_scroll > vi_row)
		vi_scroll = vi_row;
	if (vi_row >= vi_scroll + (int)term_height - 1)
		vi_scroll = vi_row - ((int)term_height - 2);
	if (vi_scroll_x > vi_col)
		vi_scroll_x = vi_col;
	if (vi_col >= vi_scroll_x + (int)term_width - 4)
		vi_scroll_x = vi_col - (int)term_width + 5;
	if (vi_scroll_x < 0)
		vi_scroll_x = 0;
}

static int	vi_event(void)
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
		if (in_read == 1)
		{
			if (read_key != 0)
			{
				char	c = read_key;

				read_key = 0;
				in_read = 0;
				return ((int)(uint8_t)c);
			}
			if (nav_key == 0)
			{
				in_read = 0;
				return (EV_ESC);
			}
		}
	}
}

static void	vi_move(int dir)
{
	switch (dir)
	{
		case 1:
			if (vi_row > 0)
			{
				vi_row--;
				vi_clamp();
			}
			break;
		case 2:
			if (vi_row < vi_lcount - 1)
			{
				vi_row++;
				vi_clamp();
			}
			break;
		case 3:
			if (vi_col > 0)
			{
				vi_col--;
				vi_clamp();
			}
			else if (vi_row > 0)
			{
				vi_row--;
				vi_col = vi_llen(vi_row);
			}
			break;
		case 4:
			if (vi_col < vi_llen(vi_row))
			{
				vi_col++;
				vi_clamp();
			}
			else if (vi_row < vi_lcount - 1)
			{
				vi_row++;
				vi_col = 0;
			}
			break;
	}
}

static void	vi_enter_insert(int kind)
{
	if (kind == 'i')
		vi_mode = VI_M_INS;
	else if (kind == 'a')
	{
		if (vi_col < vi_llen(vi_row))
			vi_col++;
		vi_mode = VI_M_INS;
	}
	else if (kind == 'I')
	{
		vi_col = 0;
		while (vi_col < vi_llen(vi_row)
			&& (vi_lines[vi_row][vi_col] == ' ' || vi_lines[vi_row][vi_col] == '\t'))
			vi_col++;
		vi_mode = VI_M_INS;
	}
	else if (kind == 'A')
	{
		vi_col = vi_llen(vi_row);
		vi_mode = VI_M_INS;
	}
	else if (kind == 'o')
	{
		vi_open_line(0);
		vi_mode = VI_M_INS;
	}
	else if (kind == 'O')
	{
		vi_open_line(1);
		vi_mode = VI_M_INS;
	}
	vi_msg[0] = 0;
}

static void	vi_norm_key(int ev)
{
	if (ev == EV_CANCEL)
		return;
	if (vi_op_pending)
	{
		int	p = vi_op;

		vi_op_pending = 0;
		vi_op = OP_NONE;
		if (p == OP_DELETE && ev == 'd')
		{
			vi_delete_line();
			vi_clamp();
			return;
		}
		if (p == OP_YANK && ev == 'y')
		{
			int	ll = vi_llen(vi_row);
			char	tmp[VI_YANK_MAX];

			if (ll >= VI_YANK_MAX)
				ll = VI_YANK_MAX - 1;
			memcpy(tmp, vi_lines[vi_row], (size_t)ll);
			tmp[ll] = 0;
			vi_yank_text(tmp, ll, 1);
			vi_set_msg("1 line yanked");
			return;
		}
		if (p == OP_CHANGE && ev == 'c')
		{
			vi_save_undo();
			bzero(vi_lines[vi_row], VI_MAX_COL);
			vi_col = 0;
			vi_lcount = vi_row + 1;
			if (vi_lcount < 1)
				vi_lcount = 1;
			if (vi_row >= vi_lcount)
				vi_row = vi_lcount - 1;
			vi_modified = 1;
			vi_mode = VI_M_INS;
			vi_msg[0] = 0;
			return;
		}
		if (ev == 'w' || ev == 'b' || ev == 'e' || ev == '$' || ev == '0'
			|| ev == '^' || ev == 'h' || ev == 'l')
		{
			vi_apply_op_motion(p, 0, ev);
			return;
		}
		if (ev == 'j' || ev == 'k' || ev == 'G' || ev == 'H' || ev == 'M'
			|| ev == 'L')
		{
			vi_apply_op_motion(p, 1, ev);
			return;
		}
		return;
	}
	if (vi_pending)
	{
		int	p = vi_pending;

		vi_pending = 0;
		if (p == 'r')
		{
			if (ev >= 0x20 && ev < 0x7F && ev != '\n')
			{
				vi_save_undo();
				vi_lines[vi_row][vi_col] = (char)ev;
				vi_modified = 1;
			}
			return;
		}
		if (p == 'g' && ev == 'g')
		{
			vi_row = 0;
			vi_col = 0;
			return;
		}
		if (p == 'z')
		{
			int	target;

			if (ev == 'z')
				target = vi_row - ((int)term_height - 2) / 2;
			else if (ev == 't')
				target = vi_scroll;
			else if (ev == 'b')
				target = vi_scroll + (int)term_height - 3;
			else if (ev == 'm')
				target = vi_scroll + ((int)term_height - 2) / 2;
			else
				target = vi_row;
			vi_scroll = target;
			if (vi_scroll < 0)
				vi_scroll = 0;
			if (vi_scroll > vi_lcount - 1)
				vi_scroll = vi_lcount - 1;
			return;
		}
		if (p == 'f')
		{
			char	*line = vi_lines[vi_row];
			int	len = vi_llen(vi_row);

			for (int i = vi_col + 1; i < len; i++)
			{
				if (line[i] == (char)ev)
				{
					vi_col = i;
					return;
				}
			}
			return;
		}
		if (p == '>' && ev == '>')
		{
			vi_indent(1);
			return;
		}
		if (p == '<' && ev == '<')
		{
			vi_indent(-1);
			return;
		}
		return;
	}

	if (ev >= EV_NAV(1) && ev <= EV_NAV(4))
	{
		vi_move(ev & 0xFF);
		return;
	}

	switch (ev)
	{
		case 'h':
		case '\b':
			vi_move(3);
			break;
		case 'j':
			vi_move(2);
			break;
		case 'k':
			vi_move(1);
			break;
		case 'l':
		case ' ':
			vi_move(4);
			break;
		case '0':
			vi_col = 0;
			break;
		case '^':
			vi_col = 0;
			while (vi_col < vi_llen(vi_row)
				&& (vi_lines[vi_row][vi_col] == ' ' || vi_lines[vi_row][vi_col] == '\t'))
				vi_col++;
			break;
		case '$':
			vi_col = vi_llen(vi_row);
			if (vi_col > 0)
				vi_col--;
			break;
		case 'w':
			vi_word_fwd();
			break;
		case 'b':
			vi_word_back();
			break;
		case 'e':
		{
			int	end = vi_word_end(vi_row, vi_col);

			if (end == vi_col && vi_col < vi_llen(vi_row))
				end = vi_col + 1;
			vi_col = end;
			if (vi_col >= vi_llen(vi_row) && vi_row < vi_lcount - 1)
			{
				vi_row++;
				vi_col = 0;
			}
			break;
		}
		case 'x':
			vi_save_undo();
			vi_delete_char();
			if (vi_col >= vi_llen(vi_row) && vi_col > 0)
				vi_col--;
			break;
		case 'X':
			if (vi_col > 0)
			{
				vi_save_undo();
				vi_col--;
				vi_delete_char();
			}
			break;
		case 'D':
			vi_save_undo();
			vi_delete_to_eol();
			if (vi_col >= vi_llen(vi_row) && vi_col > 0)
				vi_col--;
			break;
		case 'C':
			vi_save_undo();
			vi_delete_to_eol();
			vi_mode = VI_M_INS;
			vi_msg[0] = 0;
			break;
		case 'S':
			vi_save_undo();
			bzero(vi_lines[vi_row], VI_MAX_COL);
			vi_col = 0;
			vi_modified = 1;
			vi_mode = VI_M_INS;
			vi_msg[0] = 0;
			break;
		case 'J':
			vi_join_lines();
			break;
		case 'p':
			vi_paste_after();
			break;
		case 'P':
			vi_paste_before();
			break;
		case 'y':
			vi_op = OP_YANK;
			vi_op_linewise = 0;
			vi_op_pending = 1;
			break;
		case 'd':
			vi_op = OP_DELETE;
			vi_op_linewise = 0;
			vi_op_pending = 1;
			break;
		case 'c':
			vi_op = OP_CHANGE;
			vi_op_linewise = 0;
			vi_op_pending = 1;
			break;
		case 'G':
			vi_row = vi_lcount - 1;
			vi_col = 0;
			break;
		case 'H':
			vi_row = vi_scroll;
			vi_col = 0;
			break;
		case 'M':
			vi_row = vi_scroll + ((int)term_height - 2) / 2;
			if (vi_row >= vi_lcount)
				vi_row = vi_lcount - 1;
			vi_col = 0;
			break;
		case 'L':
			vi_row = vi_scroll + (int)term_height - 3;
			if (vi_row >= vi_lcount)
				vi_row = vi_lcount - 1;
			vi_col = 0;
			break;
		case '\n':
			if (vi_row < vi_lcount - 1)
			{
				vi_row++;
				vi_col = 0;
			}
			break;
		case '~':
			vi_toggle_case();
			break;
		case 'u':
			vi_do_undo();
			break;
		case 'U':
			vi_do_redo();
			break;
		case '.':
			if (vi_dot_valid)
			{
				if (vi_dot_op == OP_DELETE && vi_dot_linewise)
					vi_delete_line();
				else if (vi_dot_op == OP_CHANGE)
				{
					bzero(vi_lines[vi_row], VI_MAX_COL);
					vi_col = 0;
					vi_modified = 1;
					vi_mode = VI_M_INS;
				}
			}
			break;
		case 'i':
		case 'a':
		case 'I':
		case 'A':
		case 'o':
		case 'O':
			vi_dot_op = OP_CHANGE;
			vi_dot_linewise = (ev == 'o' || ev == 'O' || ev == 'S');
			vi_dot_valid = 1;
			vi_enter_insert(ev);
			break;
		case 'v':
			vi_vis_type = VIS_CHAR;
			vi_vis_start_row = vi_row;
			vi_vis_start_col = vi_col;
			vi_mode = VI_M_VIS;
			vi_msg[0] = 0;
			break;
		case 'V':
			vi_vis_type = VIS_LINE;
			vi_vis_start_row = vi_row;
			vi_vis_start_col = 0;
			vi_col = 0;
			vi_mode = VI_M_VIS;
			vi_msg[0] = 0;
			break;
		case ':':
			vi_mode = VI_M_CMD;
			vi_cmd_len = 0;
			bzero(vi_cmd, sizeof(vi_cmd));
			vi_msg[0] = 0;
			break;
		case '/':
			vi_mode = VI_M_SEARCH;
			vi_search_dir = 1;
			vi_search_prompt_len = 0;
			bzero(vi_search_prompt, sizeof(vi_search_prompt));
			vi_msg[0] = 0;
			break;
		case '?':
			vi_mode = VI_M_SEARCH;
			vi_search_dir = -1;
			vi_search_prompt_len = 0;
			bzero(vi_search_prompt, sizeof(vi_search_prompt));
			vi_msg[0] = 0;
			break;
		case 'n':
			vi_search_next();
			break;
		case 'N':
			vi_search_prev();
			break;
		case '%':
			vi_move_to_matching();
			break;
		case '{':
			vi_move_to_paragraph(0);
			break;
		case '}':
			vi_move_to_paragraph(1);
			break;
		case 'f':
			vi_pending = 'f';
			break;
		case 'z':
			vi_pending = 'z';
			break;
		case 'g':
			vi_pending = 'g';
			break;
		case 'r':
			vi_pending = 'r';
			break;
		case '>':
			vi_pending = '>';
			break;
		case '<':
			vi_pending = '<';
			break;
	}
}

static void	vi_cmd_key(int ev)
{
	if (ev == EV_ESC || ev == EV_CANCEL)
	{
		vi_mode = VI_M_NORM;
		vi_msg[0] = 0;
		return;
	}
	if (ev == '\b')
	{
		if (vi_cmd_len > 0)
			vi_cmd[--vi_cmd_len] = 0;
		return;
	}
	if (ev == '\n')
	{
		vi_cmd[vi_cmd_len] = 0;
		vi_exec_cmd(vi_cmd);
		vi_mode = VI_M_NORM;
		return;
	}
	if (ev >= 0x20 && ev < 0x7F && ev != ':' && vi_cmd_len < VI_CMD_SZ - 1)
		vi_cmd[vi_cmd_len++] = (char)ev;
}

static void	vi_search_key(int ev)
{
	if (ev == EV_ESC || ev == EV_CANCEL)
	{
		vi_mode = VI_M_NORM;
		vi_msg[0] = 0;
		return;
	}
	if (ev == '\b')
	{
		if (vi_search_prompt_len > 0)
			vi_search_prompt[--vi_search_prompt_len] = 0;
		return;
	}
	if (ev == '\n')
	{
		vi_search_prompt[vi_search_prompt_len] = 0;
		strncpy(vi_search_pat, vi_search_prompt, VI_SEARCH_SZ - 1);
		vi_search_pat[VI_SEARCH_SZ - 1] = 0;
		vi_search_len = vi_search_prompt_len;
		vi_search_highlight = 1;
		vi_search_next();
		vi_mode = VI_M_NORM;
		return;
	}
	if (ev >= 0x20 && ev < 0x7F && vi_search_prompt_len < VI_SEARCH_SZ - 1)
		vi_search_prompt[vi_search_prompt_len++] = (char)ev;
}

static void	vi_visual_key(int ev)
{
	if (ev == EV_ESC || ev == EV_CANCEL)
	{
		vi_vis_type = VIS_NONE;
		vi_mode = VI_M_NORM;
		return;
	}
	if (ev >= EV_NAV(1) && ev <= EV_NAV(4))
	{
		vi_move(ev & 0xFF);
		return;
	}
	switch (ev)
	{
		case 'h':
		case '\b':
			vi_move(3);
			break;
		case 'j':
			vi_move(2);
			break;
		case 'k':
			vi_move(1);
			break;
		case 'l':
		case ' ':
			vi_move(4);
			break;
		case '0':
			vi_col = 0;
			break;
		case '$':
			vi_col = vi_llen(vi_row);
			if (vi_col > 0)
				vi_col--;
			break;
		case '^':
			vi_col = 0;
			while (vi_col < vi_llen(vi_row)
				&& (vi_lines[vi_row][vi_col] == ' ' || vi_lines[vi_row][vi_col] == '\t'))
				vi_col++;
			break;
		case 'w':
			vi_word_fwd();
			break;
		case 'b':
			vi_word_back();
			break;
		case 'G':
			vi_row = vi_lcount - 1;
			vi_col = 0;
			break;
		case 'H':
			vi_row = vi_scroll;
			vi_col = 0;
			break;
		case 'M':
			vi_row = vi_scroll + ((int)term_height - 2) / 2;
			if (vi_row >= vi_lcount)
				vi_row = vi_lcount - 1;
			vi_col = 0;
			break;
		case 'L':
			vi_row = vi_scroll + (int)term_height - 3;
			if (vi_row >= vi_lcount)
				vi_row = vi_lcount - 1;
			vi_col = 0;
			break;
		case 'd':
		case 'x':
			vi_visual_delete();
			break;
		case 'y':
			vi_visual_yank();
			vi_vis_type = VIS_NONE;
			vi_mode = VI_M_NORM;
			break;
		case 'c':
			vi_visual_delete();
			vi_mode = VI_M_INS;
			break;
		case 'p':
			vi_visual_delete();
			vi_paste_before();
			break;
		case 'V':
			vi_vis_type = vi_vis_type == VIS_LINE ? VIS_CHAR : VIS_LINE;
			if (vi_vis_type == VIS_LINE)
				vi_col = 0;
			break;
		case 'v':
			vi_vis_type = vi_vis_type == VIS_CHAR ? VIS_LINE : VIS_CHAR;
			break;
		case 'o':
		{
			int	tmp_r = vi_vis_start_row;
			int	tmp_c = vi_vis_start_col;

			vi_vis_start_row = vi_row;
			vi_vis_start_col = vi_col;
			vi_row = tmp_r;
			vi_col = tmp_c;
			break;
		}
		case '~':
			vi_toggle_case();
			break;
		case ':':
			vi_vis_type = VIS_NONE;
			vi_mode = VI_M_CMD;
			vi_cmd_len = 0;
			bzero(vi_cmd, sizeof(vi_cmd));
			vi_msg[0] = 0;
			break;
	}
}

static void	vi_ins_key(int ev)
{
	if (ev == EV_ESC || ev == EV_CANCEL)
	{
		vi_mode = VI_M_NORM;
		if (vi_col > 0)
			vi_col--;
		vi_clamp();
		return;
	}
	if (ev >= EV_NAV(1) && ev <= EV_NAV(4))
	{
		vi_move(ev & 0xFF);
		return;
	}
	if (ev == EV_CTRL('r'))
	{
		vi_do_redo();
		return;
	}
	switch (ev)
	{
		case EV_REDRAW:
			return;
		case '\b':
			vi_backspace();
			break;
		case '\n':
			vi_newline();
			break;
		case '\t':
			for (int i = 0; i < 4; i++)
				vi_insert_char(' ');
			break;
		case EV_CTRL('w'):
		{
			char	*line = vi_lines[vi_row];
			int	len = vi_llen(vi_row);

			if (vi_col > 0)
			{
				int	start = vi_col - 1;
				int	rm;

				while (start > 0 && (line[start] == ' ' || line[start] == '\t'))
					start--;
				while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t')
					start--;
				rm = vi_col - start;
				for (int i = start; i <= len; i++)
					line[i] = line[i + rm];
				vi_col = start;
				vi_modified = 1;
			}
			break;
		}
		default:
			if (ev >= 0x20 && ev < 0x7F)
				vi_insert_char(ev);
			break;
	}
}

int	cmd_vi(int argc, char **argv)
{
	if (argc < 2)
	{
		printk("Usage: vi <path>\n");
		printk("  NORMAL: h j k l 0 $ ^ w b e G H M L x X D C S J ~ u U .\n");
		printk("  OPERATORS: dd yy cc dw dy cw d$ y$ d0 y0 d^ y^ >><<\n");
		printk("  VISUAL: v V  y d c p x o ~\n");
		printk("  SEARCH: /pat ?pat n N  f<char> %%\n");
		printk("  INSERT: i a I A o O  Ctrl-w Ctrl-h\n");
		printk("  EX: :w :q :wq :x :!cmd :e file :r file\n");
		printk("       :%s/p/r/g :s// :set nu :set rnu :noh :d :>> :<<\n");
		printk("  SCROLL: zt zb zm zz  { }\n");
		return (0);
	}
	vfs_cmd_resolve(argv[1], vi_fname, sizeof(vi_fname));
	vi_fname[sizeof(vi_fname) - 1] = 0;

	vi_lcount = 1;
	bzero(vi_lines[0], VI_MAX_COL);
	vi_row = 0;
	vi_col = 0;
	vi_scroll = 0;
	vi_scroll_x = 0;
	vi_modified = 0;
	vi_mode = VI_M_NORM;
	vi_done = 0;
	vi_pending = 0;
	vi_msg[0] = 0;
	vi_op = OP_NONE;
	vi_op_pending = 0;
	vi_vis_type = VIS_NONE;
	vi_search_len = 0;
	vi_search_highlight = 0;
	vi_show_nu = 0;
	vi_show_rnu = 0;
	vi_undo_valid = 0;
	vi_dot_valid = 0;
	vi_load_file(argv[1]);
	if (vi_lcount < 1)
	{
		vi_lcount = 1;
		bzero(vi_lines[0], VI_MAX_COL);
	}

	while (!vi_done)
	{
		int	ev;

		vi_clamp();
		vi_draw();
		ev = vi_event();
		if (vi_mode == VI_M_CMD)
			vi_cmd_key(ev);
		else if (vi_mode == VI_M_SEARCH)
			vi_search_key(ev);
		else if (vi_mode == VI_M_VIS)
			vi_visual_key(ev);
		else if (ev == EV_REDRAW)
			;
		else if (vi_mode == VI_M_INS)
			vi_ins_key(ev);
		else
			vi_norm_key(ev);
	}

	term_clear();
	t_row = 0;
	t_col = 0;
	update_cursor(0, 0);
	return (0);
}
