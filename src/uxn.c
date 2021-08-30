#include "uxn.h"

/*
Copyright (u) 2021 Devine Lu Linvega
Copyright (u) 2021 Andrew Alderwick

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define MODE_RETURN 0x20
#define MODE_SHORT 0x40
#define MODE_KEEP 0x80

#pragma mark - Operations

/* clang-format off */

/* Utilities */
static void   (*push)(Stack *s, Uint16 a);
static Uint16 (*pop8)(Stack *s);
static Uint16 (*pop)(Stack *s);
static void   (*poke)(Uint8 *m, Uint16 a, Uint16 b);
static Uint16 (*peek)(Uint8 *m, Uint16 a);
static void   (*devw)(Device *d, Uint8 a, Uint16 b);
static Uint16 (*devr)(Device *d, Uint8 a);
static void   (*warp)(Uxn *u, Uint16 a);
static void   (*pull)(Uxn *u);
/* byte mode */
static void   push8(Stack *s, Uint16 a) { if(s->ptr == 0xff) { s->error = 2; return; } s->dat[s->ptr++] = a; }
static Uint16 pop8k(Stack *s) { if(s->kptr == 0) { s->error = 1; return 0; } return s->dat[--s->kptr]; }
static Uint16 pop8d(Stack *s) { if(s->ptr == 0) { s->error = 1; return 0; } return s->dat[--s->ptr]; }
static void   poke8(Uint8 *m, Uint16 a, Uint16 b) { m[a] = b; }
static Uint16 peek8(Uint8 *m, Uint16 a) { return m[a]; }
static void   devw8(Device *d, Uint8 a, Uint16 b) { d->dat[a & 0xf] = b; d->talk(d, a & 0x0f, 1); }
static Uint16 devr8(Device *d, Uint8 a) { d->talk(d, a & 0x0f, 0); return d->dat[a & 0xf];  }
static void   warp8(Uxn *u, Uint16 a){ u->ram.ptr += (Sint8)a; }
static void   pull8(Uxn *u){ push8(u->src, peek8(u->ram.dat, u->ram.ptr++)); }
/* short mode */
static void   push16(Stack *s, Uint16 a) { push8(s, a >> 8); push8(s, a); }
static Uint16 pop16(Stack *s) { Uint8 a = pop8(s), b = pop8(s); return a + (b << 8); }
	   void   poke16(Uint8 *m, Uint16 a, Uint16 b) { poke8(m, a, b >> 8); poke8(m, a + 1, b); }
	   Uint16 peek16(Uint8 *m, Uint16 a) { return (peek8(m, a) << 8) + peek8(m, a + 1); }
static void   devw16(Device *d, Uint8 a, Uint16 b) { devw8(d, a, b >> 8); devw8(d, a + 1, b); }
static Uint16 devr16(Device *d, Uint8 a) { return (devr8(d, a) << 8) + devr8(d, a + 1); }
static void   warp16(Uxn *u, Uint16 a){ u->ram.ptr = a; }
static void   pull16(Uxn *u){ push16(u->src, peek16(u->ram.dat, u->ram.ptr++)); u->ram.ptr++; }

#pragma mark - Core

