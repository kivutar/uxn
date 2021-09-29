#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "uxn.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <SDL.h>
#include "devices/ppu.h"
#include "devices/apu.h"
#pragma GCC diagnostic pop

/*
Copyright (c) 2021 Devine Lu Linvega

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define WIDTH 64 * 8
#define HEIGHT 40 * 8
#define PAD 4
#define FIXED_SIZE 0
#define POLYPHONY 4
#define BENCH 0

static SDL_Window *gWindow;
static SDL_Texture *gTexture;
static SDL_Renderer *gRenderer;
static SDL_AudioDeviceID audio_id;
static SDL_Rect gRect;
/* devices */
static Ppu ppu;
static Apu apu[POLYPHONY];
static Device *devsystem, *devscreen, *devmouse, *devctrl, *devaudio0, *devconsole;
static Uint8 zoom = 1, reqdraw = 0;
static Uint32 *ppu_screen, stdin_event, audio0_event, palette[16];

static Uint8 font[][8] = {
	{0x00, 0x7c, 0x82, 0x82, 0x82, 0x82, 0x82, 0x7c},
	{0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
	{0x00, 0x7c, 0x82, 0x02, 0x7c, 0x80, 0x80, 0xfe},
	{0x00, 0x7c, 0x82, 0x02, 0x1c, 0x02, 0x82, 0x7c},
	{0x00, 0x0c, 0x14, 0x24, 0x44, 0x84, 0xfe, 0x04},
	{0x00, 0xfe, 0x80, 0x80, 0x7c, 0x02, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x80, 0xfc, 0x82, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x02, 0x1e, 0x02, 0x02, 0x02},
	{0x00, 0x7c, 0x82, 0x82, 0x7c, 0x82, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x82, 0x7e, 0x02, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x02, 0x7e, 0x82, 0x82, 0x7e},
	{0x00, 0xfc, 0x82, 0x82, 0xfc, 0x82, 0x82, 0xfc},
	{0x00, 0x7c, 0x82, 0x80, 0x80, 0x80, 0x82, 0x7c},
	{0x00, 0xfc, 0x82, 0x82, 0x82, 0x82, 0x82, 0xfc},
	{0x00, 0x7c, 0x82, 0x80, 0xf0, 0x80, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x80, 0xf0, 0x80, 0x80, 0x80}};

static int
clamp(int val, int min, int max)
{
	return (val >= min) ? (val <= max) ? val : max : min;
}

static int
error(char *msg, const char *err)
{
	fprintf(stderr, "%s: %s\n", msg, err);
	return 0;
}

#pragma mark - Generics

static void
audio_callback(void *u, Uint8 *stream, int len)
{
	int i, running = 0;
	Sint16 *samples = (Sint16 *)stream;
	SDL_memset(stream, 0, len);
	for(i = 0; i < POLYPHONY; ++i)
		running += apu_render(&apu[i], samples, samples + len / 2);
	if(!running)
		SDL_PauseAudioDevice(audio_id, 1);
	(void)u;
}

void
apu_finished_handler(Apu *c)
{
	SDL_Event event;
	event.type = audio0_event + (c - apu);
	SDL_PushEvent(&event);
}

static int
stdin_handler(void *p)
{
	SDL_Event event;
	event.type = stdin_event;
	while(read(0, &event.cbutton.button, 1) > 0)
		SDL_PushEvent(&event);
	return 0;
	(void)p;
}

void
set_palette(Uint8 *addr)
{
	int i;
	for(i = 0; i < 4; ++i) {
		Uint8
			r = (*(addr + i / 2) >> (!(i % 2) << 2)) & 0x0f,
			g = (*(addr + 2 + i / 2) >> (!(i % 2) << 2)) & 0x0f,
			b = (*(addr + 4 + i / 2) >> (!(i % 2) << 2)) & 0x0f;
		palette[i] = 0xff000000 | (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
	}
	for(i = 4; i < 16; ++i)
		palette[i] = palette[i / 4];
	reqdraw = 1;
}

static void
set_inspect(Uint8 flag)
{
	devsystem->dat[0xe] = flag;
	reqdraw = 1;
}

static void
set_window_size(SDL_Window *window, int w, int h)
{
	SDL_Point win, win_old;
	SDL_GetWindowPosition(window, &win.x, &win.y);
	SDL_GetWindowSize(window, &win_old.x, &win_old.y);
	SDL_SetWindowPosition(window, (win.x + win_old.x / 2) - w / 2, (win.y + win_old.y / 2) - h / 2);
	SDL_SetWindowSize(window, w, h);
}

static void
set_zoom(Uint8 scale)
{
	if(scale == zoom || !gWindow)
		return;
	set_window_size(gWindow, (ppu.width + PAD * 2) * zoom, (ppu.height + PAD * 2) * zoom);
	reqdraw = 1;
}

static int
set_size(Uint16 width, Uint16 height, int is_resize)
{
	ppu_set_size(&ppu, width, height);
	gRect.x = PAD;
	gRect.y = PAD;
	gRect.w = ppu.width;
	gRect.h = ppu.height;
	if(!(ppu_screen = realloc(ppu_screen, ppu.width * ppu.height * sizeof(Uint32))))
		return error("ppu_screen", "Memory failure");
	memset(ppu_screen, 0, ppu.width * ppu.height * sizeof(Uint32));
	if(gTexture != NULL) SDL_DestroyTexture(gTexture);
	SDL_RenderSetLogicalSize(gRenderer, ppu.width + PAD * 2, ppu.height + PAD * 2);
	gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ppu.width + PAD * 2, ppu.height + PAD * 2);
	if(gTexture == NULL || SDL_SetTextureBlendMode(gTexture, SDL_BLENDMODE_NONE))
		return error("sdl_texture", SDL_GetError());
	if(is_resize)
		set_window_size(gWindow, (ppu.width + PAD * 2) * zoom, (ppu.height + PAD * 2) * zoom);
	reqdraw = 1;
	return 1;
}

static void
capture_screen(void)
{
	const Uint32 format = SDL_PIXELFORMAT_RGB24;
	time_t t = time(NULL);
	char fname[64];
	int w, h;
	SDL_Surface *surface;
	SDL_GetRendererOutputSize(gRenderer, &w, &h);
	surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, format);
	SDL_RenderReadPixels(gRenderer, NULL, format, surface->pixels, surface->pitch);
	strftime(fname, sizeof(fname), "screenshot-%Y%m%d-%H%M%S.bmp", localtime(&t));
	SDL_SaveBMP(surface, fname);
	SDL_FreeSurface(surface);
	fprintf(stderr, "Saved %s\n", fname);
}

