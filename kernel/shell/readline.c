#include <stddef.h>
#include <stdint.h>
#include "cursor.h"
#include "printk.h"
#include "term.h"
#include "commands.h"
#include "string.h"

extern volatile uint8_t	in_read;
extern volatile char	read_key;
extern volatile int8_t	nav_key;
extern volatile uint8_t	do_clear_screen;
extern volatile uint8_t	cancel_input;
extern volatile uint8_t	word_del;

/* Cursor cell (VGA offset) right after the prompt, i.e. the cell owned by
 * buf[0].  Every edit operation renders through absolute cells so insert /
 * delete / cursor moves never corrupt surrounding text. */
static size_t	g_base_cell = 0;

static void	cell_put(size_t k, char c)
{
	size_t	cell = g_base_cell + k;
	size_t	row;

	if (cell / term_width >= term_height)
		return;
	row = cell / term_width;
	term_putch_at(c, t_color, cell % term_width, row);
}

static void	cell_cursor(size_t pos)
{
	size_t	cell = g_base_cell + pos;

	t_row = cell / term_width;
	t_col = cell % term_width;
	update_cursor(t_col, t_row);
}

static void	cell_draw(size_t from, const char *buf, size_t len)
{
	for (size_t k = from; k < len; k++)
		cell_put(k, buf[k]);
}

static void	cell_clear(size_t from, size_t to)
{
	for (size_t k = from; k < to; k++)
		cell_put(k, ' ');
}

static void	insert_char(char *buf, size_t size, size_t *len, size_t *pos, char c)
{
	if (*len + 1 >= size)
		return;
	memmove(buf + *pos + 1, buf + *pos, *len - *pos + 1);
	buf[*pos] = c;
	(*len)++;
	cell_draw(*pos, buf, *len);
	(*pos)++;
	cell_cursor(*pos);
}

static void	backspace_at(char *buf, size_t *len, size_t *pos)
{
	size_t	old = *len;

	if (*pos == 0)
		return;
	memmove(buf + *pos - 1, buf + *pos, *len - *pos + 1);
	(*len)--;
	(*pos)--;
	cell_draw(*pos, buf, *len);
	cell_clear(*len, old);
	cell_cursor(*pos);
}

/* Alt+Backspace: delete everything from the cursor back to the start of the
 * previous word (whitespace included). */
static void	delete_word(char *buf, size_t *len, size_t *pos)
{
	size_t	start = *pos;
	size_t	old = *len;

	if (start == 0)
		return;
	while (start > 0 && buf[start - 1] == ' ')
		start--;
	while (start > 0 && buf[start - 1] != ' ')
		start--;
	if (start == *pos)
		return;
	memmove(buf + start, buf + *pos, *len - *pos + 1);
	*len -= (*pos - start);
	*pos = start;
	cell_draw(start, buf, *len);
	cell_clear(*len, old);
	cell_cursor(*pos);
}

static void	cursor_left(size_t *pos)
{
	if (*pos > 0)
	{
		(*pos)--;
		cell_cursor(*pos);
	}
}

static void	cursor_right(size_t *pos, size_t len)
{
	if (*pos < len)
	{
		(*pos)++;
		cell_cursor(*pos);
	}
}

static size_t	replace_line(char *buf, size_t size, size_t i, size_t *pos, const char *line)
{
	size_t	old = i;

	cell_clear(0, old);
	i = 0;
	for (size_t k = 0; line[k] != 0 && i < size - 1; k++)
	{
		buf[i] = line[k];
		i++;
	}
	buf[i] = 0;
	cell_draw(0, buf, i);
	*pos = i;
	cell_cursor(i);
	return (i);
}

int		readline(char *buf, size_t size)
{
	size_t	i = 0;
	size_t	pos = 0;
	int		hooked = 0;
	/* start a fresh prompt: don't inherit stale state from the previous
	 * prompt/command (e.g. left-over arrows or a held Enter running the
	 * last history entry by itself). */
	cancel_input = 0;
	word_del = 0;
	read_key = 0;
	nav_key = 0;
	in_read = 0;
	g_base_cell = t_row * term_width + t_col;
	while (i < size)
	{
		if (cancel_input == 1)
		{
			size_t	old = i;

			cancel_input = 0;
			i = 0;
			pos = 0;
			cell_clear(0, old);
			cell_cursor(0);
			buf[i] = 0;
			printk("^C\n");
			return (0);
		}
		if (do_clear_screen == 1)
		{
			do_clear_screen = 0;
			term_clear();
			printk("%s> ", g_cwd);
			i = 0;
			pos = 0;
			buf[0] = 0;
			hooked = 0;
			g_base_cell = t_row * term_width + t_col;
			continue ;
		}
		if (in_read == 1 && nav_key != 0)
		{
			in_read = 0;
			if (nav_key == 1)
			{
				const char	*cur;

				if (!hooked)
					shell_history_pos_set(shell_history_count());
				shell_history_prev();
				cur = shell_history_current();
				i = replace_line(buf, size, i, &pos, cur ? cur : "");
				hooked = 1;
			}
			else if (nav_key == 2)
			{
				const char	*cur = shell_history_current();

				shell_history_next();
				cur = shell_history_current();
				i = replace_line(buf, size, i, &pos, cur ? cur : "");
				hooked = 1;
			}
			else if (nav_key == 3)
			{
				cursor_left(&pos);
			}
			else if (nav_key == 4)
			{
				cursor_right(&pos, i);
			}
			nav_key = 0;
			continue ;
		}
		if (in_read == 1 && word_del)
		{
			in_read = 0;
			delete_word(buf, &i, &pos);
			word_del = 0;
			hooked = 0;
			buf[i] = 0;
			continue ;
		}
		if (in_read == 1 && read_key != 0)
		{
			in_read = 0;
			if (read_key == '\n')
			{
				read_key = 0;
				return (i);
			}
			if (read_key == '\b')
			{
				backspace_at(buf, &i, &pos);
			}
			else if (read_key == '\t')
			{
				;
			}
			else if (read_key >= 0x20 && read_key < 0x7F)
			{
				insert_char(buf, size, &i, &pos, read_key);
			}
			hooked = 0;
			buf[i] = 0;
			read_key = 0;
			continue ;
		}
	}
	return (i);
}

char	wait_key(void)
{
	while (1)
	{
		if (in_read == 1 && read_key != 0 && read_key != '\0')
		{
			char k = read_key;
			in_read = 0;
			return (k);
		}
	}
}
