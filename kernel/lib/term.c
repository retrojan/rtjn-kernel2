#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include "term.h"
#include "vga.h"
#include "fbcon.h"
#include "cursor.h"
#include "string.h"
#include "printk.h"

size_t t_row = 0;
size_t t_col = 0;
uint8_t t_color = VGA_ENTRY_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
uint16_t* t_buf = (uint16_t*)0xB8000;
size_t term_width = VGA_WIDTH;
size_t term_height = VGA_HEIGHT;

size_t	term_get_width(void)
{
	return (term_width);
}

size_t	term_get_height(void)
{
	return (term_height);
}

void	term_init(void)
{
	term_width = VGA_WIDTH;
	term_height = VGA_HEIGHT;
	t_row = 0;
	t_col = 0;
	t_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	t_buf = (uint16_t*)0xB8000;
	for (size_t y = 0; y < VGA_HEIGHT; y++)
	{
		for (size_t x = 0; x < VGA_WIDTH; x++)
		{
			const size_t index = y * VGA_WIDTH + x;
			t_buf[index] = vga_entry(' ', t_color);
		}
	}
	enable_cursor(0, 15);
}

static uint16_t	fb_shadow[TERM_MAX_W * TERM_MAX_H];
static int		term_fb_mode = 0;

static void	fb_sync_all(void)
{
	for (size_t y = 0; y < term_height && y < TERM_MAX_H; y++)
	{
		for (size_t x = 0; x < term_width && x < TERM_MAX_W; x++)
		{
			uint16_t cell = t_buf[y * term_width + x];
			fb_render_cell(x, y, (char)(cell & 0xFF), (uint8_t)((cell >> 8) & 0x0F), (uint8_t)(((cell >> 8) >> 4) & 0x0F));
		}
	}
}

void	term_putch_at(char c, uint8_t color, size_t x, size_t y)
{
	const size_t index = y * term_width + x;
	t_buf[index] = vga_entry(c, color);
	if (term_fb_mode)
		fb_render_cell(x, y, c, (uint8_t)(color & 0x0F), (uint8_t)((color >> 4) & 0x0F));
}

static void shift_terminal_content(void)
{
	for (size_t y = 0; y < term_height - 1; y++)
	{
		for (size_t x = 0; x < term_width; x++)
		{
			const size_t index = y * term_width + x;
			t_buf[index] = t_buf[index + term_width];
		}
	}
	for (size_t x = 0; x < term_width; x++)
	{
		const size_t index = (term_height - 1) * term_width + x;
		t_buf[index] = vga_entry(' ', t_color);
	}
	if (term_fb_mode)
		fb_sync_all();
}

void	term_clear(void)
{
	for (size_t y = 0; y < term_height && y < TERM_MAX_H; y++)
	{
		for (size_t x = 0; x < term_width && x < TERM_MAX_W; x++)
		{
			t_buf[y * term_width + x] = vga_entry(' ', t_color);
		}
	}
	t_row = 0;
	t_col = 0;
	if (term_fb_mode)
	{
		fb_clear_bg((uint8_t)((t_color >> 4) & 0x0F));
		fb_sync_all();
	}
}

int	term_set_fb(uintptr_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint32_t bpp)
{
	int ok;

	if (w > TERM_MAX_W || h > TERM_MAX_H)
	{
		w = TERM_MAX_W;
		h = TERM_MAX_H;
	}
	ok = fb_init(addr, pitch, w, h, bpp);
	if (!ok)
		return (0);
	t_buf = fb_shadow;
	term_fb_mode = 1;
	term_width = w / 8;
	term_height = h / 16;
	if (term_width < 1) term_width = 1;
	if (term_height < 1) term_height = 1;
	t_row = 0;
	t_col = 0;
	t_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	fb_clear_bg(VGA_COLOR_BLACK);
	fb_sync_all();
	return (1);
}

static void	term_putchar(char c)
{
	if (c == '\n')
	{
		t_row++;
		t_col = 0;
	}
	else if (c == '\t')
	{
		for (size_t i = 0; (t_col % 4 != 0 && (term_width - t_col) > 1) || i == 0; i++)
		{
			term_putch_at(' ', t_color, t_col, t_row);
			t_col++;
		}
	}
	else
	{
		term_putch_at(c, t_color, t_col, t_row);
		t_col++;
		if (t_col == term_width)
		{
			t_col = 0;
			t_row++;
		}
	}
	if (t_row == term_height)
	{
		shift_terminal_content();
		t_row--;
	}
}

void	term_write(const char* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		term_putchar(data[i]);
}

void puts(const char* data)
{
	term_write(data, strlen(data));
	update_cursor(t_col, t_row);
}

