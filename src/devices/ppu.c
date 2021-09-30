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
	Uint32 row, bound = p->height * p->width / 2;
	for(row = 0; row < bound; ++row)
		p->pixels[row] = 0;
}

Uint8
ppu_set_size(Ppu *p, Uint16 width, Uint16 height)
{
	ppu_clear(p);
	p->width = width;
	p->height = height;
	p->pixels = realloc(p->pixels, p->width * p->height * sizeof(Uint8) / 2);
	ppu_clear(p);
	return !!p->pixels;
}

Uint8
ppu_read(Ppu *p, Uint16 x, Uint16 y)
{
	Uint32 row = (x + y * p->width) / 0x2;
	Uint8 shift = !(x & 0x1) << 2;
	Uint8 pix = p->pixels[row] >> shift;
	if(pix & 0x0c)
		pix = pix >> 2;
	return pix & 0x3;
}

void
ppu_write(Ppu *p, Uint8 layer, Uint16 x, Uint16 y, Uint8 color)
{
	Uint32 row = (x + y * p->width) / 0x2;
	Uint8 shift = (!(x & 0x1) << 2) + (layer << 1);
	Uint8 pix = p->pixels[row];
	Uint8 mask = ~(0x3 << shift);
	Uint8 pixnew = (pix & mask) + (color << shift);
	if(x < p->width && y < p->height)
		p->pixels[row] = pixnew;
	if(pix != pixnew)
		p->reqdraw = 1;
}

void
ppu_1bpp(Ppu *p, Uint8 layer, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy)
{
	Uint16 v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = (sprite[v] >> (7 - h)) & 0x1;
			if(ch1 || blending[4][color])
				ppu_write(p,
					layer,
					x + (flipx ? 7 - h : h),
					y + (flipy ? 7 - v : v),
					blending[ch1][color]);
		}
}

void
ppu_2bpp(Ppu *p, Uint8 layer, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy)
{
	Uint16 v, h;
	for(v = 0; v < 8; v++)
		for(h = 0; h < 8; h++) {
			Uint8 ch1 = ((sprite[v] >> (7 - h)) & 0x1);
			Uint8 ch2 = ((sprite[v + 8] >> (7 - h)) & 0x1);
			Uint8 ch = ch1 + ch2 * 2;
			if(ch || blending[4][color])
				ppu_write(p,
					layer,
					x + (flipx ? 7 - h : h),
					y + (flipy ? 7 - v : v),
					blending[ch][color]);
		}
}