static void
draw_inspect(Ppu *p, Uint8 *stack, Uint8 wptr, Uint8 rptr, Uint8 *memory)
{
	Uint8 i, x, y, b;
	for(i = 0; i < 0x20; ++i) { /* stack */
		x = ((i % 8) * 3 + 1) * 8, y = (i / 8 + 1) * 8, b = stack[i];
		ppu_1bpp(p, 1, x, y, font[(b >> 4) & 0xf], 1 + (wptr == i) * 0x7, 0, 0);
		ppu_1bpp(p, 1, x + 8, y, font[b & 0xf], 1 + (wptr == i) * 0x7, 0, 0);
	}
	/* return pointer */
	ppu_1bpp(p, 1, 0x8, y + 0x10, font[(rptr >> 4) & 0xf], 0x2, 0, 0);
	ppu_1bpp(p, 1, 0x10, y + 0x10, font[rptr & 0xf], 0x2, 0, 0);
	for(i = 0; i < 0x20; ++i) { /* memory */
		x = ((i % 8) * 3 + 1) * 8, y = 0x38 + (i / 8 + 1) * 8, b = memory[i];
		ppu_1bpp(p, 1, x, y, font[(b >> 4) & 0xf], 3, 0, 0);
		ppu_1bpp(p, 1, x + 8, y, font[b & 0xf], 3, 0, 0);
	}
	for(x = 0; x < 0x10; ++x) { /* guides */
		ppu_write(p, 1, x, p->height / 2, 2);
		ppu_write(p, 1, p->width - x, p->height / 2, 2);
		ppu_write(p, 1, p->width / 2, p->height - x, 2);
		ppu_write(p, 1, p->width / 2, x, 2);
		ppu_write(p, 1, p->width / 2 - 0x10 / 2 + x, p->height / 2, 2);
		ppu_write(p, 1, p->width / 2, p->height / 2 - 0x10 / 2 + x, 2);
	}
}

