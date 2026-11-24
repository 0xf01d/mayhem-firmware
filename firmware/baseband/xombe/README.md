# xombe — vendored DMR AMBE+2 vocoder

Vendored subset of [xombe](https://github.com/x0rro/xombe) (upstream commit
`2f0a8e744645b51c1749a39c8a4ee581fef1f20a`, "xombe 0.1.0", MIT license).
xombe is a clean-room fixed-point (C99, no malloc) AMBE+2 codec with
DMR-compatible 49-bit/72-bit framing. It is used here to decode and encode the
dPMR446 voice channel (four 72-bit AMBE+2 3600/2450 frames per 80 ms TCH,
TS 102 490 §9.1).

Only what the PMR446 app needs is kept, and it is rebuilt per baseband image
with this configuration (`xombe_config.h`):

- `XOMBE_HAVE_ENCODER 1` — mic → AMBE+2 TX voice.
- `XOMBE_KERNELS_Q15 1` — Cortex-M4 DSP-extension kernels (smlad/smuad/qadd16/...).
- `XOMBE_HAVE_DSTAR 0` — D-STAR codec and framing dropped.
- `XOMBE_HAVE_CQUANT 0` — closed-loop spectral quantizer dropped; TX uses the
  open-loop quantizer (`cfg.dmr_quant = XOMBE_ENC_QUANT_OPEN`, which the
  encoder must be configured with explicitly — the default is CLOSED).
- `XOMBE_HAVE_TONES 0` — DMR tone/CSBK signalling dropped (dPMR446 has its own
  tone signalling in `common/dpmr.hpp`).

Kept directories: `src/common`, `src/dec`, `src/enc`, `src/dsp`
(minus fft), `src/fec` (golay/pn/interleave/stats — the DMR 49→72 mapping is
required for over-the-air interop), `src/tables` (minus D-STAR/cquant tables),
`src/internal.h`, `src/state.h`, `include/xombe.h`, `LICENSE`.

## PortaPack vendor patches

1. `src/dsp/acle_compat.h` (new): the GCC 9.2.1 arm-none-eabi toolchain's
   `arm_acle.h` declares none of the DSP-extension intrinsics (and no
   `int16x2_t`). This header supplies the subset `q15ops.h` uses as inline
   assembly with ACLE-compatible names, so the q15 kernels emit the real
   Cortex-M4 instructions. `src/dsp/q15ops.h` selects it for GCC < 10 and
   `<arm_acle.h>` otherwise.
2. `src/enc/frontend.c`: tone detection block wrapped in
   `#if XOMBE_HAVE_TONES`.
3. `src/enc/ambe2_enc.c`: closed-loop quantizer workspace and calls wrapped in
   `#if XOMBE_HAVE_CQUANT` / `#elif` guards (D-STAR include moved under
   `XOMBE_HAVE_DSTAR`).
4. `src/enc/encoder.c`: D-STAR includes, tables, and output fallbacks guarded
   with `#if XOMBE_HAVE_DSTAR`; without it the D-STAR output pointer is
   ignored/`(void)`-cast and filled with the silence frame.
5. `src/state.h`: the closed-loop quantizer workspace union in
   `struct xombe_enc` is guarded `#if !XOMBE_HAVE_DSTAR && XOMBE_HAVE_CQUANT`
   (drops ~19.9 KiB of encoder RAM).

Upstream is not modified. Report of the fit/measure work (image budgets,
`--print-memory-usage` output, objdump evidence of the DSP instructions) is in
the PMR446 TX commit messages and the phase report.

## Patents

The MIT license covers the code. It says nothing about AMBE+2 itself, and AMBE+2
(DVSI) is patented technology: shipping a vocoder that implements it can require
a licence from the patent holder regardless of the source licence, and
implementations written from the specification rather than from licensed
reference code have been the subject of enforcement before.

This is worth knowing before anyone distributes a build with the dPMR446 voice
images enabled: the layer-2 dPMR446 app and its signalling receiver do not
involve AMBE+2 and are unaffected, but `baseband_dpmr446_voice`,
`baseband_dpmr446_tx` and the combined image do. Nothing in this repository
grants patent rights, and the author of this vendoring is not in a position to
grant any.
