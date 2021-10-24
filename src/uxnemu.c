#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "uxn.h"
#include "libretro.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
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

static Uxn u;

static retro_input_state_t input_state_cb;
static retro_input_poll_t input_poll_cb;
static retro_video_refresh_t video_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_batch_t audio_cb;

#define WIDTH 64 * 8
#define HEIGHT 40 * 8
#define PAD 0
#define FIXED_SIZE 0
#define POLYPHONY 4

/* devices */
static Ppu ppu;
static Apu apu[POLYPHONY];
static Device *devsystem, *devscreen, *devmouse, *devctrl, *devaudio0, *devconsole;
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
	memset(stream, 0, len);
	for(i = 0; i < POLYPHONY; ++i)
		running += apu_render(&apu[i], samples, samples + len / 2);
	// if(!running)
	// 	SDL_PauseAudioDevice(audio_id, 1);
	(void)u;
}

void
apu_finished_handler(Apu *c)
{
	// SDL_Event event;
	// event.type = audio0_event + (c - apu);
	// SDL_PushEvent(&event);
}

static int
stdin_handler(void *p)
{
	// SDL_Event event;
	// event.type = stdin_event;
	// while(read(0, &event.cbutton.button, 1) > 0)
	// 	SDL_PushEvent(&event);
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
	ppu.reqdraw = 1;
}

// static void
// set_window_size(SDL_Window *window, int w, int h)
// {
// 	SDL_Point win, win_old;
// 	SDL_GetWindowPosition(window, &win.x, &win.y);
// 	SDL_GetWindowSize(window, &win_old.x, &win_old.y);
// 	SDL_SetWindowPosition(window, (win.x + win_old.x / 2) - w / 2, (win.y + win_old.y / 2) - h / 2);
// 	SDL_SetWindowSize(window, w, h);
// }

static int
set_size(Uint16 width, Uint16 height, int is_resize)
{
	ppu_set_size(&ppu, width, height);
	// gRect.x = PAD;
	// gRect.y = PAD;
	// gRect.w = ppu.width;
	// gRect.h = ppu.height;
	if(!(ppu_screen = realloc(ppu_screen, ppu.width * ppu.height * sizeof(Uint32))))
		return error("ppu_screen", "Memory failure");
	memset(ppu_screen, 0, ppu.width * ppu.height * sizeof(Uint32));
	// if(gTexture != NULL) SDL_DestroyTexture(gTexture);
	// SDL_RenderSetLogicalSize(gRenderer, ppu.width + PAD * 2, ppu.height + PAD * 2);
	// gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ppu.width + PAD * 2, ppu.height + PAD * 2);
	// if(gTexture == NULL || SDL_SetTextureBlendMode(gTexture, SDL_BLENDMODE_NONE))
	// 	return error("sdl_texture", SDL_GetError());
	// SDL_UpdateTexture(gTexture, NULL, ppu_screen, sizeof(Uint32));
	// if(is_resize)
	// 	set_window_size(gWindow, (ppu.width + PAD * 2) * zoom, (ppu.height + PAD * 2) * zoom);
	ppu.reqdraw = 1;
	return 1;
}

