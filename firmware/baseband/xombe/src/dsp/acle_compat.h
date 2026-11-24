#ifndef XOMBE_ACLE_COMPAT_H
#define XOMBE_ACLE_COMPAT_H

/* PortaPack vendor patch. The GCC 9.2.1 arm-none-eabi toolchain used by this
 * firmware ships an arm_acle.h that declares none of the DSP-extension
 * intrinsics (and no int16x2_t). This header provides the subset q15ops.h
 * needs, as inline assembly with ACLE-compatible names and signatures, so the
 * q15 kernels emit the real Cortex-M4 DSP instructions (smlad, smuad, ...).
 * GCC >= 10 (or a toolchain with a complete arm_acle.h) uses <arm_acle.h>
 * instead; see the guard in q15ops.h. */

#if defined(__ARM_FEATURE_DSP) && defined(__GNUC__) && !defined(__clang__)

__extension__ typedef int32_t int16x2_t;

static inline int32_t __smuad(int16x2_t a, int16x2_t b)
{
    int32_t r;
    __asm__("smuad %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline int32_t __smlad(int16x2_t a, int16x2_t b, int32_t acc)
{
    int32_t r;
    __asm__("smlad %0, %1, %2, %3" : "=r"(r) : "r"(a), "r"(b), "r"(acc));
    return r;
}

static inline int64_t __smlald(int16x2_t a, int16x2_t b, int64_t acc)
{
    int64_t r;
    __asm__("smlald %Q0, %R0, %2, %3" : "=&r"(r) : "0"(acc), "r"(a), "r"(b));
    return r;
}

static inline int32_t __smlsd(int16x2_t a, int16x2_t b, int32_t acc)
{
    int32_t r;
    __asm__("smlsd %0, %1, %2, %3" : "=r"(r) : "r"(a), "r"(b), "r"(acc));
    return r;
}

static inline int32_t __smladx(int16x2_t a, int16x2_t b, int32_t acc)
{
    int32_t r;
    __asm__("smladx %0, %1, %2, %3" : "=r"(r) : "r"(a), "r"(b), "r"(acc));
    return r;
}

static inline uint32_t __qadd16(int16x2_t a, int16x2_t b)
{
    uint32_t r;
    __asm__("qadd16 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline uint32_t __qsub16(int16x2_t a, int16x2_t b)
{
    uint32_t r;
    __asm__("qsub16 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline uint32_t __shadd16(int16x2_t a, int16x2_t b)
{
    uint32_t r;
    __asm__("shadd16 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline uint32_t __shsub16(int16x2_t a, int16x2_t b)
{
    uint32_t r;
    __asm__("shsub16 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline uint32_t __qsax(int16x2_t a, int16x2_t b)
{
    uint32_t r;
    __asm__("qsax %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline uint32_t __qasx(int16x2_t a, int16x2_t b)
{
    uint32_t r;
    __asm__("qasx %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline uint32_t __shsax(int16x2_t a, int16x2_t b)
{
    uint32_t r;
    __asm__("shsax %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline uint32_t __shasx(int16x2_t a, int16x2_t b)
{
    uint32_t r;
    __asm__("shasx %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline int __clz(uint32_t x)
{
    int r;
    __asm__("clz %0, %1" : "=r"(r) : "r"(x));
    return r;
}

static inline uint32_t __rbit(uint32_t x)
{
    uint32_t r;
    __asm__("rbit %0, %1" : "=r"(r) : "r"(x));
    return r;
}

#endif /* __ARM_FEATURE_DSP && GCC && !clang */

#endif
