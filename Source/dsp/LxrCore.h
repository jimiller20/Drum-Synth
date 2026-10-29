// LxrCore.h - building blocks of the LXR-02 sound engine, ported to portable C++.
//
// Ported from the Erica Synths LXR-02 firmware (Oscillator.c, ResonantFilter.c, SlopeEg2.c,
// Decay.c, snapEg.c, transientGenerator.c, lfo.c, distortion.c, 1PoleLp.c).
// Copyright (C) 2012-2026 Erica Synths; port (C) 2026 Ferrum 8 contributors.
// SPDX-License-Identifier: GPL-3.0-only
//
// Porting notes
// - The firmware renders 16-bit integer audio in control blocks of 64 samples at 44.14 kHz.
//   Envelopes, LFOs and pitch updates run once per block. This port keeps that structure: a
//   control block lasts 64/44140 s at any host sample rate, so every envelope time, LFO rate and
//   pitch-envelope curve matches the hardware. Oscillators, filters and noise use the real rate.
// - Integer behaviour that shapes the sound is kept: 32-bit phase accumulators, the same
//   interpolated wavetables, saturating 16-bit adds, and the ARM float->unsigned conversion
//   (negative values become 0), which makes the LXR's phase modulation one-sided.
// - Parameters arrive as floats in "MIDI units" (0..127, fractional allowed) instead of uint8,
//   so knobs are smooth; integer settings give exactly the firmware's values.
#pragma once
#include "LxrTables.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lxr
{
constexpr float  kLxrFs       = 44140.0f; // the firmware's real I2S rate (REAL_FS)
constexpr int    kLxrBlock    = 64;       // OUTPUT_DMA_SIZE
constexpr int    kMaxCtrlLen  = 320;      // control block length at up to ~220 kHz
constexpr float  kSemitoneUp  = 1.0594630943592952645618252949463f;
constexpr uint32_t kTransientEnd = 2311061504u; // 2204 << 20

//---------------------------------------------------------------------------------------------
// integer helpers mirroring the Cortex-M7 behaviour
inline int16_t sat16 (int32_t x) { return (int16_t) (x > 32767 ? 32767 : (x < -32768 ? -32768 : x)); }
inline int16_t qadd16 (int16_t a, int16_t b) { return sat16 ((int32_t) a + (int32_t) b); }
inline int16_t qsub16 (int16_t a, int16_t b) { return sat16 ((int32_t) a - (int32_t) b); }
// float -> int16 with saturation (the firmware relies on values staying in range; we clip)
inline int16_t f2s16 (float x)
{
    if (! (x == x)) return 0;
    if (x >= 32767.0f) return 32767;
    if (x <= -32768.0f) return -32768;
    return (int16_t) x; // truncation toward zero, like VCVT
}
inline int16_t d2s16 (double x)
{
    if (! (x == x)) return 0;
    if (x >= 32767.0) return 32767;
    if (x <= -32768.0) return -32768;
    return (int16_t) x;
}
// ARM VCVT.U32.F32: negative -> 0, overflow -> 0xFFFFFFFF
inline uint32_t armF2U (double x)
{
    if (! (x > 0.0)) return 0u;
    if (x >= 4294967295.0) return 0xFFFFFFFFu;
    return (uint32_t) x;
}
inline uint8_t clz32 (uint32_t v)
{
    if (v == 0) return 32;
    uint8_t n = 0;
    while ((v & 0x80000000u) == 0) { v <<= 1; ++n; }
    return n;
}
inline float clamp01 (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// valueShaper.h
inline float valueShaperF2F (float val, float shape)
{
    const float k = 2.0f * shape / (1.0001f - shape);
    return ((1.0f + k) * val / (1.0f + k * std::fabs (val)));
}

// MidiNoteFrequencies[] lookup like the firmware; fractional notes (smooth knobs, morphing)
// glide geometrically between table entries, so integer settings give the exact hardware pitch
inline float midiNoteHz (float note)
{
    note = note < 0.0f ? 0.0f : (note > 127.0f ? 127.0f : note);
    const int n = (int) note;
    const float frac = note - (float) n;
    if (frac <= 0.0f || n >= 127) return midiNoteFrequencies[n];
    return midiNoteFrequencies[n] * std::pow (2.0f, frac / 12.0f);
}
// MidiParser.c midiParser_calcDetune: +-half a semitone around value 63.5
inline float calcDetune (float value127) { return 1.0f + (value127 / 127.0f - 0.5f) * (kSemitoneUp - 1.0f); }

//---------------------------------------------------------------------------------------------
// tiny deterministic RNG standing in for the STM32 hardware RNG
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

//---------------------------------------------------------------------------------------------
// one pole smoother (1PoleLp.c). Coefficients given at 44.1 kHz are converted to the host rate.
struct OnePole
{
    double b = 0.0, r = 0.01;
    void setCoefAt44k (double coef, double srRatio) { r = 1.0 - std::pow (1.0 - coef, srRatio); }
    float process (double in) { b = r * (in - b) + b; return (float) b; }
};

//---------------------------------------------------------------------------------------------
enum OscWave : uint8_t { WAVE_SINE, WAVE_TRI, WAVE_SAW, WAVE_REC, WAVE_NOISE, WAVE_PWM, NUM_OSC_WAVES };

struct Osc
{
    int16_t  output    = 0;
    uint32_t phaseInc  = 0;
    uint32_t phase     = 0;
    float    freq      = 440.0f;
    uint8_t  waveform  = WAVE_SINE;
    uint8_t  tableOffset = 4;
    float    pitchMod  = 1.0f;
    float    fmMod     = 0.0f;
    float    coarse    = 70.0f;   // midiFreq >> 8
    float    fine      = 64.0f;   // midiFreq & 0xff
    int      baseNote  = 60;      // the last played midi note (SEQ_DEFAULT_NOTE = 60)

    void recalcFreq()
    {
        float note = coarse + (float) (baseNote - 60);
        note = note > 127.0f ? 127.0f : (note < 0.0f ? 0.0f : note);
        freq = midiNoteHz (note) * calcDetune (fine);
    }
    void setBaseNote (int n) { baseNote = n; recalcFreq(); }

    // Oscillator.c freqToTableIndex: integer log2 of f/440 (0 for f < 440 wraps to table 4)
    static uint8_t freqToTableIndex (float f)
    {
        const uint32_t v = armF2U (f / 440.0f);
        const uint8_t l2 = (uint8_t) (31 - clz32 (v));
        const int note = 12 * (int) l2 + 70;
        const uint8_t idx = (uint8_t) (note / 12);
        return idx > 10 ? 10 : idx;
    }

    // osc_setFreq: freq2PhaseIncr (4096-entry sine / noise) and freq2PhaseIncr1024 (wavetables),
    // evaluated in single precision exactly like the firmware so phases stay sample-identical
    void setFreq (double sampleRate)
    {
        const float currentFreq = freq * pitchMod;
        const float fs = (float) sampleRate;
        if (waveform == WAVE_TRI || waveform == WAVE_SAW || waveform == WAVE_REC || waveform == WAVE_PWM)
        {
            phaseInc = armF2U ((double) (((1024.0f * currentFreq) / fs) * 4194304.0f));
            tableOffset = freqToTableIndex (currentFreq);
        }
        else
            phaseInc = armF2U ((double) (((4096.0f * currentFreq) / fs) * 1048576.0f));
    }

    void resetPhaseForTrigger (float offset)
    {
        if (waveform == WAVE_SINE)
            phase = (uint32_t) (1024.0 + ((double) (0x3ffu << 20) - 1024.0) * offset);
        else if (waveform > WAVE_SINE && waveform <= WAVE_REC)
            phase = (uint32_t) ((double) (0xffu << 22) * offset);
        else
            phase = 0;
    }

    //-----------------------------------------------------------------------------------------
    static inline int16_t interpSine (uint32_t index)
    {
        uint32_t itg = index >> 20;
        int16_t out = sineTable[itg++];
        const uint32_t frac = index & 0xFFFFF;
        return (int16_t) (out + (int16_t) (((int64_t) (sineTable[itg] - out) * frac) >> 20));
    }
    static inline int16_t interpTable (const int16_t (*table)[1025], uint8_t off, uint32_t index)
    {
        uint32_t itg = index >> 22;
        int16_t out = table[off][itg++];
        const uint32_t frac = index & 0x3FFFFF;
        return (int16_t) (out + (int16_t) (((int64_t) (table[off][itg] - out) * frac) >> 22));
    }
    const int16_t (*tableFor() const)[1025]
    {
        switch (waveform) { case WAVE_SAW: return sawTable; case WAVE_REC: return recTable; default: return triTable; }
    }

    void sineBlock (int16_t* buf, int n, float gain)
    {
        for (int i = 0; i < n; ++i) { const int16_t o = interpSine (phase); phase += phaseInc; buf[i] = f2s16 (o * gain); }
    }
    void tableBlock (int16_t* buf, int n, float gain)
    {
        auto* t = tableFor();
        for (int i = 0; i < n; ++i) { const int16_t o = interpTable (t, tableOffset, phase); phase += phaseInc; buf[i] = f2s16 (o * gain); }
    }
    // The firmware computes the second saw's phase as (float) phase + pwm * (UINT32_MAX / 2) and
    // converts back to uint32: the sum saturates at 0xFFFFFFFF instead of wrapping, which gives
    // the LXR "PWM" wave its ramp-and-step shape (and silence at pulse width 0).
    static inline uint32_t pwmPhase (uint32_t p, float pwm)
    {
        const float sum = (float) p + pwm * (float) (UINT32_MAX / 2);
        return armF2U (sum);
    }
    void pwmBlock (int16_t* buf, int n, float gain, float pwm)
    {
        for (int i = 0; i < n; ++i)
        {
            const int16_t a = interpTable (sawTable, tableOffset, phase);
            const int16_t b = interpTable (sawTable, tableOffset, pwmPhase (phase, pwm));
            const int16_t o = qsub16 (a, b);
            phase += phaseInc; output = o; buf[i] = f2s16 (o * gain);
        }
    }
    void noiseBlock (int16_t* buf, int n, float gain, Rng& rng)
    {
        for (int i = 0; i < n; ++i)
        {
            const uint32_t last = phase;
            phase += phaseInc;
            if (last > phase) output = (int16_t) (uint16_t) rng.next();
            buf[i] = f2s16 (output * gain);
        }
    }
    // calcNextOscSampleBlock
    void block (int16_t* buf, int n, float gain, float pwm, Rng& rng)
    {
        switch (waveform)
        {
            case WAVE_SINE:  sineBlock (buf, n, gain); break;
            case WAVE_TRI: case WAVE_SAW: case WAVE_REC: tableBlock (buf, n, gain); break;
            case WAVE_NOISE: noiseBlock (buf, n, gain, rng); break;
            case WAVE_PWM:   pwmBlock (buf, n, gain, pwm); break;
            default: std::memset (buf, 0, sizeof (int16_t) * (size_t) n); break;
        }
    }
    // calcNextOscSampleFmBlock. The modulator is scaled by fmMod and converted to unsigned:
    // on the STM32 negative products become 0, so only the positive half pushes the phase.
    void fmBlock (const int16_t* mod, int16_t* buf, int n, float gain, float pwm, Rng& rng)
    {
        switch (waveform)
        {
            case WAVE_SINE:
                for (int i = 0; i < n; ++i)
                {
                    const uint32_t index = phase + (armF2U (mod[i] * fmMod) << 17);
                    const int16_t o = interpSine (index);
                    phase += phaseInc; buf[i] = f2s16 (o * gain);
                }
                break;
            case WAVE_TRI: case WAVE_SAW: case WAVE_REC:
            {
                auto* t = tableFor();
                for (int i = 0; i < n; ++i)
                {
                    const uint32_t index = phase + (armF2U (mod[i] * fmMod) << 19);
                    const int16_t o = interpTable (t, tableOffset, index);
                    phase += phaseInc; output = o; buf[i] = f2s16 (o * gain);
                }
                break;
            }
            case WAVE_NOISE: noiseBlock (buf, n, gain, rng); break;
            case WAVE_PWM:
            {
                for (int i = 0; i < n; ++i)
                {
                    const uint32_t index = phase + (armF2U (mod[i] * fmMod) << 19);
                    const int16_t a = interpTable (sawTable, tableOffset, index);
                    const int16_t b = interpTable (sawTable, tableOffset, pwmPhase (index, pwm));
                    const int16_t o = qsub16 (a, b);
                    phase += phaseInc; output = o; buf[i] = f2s16 (o * gain);
                }
                break;
            }
            default: std::memset (buf, 0, sizeof (int16_t) * (size_t) n); break;
        }
    }
};

//---------------------------------------------------------------------------------------------
// ResonantFilter.c - zero delay feedback SVF with soft clipped integrators
enum FilterType : uint8_t { FILTER_LP = 1, FILTER_HP, FILTER_BP, FILTER_UNITY_BP, FILTER_NOTCH, FILTER_PEAK, FILTER_NAIVE_2_POLE, FILTER_OFF };

struct ResonantFilter
{
    float f = 0.2f;   // cutoff as fraction of the LXR sample rate (0..0.45)
    float g = 0.0f, q = 0.9f, s1 = 0, s2 = 0, a = 0, b = 0, drive = 0.4f;
    float srRatio = 1.0f; // 44140 / host rate

    static float fastTan (float x) { const float x2 = x * x; return (-15 * x + x * x2) / (3 * (-5 + 2 * x2)); }
    static float fastTanh (float v) { if (v < -1.95f) return -1.0f; if (v > 1.95f) return 1.0f; return 4.15f * v / (4.29f + v * v); }
    static float tanhXdX (float x) { const float a2 = x * x; return ((a2 + 105) * a2 + 945) / ((15 * a2 + 420) * a2 + 945); }
    static float softClipTwo (float in) { return in * tanhXdX (0.5f * in); }

    void reset() { s1 = s2 = a = b = 0; }
    void setReso (float feedback) { q = 1 - feedback; if (q < 0.1f) q = 0.02f; }
    void setDrive (float d) { drive = 0.4f + d * d * 6; }
    void setValue (float val) { f = val * 0.45f; recalc(); }
    void recalc() { g = fastTan (3.14159265358979323846f * f * srRatio); } // M_PI is not standard C++ (MSVC)

    void process (uint8_t type, int16_t* buf, int n)
    {
        if (type == FILTER_OFF || type < FILTER_LP || type > FILTER_OFF) return;
        const float fg = g;
        const float R = f >= 0.4499f ? 1.0f : q;
        const float ff = fg * fg;
        if (type == FILTER_NAIVE_2_POLE)
        {
            const float f_lp2 = std::min (0.995f, f * 2.21f * srRatio);
            for (int i = 0; i < n; ++i)
            {
                const float x = softClipTwo ((buf[i] / 32767.0f) * drive);
                const float qq = (1 - q) * 1.4f + (1 - q) / (1.0f - f_lp2);
                a += f_lp2 * ((x - a) + qq * (a - b));
                a = std::clamp (a, -1.0f, 1.0f);
                b += f_lp2 * (a - b);
                b = std::clamp (b, -1.0f, 1.0f);
                buf[i] = sat16 ((int32_t) (b * 32767.0f));
            }
            return;
        }
        if (! (s1 == s1)) s1 = 0; // removeMaliciousSample
        if (! (s2 == s2)) s2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const float x = softClipTwo ((buf[i] / 32767.0f) * drive);
            const float g0 = 1.0f / (1.0f + fg * 2 * R);
            const float f1 = ff * g0;
            const float y1 = (f1 * x + s2 + fg * g0 * s1) / (f1 + 1);
            const float xx = x - y1;
            const float y0 = (softClipTwo (s1) + fg * xx) * g0;
            s1 = softClipTwo (s1) + 2 * fg * (xx - 2 * R * y0);
            s2 = s2 + 2 * fg * y0;
            float out;
            switch (type)
            {
                case FILTER_LP:       out = fastTanh (y1); break;
                case FILTER_HP:       out = x - 2 * R * y0 - y1; break;
                case FILTER_BP:       out = y0; break;
                case FILTER_UNITY_BP: out = 2 * R * y0; break;
                case FILTER_NOTCH:    out = x - 2 * R * y0; break;
                default:              { const float h = x - 2 * R * y0 - y1; out = y1 - h; } break; // peak
            }
            buf[i] = sat16 ((int32_t) (out * 32767.0f));
        }
    }
};

//---------------------------------------------------------------------------------------------
// SlopeEg2.c - attack/decay envelope with curve and attack-phase repeats (claps)
struct SlopeEg2
{
    enum { STOPPED = 0, ATTACK = 1, DECAY = 2, REPEAT = 3 };
    double attack = 0.01, decay = 0.01, slope = 0, invSlope = 0, value = 0;
    int repeat = 0, repeatCnt = 0;
    uint8_t state = STOPPED;

    static double calcSlopeValue (double val, double s) { return (1.0 + s) * val / (1.0 + s * std::fabs (val)); }
    static double calcTime (float data2, double amount)
    {
        const double val = data2 / 127.0;
        const double ret = (1.0 - ((1.0 + amount) * val / (1.0 + amount * std::fabs (val)))) * 2.0; // USE_BIG_SAMPLE_BUFFER
        return ret > 1.0 ? 1.0 : ret;
    }
    static constexpr double kAttackK = 2 * 0.99 / (1.0 - 0.99);
    static constexpr double kDecayK  = 2 * 0.999 / (1.0 - 0.999);
    static double decayFor (float data2) { return calcTime (data2, kDecayK); }

    void setAttack (float data2) { attack = calcTime (data2, kAttackK); }
    void setDecay (float data2)  { decay = calcTime (data2, kDecayK); }
    void setSlope (float data2)
    {
        const double amt = ((data2 / 127.f) - 0.5f) * 1.999f;
        slope = 2 * amt / (1 - amt);
        invSlope = 2 * (-amt) / (1 + amt);
    }
    void trigger()
    {
        if (! repeat) state = ATTACK;
        else { state = REPEAT; value = 1.0; }
        repeatCnt = repeat;
    }
    bool isIdle() const { return state == STOPPED || (state == DECAY && value <= 0); }
    double calc()
    {
        double val = value;
        switch (state)
        {
            case STOPPED: return 0.0;
            case REPEAT:
                if (val > 0) val -= attack;
                else
                {
                    if (--repeatCnt > 0) val = 1.0;
                    else { state = DECAY; val = 1.0; }
                }
                value = val;
                return calcSlopeValue (val, slope);
            case ATTACK:
                val += attack;
                if (val >= 1.0) { val = 1.0; state = DECAY; }
                value = val;
                return calcSlopeValue (val, invSlope);
            case DECAY:
                if (val > 0) { val -= decay; value = val; return calcSlopeValue (val, slope); }
                value = 0; return 0;
        }
        return 0;
    }
};

//---------------------------------------------------------------------------------------------
// Decay.c - the pitch envelope
struct DecayEg
{
    float decay = 0.01f, slope = 0.0f, value = 0.0f;
    static float calcTime (float data2)
    {
        constexpr float K = 2 * 0.99f / (1.f - 0.99f);
        const float val = data2 / 127.f;
        const float ret = (1.f - ((1.f + K) * val / (1.f + K * std::fabs (val)))) * 2.f;
        return ret > 1.f ? 1.f : ret;
    }
    void setDecay (float data2) { decay = calcTime (data2); }
    void setSlope (float data2) { const float amt = ((data2 / 127.f) - 0.5f) * 2; slope = 2 * amt / (1 - amt); }
    void trigger() { value = 1.f; }
    float calc()
    {
        float val = value;
        if (val > 0) val -= decay; else val = 0;
        value = val;
        // the curve's end points divide by zero (slope = inf at 127, 0/0 at slope -1 and val 1);
        // return the curve's limit there instead of NaN
        const float r = (1 + slope) * val / (1 + slope * std::fabs (val));
        if (! std::isfinite (r)) return val > 0 ? 1.0f : 0.0f;
        return r;
    }
};

// snapEg.c
struct SnapEg
{
    float value = 0;
    void trigger() { value = 1; }
    float calc (float time)
    {
        float ret = 0;
        if (value > 0) { ret = value * value * 24.f; value -= 0.2f * time; }
        return ret;
    }
};

//---------------------------------------------------------------------------------------------
// transientGenerator.c. Waveform 0 = "snap" (pitch spike), 1 = "offset" (phase click),
// 2..13 = the 12 sampled attacks.
struct TransientGen
{
    uint32_t phase = 0;
    float pitch = 1.f, volume = 1.f;
    uint8_t waveform = 0;
    void trigger() { phase = 0; }
    void block (int16_t* buf, int n, float srRatio)
    {
        if (waveform <= 1 || phase >= kTransientEnd) { std::memset (buf, 0, sizeof (int16_t) * (size_t) n); return; }
        const int8_t* data = transientData[std::min<int> (waveform - 2, kNumTransientSamples - 1)];
        // "phase += pitch * (1 << 20)" on a uint32 runs in single precision on the hardware
        const float inc = pitch * (float) (1 << 20) * srRatio;
        for (int i = 0; i < n; ++i)
        {
            if (phase < kTransientEnd)
            {
                const uint32_t idx = phase >> 20;
                buf[i] = f2s16 (volume * (float) (data[idx] * 256) * (idx < (uint32_t) kTransientLength ? 1.f : 0.f));
                phase = armF2U ((double) ((float) phase + inc));
            }
            else buf[i] = 0;
        }
    }
};

//---------------------------------------------------------------------------------------------
// lfo.c - runs at control rate
enum LfoWave : uint8_t { LFO_SINE, LFO_TRI, LFO_SAW_UP, LFO_SAW_DOWN, LFO_REC, LFO_NOISE, LFO_EXP_UP, LFO_EXP_DOWN, NUM_LFO_WAVES };

struct Lfo
{
    uint32_t phase = 0, phaseInc = 1, phaseOffset = 0;
    uint8_t waveform = LFO_SINE, sync = 0;
    float rnd = 0.5f, freq = 1;
    static constexpr float kLfoSr = kLxrFs / (float) kLxrBlock;

    void setFreq (float v01)
    {
        float f = 0.0078125f + v01 * (1.f - 0.0078125f);
        f = f * f * f;
        freq = f * 200.0f;
    }
    void updateInc (double bpm)
    {
        if (sync == 0) { phaseInc = armF2U (freq / kLfoSr * 4294967295.0); return; }
        static const float scalers[12] = { 0, 0.25f, 0.5f, 1, 2, 3, 4, 6, 8, 12, 16, 32 };
        const double tempoAsFrequency = bpm > 0.0 ? bpm / 60.0 / 4.0 : 0.0;
        phaseInc = armF2U (tempoAsFrequency * scalers[std::min<int> (sync, 11)] / kLfoSr * 4294967295.0);
    }
    float calc (Rng& rng)
    {
        const uint32_t old = phase;
        phase += phaseInc;
        const bool overflow = old > phase;
        const float p = phase / 4294967295.0f;
        switch (waveform)
        {
            case LFO_SINE:      return ((sineTable[phase >> 20] / 32767.f) + 1) / 2.f;
            case LFO_TRI:       return 1.f - std::fabs (p * 2 - 1);
            case LFO_SAW_UP:    return p;
            case LFO_SAW_DOWN:  return 1.f - p;
            case LFO_REC:       return phase > 0x7fffffffu ? 1.f : 0.f;
            case LFO_NOISE:     if (overflow) rnd = rng.next() / 4294967295.0f; return rnd;
            case LFO_EXP_UP:    return p * p * p;
            case LFO_EXP_DOWN:  { const float x = 1.f - p; return x * x * x; }
            default:            return 0;
        }
    }
};

//---------------------------------------------------------------------------------------------
// distortion.c - soft clipper per voice; the shape is smoothed per sample (drum voice style)
struct VoiceDistortion
{
    float shape = 0;
    OnePole smooth;
    void setShape (float s) { s -= 0.0078125f; shape = 2 * s / (1 - s); }
    void process (int16_t* buf, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float sh = smooth.process (shape);
            float x = buf[i] / 32767.f;
            x = (1 + sh) * x / (1 + sh * std::fabs (x));
            buf[i] = f2s16 (x * 32767);
        }
    }
};
} // namespace lxr
