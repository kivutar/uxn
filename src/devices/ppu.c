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

void
ppu_frame(Ppu *p)
{
	p->redraw = 0;
	p->i0 = p->width / PPW + p->height * p->stride;
	p->i1 = 0;
}

static void
ppu_clear(Ppu *p)
{
	unsigned int i;
	for(i = 0; i < p->stride * p->height; ++i)
		p->dat[i] = 0;
}

Uint8
ppu_read(Ppu *p, Uint16 x, Uint16 y)
{
	unsigned int i = x / PPW + y * p->stride, shift = x % PPW * 4;
	return (p->dat[i] >> shift) & 0xf;
}

void
ppu_write(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 color)
{
	unsigned int v, i = x / PPW + y * p->stride, shift = x % PPW * 4;
	if(x >= p->width || y >= p->height)
		return;
	v = p->dat[i];
	if(fg) shift += 2;
	p->dat[i] &= ~(3 << shift);
	p->dat[i] |= color << shift;
	if((v ^ p->dat[i]) != 0) {
		p->redraw = 1;
		p->i0 = p->i0 < i ? p->i0 : i;
		p->i1 = p->i1 > i ? p->i1 : i;
	}
}

void
ppu_1bpp(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy)
{
	Uint16 v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = (sprite[v] >> (7 - h)) & 0x1;
			if(ch1 || blending[4][color])
				ppu_write(p,
					fg,
					x + (flipx ? 7 - h : h),
					y + (flipy ? 7 - v : v),
					blending[ch1][color]);
		}
}

void
ppu_2bpp(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy)
{
	Uint16 v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = ((sprite[v] >> (7 - h)) & 0x1);
			Uint8 ch2 = ((sprite[v + 8] >> (7 - h)) & 0x1);
			Uint8 ch = ch1 + ch2 * 2;
			if(ch || blending[4][color])
				ppu_write(p,
					fg,
					x + (flipx ? 7 - h : h),
					y + (flipy ? 7 - v : v),
					blending[ch][color]);
		}
}

/* output */

int
ppu_set_size(Ppu *p, Uint16 width, Uint16 height)
{
	p->width = width;
	p->stride = (width + PPW - 1) / PPW;
	p->height = height;
	p->dat = realloc(p->dat, p->stride * p->height * sizeof(unsigned int));
	if(p->dat == NULL) return 0;
	ppu_clear(p);
	ppu_frame(p);
	return 1;
}
