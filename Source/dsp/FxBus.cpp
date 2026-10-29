// FxBus.cpp - see FxBus.h
// Copyright (C) 2012-2026 Erica Synths; port (C) 2026 Ferrum 8 contributors.
// SPDX-License-Identifier: GPL-3.0-only
#include "FxBus.h"
#include <cstdio>

namespace ferrum
{
using namespace lxr;

static inline float ssat (float x) { return x > 32767.f ? 32767.f : (x < -32768.f ? -32768.f : x); }
static inline double convCoef (double coefAt44k, double srRatio) { return 1.0 - std::pow (1.0 - coefAt44k, srRatio); }

// FX buffer timing constants from Delay.h, in LXR samples
static constexpr float kLowMin = 0.0006f * kLxrFs;
static constexpr float kLowMax = 0.02f * kLxrFs;
static constexpr float kHiMin  = 0.02f * kLxrFs;
static constexpr float kHiMax  = 32767.f;

void FxBus::prepare (double sampleRate)
{
    sr = sampleRate;
    srRatio = (float) (kLxrFs / sampleRate);
    cHp1 = convCoef (0.64116641920131193922, srRatio);
    cLp1 = convCoef (1. - 0.1025353241628911807, srRatio);
    cHp2 = convCoef (1. - 0.80326417028080632238, srRatio);
    cHp3 = convCoef (1. - 0.99149556438154282898, srRatio);
    cLp2 = convCoef (1. - 0.93130041642945205043, srRatio);

    uint32_t n = 1;
    while (n < (uint32_t) std::ceil (32768.0 / srRatio)) n <<= 1;
    buf.assign (n, 0.f);
    rng.seed (0xF00Du);
    ringOsc.waveform = WAVE_REC; ringOsc.fmMod = 0; ringOsc.pitchMod = 1.f;
    first = true;
    reset();
    setSettings (s);
}

void FxBus::reset()
{
    for (int c = 0; c < 2; ++c) hp1[c] = hp2[c] = hp3[c] = lp1[c] = lp2[c] = lp3[c] = slew[c] = lastOut[c] = 0;
    env = 0;
    std::fill (buf.begin(), buf.end(), 0.f);
    write = (uint32_t) (buf.size() / 20 - 10);
    oldRead = newRead = 0; fracOld = fracNew = 0; interpValue = interpStep = 0; timePending = false;
    tone.b = tone2.b = 0;
}

void FxBus::setSettings (const FxSettings& ns)
{
    const bool typeChanged = first || ns.type != s.type || ns.pingPong != s.pingPong;
    s = ns;
    if (typeChanged && ! first) reset();
    first = false;
    mask = (uint32_t) (s.pingPong ? buf.size() / 2 - 1 : buf.size() - 1);

    // ring mod (Ringmod_setFreq / setWave)
    ringOsc.waveform = (uint8_t) std::clamp (s.ringWave, 0, (int) NUM_OSC_WAVES - 1);
    ringOsc.freq = s.p[0] * 880.f;
    ringOsc.setFreq (sr);

    // compressor
    attack  = std::exp (-1000.0 / ((0.03 + s.p[0] * 100.0) * sr));
    release = std::exp (-1000.0 / ((25.0 + s.p[1] * 175.0) * sr));
    thresh  = -96.0 + s.p[2] * 96.0;
    markup  = 1.0 + s.p[3] * 2.0;
    static const double ratios[6] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    ratioV = ratios[std::clamp (s.ratio, 0, 5)];

    // delay (FxBus_updateParameter: range first, then time)
    range = s.p[1];
    setDelayTime (s.p[0]);
    tone.setCoefAt44k (s.p[2] * s.p[2], srRatio);
    tone2.setCoefAt44k (s.p[2] * s.p[2], srRatio);
    feedback = s.p[3];
}

float FxBus::calcDelayTime (float timeCv) const
{
    auto lerp = [] (float a, float b, float x) { return a * (1 - x) + b * x; };
    const float mn = lerp (kLowMin, kHiMin, range);
    const float mx = lerp (kLowMax, s.pingPong ? kHiMax * 0.48f : kHiMax, range);
    return lerp (mn, mx, timeCv) / srRatio; // host samples
}

void FxBus::setDelayTime (float t)
{
    if (interpValue <= 0)
    {
        delayTime = t;
        const float offset = std::min (calcDelayTime (delayTime * range), (float) mask - 2.f);
        const uint32_t offItg = (uint32_t) offset;
        fracNew = offset - (float) offItg;
        newRead = (write - 1 - offItg) & mask;
        interpValue = 1.f;
        interpStep = (float) (1.0 / (sr * 0.01));
    }
    else { pendingTime = t; timePending = true; }
}

// the firmware stores the delay line as __fp16
float FxBus::toHalf (float x)
{
    if (x == 0.f || ! std::isfinite (x)) return 0.f;
    int e;
    const float m = std::frexp (x, &e);
    return std::ldexp (std::round (m * 2048.f) / 2048.f, e);
}

void FxBus::processDistortion (float& l, float& r)
{
    float io[2] = { l, r };
    const double coff = convCoef (0.06537948995454451662 + s.p[2] * 0.92410701734426630737, srRatio);
    for (int c = 0; c < 2; ++c)
    {
        const float x = io[c];
        double wetv = 0;
        switch (s.distType)
        {
            case DistType::Tube:
            {
                const int32_t in = (int32_t) (x * (1.f + s.p[0] * 4));
                const uint8_t itg = (uint8_t) ((sat16 (in) >> 9) + 64);
                const float frac = (float) (in & 0x1FF) / 512.f;
                double v = ecc83Table[itg] + (ecc83Table[itg + 1] - ecc83Table[itg]) * frac;
                lp3[c] += coff * (v - lp3[c]);
                const float outMod = (1.0f + 0.55f * s.p[1]) + 0.55f * s.p[1];
                wetv = lp3[c] * outMod * s.p[3];
                break;
            }
            case DistType::Fold:
            {
                const float v = x / 32768.f * (1.f + s.p[0] * 4.f) + s.p[1];
                lp3[c] += coff * (v - lp3[c]);
                double in = lp3[c];
                const double thr = 1.0;
                if (in > thr || in < -thr) in = std::fabs (std::fabs (std::fmod (in - thr, thr * 4)) - thr * 2) - thr;
                wetv = in * s.p[3];
                break;
            }
            case DistType::Rat:
            default:
            {
                double sig = x / 32768.;
                sig += lastOut[c] * s.p[1] * 0.125;
                hp1[c] += cHp1 * (sig - hp1[c]); sig = sig - hp1[c];
                lp1[c] += cLp1 * (sig - lp1[c]); sig = lp1[c];
                hp2[c] += cHp2 * (sig - hp2[c]); sig = lp1[c] - hp2[c];
                hp3[c] += cHp3 * (sig - hp3[c]); sig = sig - hp3[c];
                lp2[c] += cLp2 * (sig - lp2[c]); sig = lp2[c];
                sig *= 1 + s.p[0] * 2304;
                double diff = sig - slew[c];
                const double lim = 6.79655641141821477118 * srRatio;
                if (std::fabs (diff) > lim) diff = diff > 0 ? lim : -lim;
                slew[c] = std::clamp (slew[c] + diff, -4.2, 4.2);
                // diode(): 1N914 transfer curve lookup
                const float in = std::clamp ((float) slew[c], -1.f, 1.f);
                const float index = (in + 1.f) * 128.f;
                const int i0 = std::min ((int) index, 255);
                const float fr = index - (float) i0;
                const double d = diodeTable[i0] + (diodeTable[i0 + 1] - diodeTable[i0]) * fr;
                lp3[c] += coff * (d - lp3[c]);
                lastOut[c] = lp3[c] * s.p[3];
                wetv = lastOut[c];
                break;
            }
        }
        const float wet = ssat ((float) (int32_t) (wetv * 32768.0));
        io[c] = x + ssat ((wet - x) * s.dryWet);
    }
    l = io[0]; r = io[1];
}

void FxBus::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    constexpr float k = 1.0f / 32768.0f;
    switch (s.type)
    {
        case FxType::Off:
        default:
            for (int i = 0; i < n; ++i) { outL[i] += inL[i] * k; outR[i] += inR[i] * k; }
            return;

        case FxType::Distortion:
            for (int i = 0; i < n; ++i)
            {
                float l = inL[i], r = inR[i];
                processDistortion (l, r);
                outL[i] += l * k; outR[i] += r * k;
            }
            return;

        case FxType::RingMod:
        {
            int16_t ob[256];
            int done = 0;
            while (done < n)
            {
                const int m = std::min (256, n - done);
                ringOsc.block (ob, m, 1.f, 0.3f, rng);
                for (int i = 0; i < m; ++i)
                {
                    const float l = inL[done + i], r = inR[done + i];
                    const float wl = (float) (((int32_t) l * (int32_t) ob[i]) >> 16);
                    const float wr = (float) (((int32_t) r * (int32_t) ob[i]) >> 16);
                    outL[done + i] += (l + ssat ((wl - l) * s.dryWet)) * k;
                    outR[done + i] += (r + ssat ((wr - r) * s.dryWet)) * k;
                }
                done += m;
            }
            return;
        }

        case FxType::Compressor:
            for (int i = 0; i < n; ++i)
            {
                const float l = inL[i], r = inR[i];
                const double in = 0.5 * (l / 32768.0) + 0.5 * (r / 32768.0);
                const double peakdB = std::log (std::fabs (in) + 1.0E-25) * 8.6858896380650365530225783783321;
                double over = peakdB - thresh;
                if (over < 0.0) over = 0.0;
                const double coeff = over > env ? attack : release;
                env = over + coeff * (env - over);
                const double gr = std::exp (env * (ratioV - 1.0) * 0.11512925464970228420089957273422);
                const float wl = ssat ((float) (l * gr * markup)), wr = ssat ((float) (r * gr * markup));
                outL[i] += (l + ssat ((wl - l) * s.dryWet)) * k;
                outR[i] += (r + ssat ((wr - r) * s.dryWet)) * k;
            }
            return;

        case FxType::Delay:
        {
            const uint32_t half = (uint32_t) buf.size() / 2;
            auto read = [this] (uint32_t& pos, float frac, uint32_t offset) // Delay_calcBuffer
            {
                const uint32_t p2 = (pos + 1) & mask;
                const float v = (buf[offset + pos] * (1 - frac) + buf[offset + p2] * frac) * 32767.f;
                pos = (pos + 1) & mask;
                return (float) (int16_t) ssat (v);
            };
            for (int i = 0; i < n; ++i)
            {
                const float l = inL[i], r = inR[i];
                float oL, oR;
                if (interpValue <= 0.f)
                {
                    uint32_t p = oldRead;
                    oL = read (p, fracOld, 0);
                    oR = s.pingPong ? read (oldRead, fracOld, half) : oL;
                    oldRead = p;
                    if (timePending) { timePending = false; setDelayTime (pendingTime); }
                }
                else
                {
                    uint32_t po = oldRead, pn = newRead;
                    const float aL = read (po, fracOld, 0), bL = read (pn, fracNew, 0);
                    float aR = aL, bR = bL;
                    if (s.pingPong) { aR = read (oldRead, fracOld, half); bR = read (newRead, fracNew, half); }
                    oldRead = po; newRead = pn;
                    oL = aL * interpValue + bL * (1.f - interpValue);
                    oR = aR * interpValue + bR * (1.f - interpValue);
                    interpValue -= interpStep;
                    if (interpValue <= 0.f) { oldRead = newRead; fracOld = fracNew; }
                }
                oL = tone.process (oL);
                oR = s.pingPong ? tone2.process (oR) : oL;
                if (s.pingPong)
                {
                    const float vL = std::clamp ((float) qadd16 ((int16_t) ssat (l), (int16_t) ssat (oL * feedback)) / 32767.f, -1.f, 1.f);
                    const float vR = std::clamp ((float) qadd16 ((int16_t) ssat (r), (int16_t) ssat (oR * feedback)) / 32767.f, -1.f, 1.f);
                    buf[half + write] = toHalf (vL);
                    buf[write] = toHalf (vR);
                }
                else
                {
                    const int16_t sum = qadd16 (qadd16 ((int16_t) ssat (l), (int16_t) ssat (r)), (int16_t) ssat (oL * feedback));
                    buf[write] = toHalf (std::clamp (sum / 32767.f, -1.f, 1.f));
                }
                write = (write + 1) & mask;
                outL[i] += ssat (l + ssat ((oL - l) * s.dryWet)) * k;
                outR[i] += ssat (r + ssat ((oR - r) * s.dryWet)) * k;
            }
            return;
        }
    }
}