static void
redraw(Uxn *u)
{
	Uint16 x, y, y0 = 0, y1 = ppu.height;
	SDL_Rect up = gRect;
	if(devsystem->dat[0xe])
		draw_inspect(&ppu, u->wst.dat, u->wst.ptr, u->rst.ptr, u->ram.dat);
	if(!reqdraw && ppu.reqdraw) {
		y0 = ppu.i0 / ppu.stride;
		y1 = ppu.i1 / ppu.stride + 1;
		up.y += y0;
		up.h = y1 - y0;
	}
	for(y = y0; y < y1; ++y)
		for(x = 0; x < ppu.width; ++x)
			ppu_screen[x + y * ppu.width] = palette[ppu_read(&ppu, x, y)];
	SDL_UpdateTexture(gTexture, &up, ppu_screen + y0 * ppu.width, ppu.width * sizeof(Uint32));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
	reqdraw = 0;
	ppu_frame(&ppu);
}

static void
quit(void)
{
	SDL_UnlockAudioDevice(audio_id);
	SDL_DestroyTexture(gTexture);
	gTexture = NULL;
	SDL_DestroyRenderer(gRenderer);
	gRenderer = NULL;
	SDL_DestroyWindow(gWindow);
	SDL_Quit();
	exit(0);
}

static int
init(void)
{
	SDL_AudioSpec as;
	SDL_zero(as);
	as.freq = SAMPLE_FREQUENCY;
	as.format = AUDIO_S16;
	as.channels = 2;
	as.callback = audio_callback;
	as.samples = 512;
	as.userdata = NULL;
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		error("sdl", SDL_GetError());
		if(SDL_Init(SDL_INIT_VIDEO) < 0)
			return error("sdl", SDL_GetError());
	} else {
		audio_id = SDL_OpenAudioDevice(NULL, 0, &as, NULL, 0);
		if(!audio_id)
			error("sdl_audio", SDL_GetError());
	}
	gWindow = SDL_CreateWindow("Uxn", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, (WIDTH + PAD * 2) * zoom, (HEIGHT + PAD * 2) * zoom, SDL_WINDOW_SHOWN);
	if(gWindow == NULL)
		return error("sdl_window", SDL_GetError());
	gRenderer = SDL_CreateRenderer(gWindow, -1, 0);
	if(gRenderer == NULL)
		return error("sdl_renderer", SDL_GetError());
	stdin_event = SDL_RegisterEvents(1);
	audio0_event = SDL_RegisterEvents(POLYPHONY);
	SDL_CreateThread(stdin_handler, "stdin", NULL);
	SDL_StartTextInput();
	SDL_ShowCursor(SDL_DISABLE);
	return 1;
}

static void
domouse(SDL_Event *event)
{
	Uint8 flag = 0x00;
	Uint16 x = clamp(event->motion.x - PAD, 0, ppu.width - 1);
	Uint16 y = clamp(event->motion.y - PAD, 0, ppu.height - 1);
	if(event->type == SDL_MOUSEWHEEL) {
		devmouse->dat[7] = event->wheel.y;
		return;
	}
	poke16(devmouse->dat, 0x2, x);
	poke16(devmouse->dat, 0x4, y);
	devmouse->dat[7] = 0x00;
	switch(event->button.button) {
	case SDL_BUTTON_LEFT: flag = 0x01; break;
	case SDL_BUTTON_RIGHT: flag = 0x10; break;
	}
	switch(event->type) {
	case SDL_MOUSEBUTTONDOWN:
		devmouse->dat[6] |= flag;
		break;
	case SDL_MOUSEBUTTONUP:
		devmouse->dat[6] &= (~flag);
		break;
	}
}