/* Longest single formatted value (e.g. a curl body) we buffer at once.
 * Anything longer is truncated; term_write streams it out if it exceeds
 * PRINTK_BUFF_LEN so we never overrun the working buffers. */
# define PRINTK_LOC_MAX	4096

static	int	get_flag(char c)
{
	int		ret = 0;

	switch (c)
	{
		case 's':
			ret = PRINTK_STR;
			break ;
		case 'p':
			ret = PRINTK_PTR;
			break ;
		case 'c':
			ret = PRINTK_CHR;
			break ;
		case 'd':
			ret = PRINTK_INT;
			break ;
		case 'u':
			ret = PRINTK_UINT;
			break ;
		case 'x':
			ret = PRINTK_HEX32;
			break ;
		case 'X':
			ret = PRINTK_HEX64;
			break ;
		case 'b':
			ret = PRINTK_BIN;
			break ;
	}
	return (ret);
}

static void	get_str(va_list *ap, char *loc_buff)
{
	char	*n = va_arg(*ap, char*);
	size_t	k = 0;

	if (!n)
		n = "(null)";
	while (n[k] && k < PRINTK_LOC_MAX - 1)
	{
		loc_buff[k] = n[k];
		k++;
	}
	loc_buff[k] = 0;
}

static void	get_ptr(va_list *ap, char *loc_buff)
{
	uint32_t	n = va_arg(*ap, uint32_t);
	loc_buff[0] = '0';
	loc_buff[1] = 'x';
	itoa_base_buf(n, 16, loc_buff + 2);
}

static void	get_char(va_list *ap, char *loc_buff)
{
	int		c = va_arg(*ap, int);
	loc_buff[0] = (char)c;
	loc_buff[1] =  0;
}

static void	get_int(va_list *ap, char *loc_buff)
{
	int		n = va_arg(*ap, int);
	itoa_buf(n, loc_buff);
}

static void	get_uint(va_list *ap, char *loc_buff)
{
	uint32_t	n = va_arg(*ap, uint32_t);
	itoa_base_buf(n, 10, loc_buff);
}

static void	get_hex32(va_list *ap, char *loc_buff)
{
	uint64_t	n = va_arg(*ap, uint32_t);
	itoa_base_buf(n, 16, loc_buff);
}

static void	get_hex64(va_list *ap, char *loc_buff)
{
	uint64_t	n = va_arg(*ap, uint64_t);
	itoa_base_buf(n, 16, loc_buff);
}

static void	get_bin(va_list *ap, char *loc_buff)
{
	uint32_t	n = va_arg(*ap, uint32_t);
	itoa_base_buf(n, 2, loc_buff);
}

static void	flush_printk_buff(char *buff, size_t *j)
{
	term_write(buff, *j);
	update_cursor(t_col, t_row);
	*j = 0;
}

static void	process_flag(va_list *ap, int flag, char *buff, size_t *j)
{
	static void (*fl[PRINTK_FLAGS_LEN])(va_list *ap, char *loc_buff) = {0x0, get_str,
			get_ptr, get_char, get_int, get_uint, get_hex32, get_hex64, get_bin};
	size_t	len;
	char	loc_buff[PRINTK_LOC_MAX];

	bzero(loc_buff, PRINTK_LOC_MAX);
	fl[flag](ap, loc_buff);
	len = strlen(loc_buff);
	/* A single value longer than the output buffer is streamed straight to
	 * the terminal (never strcat'ed), so we don't overflow anything. */
	if (len >= PRINTK_BUFF_LEN)
	{
		if (*j > 0)
			flush_printk_buff(buff, j);
		term_write(loc_buff, len);
		update_cursor(t_col, t_row);
		return;
	}
	if (*j + len > PRINTK_BUFF_LEN)
	{
		flush_printk_buff(buff, j);
	}
	strcat(buff, loc_buff);
	*j += len;
}

int		printk(const char *restrict format, ...)
{
	va_list	ap;
	size_t	len = strlen(format);
	size_t	j = 0;
	char	buff[PRINTK_BUFF_LEN];
	int		flag;

	bzero(buff, PRINTK_BUFF_LEN);
	va_start(ap, format);
	for (size_t i = 0; i < len; i++)
	{
		if (format[i] == '%' && i + 1 < len)
		{
			if ((flag = get_flag(format[i + 1])) != 0)
			{
				process_flag(&ap, flag, buff, &j);
			}
			else if (format[i + 1] == '%')
			{
				buff[j] = '%';
				j++;
			}
			i++;
		}
		else
		{
			buff[j] = format[i];
			j++;
		}

		if (j == PRINTK_BUFF_LEN && i + 1 < len)
			flush_printk_buff(buff, &j);
	}
	va_end(ap);
	flush_printk_buff(buff, &j);
	return (0);
}
