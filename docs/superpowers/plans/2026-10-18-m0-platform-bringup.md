# Plan 1 — M0: Platform bring-up and the smoke XEX

- **Date:** 2026-10-18
- **Spec:** `docs/superpowers/specs/2026-10-18-ucn-xbox360-port-design.md`
- **Goal (M0):** a working `powerpc-xsdk-elf` toolchain, a built `libxsdk.a`, and two XEX files the user can launch from Aurora on the real console:
  1. `xsdk_test.xex` — xsdk's own full platform test title (writes `game:\xsdk_test.log`),
  2. `ucn360_m0.xex` — our smoke title: a recognizable animated 720p pattern, GPU splash, `game:\ucn360_m0.log` with PASS/FAIL lines.
- **Why first:** this validates the whole chain (toolchain → libxsdk → ELF → XEX2 → retail kernel/Aurora → display) on the user's FreeMyXe console before any engine work. Fallback (libxenon/XeLL) is only worth building if this fails.
- **Host constraints found:** no passwordless sudo; `patch` and `texinfo` are missing; `gmp`/`mpfr`/`mpc` dev headers are missing. Everything below avoids root: `patch` is built into `~/.local`, GCC's prerequisites are vendored into its source tree, and `MAKEINFO=true` skips documentation builds.
- **Numbers:** toolchain ≈ 45–75 min on 4 jobs; xsdk build ≈ 3–5 min; disk ≈ 10 GB under `$HOME`.

## Conventions

- Work from `/home/mohamd/Projects/UCN_MVP_Xbox360` unless a step says otherwise.
- Commit after each code task (`git add` + `git commit`).
- Long builds run in the background; verify their log tail, not the console.

---

## Task 0 — Land this plan

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360
git add docs/superpowers/plans/2026-10-18-m0-platform-bringup.md
git commit -m "Add M0 platform bring-up plan"
```

---

## Task 1 — Host build prerequisite: GNU patch (no root)

`third_party/xsdk/tools/toolchain/build.sh` calls `patch` directly; the host has none.

```bash
set -e
mkdir -p "$HOME/.local/src" && cd "$HOME/.local/src"
curl -sSfLO https://ftp.gnu.org/gnu/patch/patch-2.8.tar.xz
tar xf patch-2.8.tar.xz
cd patch-2.8
./configure --prefix="$HOME/.local" >/dev/null
make -j4 >/dev/null
make install >/dev/null
"$HOME/.local/bin/patch" --version | head -1
```

**Verify:** prints `GNU patch 2.8`. (`m4`, `flex`, `bison`, `zip`, `perl`, `xvfb-run`, `sha256sum` were already confirmed present; `texinfo` stays absent and is worked around with `MAKEINFO=true`.)

**If this fails:** fall back to the `git apply` shim (a `patch` script in `~/.local/bin` that runs `git -C "$DIR" apply -pN -`), and note it here.

---

## Task 2 — Clone xsdk, pinned

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360
mkdir -p third_party
[ -d third_party/xsdk ] || git clone --depth 1 https://github.com/xenosworks/xsdk third_party/xsdk
git -C third_party/xsdk rev-parse HEAD | tee third_party/XSDK_COMMIT
git -C third_party/xsdk log -1 --format='%H %ad %s' --date=short
```

**Verify:** `third_party/XSDK_COMMIT` exists and `third_party/xsdk/tools/elf2xex.py` is present. All API details this plan relies on (`gpu.h`, `present.h`, `display.h`, `splash.h`, `elf2xex.py`, `src/xsdk.ld`, `tools/toolchain/build.sh`) were read from this repository at the `main` head of 2026-10-18.

---

## Task 3 — Build the cross toolchain (long; two phases)