static void
doctrl(SDL_Event *event, int z)
{
	Uint8 flag = 0x00;
	SDL_Keymod mods = SDL_GetModState();
	devctrl->dat[2] &= 0xf8;
	if(mods & KMOD_CTRL) devctrl->dat[2] |= 0x01;
	if(mods & KMOD_ALT) devctrl->dat[2] |= 0x02;
	if(mods & KMOD_SHIFT) devctrl->dat[2] |= 0x04;
	/* clang-format off */
	switch(event->key.keysym.sym) {
	case SDLK_ESCAPE: flag = 0x08; break;
	case SDLK_UP: flag = 0x10; break;
	case SDLK_DOWN: flag = 0x20; break;
	case SDLK_LEFT: flag = 0x40; break;
	case SDLK_RIGHT: flag = 0x80; break;
	case SDLK_F1: if(z) set_zoom(zoom > 2 ? 1 : zoom + 1); break;
	case SDLK_F2: if(z) set_inspect(!devsystem->dat[0xe]); break;
	case SDLK_F3: if(z) capture_screen(); break;
	}
	/* clang-format on */
	if(z) {
		devctrl->dat[2] |= flag;
		if(event->key.keysym.sym < 0x20 || event->key.keysym.sym == SDLK_DELETE)
			devctrl->dat[3] = event->key.keysym.sym;
		else if((mods & KMOD_CTRL) && event->key.keysym.sym >= SDLK_a && event->key.keysym.sym <= SDLK_z)
			devctrl->dat[3] = event->key.keysym.sym - (mods & KMOD_SHIFT) * 0x20;
	} else
		devctrl->dat[2] &= ~flag;
}

#pragma mark - Devices

static int
system_talk(Device *d, Uint8 b0, Uint8 w)
{
	if(!w) { /* read */
		switch(b0) {
		case 0x2: d->dat[0x2] = d->u->wst.ptr; break;
		case 0x3: d->dat[0x3] = d->u->rst.ptr; break;
		}
	} else { /* write */
		switch(b0) {
		case 0x2: d->u->wst.ptr = d->dat[0x2]; break;
		case 0x3: d->u->rst.ptr = d->dat[0x3]; break;
		case 0xf: return 0;
		}
		if(b0 > 0x7 && b0 < 0xe)
			set_palette(&d->dat[0x8]);
	}
	return 1;
}

static int
console_talk(Device *d, Uint8 b0, Uint8 w)
{
	if(w) {
		if(b0 == 0x1)
			d->vector = peek16(d->dat, 0x0);
		if(b0 > 0x7)
			write(b0 - 0x7, (char *)&d->dat[b0], 1);
	}
	return 1;
}

static int
screen_talk(Device *d, Uint8 b0, Uint8 w)
{
	if(!w) switch(b0) {
		case 0x2: d->dat[0x2] = ppu.width >> 8; break;
		case 0x3: d->dat[0x3] = ppu.width; break;
		case 0x4: d->dat[0x4] = ppu.height >> 8; break;
		case 0x5: d->dat[0x5] = ppu.height; break;
		}
	else
		switch(b0) {
		case 0x1: d->vector = peek16(d->dat, 0x0); break;
		case 0x5:
			if(!FIXED_SIZE) return set_size(peek16(d->dat, 0x2), peek16(d->dat, 0x4), 1);
			break;
		case 0xe: {
			Uint16 x = peek16(d->dat, 0x8);
			Uint16 y = peek16(d->dat, 0xa);
			Uint8 layer = d->dat[0xe] & 0x40;
			ppu_write(&ppu, layer, x, y, d->dat[0xe] & 0x3);
			if(d->dat[0x6] & 0x01) poke16(d->dat, 0x8, x + 1); /* auto x+1 */
			if(d->dat[0x6] & 0x02) poke16(d->dat, 0xa, y + 1); /* auto y+1 */
			break;
		}
		case 0xf: {
			Uint16 x = peek16(d->dat, 0x8);
			Uint16 y = peek16(d->dat, 0xa);
			Uint8 layer = d->dat[0xf] & 0x40;
			Uint8 *addr = &d->mem[peek16(d->dat, 0xc)];
			if(d->dat[0xf] & 0x80) {
				ppu_2bpp(&ppu, layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] & 0x10, d->dat[0xf] & 0x20);
				if(d->dat[0x6] & 0x04) poke16(d->dat, 0xc, peek16(d->dat, 0xc) + 16); /* auto addr+16 */
			} else {
				ppu_1bpp(&ppu, layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] & 0x10, d->dat[0xf] & 0x20);
				if(d->dat[0x6] & 0x04) poke16(d->dat, 0xc, peek16(d->dat, 0xc) + 8); /* auto addr+8 */
			}
			if(d->dat[0x6] & 0x01) poke16(d->dat, 0x8, x + 8); /* auto x+8 */
			if(d->dat[0x6] & 0x02) poke16(d->dat, 0xa, y + 8); /* auto y+8 */
			break;
		}
		}
	return 1;
}

