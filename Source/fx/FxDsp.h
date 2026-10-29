// FxDsp.h - small DSP building blocks shared by the FX modules
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ferrum::fx::dsp
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return std::pow (10.f, db / 20.f); }
inline float gainToDb (float g) { return 20.f * std::log10 (std::max (g, 1.0e-9f)); }
inline float flush (float x) { return std::fabs (x) < 1.0e-20f ? 0.f : x; } // keep denormals out of feedback paths

// parameter smoothing: a one-pole glide towards the target
struct Smooth
{
    float v = 0.f, target = 0.f, coef = 0.f;
    void setTime (double sr, double ms) { coef = (float) std::exp (-1.0 / std::max (1.0, sr * ms * 0.001)); }
    void snap (float x) { v = target = x; }
    float next() { v = target + coef * (v - target); return v; }
};

// RBJ biquad, transposed direct form II
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = flush (b1 * x - a1 * y + z2);
        z2 = flush (b2 * x - a2 * y);
        return y;
    }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
    static double w0 (double sr, double f) { return 2.0 * kPi * std::clamp (f, 5.0, sr * 0.49) / sr; }
    void lowpass (double sr, double f, double q)
    {
        const double w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double sr, double f, double q)
    {
        const double w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandpass (double sr, double f, double q) // 0 dB peak
    {
        const double w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void notch (double sr, double f, double q)
    {
        const double w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1, -2 * c, 1, 1 + al, -2 * c, 1 - al);
    }
    void peak (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double sr, double f, double db, double q = 0.707)
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
        const double s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
             (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
    }
    void highShelf (double sr, double f, double db, double q = 0.707)
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
        const double s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
             (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }
    void bypass() { set (1, 0, 0, 1, 0, 0); }
};

struct OnePole
{
    float a = 1.f, z = 0.f;
    void setLowpass (double sr, double f) { a = (float) (1.0 - std::exp (-2.0 * kPi * std::clamp (f, 1.0, sr * 0.49) / sr)); }
    float lp (float x) { z = flush (z + a * (x - z)); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0.f; }
};

struct DcBlocker
{
    float x1 = 0, y1 = 0, r = 0.9995f;
    void set (double sr, double f = 8.0) { r = (float) std::exp (-2.0 * kPi * f / sr); }
    float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = flush (y); return y; }
    void reset() { x1 = y1 = 0; }
};

// first-order tilt around a pivot: < 0 darker, > 0 brighter
struct Tilt
{
    OnePole split;
    float gl = 1.f, gh = 1.f;
    void set (double sr, double pivot, float db) { split.setLowpass (sr, pivot); gl = dbToGain (-db * 0.5f); gh = dbToGain (db * 0.5f); }
    float process (float x) { const float l = split.lp (x); return l * gl + (x - l) * gh; }
};

