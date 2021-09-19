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
	int i;
	for(i = 0; i < p->width / 2 * p->height; ++i)
		p->dat[i] = 0;
}

int
ppu_pixel(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 color)
{
	unsigned int i = (x + y * p->width) / 2, shift = (x % 2) * 4;
	int ret = p->dat[i];
	if(fg) shift += 2;
	p->dat[i] &= ~(3 << shift);
	p->dat[i] |= color << shift;
	return ret ^ p->dat[i];
}

int
ppu_1bpp(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy)
{
	Uint16 v, h;
	int ret = 0;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = (sprite[v] >> (7 - h)) & 0x1;
			if(ch1 || blending[4][color])
				ret |= ppu_pixel(p,
					fg,
					x + (flipx ? 7 - h : h),
					y + (flipy ? 7 - v : v),
					blending[ch1][color]);
		}
	return ret;
}

int
ppu_2bpp(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy)
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
					fg,
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
	/* round width up to nearest multiple of 2 */
	width += width % 2;
	p->width = width;
	p->height = height;
	p->dat = realloc(p->dat, p->width / 2 * p->height);
	if(p->dat == NULL) return 0;
	ppu_clear(p);
	return 1;
}