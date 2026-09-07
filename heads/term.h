#ifndef TERM_H
# define TERM_H

# include <stdint.h>
# include <stddef.h>

# define TERM_MAX_W	160
# define TERM_MAX_H	50

extern size_t terminal_row;
extern size_t terminal_column;
extern uint8_t terminal_color;
extern uint16_t* terminal_buffer;
extern size_t term_width;
extern size_t term_height;

size_t	term_get_width(void);
size_t	term_get_height(void);

void	init_term(void);
void	term_write(const char* data, size_t size);
void	puts(const char* data);
void	terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void	terminal_clear(void);
int		terminal_set_framebuffer(uintptr_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint32_t bpp);

static inline void terminal_setcolor(uint8_t color)
{
	terminal_color = color;
}


#endif