const char* FxBus::paramName (FxType t, int idx)
{
    static const char* dist[4] = { "Drive", "Color", "Tone", "Volume" };
    static const char* ring[4] = { "Freq", nullptr, nullptr, nullptr };
    static const char* comp[4] = { "Attack", "Release", "Threshold", "Gain" };
    static const char* del[4]  = { "Time", "Range", "Tone", "Feedback" };
    if (idx < 0 || idx > 3) return nullptr;
    switch (t)
    {
        case FxType::Distortion: return dist[idx];
        case FxType::RingMod:    return ring[idx];
        case FxType::Compressor: return comp[idx];
        case FxType::Delay:      return del[idx];
        default: return nullptr;
    }
}

std::string FxBus::formatParam (FxType t, int idx, float v)
{
    char b[48];
    switch (t)
    {
        case FxType::RingMod: std::snprintf (b, sizeof b, "%.0f Hz", v * 880.0); break;
        case FxType::Compressor:
            if (idx == 0) std::snprintf (b, sizeof b, "%.1f ms", 0.03 + v * 100.0);
            else if (idx == 1) std::snprintf (b, sizeof b, "%.0f ms", 25.0 + v * 175.0);
            else if (idx == 2) std::snprintf (b, sizeof b, "%.1f dB", -96.0 + v * 96.0);
            else std::snprintf (b, sizeof b, "+%.1f dB", 20.0 * std::log10 (1.0 + v * 2.0));
            break;
        default: std::snprintf (b, sizeof b, "%.0f%%", v * 100.0); break;
    }
    return b;
}
} // namespace ferrum
