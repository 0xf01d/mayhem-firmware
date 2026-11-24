#ifndef XOMBE_CONFIG_H
#define XOMBE_CONFIG_H

/* PortaPack vendored build configuration (upstream generates this from
 * xombe_config.h.in with CMake). Subset built here, see README.md:
 * DMR (AMBE+2 half rate) decoder + encoder only, q15 kernels, closed-loop
 * amplitude quantizer and tone detection compiled out to fit the 32 KiB
 * baseband image budget. */

#define XOMBE_HAVE_ENCODER 1
#define XOMBE_HAVE_DSTAR 0
#define XOMBE_KERNELS_Q15 1

/* PortaPack vendor patches (upstream builds these in, gated at runtime):
 * closed-loop amplitude quantizer (enc/common/cquant.c + cq_q15_tables.c,
 * ~20 KiB flash tables and XOMBE_ENC_CQ_SIZE of encoder state) and encoder
 * tone detection (enc/tone_detect.c + its DSP tables). The public tone
 * output path (xombe_enc_tone / xombe__tone_valid) stays, decoder tone
 * synthesis stays. */
#define XOMBE_HAVE_CQUANT 0
#define XOMBE_HAVE_TONES 0

/* static build, no DLL import/export decoration */
#define XOMBE_API

#endif