static int
file_talk(Device *d, Uint8 b0, Uint8 w)
{
	Uint8 read = b0 == 0xd;
	if(w && (read || b0 == 0xf)) {
		char *name = (char *)&d->mem[peek16(d->dat, 0x8)];
		Uint16 result = 0, length = peek16(d->dat, 0xa);
		long offset = (peek16(d->dat, 0x4) << 16) + peek16(d->dat, 0x6);
		Uint16 addr = peek16(d->dat, b0 - 1);
		FILE *f = fopen(name, read ? "rb" : (offset ? "ab" : "wb"));
		if(f) {
			if(fseek(f, offset, SEEK_SET) != -1)
				result = read ? fread(&d->mem[addr], 1, length, f) : fwrite(&d->mem[addr], 1, length, f);
			fclose(f);
		}
		poke16(d->dat, 0x2, result);
	}
	return 1;
}

static int
audio_talk(Device *d, Uint8 b0, Uint8 w)
{
	Apu *c = &apu[d - devaudio0];
	if(!audio_id) return 1;
	if(!w) {
		if(b0 == 0x2)
			poke16(d->dat, 0x2, c->i);
		else if(b0 == 0x4)
			d->dat[0x4] = apu_get_vu(c);
	} else if(b0 == 0xf) {
		SDL_LockAudioDevice(audio_id);
		c->len = peek16(d->dat, 0xa);
		c->addr = &d->mem[peek16(d->dat, 0xc)];
		c->volume[0] = d->dat[0xe] >> 4;
		c->volume[1] = d->dat[0xe] & 0xf;
		c->repeat = !(d->dat[0xf] & 0x80);
		apu_start(c, peek16(d->dat, 0x8), d->dat[0xf] & 0x7f);
		SDL_UnlockAudioDevice(audio_id);
		SDL_PauseAudioDevice(audio_id, 0);
	}
	return 1;
}

static int
datetime_talk(Device *d, Uint8 b0, Uint8 w)
{
	time_t seconds = time(NULL);
	struct tm *t = localtime(&seconds);
	t->tm_year += 1900;
	poke16(d->dat, 0x0, t->tm_year);
	d->dat[0x2] = t->tm_mon;
	d->dat[0x3] = t->tm_mday;
	d->dat[0x4] = t->tm_hour;
	d->dat[0x5] = t->tm_min;
	d->dat[0x6] = t->tm_sec;
	d->dat[0x7] = t->tm_wday;
	poke16(d->dat, 0x08, t->tm_yday);
	d->dat[0xa] = t->tm_isdst;
	(void)b0;
	(void)w;
	return 1;
}

static int
nil_talk(Device *d, Uint8 b0, Uint8 w)
{
	if(w && b0 == 0x1)
		d->vector = peek16(d->dat, 0x0);
	(void)d;
	(void)b0;
	(void)w;
	return 1;
}

static const char *errors[] = {"underflow", "overflow", "division by zero"};

int
uxn_halt(Uxn *u, Uint8 error, char *name, int id)
{
	fprintf(stderr, "Halted: %s %s#%04x, at 0x%04x\n", name, errors[error - 1], id, u->ram.ptr);
	return 0;
}

