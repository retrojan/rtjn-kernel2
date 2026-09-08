#ifndef TERM_H
# define TERM_H

# include <stdint.h>
# include <stddef.h>

# define TERM_MAX_W	160
# define TERM_MAX_H	50

extern size_t t_row;
extern size_t t_col;
extern uint8_t t_color;
extern uint16_t* t_buf;
extern size_t term_width;
extern size_t term_height;

size_t	term_get_width(void);
size_t	term_get_height(void);

void	term_init(void);
void	term_write(const char* data, size_t size);
void	puts(const char* data);
void	term_putch_at(char c, uint8_t color, size_t x, size_t y);
void	term_clear(void);
int		term_set_fb(uintptr_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint32_t bpp);

static inline void term_setcolor(uint8_t color)
{
	t_color = color;
}


#endif
