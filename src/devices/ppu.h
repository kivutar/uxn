#include <stdio.h>
#include <stdlib.h>

/*
Copyright (c) 2021 Devine Lu Linvega
Copyright (c) 2021 Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

/* pixels per word in ppu.dat */

#define PPW (sizeof(unsigned int) * 2)

typedef unsigned char Uint8;
typedef unsigned short Uint16;
typedef unsigned int Uint32;

typedef struct Ppu {
	Uint16 width, height;
	unsigned int *dat, stride;
} Ppu;

int ppu_set_size(Ppu *p, Uint16 width, Uint16 height);
unsigned int ppu_pixel(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 color);
unsigned int ppu_1bpp(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy);
unsigned int ppu_2bpp(Ppu *p, int fg, Uint16 x, Uint16 y, Uint8 *sprite, Uint8 color, Uint8 flipx, Uint8 flipy);