// interpolating delay line
struct DelayLine
{
    std::vector<float> buf;
    int w = 0, mask = 0;
    void setMax (int samples)
    {
        int n = 1;
        while (n < samples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.f);
        mask = n - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.f); }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float tap (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
    float read (float d) const // linear interpolation, d in samples (>= 0)
    {
        d = std::clamp (d, 0.f, (float) (mask - 2));
        const int i = (int) d;
        const float f = d - (float) i;
        return tap (i) + (tap (i + 1) - tap (i)) * f;
    }
    float readCubic (float d) const
    {
        d = std::clamp (d, 1.f, (float) (mask - 3));
        const int i = (int) d;
        const float f = d - (float) i;
        const float y0 = tap (i - 1), y1 = tap (i), y2 = tap (i + 1), y3 = tap (i + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

// Schroeder allpass on a delay line
struct Allpass
{
    DelayLine d;
    int len = 1;
    float g = 0.5f;
    void setup (int samples, float gain) { d.setMax (samples + 2); len = std::max (1, samples); g = gain; }
    float process (float x)
    {
        const float delayed = d.tap (len - 1);
        const float v = x + g * delayed;
        d.push (flush (v));
        return delayed - g * v;
    }
    void reset() { d.reset(); }
};

// linear-phase 2x oversampler (windowed-sinc half-band FIR); latency = kLatency base-rate samples
class Oversampler2x
{
public:
    static constexpr int kTaps = 47;
    static constexpr int kLatency = (kTaps - 1) / 2; // up + down at 2x = 46 fast samples = 23 base samples
    Oversampler2x()
    {
        const int M = kTaps - 1;
        double sum = 0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double x = n - M / 2.0, fc = 0.23; // cutoff relative to the 2x rate
            const double sinc = x == 0 ? 2 * fc : std::sin (2 * kPi * fc * x) / (kPi * x);
            const double win = 0.35875 - 0.48829 * std::cos (2 * kPi * n / M) + 0.14128 * std::cos (4 * kPi * n / M) - 0.01168 * std::cos (6 * kPi * n / M);
            h[n] = sinc * win;
            sum += h[n];
        }
        for (int k = 0; k < kTaps; ++k) { h[k] /= sum; hf[k] = (float) h[k]; }
        reset();
    }
    void reset() { std::fill (std::begin (up), std::end (up), 0.f); std::fill (std::begin (dn), std::end (dn), 0.f); pu = pd = 0; }
    void upsample (float x, float& o0, float& o1)
    {
        o0 = filter (up, pu, 2.f * x);
        o1 = filter (up, pu, 0.f);
    }
    float downsample (float a, float b)
    {
        push (dn, pd, a);
        return filter (dn, pd, b);
    }

private:
    // each sample is stored twice so the newest kTaps samples are always contiguous
    static void push (float* buf, int& p, float v) { buf[p] = buf[p + kTaps] = v; p = p + 1 == kTaps ? 0 : p + 1; }
    float filter (float* buf, int& p, float v)
    {
        push (buf, p, v);
        const float* w = buf + p; // oldest .. newest
        float y = 0;
        for (int k = 0; k < kTaps; ++k) y += hf[k] * w[k];
        return y;
    }
    double h[kTaps] {};
    float hf[kTaps] {};
    float up[2 * kTaps] {}, dn[2 * kTaps] {};
    int pu = 0, pd = 0;
};

// fixed delay used to keep a dry signal aligned with an oversampled wet signal
struct LatencyDelay
{
    std::vector<float> b;
    int p = 0;
    void set (int n) { b.assign ((size_t) std::max (1, n), 0.f); p = 0; }
    float process (float x) { const float y = b[(size_t) p]; b[(size_t) p] = x; p = (p + 1) % (int) b.size(); return y; }
    void reset() { std::fill (b.begin(), b.end(), 0.f); }
};

// envelope follower (peak, separate attack / release)
struct Envelope
{
    float env = 0, att = 0, rel = 0;
    void set (double sr, double attackMs, double releaseMs)
    {
        att = (float) std::exp (-1.0 / std::max (1.0, sr * attackMs * 0.001));
        rel = (float) std::exp (-1.0 / std::max (1.0, sr * releaseMs * 0.001));
    }
    float process (float x)
    {
        const float a = std::fabs (x);
        env = flush (a > env ? a + att * (env - a) : a + rel * (env - a));
        return env;
    }
    void reset() { env = 0; }
};

struct Noise
{
    uint32_t s = 0x1234567u;
    float white() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};

// a smoothed random walk (wow drift, ensemble motion)
struct Drift
{
    Noise n; OnePole f;
    void set (double sr, double hz) { f.setLowpass (sr, hz); }
    float next() { return f.lp (n.white()) * 4.f; }
};

inline float softClip (float x) { return x / (1.f + std::fabs (x)); }
inline float tanhFast (float x) // Pade approximant, reaches exactly +-1 at +-3 (|err| < 0.025)
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
} // namespace ferrum::fx::dsp
