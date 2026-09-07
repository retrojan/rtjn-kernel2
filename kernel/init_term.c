#include "term.h"
#include "cursor.h"
#include "vga.h"

size_t terminal_row = 0;
size_t terminal_column = 0;
uint8_t terminal_color = VGA_ENTRY_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
uint16_t* terminal_buffer = (uint16_t*)0xB8000;
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

void	init_term(void)
{
	term_width = VGA_WIDTH;
	term_height = VGA_HEIGHT;
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	terminal_buffer = (uint16_t*)0xB8000;
	for (size_t y = 0; y < VGA_HEIGHT; y++)
	{
		for (size_t x = 0; x < VGA_WIDTH; x++)
		{
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}
	enable_cursor(0, 15);
}