### Phase A — first attempt (expected to fail fast at gcc-stage1 configure)

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360/third_party/xsdk
export PATH="$HOME/.local/bin:$PATH"
MAKEINFO=true JOBS=4 bash tools/toolchain/build.sh > /tmp/opencode/ucn-toolchain-build.log 2>&1
```

Expected: binutils builds (~4 min), then `gcc-stage1` configure stops with `Building GCC requires GMP 4.2+, MPFR 2.4.0+ and MPC 0.8.0+`. That is the trigger for Phase B; the failure is expected and the work up to it is kept (`~/xsdk-gcc-work`).

### Phase B — vendor GCC's prerequisites, then re-run

build.sh's `unpack()` only re-extracts sources when the local patch hash changes, so directories added inside the extracted GCC tree survive the re-run.

```bash
set -e
cd "$HOME/xsdk-gcc-work/src"
GCCDIR=gcc-15.3.0
cd "$GCCDIR"
curl -sSfLO https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz
curl -sSfLO https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.1.tar.xz
curl -sSfLO https://ftp.gnu.org/gnu/mpc/mpc-1.3.1.tar.gz
mkdir -p gmp mpfr mpc
tar xf gmp-6.3.0.tar.xz  -C gmp  --strip-components=1
tar xf mpfr-4.2.1.tar.xz -C mpfr --strip-components=1
tar xf mpc-1.3.1.tar.gz  -C mpc  --strip-components=1
ls gmp/gmp.h mpfr/mpfr.h mpc/mpc.h
cd /home/mohamd/Projects/UCN_MVP_Xbox360/third_party/xsdk
export PATH="$HOME/.local/bin:$PATH"
MAKEINFO=true JOBS=4 bash tools/toolchain/build.sh >> /tmp/opencode/ucn-toolchain-build.log 2>&1
```

Runs in the background (~45–70 min: gcc-stage1, newlib, then full C/C++ GCC). The script ends with two self-checks (linker refuses mixed calling conventions; `-mcheck-fpr-alignment` compile check) and prints `done: $PREFIX/bin/powerpc-xsdk-elf-gcc`.

**Verify:**

```bash
"$HOME/xsdk-gcc/bin/powerpc-xsdk-elf-gcc" --version | head -1   # expect: ... 15.3.0
tail -n 6 /tmp/opencode/ucn-toolchain-build.log                 # expect: no error; "done:" line if it completed
```

**If gcc/newlib documentation steps still fail:** add `MAKEINFO=true` is already set; then build texinfo into `~/.local` from `https://ftp.gnu.org/gnu/texinfo/texinfo-7.2.tar.xz` with `MAKEINFO=true ./configure --prefix=$HOME/.local` and re-run the failing phase. Do not change anything else in build.sh.

---

## Task 4 — Build xsdk

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360/third_party/xsdk
make -j4 2>&1 | tail -n 5
ls -la build/libxsdk.a build/xsdk_test.xex build/host/libxsc.so
```

The Makefile defaults to `TOOLCHAIN=$(HOME)/xsdk-gcc/bin/powerpc-xsdk-elf-`, runs `tools/gen_imports.py`, builds all tests and probes and packs each with `tools/elf2xex.py`.

**Verify:** the three artifacts exist (`.xex` non-trivial size, e.g. > 300 KB). `make` also runs `tools/xenia_scan.py` on the test ELF as a sanity scanner; a clean build suffices.

---

## Task 5 — The UCN 360 smoke title

### 5.1 `platform/xenon/m0_smoke/m0_smoke.c`

Full source:

```c
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
#include "xsdk/splash.h"
#include "xsdk/thread.h"

enum {
	WIDTH = 1280,
	HEIGHT = 720,
	BUFFERS = 2,
	FRAMES = 600,      /* about 10 s at 16 ms a frame */
	FRAME_MS = 16,
	SPLASH_MS = 1500,
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
	xsdk_log_write("ucn360 m0: splash %s", NT_SUCCESS(xsdk_splash_show(SPLASH_MS)) ? "shown" : "absent");

	if (!NT_SUCCESS(xsdk_gpu_open())) {
		xsdk_log_write("ucn360 m0: FAIL, gpu open");
		return 1;
	}

	uint32_t width = 0;
	uint32_t height = 0;
	const bool has_display = NT_SUCCESS(xsdk_display_open(XSDK_DISPLAY_720P, &width, &height)) && width == WIDTH &&
		height == HEIGHT;
	xsdk_log_write("ucn360 m0: display %s", has_display ? "1280x720" : "absent (as in Xenia)");

	const uint32_t buffer_bytes = xsdk_gpu_resolve_bytes(WIDTH, HEIGHT);
	volatile uint32_t *const buffers[BUFFERS] = { xsdk_gpu_alloc(buffer_bytes), xsdk_gpu_alloc(buffer_bytes) };
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

