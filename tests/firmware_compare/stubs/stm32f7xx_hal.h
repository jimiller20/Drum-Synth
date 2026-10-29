/* Host-side stand-in for the STM32 HAL: just the types and Cortex-M intrinsics the LXR DSP uses.
   The two ARM conversion behaviours that matter for the sound are made explicit (armF2U_c). */
#ifndef FW_STUB_HAL_H
#define FW_STUB_HAL_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
typedef struct { int dummy; } RNG_HandleTypeDef;
typedef union { struct { uint32_t _reserved0:27; uint32_t Q:1; uint32_t V:1; uint32_t C:1; uint32_t Z:1; uint32_t N:1; } b; uint32_t w; } APSR_Type;
static inline uint32_t __get_APSR (void) { return 0; }
#define I2S_AUDIOFREQ_44K 44100u
static inline int32_t __SSAT (int32_t x, int bits) { const int32_t mx = (1 << (bits - 1)) - 1, mn = -(1 << (bits - 1)); return x > mx ? mx : (x < mn ? mn : x); }
static inline int16_t fw_sat16 (int32_t x) { return (int16_t) (x > 32767 ? 32767 : (x < -32768 ? -32768 : x)); }
static inline uint32_t __QADD16 (uint32_t a, uint32_t b)
{
    const int16_t lo = fw_sat16 ((int32_t) (int16_t) (a & 0xFFFF) + (int32_t) (int16_t) (b & 0xFFFF));
    const int16_t hi = fw_sat16 ((int32_t) (int16_t) (a >> 16) + (int32_t) (int16_t) (b >> 16));
    return (uint32_t) (uint16_t) lo | ((uint32_t) (uint16_t) hi << 16);
}
static inline uint32_t __QSUB16 (uint32_t a, uint32_t b)
{
    const int16_t lo = fw_sat16 ((int32_t) (int16_t) (a & 0xFFFF) - (int32_t) (int16_t) (b & 0xFFFF));
    const int16_t hi = fw_sat16 ((int32_t) (int16_t) (a >> 16) - (int32_t) (int16_t) (b >> 16));
    return (uint32_t) (uint16_t) lo | ((uint32_t) (uint16_t) hi << 16);
}
static inline uint32_t __PKHBT (uint32_t a, uint32_t b, int sh) { return (a & 0xFFFFu) | ((b << sh) & 0xFFFF0000u); }
static inline uint8_t __CLZ (uint32_t v) { return v ? (uint8_t) __builtin_clz (v) : 32; }
/* VCVT.U32.F32: negative -> 0, too large -> 0xFFFFFFFF */
static inline uint32_t armF2U_c (float x) { if (! (x > 0.0f)) return 0u; if (x >= 4294967295.0f) return 0xFFFFFFFFu; return (uint32_t) x; }
#endif
