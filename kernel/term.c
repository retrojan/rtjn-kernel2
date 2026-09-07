#include <stdint.h>
#include <stddef.h>
#include "term.h"
#include "vga.h"
#include "fbcon.h"

static uint16_t	fb_shadow[TERM_MAX_W * TERM_MAX_H];
static int		term_fb_mode = 0;

static void	fb_sync_all(void)
{
	for (size_t y = 0; y < term_height && y < TERM_MAX_H; y++)
	{
		for (size_t x = 0; x < term_width && x < TERM_MAX_W; x++)
		{
			uint16_t cell = terminal_buffer[y * term_width + x];
			fb_render_cell(x, y, (char)(cell & 0xFF), (uint8_t)((cell >> 8) & 0x0F), (uint8_t)(((cell >> 8) >> 4) & 0x0F));
		}
	}
}

void	terminal_putentryat(char c, uint8_t color, size_t x, size_t y)
{
	const size_t index = y * term_width + x;
	terminal_buffer[index] = vga_entry(c, color);
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
			terminal_buffer[index] = terminal_buffer[index + term_width];
		}
	}
	for (size_t x = 0; x < term_width; x++)
	{
		const size_t index = (term_height - 1) * term_width + x;
		terminal_buffer[index] = vga_entry(' ', terminal_color);
	}
	if (term_fb_mode)
		fb_sync_all();
}

void	terminal_clear(void)
{
	for (size_t y = 0; y < term_height && y < TERM_MAX_H; y++)
	{
		for (size_t x = 0; x < term_width && x < TERM_MAX_W; x++)
		{
			terminal_buffer[y * term_width + x] = vga_entry(' ', terminal_color);
		}
	}
	terminal_row = 0;
	terminal_column = 0;
	if (term_fb_mode)
	{
		fb_clear_bg((uint8_t)((terminal_color >> 4) & 0x0F));
		fb_sync_all();
	}
}

int	terminal_set_framebuffer(uintptr_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint32_t bpp)
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
	terminal_buffer = fb_shadow;
	term_fb_mode = 1;
	term_width = w / 8;
	term_height = h / 16;
	if (term_width < 1) term_width = 1;
	if (term_height < 1) term_height = 1;
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	fb_clear_bg(VGA_COLOR_BLACK);
	fb_sync_all();
	return (1);
}

static void	term_putchar(char c)
{
	if (c == '\n')
	{
		terminal_row++;
		terminal_column = 0;
	}
	else if (c == '\t')
	{
		for (size_t i = 0; (terminal_column % 4 != 0 && (term_width - terminal_column) > 1) || i == 0; i++)
		{
			terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
			terminal_column++;
		}
	}
	else
	{
		terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
		terminal_column++;
		if (terminal_column == term_width)
		{
			terminal_column = 0;
			terminal_row++;
		}
	}
	if (terminal_row == term_height)
	{
		shift_terminal_content();
		terminal_row--;
	}
}

void	term_write(const char* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		term_putchar(data[i]);
}
