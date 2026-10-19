/* UCN 360 — milestone 0 smoke title.
 *
 * Proves the chain on the console: powerpc-xsdk-elf -> libxsdk.a -> elf2xex.py -> an XEX2 the
 * retail kernel/Aurora launches -> a visible animated 720p pattern and a log at
 * game:\ucn360_m0.log.
 *
 * Modeled on xsdk's tests/test_present.c (display open/show) and tests/probe_display.c (a
 * CPU-filled tiled surface). No shaders and no GPU draws: the pattern is written by the CPU, so
 * every pixel check also runs where there is no display, as in Xenia.
 */
#include <stdbool.h>
#include <stdint.h>

#include "xsdk/display.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "xsdk/thread.h"

enum {
	WIDTH = 1280,
	HEIGHT = 720,
	BUFFERS = 2,
	FRAMES = 600, /* about 10 s at 16 ms a frame */
	FRAME_MS = 16,
	PRESENT_TIMEOUT_MS = 1000,
	LOG_EVERY = 120,
	BAR_WIDTH = 6,
	STRIPE_WIDTH = 128,
};

/* libxenon:drivers/console/console.c:60-61, BSD 3-clause, Copyright 2009 Georg Lukas. */
static uint32_t tiled_index(uint32_t x, uint32_t y, uint32_t width)
{
	return ((y >> 5) * 32 * width + ((x >> 5) << 10) + (x & 3) + ((y & 1) << 2) + (((x & 31) >> 2) << 3) +
			   (((y & 31) >> 1) << 6)) ^
		((y & 8) << 2);
}

/* A pixel's bytes in memory are B, G, R, A (tests/probe_display.c:29-33 and tests/test_present.c:31-35),
 * so a CPU store writes 0xBBGGRRAA. */
enum {
	STRIPE_A = 0xFFFFFFFF, /* white: B=FF G=FF R=FF A=FF */
	STRIPE_B = 0xA0A0A0FF, /* light gray */
	BAR = 0x0000FFFF,      /* red */
	DIAGONAL = 0x00FF00FF, /* green */
};

/* One pixel of the static pattern: a diagonal over stripes. */
static uint32_t pattern_pixel(uint32_t x, uint32_t y)
{
	if (x == y || x == y + 1)
		return DIAGONAL;
	return (x / STRIPE_WIDTH) % 2 ? STRIPE_B : STRIPE_A;
}

static void fill_pattern(volatile uint32_t *pixels)
{
	for (uint32_t y = 0; y < HEIGHT; y++)
		for (uint32_t x = 0; x < WIDTH; x++)
			pixels[tiled_index(x, y, WIDTH)] = pattern_pixel(x, y);
	__asm__ volatile("sync" ::: "memory");
}

/* Paints the bar's column run at x. */
static void bar_span(volatile uint32_t *pixels, uint32_t x, uint32_t pixel)
{
	for (uint32_t dx = 0; dx < BAR_WIDTH && x + dx < WIDTH; dx++)
		for (uint32_t y = 0; y < HEIGHT; y++)
			pixels[tiled_index(x + dx, y, WIDTH)] = pixel;
}

/* Repaints the bar's column run at x with the pattern it covered. */
static void bar_restore(volatile uint32_t *pixels, uint32_t x)
{
	for (uint32_t dx = 0; dx < BAR_WIDTH && x + dx < WIDTH; dx++)
		for (uint32_t y = 0; y < HEIGHT; y++)
			pixels[tiled_index(x + dx, y, WIDTH)] = pattern_pixel(x + dx, y);
}

/* Moves the bar from where it was to x, repainting what it covered. */
static void move_bar(volatile uint32_t *pixels, uint32_t previous_x, uint32_t x)
{
	bar_span(pixels, x, BAR);
	if (previous_x != x)
		bar_restore(pixels, previous_x);
	__asm__ volatile("sync" ::: "memory");
}

static uint32_t pixel_at(volatile const uint32_t *pixels, uint32_t x, uint32_t y)
{
	return pixels[tiled_index(x, y, WIDTH)];
}

