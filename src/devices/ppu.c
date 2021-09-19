#include "ppu.h"

/*
Copyright (c) 2021 Devine Lu Linvega
Copyright (c) 2021 Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

static Uint8 blending[5][16] = {
	{0, 0, 0, 0, 1, 0, 1, 1, 2, 2, 0, 2, 3, 3, 3, 0},
	{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
	{1, 2, 3, 1, 1, 2, 3, 1, 1, 2, 3, 1, 1, 2, 3, 1},
	{2, 3, 1, 2, 2, 3, 1, 2, 2, 3, 1, 2, 2, 3, 1, 2},
	{1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0}};

static void
ppu_clear(Ppu *p)
{
	int x, y;
	for(y = 0; y < p->height; ++y) {
		for(x = 0; x < p->width; ++x) {
			ppu_pixel(p, p->bg, x, y, 0);
			ppu_pixel(p, p->fg, x, y, 0);
		}
	}
}

int
ppu_pixel(Ppu *p, Uint8 *layer, Uint16 x, Uint16 y, Uint8 color)
{
	int row = (y % 8) + ((x / 8 + y / 8 * p->width / 8) * 16), col = x % 8, ret;
	Uint8 w;
	if(x >= p->width || y >= p->height)
		return 0;
	w = layer[row];
	if(color == 0 || color == 2)
		layer[row] &= ~(1UL << (7 - col));
	else
		layer[row] |= 1UL << (7 - col);
	ret = w ^ layer[row];
	w = layer[row + 8];
	if(color == 0 || color == 1)
		layer[row + 8] &= ~(1UL << (7 - col));
	else
		layer[row + 8] |= 1UL << (7 - col);
	return ret | (w ^ layer[row + 8]);
}

int
ppu_1bpp(Ppu *p, Uint8 *layer, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy)
{
	Uint16 v, h;
	int ret = 0;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = (sprite[v] >> (7 - h)) & 0x1;
			if(ch1 || blending[4][color])
				ret |= ppu_pixel(p,
					layer,
					x + (flipx ? 7 - h : h),
					y + (flipy ? 7 - v : v),
					blending[ch1][color]);
		}
	return ret;
}

int
ppu_2bpp(Ppu *p, Uint8 *layer, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy)
{
	Uint16 v, h;
	int ret = 0;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = ((sprite[v] >> (7 - h)) & 0x1);
			Uint8 ch2 = ((sprite[v + 8] >> (7 - h)) & 0x1);
			Uint8 ch = ch1 + ch2 * 2;
			if(ch || blending[4][color])
				ret |= ppu_pixel(p,
					layer,
					x + (flipx ? 7 - h : h),
					y + (flipy ? 7 - v : v),
					blending[ch][color]);
		}
	return ret;
}

/* output */

int
ppu_set_size(Ppu *p, Uint16 width, Uint16 height)
{
	/* round width and height up to nearest multiple of 8 */
	width = ((width - 1) | 0x7) + 1;
	height = ((height - 1) | 0x7) + 1;
	p->width = width;
	p->height = height;
	p->bg = realloc(p->bg, p->width / 4 * p->height * sizeof(Uint8));
	p->fg = realloc(p->fg, p->width / 4 * p->height * sizeof(Uint8));
	ppu_clear(p);
	return p->bg && p->fg;
}