static void
draw_inspect(Ppu *p, Uint8 *stack, Uint8 wptr, Uint8 rptr, Uint8 *memory)
{
	Uint8 i, x, y, b;
	for(i = 0; i < 0x20; ++i) {
		x = ((i % 8) * 3 + 1) * 8, y = (i / 8 + 1) * 8, b = stack[i];
		/* working stack */
		ppu_1bpp(p, 1, x, y, font[(b >> 4) & 0xf], 1 + (wptr == i) * 0x7, 0, 0);
		ppu_1bpp(p, 1, x + 8, y, font[b & 0xf], 1 + (wptr == i) * 0x7, 0, 0);
		y = 0x28 + (i / 8 + 1) * 8;
		b = memory[i];
		/* return stack */
		ppu_1bpp(p, 1, x, y, font[(b >> 4) & 0xf], 3, 0, 0);
		ppu_1bpp(p, 1, x + 8, y, font[b & 0xf], 3, 0, 0);
	}
	/* return pointer */
	ppu_1bpp(p, 1, 0x8, y + 0x10, font[(rptr >> 4) & 0xf], 0x2, 0, 0);
	ppu_1bpp(p, 1, 0x10, y + 0x10, font[rptr & 0xf], 0x2, 0, 0);
	/* guides */
	for(x = 0; x < 0x10; ++x) {
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
	Uint16 x, y;
	if(devsystem->dat[0xe])
		draw_inspect(&ppu, u->wst.dat, u->wst.ptr, u->rst.ptr, u->ram.dat);
	for(y = 0; y < ppu.height; ++y)
		for(x = 0; x < ppu.width; ++x)
			ppu_screen[x + y * ppu.width] = palette[ppu_read(&ppu, x, y)];
	video_cb(ppu_screen, ppu.width, ppu.height, ppu.width * sizeof(Uint32));
	ppu.reqdraw = 0;
}

static Sint16 mouse_x = 0;
static Sint16 mouse_y = 0;
static Sint16 mouse_left = 0;
static Sint16 mouse_right = 0;

static void
domouse()
{
	Sint16 motion_x = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
	Sint16 motion_y = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
	Sint16 left = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
	Sint16 right = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);

	mouse_x += motion_x;
	mouse_y += motion_y;

	Uint8 flag = 0x00;
	mouse_x = clamp(mouse_x, 0, ppu.width - 1);
	mouse_y = clamp(mouse_y, 0, ppu.height - 1);
	// if(event->type == SDL_MOUSEWHEEL) {
	// 	devmouse->dat[7] = event->wheel.y;
	// 	return;
	// }
	poke16(devmouse->dat, 0x2, mouse_x);
	poke16(devmouse->dat, 0x4, mouse_y);
	devmouse->dat[7] = 0x00;

	if (left && !mouse_left)
		devmouse->dat[6] |= 0x01;
	else if (!left && mouse_left)
		devmouse->dat[6] &= (~0x01);
	if (right && !mouse_right)
		devmouse->dat[6] |= 0x10;
	else if (!right && mouse_right)
		devmouse->dat[6] &= (~0x10);

	mouse_left = left;
	mouse_right = right;
}

// static void
// doctrl(SDL_Event *event, int z)
// {
// 	Uint8 flag = 0x00;
// 	SDL_Keymod mods = SDL_GetModState();
// 	devctrl->dat[2] &= 0xf8;
// 	if(mods & KMOD_CTRL) devctrl->dat[2] |= 0x01;
// 	if(mods & KMOD_ALT) devctrl->dat[2] |= 0x02;
// 	if(mods & KMOD_SHIFT) devctrl->dat[2] |= 0x04;
// 	/* clang-format off */
// 	switch(event->key.keysym.sym) {
// 	case SDLK_ESCAPE: flag = 0x08; break;
// 	case SDLK_UP: flag = 0x10; break;
// 	case SDLK_DOWN: flag = 0x20; break;
// 	case SDLK_LEFT: flag = 0x40; break;
// 	case SDLK_RIGHT: flag = 0x80; break;
// 	case SDLK_F1: if(z) set_zoom(zoom > 2 ? 1 : zoom + 1); break;
// 	case SDLK_F2: if(z) devsystem->dat[0xe] = !devsystem->dat[0xe]; break;
// 	case SDLK_F3: if(z) capture_screen(); break;
// 	}
// 	/* clang-format on */
// 	if(z) {
// 		devctrl->dat[2] |= flag;
// 		if(event->key.keysym.sym < 0x20 || event->key.keysym.sym == SDLK_DELETE)
// 			devctrl->dat[3] = event->key.keysym.sym;
// 		else if((mods & KMOD_CTRL) && event->key.keysym.sym >= SDLK_a && event->key.keysym.sym <= SDLK_z)
// 			devctrl->dat[3] = event->key.keysym.sym - (mods & KMOD_SHIFT) * 0x20;
// 	} else
// 		devctrl->dat[2] &= ~flag;
// }

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
			ppu_write(&ppu, !!layer, x, y, d->dat[0xe] & 0x3);
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
				ppu_2bpp(&ppu, !!layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] & 0x10, d->dat[0xf] & 0x20);
				if(d->dat[0x6] & 0x04) poke16(d->dat, 0xc, peek16(d->dat, 0xc) + 16); /* auto addr+16 */
			} else {
				ppu_1bpp(&ppu, !!layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] & 0x10, d->dat[0xf] & 0x20);
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
	// if(!audio_id) return 1;
	if(!w) {
		if(b0 == 0x2)
			poke16(d->dat, 0x2, c->i);
		else if(b0 == 0x4)
			d->dat[0x4] = apu_get_vu(c);
	} else if(b0 == 0xf) {
		// SDL_LockAudioDevice(audio_id);
		c->len = peek16(d->dat, 0xa);
		c->addr = &d->mem[peek16(d->dat, 0xc)];
		c->volume[0] = d->dat[0xe] >> 4;
		c->volume[1] = d->dat[0xe] & 0xf;
		c->repeat = !(d->dat[0xf] & 0x80);
		apu_start(c, peek16(d->dat, 0x8), d->dat[0xf] & 0x7f);
		// SDL_UnlockAudioDevice(audio_id);
		// SDL_PauseAudioDevice(audio_id, 0);
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
load(Uxn *u, const char *filepath)
{
	FILE *f;
	if(!(f = fopen(filepath, "rb"))) return 0;
	fread(u->ram.dat + PAGE_PROGRAM, sizeof(u->ram.dat) - PAGE_PROGRAM, 1, f);
	fprintf(stderr, "Loaded %s\n", filepath);
	return 1;
}

void
retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = "uxn";
	info->library_version = "1.0";
	info->need_fullpath = true;
	info->valid_extensions = "rom";
}