int
uxn_eval(Uxn *u, Uint16 vec)
{
	Uint8 instr;
	Uint16 a,b,c;
	if(!vec || u->dev[0].dat[0xf])
		return 0;
	u->ram.ptr = vec;
	if(u->wst.ptr > 0xf8) u->wst.ptr = 0xf8;
	while((instr = u->ram.dat[u->ram.ptr++])) {
		/* Return Mode */
		if(instr & MODE_RETURN) {
			u->src = &u->rst; 
			u->dst = &u->wst;
		} else {
			u->src = &u->wst; 
			u->dst = &u->rst;
		}
		/* Keep Mode */
		if(instr & MODE_KEEP) {
			pop8 = pop8k;
			u->src->kptr = u->src->ptr;
		} else {
			pop8 = pop8d;
		}
		/* Short Mode */
		if(instr & MODE_SHORT) {
			push = push16; pop = pop16;
			poke = poke16; peek = peek16;
			devw = devw16; devr = devr16;
			warp = warp16; pull = pull16;
		} else {
			push = push8; pop = pop8;
			poke = poke8; peek = peek8;
			devw = devw8; devr = devr8;
			warp = warp8; pull = pull8;
		}
		switch(instr & 0x1f){
			/* Stack */
			case 0x00: /* LIT */ pull(u); break;
			case 0x01: /* INC */ a = pop(u->src); push(u->src, a + 1); break;
			case 0x02: /* POP */ pop(u->src); break;
			case 0x03: /* DUP */ a = pop(u->src); push(u->src, a); push(u->src, a); break;
			case 0x04: /* NIP */ a = pop(u->src); pop(u->src); push(u->src, a); break;
			case 0x05: /* SWP */ a = pop(u->src), b = pop(u->src); push(u->src, a); push(u->src, b); break;
			case 0x06: /* OVR */ a = pop(u->src), b = pop(u->src); push(u->src, b); push(u->src, a); push(u->src, b); break;
			case 0x07: /* ROT */ a = pop(u->src), b = pop(u->src), c = pop(u->src); push(u->src, b); push(u->src, a); push(u->src, c); break;
			/* Logic */
			case 0x08: /* EQU */ a = pop(u->src), b = pop(u->src); push8(u->src, b == a); break;
			case 0x09: /* NEQ */ a = pop(u->src), b = pop(u->src); push8(u->src, b != a); break;
			case 0x0a: /* GTH */ a = pop(u->src), b = pop(u->src); push8(u->src, b > a); break;
			case 0x0b: /* LTH */ a = pop(u->src), b = pop(u->src); push8(u->src, b < a); break;
			case 0x0c: /* JMP */ a = pop(u->src); warp(u, a); break;
			case 0x0d: /* JNZ */ a = pop(u->src); if(pop8(u->src)) warp(u, a); break;
			case 0x0e: /* JSR */ a = pop(u->src); push16(u->dst, u->ram.ptr); warp(u, a); break;
			case 0x0f: /* STH */ a = pop(u->src); push(u->dst, a); break;
			/* Memory */
			case 0x10: /* LDZ */ a = pop8(u->src); push(u->src, peek(u->ram.dat, a)); break;
			case 0x11: /* STZ */ a = pop8(u->src); b = pop(u->src); poke(u->ram.dat, a, b); break;
			case 0x12: /* LDR */ a = pop8(u->src); push(u->src, peek(u->ram.dat, u->ram.ptr + (Sint8)a)); break;
			case 0x13: /* STR */ a = pop8(u->src); b = pop(u->src); poke(u->ram.dat, u->ram.ptr + (Sint8)a, b); break;
			case 0x14: /* LDA */ a = pop16(u->src); push(u->src, peek(u->ram.dat, a)); break;
			case 0x15: /* STA */ a = pop16(u->src); b = pop(u->src); poke(u->ram.dat, a, b); break;
			case 0x16: /* DEI */ a = pop8(u->src); push(u->src, devr(&u->dev[a >> 4], a)); break;
			case 0x17: /* DEO */ a = pop8(u->src); b = pop(u->src); devw(&u->dev[a >> 4], a, b); break;
			/* Arithmetic */
			case 0x18: /* ADD */ a = pop(u->src), b = pop(u->src); push(u->src, b + a); break;
			case 0x19: /* SUB */ a = pop(u->src), b = pop(u->src); push(u->src, b - a); break;
			case 0x1a: /* MUL */ a = pop(u->src), b = pop(u->src); push(u->src, b * a); break;
			case 0x1b: /* DIV */ a = pop(u->src), b = pop(u->src); if(a == 0) { u->src->error = 3; a = 1; } push(u->src, b / a); break;
			case 0x1c: /* AND */ a = pop(u->src), b = pop(u->src); push(u->src, b & a); break;
			case 0x1d: /* ORA */ a = pop(u->src), b = pop(u->src); push(u->src, b | a); break;
			case 0x1e: /* EOR */ a = pop(u->src), b = pop(u->src); push(u->src, b ^ a); break;
			case 0x1f: /* SFT */ a = pop8(u->src), b = pop(u->src); push(u->src, b >> (a & 0x07) << ((a & 0x70) >> 4)); break;
		}
		if(u->wst.error)
			return uxn_halt(u, u->wst.error, "Working-stack", instr);
		if(u->rst.error)
			return uxn_halt(u, u->rst.error, "Return-stack", instr);
	}
	return 1;
}

/* clang-format on */

int
uxn_boot(Uxn *u)
{
	unsigned int i;
	char *cptr = (char *)u;
	for(i = 0; i < sizeof(*u); i++)
		cptr[i] = 0x00;
	return 1;
}

Device *
uxn_port(Uxn *u, Uint8 id, void (*talkfn)(Device *d, Uint8 b0, Uint8 w))
{
	Device *d = &u->dev[id];
	d->addr = id * 0x10;
	d->u = u;
	d->mem = u->ram.dat;
	d->talk = talkfn;
	return d;
}