	uint32_t bar_x = 0;
	for (uint32_t frame = 0; frame < FRAMES; frame++) {
		volatile uint32_t *const pixels = buffers[frame % BUFFERS];
		const uint32_t next_x = (frame * 8) % WIDTH;
		move_bar(pixels, bar_x, next_x);
		bar_x = next_x;
		if (has_display)
			(void)xsdk_display_show(xsdk_gpu_address(pixels), WIDTH);
		if (frame % LOG_EVERY == 0)
			xsdk_log_write("ucn360 m0: frame %u", (unsigned)frame);
		(void)xsdk_thread_sleep(FRAME_MS);
	}

	XsdkGpuStats stats;
	xsdk_gpu_stats(&stats);
	xsdk_log_write("ucn360 m0: done, %u frames, %u swaps, %u system draws", (unsigned)FRAMES, (unsigned)stats.swaps,
		(unsigned)stats.system_draws);

	xsdk_display_close();
	for (uint32_t i = 0; i < BUFFERS; i++)
		xsdk_gpu_free(buffers[i]);
	(void)xsdk_gpu_close();
	return 0;
}
```

### 5.2 `platform/xenon/m0_smoke/Makefile`

```make
# UCN 360 — milestone 0 smoke title. Builds against a checkout of xsdk:
#   make XSDK=../../../third_party/xsdk
XSDK ?= ../../../third_party/xsdk
TOOLCHAIN ?= $(HOME)/xsdk-gcc/bin/powerpc-xsdk-elf-
CC := $(TOOLCHAIN)gcc
PYTHON ?= python3

BUILD := build
CFLAGS := -mcpu=cell -fno-pic -std=gnu17 -O2 -g -Wall -Wextra -I$(XSDK)/include
LDFLAGS := -static -nostdlib -T $(XSDK)/src/xsdk.ld -Wl,-Map,$(BUILD)/ucn360_m0.map
LIBS := -Wl,--start-group $(XSDK)/build/libxsdk.a -lstdc++ -lc -lm -lgcc -Wl,--end-group

all: $(BUILD)/ucn360_m0.xex

$(BUILD)/ucn360_m0.elf: m0_smoke.c $(XSDK)/build/libxsdk.a $(XSDK)/src/xsdk.ld
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ m0_smoke.c $(LIBS)

$(BUILD)/ucn360_m0.xex: $(BUILD)/ucn360_m0.elf $(XSDK)/tools/elf2xex.py
	$(PYTHON) $(XSDK)/tools/elf2xex.py $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
```

### 5.3 Build and inspect

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360/platform/xenon/m0_smoke
make XSDK="$PWD/../../../third_party/xsdk" 2>&1 | tail -n 5
ls -la build/ucn360_m0.elf build/ucn360_m0.xex
"$HOME/xsdk-gcc/bin/powerpc-xsdk-elf-objdump" -d build/ucn360_m0.elf | head -n 12
```

**Verify:** `.xex` exists and is > 300 KB; objdump shows `.rdata` at `0x82001000`, `.text` at `0x82030000`, and the entry (`start address 0x820309b8`) inside `.text` — the linker script's `ENTRY(xsdk_start)`.

**Commit:** `git add platform/xenon/m0_smoke && git commit -m "Add M0 smoke title (CPU pattern + display, xsdk)"`.

---