void
retro_get_system_av_info(struct retro_system_av_info *info)
{
	info->timing.fps = 60.0;
	info->timing.sample_rate = SAMPLE_FREQUENCY;

	info->geometry.base_width = WIDTH;
	info->geometry.base_height = HEIGHT;
	info->geometry.max_width = WIDTH;
	info->geometry.max_height = HEIGHT;
	info->geometry.aspect_ratio = 1.6;
}

unsigned
retro_api_version(void)
{
	return RETRO_API_VERSION;
}

void
retro_init()
{
	uxn_boot(&u);
}

bool
retro_load_game(const struct retro_game_info *game)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;

	load(&u, game->path);

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

	set_size(WIDTH, HEIGHT, 0);

	uxn_eval(&u, PAGE_PROGRAM);
	// redraw(&u);

	return true;
}

void
retro_run(void)
{
	// SDL_Event event;
	// while(SDL_PollEvent(&event) != 0) {
	// 	switch(event.type) {
	// 	case SDL_QUIT:
	// 		return error("Run", "Quit.");
	// 	case SDL_TEXTINPUT:
	// 		devctrl->dat[3] = event.text.text[0]; /* fall-thru */
	// 	case SDL_KEYDOWN:
	// 	case SDL_KEYUP:
	// 		doctrl(&event, event.type == SDL_KEYDOWN);
	// 		uxn_eval(u, devctrl->vector);
	// 		devctrl->dat[3] = 0;
	// 		break;
	// 	case SDL_WINDOWEVENT:
	// 		if(event.window.event == SDL_WINDOWEVENT_EXPOSED)
	// 			redraw(u);
	// 		break;
	// 	default:
	// 		if(event.type == stdin_event) {
	// 			devconsole->dat[0x2] = event.cbutton.button;
	// 			uxn_eval(u, devconsole->vector);
	// 		} else if(event.type >= audio0_event && event.type < audio0_event + POLYPHONY)
	// 			uxn_eval(u, peek16((devaudio0 + (event.type - audio0_event))->dat, 0));
	// 	}
	// }

	input_poll_cb();
	domouse();
	uxn_eval(&u, devmouse->vector);

	uxn_eval(&u, devscreen->vector);
	if(ppu.reqdraw || devsystem->dat[0xe])
		redraw(&u);
}

void
retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll_cb = cb;
}

void
retro_set_input_state(retro_input_state_t cb)
{
	input_state_cb = cb;
}

void
retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

void
retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;
}

void
retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	audio_cb = cb;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {}
size_t retro_get_memory_size(unsigned id) { return 0; }
void * retro_get_memory_data(unsigned id) { return NULL; }
void retro_reset(void) {}
void retro_unload_game(void) {}
void retro_deinit(void) {}
void retro_set_audio_sample(retro_audio_sample_t cb) {}
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { return false; }
bool retro_unserialize(const void *data, size_t size) { return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }
unsigned retro_get_region(void) { return 0; }