int main(void)
{
	xsdk_log_write("ucn360 m0 smoke: %ux%u, %u frames", (unsigned)WIDTH, (unsigned)HEIGHT, (unsigned)FRAMES);
	if (!NT_SUCCESS(xsdk_gpu_open())) {
		xsdk_log_write("ucn360 m0: FAIL, gpu open");
		return 1;
	}

	/* The title's own present path, as D3D titles use it: the vblank callback flips the display for the console and
	 * every frame goes through the kernel's VdSwap, which is also the only way the frames show in Xenia. */
	uint32_t width = 0;
	uint32_t height = 0;
	const bool has_display = NT_SUCCESS(xsdk_present_open(XSDK_DISPLAY_720P, BUFFERS, 1, &width, &height));
	xsdk_log_write("ucn360 m0: present %s, %ux%u", has_display ? "open" : "absent", (unsigned)width, (unsigned)height);

	/* Without a display the pattern and its checks run in two buffers of the title's own, as a headless build does. */
	const uint32_t buffer_bytes = xsdk_gpu_resolve_bytes(WIDTH, HEIGHT);
	volatile uint32_t *const buffers[BUFFERS] = {
		has_display ? xsdk_present_pixels(xsdk_present_buffer_address(0)) : xsdk_gpu_alloc(buffer_bytes),
		has_display ? xsdk_present_pixels(xsdk_present_buffer_address(1)) : xsdk_gpu_alloc(buffer_bytes),
	};
	if (!buffers[0] || !buffers[1]) {
		xsdk_log_write("ucn360 m0: FAIL, buffers");
		return 1;
	}

	for (uint32_t i = 0; i < BUFFERS; i++)
		fill_pattern(buffers[i]);

	/* (0, 10) and (128, 10) sit clear of the diagonal, which covers x == y and x == y + 1. */
	const bool pattern_ok = pixel_at(buffers[0], 0, 10) == STRIPE_A && pixel_at(buffers[0], STRIPE_WIDTH, 10) == STRIPE_B &&
		pixel_at(buffers[0], 50, 50) == DIAGONAL && pixel_at(buffers[1], 0, 10) == STRIPE_A;
	xsdk_log_write("%s: pattern written into both buffers", pattern_ok ? "PASS" : "FAIL");

	uint32_t bar_x[BUFFERS] = { 0, 0 };
	bool waited_logged = false;
	for (uint32_t frame = 0; frame < FRAMES; frame++) {
		uint32_t address = 0;
		const bool shown = has_display && NT_SUCCESS(xsdk_present_begin(&address, PRESENT_TIMEOUT_MS));
		if (has_display && !shown && !waited_logged) {
			waited_logged = true;
			xsdk_log_write("ucn360 m0: present begin timed out; showing every frame as it comes");
		}
		const uint32_t index = frame % BUFFERS;
		volatile uint32_t *const pixels = shown ? xsdk_present_pixels(address) : buffers[index];
		const uint32_t next_x = (frame * 8) % WIDTH;
		move_bar(pixels, bar_x[index], next_x);
		bar_x[index] = next_x;
		if (shown)
			xsdk_present_end();
		if (frame % LOG_EVERY == 0)
			xsdk_log_write("ucn360 m0: frame %u", (unsigned)frame);
		(void)xsdk_thread_sleep(FRAME_MS);
	}

	XsdkGpuStats stats;
	xsdk_gpu_stats(&stats);
	xsdk_log_write("ucn360 m0: done, %u frames, kicks %u, system draws %u", (unsigned)FRAMES, (unsigned)stats.kicks,
		(unsigned)stats.system_draws);
	if (has_display) {
		XsdkPresentStats present_stats;
		xsdk_present_stats(&present_stats);
		xsdk_log_write("ucn360 m0: showed %u frames in %u vblanks, %u missed, %u system", (unsigned)present_stats.flips,
			(unsigned)present_stats.vblanks, (unsigned)present_stats.missed, (unsigned)present_stats.system_frames);
		xsdk_present_close();
	} else {
		for (uint32_t i = 0; i < BUFFERS; i++)
			xsdk_gpu_free(buffers[i]);
	}
	(void)xsdk_gpu_close();
	return 0;
}