## Task 6 — Xenia smoke run on the host

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360/platform/xenon/m0_smoke
timeout 120 xvfb-run -a /home/mohamd/Downloads/xenia_edge_linux.AppImage build/ucn360_m0.xex > /tmp/opencode/xenia-m0.out 2>&1; echo "exit=$?"
ls -la build/*.log
grep -n "ucn360 m0" build/ucn360_m0.log | head -n 30
```

Expect (Xenia has no 720p display path, so the title logs `display absent` and still runs its pixel checks):
`PASS: pattern written into both buffers`, `ucn360 m0: frame 0/120/240/...`, `ucn360 m0: done, 600 frames`.

**Verify:** the log exists and holds the lines above. **Recorded (2026-10-18):** when Xenia (the Edge AppImage) is launched this way it maps `game:\` to its own storage, so the log lands at `/home/mohamd/.local/share/Xenia/cache/ucn360_m0.log`, not next to the XEX. Actual run: `PASS: pattern written into both buffers`, `frame 0/120/240/360/480`, `done, 600 frames, kicks 1, system draws 0`; `display absent (as in Xenia)` is expected — Xenia has no 720p display path. The Xenia process itself stays open (the harness killed it with a timeout, exit 124); the title had already finished.

**If Xenia can't start at all:** note the error from `/tmp/opencode/xenia-m0.out`, try without `xvfb-run` (a real DISPLAY exists), and keep the console test as the decisive one.

---

## Task 7 — Package `dist/ucn360-m0/` and hand it to the user

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360
mkdir -p dist/ucn360-m0
cp platform/xenon/m0_smoke/build/ucn360_m0.xex dist/ucn360-m0/
cp third_party/xsdk/build/xsdk_test.xex dist/ucn360-m0/
python3 - <<'EOF'
import zipfile, os
root = "dist/ucn360-m0"
with zipfile.ZipFile("dist/ucn360-m0.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for name in sorted(os.listdir(root)):
        z.write(os.path.join(root, name), name)
print("wrote dist/ucn360-m0.zip", os.path.getsize("dist/ucn360-m0.zip"))
EOF
```

### 7.1 `dist/ucn360-m0/README.txt` (exact text, Arabic, RTL-friendly plain text)

```
اختبار UCN 360 — المرحلة M0
============================

المطلوب:
1) انسخ هذا المجلد كاملًا إلى فلاشة USB مهيأة FAT32.
2) من Aurora: شغّل الملف ucn360_m0.xex
   - تستمر الشاشة حوالي 10 ثوانٍ: شعار البداية، ثم شرائط بيضاء/رمادية
     مع شريط ملوّن يتحرك من اليسار إلى اليمين.
   - يتكوّن بجانب الملف سجل اسمه: ucn360_m0.log
3) شغّل أيضًا الملف xsdk_test.xex
   - يعمل دقيقة تقريبًا ثم ينتهي، ويكتب سجلًا اسمه: xsdk_test.log

أرسل لنا:
- صورة للشاشة إن ظهر أي شيء غير الشرائط.
- ملفي السجل (*.log) إن وُجدا.

ملاحظات:
- إن لم يقلع أي ملف من Aurora، أخبرنا ماذا حدث بالضبط (شاشة سوداء؟ رسالة خطأ؟).
```

### 7.2 Commit + handoff

```bash
git add dist/ucn360-m0/README.txt .gitignore
git commit -m "Package M0 smoke drop for console testing"
```

Ask the user to run both titles and send back the two `.log` files (and a photo of the screen). Then M0 is accepted and Plan 2 (host engine, M1) starts.

---

## Acceptance criteria (M0)

1. `powerpc-xsdk-elf-gcc 15.3.0` installed at `~/xsdk-gcc`; build.sh's own two self-checks passed.
2. `third_party/xsdk/build/libxsdk.a`, `xsdk_test.xex`, `host/libxsc.so` exist.
3. `platform/xenon/m0_smoke/build/ucn360_m0.xex` builds reproducibly with `make` and shows the expected objdump layout.
4. Xenia run writes `ucn360_m0.log` containing `PASS: pattern written into both buffers` and `done, 600 frames`.
5. On the user's console: `ucn360_m0.xex` from Aurora shows the animated pattern and writes its log; `xsdk_test.xex` runs the test suite and writes its log.

## Risks noted while planning

- **FreeMyXe vs xsdk** is the one unknown the host cannot settle; that is why M0 exists. Fallback plan: libxenon/XeLL path (same engine later).
- **Doc builds without texinfo** are handled by `MAKEINFO=true`; the escape hatch is a local texinfo install (Task 3 note).
- **`wget2` vs vendor script**: prerequisites are fetched by hand with pinned URLs (Task 3 Phase B) rather than via `contrib/download_prerequisites`, avoiding `wget` flag differences.
- **Channel order of the CPU-filled surface** is deliberately not asserted; the photo settles it, and the real pipeline (resolve-based) follows `tests/test_present.c`'s proven convention.
