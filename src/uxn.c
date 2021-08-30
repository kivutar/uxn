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

static void   push8(Stack *s, Uint16 a) { if(s->ptr == 0xff) { s->error = 2; return; } s->dat[s->ptr++] = a; }
static Uint16  pop8_keep(Stack *s) { if(s->kptr == 0) { s->error = 1; return 0; } return s->dat[--s->kptr]; }
static Uint16  pop8_nokeep(Stack *s) { if(s->ptr == 0) { s->error = 1; return 0; } return s->dat[--s->ptr]; }

static void   poke8(Uint8 *m, Uint16 a, Uint16 b) { m[a] = b; }
static Uint16 peek8(Uint8 *m, Uint16 a) { return m[a]; }
void   poke16(Uint8 *m, Uint16 a, Uint16 b) { poke8(m, a, b >> 8); poke8(m, a + 1, b); }
Uint16 peek16(Uint8 *m, Uint16 a) { return (peek8(m, a) << 8) + peek8(m, a + 1); }

static void   push16(Stack *s, Uint16 a) { push8(s, a >> 8); push8(s, a); }
static Uint16 pop16(Stack *s) { Uint8 a = pop8(s), b = pop8(s); return a + (b << 8); }

static void   devw8(Device *d, Uint8 a, Uint8 b) { d->dat[a & 0xf] = b; d->talk(d, a & 0x0f, 1); }
static Uint8  devr8(Device *d, Uint8 a) { d->talk(d, a & 0x0f, 0); return d->dat[a & 0xf];  }
static void   devw16(Device *d, Uint8 a, Uint16 b) { devw8(d, a, b >> 8); devw8(d, a + 1, b); }
static Uint16 devr16(Device *d, Uint16 a) { return (devr8(d, a) << 8) + devr8(d, a + 1); }

/* Stack */
static void op_lit(Uxn *u) { push8(u->src, peek8(u->ram.dat, u->ram.ptr++)); }
static void op_inc(Uxn *u) { Uint16 a = pop(u->src); push(u->src, a + 1); }
static void op_pop(Uxn *u) { pop(u->src); }
static void op_dup(Uxn *u) { Uint16 a = pop(u->src); push(u->src, a); push(u->src, a); }
static void op_nip(Uxn *u) { Uint16 a = pop(u->src); pop(u->src); push(u->src, a); }
static void op_swp(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push(u->src, a); push(u->src, b); }
static void op_ovr(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push(u->src, b); push(u->src, a); push(u->src, b); }
static void op_rot(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src), c = pop(u->src); push(u->src, b); push(u->src, a); push(u->src, c); }
/* Logic */
static void op_equ(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push8(u->src, b == a); }
static void op_neq(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push8(u->src, b != a); }
static void op_gth(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push8(u->src, b > a); }
static void op_lth(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push8(u->src, b < a); }
static void op_jmp(Uxn *u) { Uint8 a = pop8(u->src); u->ram.ptr += (Sint8)a; }
static void op_jnz(Uxn *u) { Uint8 a = pop8(u->src); if(pop8(u->src)) u->ram.ptr += (Sint8)a; }
static void op_jsr(Uxn *u) { Uint8 a = pop8(u->src); push16(u->dst, u->ram.ptr); u->ram.ptr += (Sint8)a; }
static void op_sth(Uxn *u) { Uint16 a = pop(u->src); push(u->dst, a); }
/* Memory */
static void op_ldz(Uxn *u) { Uint8 a = pop8(u->src); push(u->src, peek(u->ram.dat, a)); }
static void op_stz(Uxn *u) { Uint8 a = pop8(u->src); Uint16 b = pop(u->src); poke(u->ram.dat, a, b); }
static void op_ldr(Uxn *u) { Uint8 a = pop8(u->src); push(u->src, peek(u->ram.dat, u->ram.ptr + (Sint8)a)); }
static void op_str(Uxn *u) { Uint8 a = pop8(u->src); Uint8 b = pop(u->src); poke(u->ram.dat, u->ram.ptr + (Sint8)a, b); }
static void op_lda(Uxn *u) { Uint16 a = pop16(u->src); push(u->src, peek(u->ram.dat, a)); }
static void op_sta(Uxn *u) { Uint16 a = pop16(u->src); Uint16 b = pop(u->src); poke(u->ram.dat, a, b); }
static void op_dei(Uxn *u) { Uint8 a = pop8(u->src); push8(u->src, devr8(&u->dev[a >> 4], a)); }
static void op_deo(Uxn *u) { Uint8 a = pop8(u->src), b = pop8(u->src); devw8(&u->dev[a >> 4], a, b); }
/* Arithmetic */
static void op_add(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push(u->src, b + a); }
static void op_sub(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push(u->src, b - a); }
static void op_mul(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push(u->src, b * a); }
static void op_div(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); if(a == 0) { u->src->error = 3; a = 1; } push(u->src, b / a); }
static void op_and(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push(u->src, b & a); }
static void op_ora(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push(u->src, b | a); }
static void op_eor(Uxn *u) { Uint16 a = pop(u->src), b = pop(u->src); push(u->src, b ^ a); }
static void op_sft(Uxn *u) { Uint16 a = pop8(u->src), b = pop(u->src); push(u->src, b >> (a & 0x07) << ((a & 0x70) >> 4)); }
/* Stack(16-bits) */
static void op_lit16(Uxn *u) { push16(u->src, peek16(u->ram.dat, u->ram.ptr++)); u->ram.ptr++; }
/* Logic(16-bits) */
static void op_jmp16(Uxn *u) { u->ram.ptr = pop16(u->src); }
static void op_jnz16(Uxn *u) { Uint16 a = pop16(u->src); if(pop8(u->src)) u->ram.ptr = a; }
static void op_jsr16(Uxn *u) { push16(u->dst, u->ram.ptr); u->ram.ptr = pop16(u->src); }
/* Memory(16-bits) */
static void op_dei16(Uxn *u) { Uint8 a = pop8(u->src); push16(u->src, devr16(&u->dev[a >> 4], a)); }
static void op_deo16(Uxn *u) { Uint8 a = pop8(u->src); Uint16 b = pop16(u->src); devw16(&u->dev[a >> 4], a, b); }

