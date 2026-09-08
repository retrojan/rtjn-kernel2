#include <stdint.h>
#include "fbcon.h"
#include "font8x16.h"

#define FB_FONT_W	8
#define FB_FONT_H	16

static uintptr_t	fb_addr = 0;
static uint32_t		fb_pitch = 0;
static uint32_t		fb_width = 0;
static uint32_t		fb_height = 0;
static uint32_t		fb_bpp = 32;
static int			fb_ready = 0;

typedef struct s_rgb
{
	uint8_t	r, g, b;
}			t_rgb;

/* Standard 16-color VGA palette (R, G, B) */
static const t_rgb palette[16] = {
	{0x00,0x00,0x00},{0x00,0x00,0xAA},{0x00,0xAA,0x00},{0x00,0xAA,0xAA},
	{0xAA,0x00,0x00},{0xAA,0x00,0xAA},{0xAA,0x55,0x00},{0xAA,0xAA,0xAA},
	{0x55,0x55,0x55},{0x55,0x55,0xFF},{0x55,0xFF,0x55},{0x55,0xFF,0xFF},
	{0xFF,0x55,0x55},{0xFF,0x55,0xFF},{0xFF,0xFF,0x55},{0xFF,0xFF,0xFF},
};

int		fb_active(void)
{
	return (fb_ready);
}

static inline void	fb_putpixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t	*px = (uint8_t*)fb_addr;

	if (x >= fb_width || y >= fb_height)
		return ;
	px += (uintptr_t)y * fb_pitch + (uintptr_t)x * (fb_bpp / 8);
	if (fb_bpp == 32)
	{
		px[0] = b; px[1] = g; px[2] = r; px[3] = 0xFF;
	}
	else if (fb_bpp == 24)
	{
		px[0] = b; px[1] = g; px[2] = r;
	}
	else if (fb_bpp == 16)
	{
		uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
		*(uint16_t*)px = v;
	}
}

int		fb_init(uintptr_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint32_t bpp)
{
	fb_addr = addr;
	fb_pitch = pitch;
	fb_width = w;
	fb_height = h;
	fb_bpp = bpp;
	fb_ready = (addr != 0 && w != 0 && h != 0);
	return (fb_ready);
}

void	fb_clear_bg(uint8_t bg_idx)
{
	const t_rgb *bg = &palette[bg_idx & 0xF];

	if (!fb_ready)
		return ;
	for (uint32_t y = 0; y < fb_height; y++)
	{
		for (uint32_t x = 0; x < fb_width; x++)
			fb_putpixel(x, y, bg->r, bg->g, bg->b);
	}
}

void	fb_render_cell(uint32_t x, uint32_t y, char c, uint8_t fg_idx, uint8_t bg_idx)
{
	const t_rgb *fg = &palette[fg_idx & 0xF];
	const t_rgb *bg = &palette[bg_idx & 0xF];
	const uint8_t *glyph;
	uint32_t	px, py;

	if (!fb_ready)
		return ;
	if (c < 0x20 || c > 0x7E)
		c = '?';
	glyph = font8x16[c - 0x20];

	for (uint32_t row = 0; row < FB_FONT_H; row++)
	{
		uint8_t bits = glyph[row];
		for (uint32_t col = 0; col < FB_FONT_W; col++)
		{
			px = x * FB_FONT_W + col;
			py = y * FB_FONT_H + row;
			if (bits & (0x80 >> col))
				fb_putpixel(px, py, fg->r, fg->g, fg->b);
			else
				fb_putpixel(px, py, bg->r, bg->g, bg->b);
		}
	}
}
