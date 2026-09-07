#ifndef FBCON_H
# define FBCON_H

# include <stdint.h>

int		fb_init(uintptr_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint32_t bpp);
int		fb_active(void);
void	fb_render_cell(uint32_t x, uint32_t y, char c, uint8_t fg_idx, uint8_t bg_idx);
void	fb_clear_bg(uint8_t bg_idx);

#endif