static void (*ops[])(Uxn *u) = {
	op_lit, op_inc, op_pop, op_dup, op_nip, op_swp, op_ovr, op_rot,
	op_equ, op_neq, op_gth, op_lth, op_jmp, op_jnz, op_jsr, op_sth, 
	op_ldz, op_stz, op_ldr, op_str, op_lda, op_sta, op_dei, op_deo,
	op_add, op_sub, op_mul, op_div, op_and, op_ora, op_eor, op_sft,
	/* 16-bit */
	op_lit16, op_inc, op_pop, op_dup, op_nip, op_swp, op_ovr, op_rot,
	op_equ, op_neq, op_gth, op_lth, op_jmp16, op_jnz16, op_jsr16, op_sth, 
	op_ldz, op_stz, op_ldr, op_str, op_lda, op_sta, op_dei16, op_deo16, 
	op_add, op_sub, op_mul, op_div, op_and, op_ora, op_eor, op_sft
};

/* clang-format on */

#pragma mark - Core

int
uxn_eval(Uxn *u, Uint16 vec)
{
	Uint8 instr;
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
			pop8 = pop8_keep;
			u->src->kptr = u->src->ptr;
		} else {
			pop8 = pop8_nokeep;
		}
		/* Short Mode */
		if(instr & MODE_SHORT) {
			pop = pop16;
			push = push16;
			peek = peek16;
			poke = poke16;
		} else {
			pop = pop8;
			push = push8;
			peek = peek8;
			poke = poke8;
		}
		(*ops[(instr & 0x1f) | ((instr & MODE_SHORT) >> 1)])(u);
		if(u->wst.error)
			return uxn_halt(u, u->wst.error, "Working-stack", instr);
		if(u->rst.error)
			return uxn_halt(u, u->rst.error, "Return-stack", instr);
	}
	return 1;
}

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
uxn_port(Uxn *u, Uint8 id, char *name, void (*talkfn)(Device *d, Uint8 b0, Uint8 w))
{
	Device *d = &u->dev[id];
	d->addr = id * 0x10;
	d->u = u;
	d->mem = u->ram.dat;
	d->talk = talkfn;
	(void)name;
	return d;
}