static int
run(Uxn *u)
{
	uxn_eval(u, PAGE_PROGRAM);
	redraw(u);
	while(1) {
		SDL_Event event;
		double elapsed, start = 0;
		if(!BENCH)
			start = SDL_GetPerformanceCounter();
		while(SDL_PollEvent(&event) != 0) {
			switch(event.type) {
			case SDL_QUIT:
				return error("Run", "Quit.");
			case SDL_TEXTINPUT:
				devctrl->dat[3] = event.text.text[0]; /* fall-thru */
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				doctrl(&event, event.type == SDL_KEYDOWN);
				uxn_eval(u, devctrl->vector);
				devctrl->dat[3] = 0;
				break;
			case SDL_MOUSEWHEEL:
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEMOTION:
				domouse(&event);
				uxn_eval(u, devmouse->vector);
				break;
			case SDL_WINDOWEVENT:
				if(event.window.event == SDL_WINDOWEVENT_EXPOSED)
					redraw(u);
				break;
			default:
				if(event.type == stdin_event) {
					devconsole->dat[0x2] = event.cbutton.button;
					uxn_eval(u, devconsole->vector);
				} else if(event.type >= audio0_event && event.type < audio0_event + POLYPHONY)
					uxn_eval(u, peek16((devaudio0 + (event.type - audio0_event))->dat, 0));
			}
		}
		uxn_eval(u, devscreen->vector);
		if(reqdraw || ppu.reqdraw || devsystem->dat[0xe])
			redraw(u);
		if(!BENCH) {
			elapsed = (SDL_GetPerformanceCounter() - start) / (double)SDL_GetPerformanceFrequency() * 1000.0f;
			SDL_Delay(clamp(16.666f - elapsed, 0, 1000));
		}
	}
	return error("Run", "Ended.");
}

static int
load(Uxn *u, char *filepath)
{
	FILE *f;
	if(!(f = fopen(filepath, "rb"))) return 0;
	fread(u->ram.dat + PAGE_PROGRAM, sizeof(u->ram.dat) - PAGE_PROGRAM, 1, f);
	fprintf(stderr, "Loaded %s\n", filepath);
	return 1;
}

int
main(int argc, char **argv)
{
	SDL_DisplayMode DM;
	Uxn u;
	int i;

	if(argc < 2)
		return error("usage", "uxnemu file.rom");
	if(!uxn_boot(&u))
		return error("Boot", "Failed to start uxn.");
	if(!load(&u, argv[argc - 1]))
		return error("Load", "Failed to open rom.");

	/* system   */ devsystem = uxn_port(&u, 0x0, system_talk);
	/* console  */ devconsole = uxn_port(&u, 0x1, console_talk);
	/* screen   */ devscreen = uxn_port(&u, 0x2, screen_talk);
	/* audio0   */ devaudio0 = uxn_port(&u, 0x3, audio_talk);
	/* audio1   */ uxn_port(&u, 0x4, audio_talk);
	/* audio2   */ uxn_port(&u, 0x5, audio_talk);
	/* audio3   */ uxn_port(&u, 0x6, audio_talk);
	/* unused   */ uxn_port(&u, 0x7, nil_talk);
	/* control  */ devctrl = uxn_port(&u, 0x8, nil_talk);
	/* mouse    */ devmouse = uxn_port(&u, 0x9, nil_talk);
	/* file     */ uxn_port(&u, 0xa, file_talk);
	/* datetime */ uxn_port(&u, 0xb, datetime_talk);
	/* unused   */ uxn_port(&u, 0xc, nil_talk);
	/* unused   */ uxn_port(&u, 0xd, nil_talk);
	/* unused   */ uxn_port(&u, 0xe, nil_talk);
	/* unused   */ uxn_port(&u, 0xf, nil_talk);

	/* set default zoom */
	SDL_GetCurrentDisplayMode(0, &DM);
	set_zoom(clamp(DM.w / 1280, 1, 3));
	/* get default zoom from flags */
	for(i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], "-s") == 0) {
			if((i + 1) < argc - 1)
				set_zoom(clamp(atoi(argv[++i]), 1, 3));
			else
				return error("Opt", "-s No scale provided.");
		}
	}

	if(!init())
		return error("Init", "Failed to initialize emulator.");
	if(!set_size(WIDTH, HEIGHT, 0))
		return error("Window", "Failed to set window size.");

	run(&u);
	quit();
	return 0;
}
