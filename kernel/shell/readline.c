#include <stddef.h>
#include <stdint.h>
#include "cursor.h"
#include "printk.h"
#include "term.h"
#include "commands.h"

extern volatile uint8_t	in_read;
extern volatile char	read_key;
extern volatile int8_t	nav_key;
extern volatile uint8_t	do_clear_screen;
extern volatile uint8_t	cancel_input;

static void	backspace(size_t *i)
{
	if (*i > 0)
	{
		(*i)--;
		if (t_col == 0 && t_row != 0)
		{
			t_col = term_width - 1;
			t_row -= 1;
		}
		else
		{
			t_col -= 1;
		}
		update_cursor(t_col, t_row);
		term_putch_at(' ', t_color, t_col, t_row);
	}
}

static size_t	replace_line(char *buf, size_t size, size_t i, const char *line)
{
	while (i > 0)
		backspace(&i);
	for (size_t k = 0; line[k] != 0 && i < size - 1; k++)
	{
		printk("%c", line[k]);
		buf[i] = line[k];
		i++;
	}
	buf[i] = 0;
	return (i);
}

int		readline(char *buf, size_t size)
{
	size_t	i = 0;
	int		hooked = 0;
	cancel_input = 0;	/* start a fresh prompt: don't inherit stale cancels */
	while (i < size)
	{
		if (cancel_input == 1)
		{
			cancel_input = 0;
			while (i > 0)
				backspace(&i);
			buf[i] = 0;
			printk("^C\n");
			return (0);
		}
		if (do_clear_screen == 1)
		{
			do_clear_screen = 0;
			term_clear();
			printk("# %s> ", g_cwd);
			i = 0;
			buf[0] = 0;
			hooked = 0;
			continue ;
		}
		if (in_read == 1 && nav_key != 0)
		{
			in_read = 0;
			if (nav_key == 1)
			{
				if (!hooked)
					shell_history_pos_set(shell_history_count());
				shell_history_prev();
			}
			else if (nav_key == 2)
			{
				shell_history_next();
			}
			if (nav_key == 1 || nav_key == 2)
			{
				const char *cur = shell_history_current();
				i = replace_line(buf, size, i, cur ? cur : "");
				hooked = 1;
			}
			nav_key = 0;
		}
		if (in_read == 1 && read_key != 0)
		{
			in_read = 0;
			if (read_key == '\n')
				return (i);
			if (read_key == '\b')
			{
				backspace(&i);
			}
			else if (read_key == '\t')
			{
				;
			}
			else if (read_key >= 0x20 && read_key < 0x7F)
			{
				printk("%c", read_key);
				buf[i] = read_key;
				i++;
			}
			hooked = 0;
			buf[i] = 0;
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
