// FxSpace.cpp - Echo (bucket brigade), Reverb (8-line FDN), Spring, Chorus / Ensemble
// SPDX-License-Identifier: GPL-3.0-only
#include "FxFactories.h"
#include "FxDsp.h"
#include <array>

namespace ferrum::fx
{
using namespace dsp;

namespace
{
struct ParamCache
{
    float last[kMaxParams];
    bool first = true;
    bool changed (const float* p, int n)
    {
        bool c = first;
        for (int i = 0; i < n; ++i) if (last[i] != p[i]) { c = true; last[i] = p[i]; }
        first = false;
        return c;
    }
};
float val (Type t, const float* p, int i) { return paramValue (info (t).params[(size_t) i], p[i]); }
int idx (Type t, const float* p, int i) { return choiceIndex (info (t).params[(size_t) i], p[i]); }

//=============================================================================================
// Echo: each repeat passes a low-pass (the BBD's anti-alias filters), a high-pass, a soft
// saturation and (with Age) noise and a narrower band. The clock wobbles (Mod) and glides when
// the time changes, like the real thing.
class Echo : public Module
{
public:
    enum { kSync, kTime, kFeedback, kTone, kMod, kAge, kPing, kMix };
    void prepare (double s) override
    {
        sr = s;
        for (auto& c : ch) c.line.setMax ((int) (sr * 4.2));
        timeS.setTime (sr, 90.0);
        drift.set (sr, 0.8);
        cache.first = true;
        reset();
    }
    void reset() override
    {
        for (auto& c : ch) { c.line.reset(); c.lp1.reset(); c.lp2.reset(); c.hp.reset(); c.last = 0; }
        timeS.snap ((float) (sr * 0.3)); lfo = 0;
    }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams)) update (p);
        static const float beats[10] = { 0, 0.125f, 1.f / 6.f, 0.25f, 1.f / 3.f, 0.5f, 0.75f, 1.f, 1.5f, 2.f };
        const float seconds = sync == 0 ? freeMs * 0.001f : beats[sync] * 60.f / (float) bpm;
        timeS.target = std::clamp (seconds, 0.005f, 4.f) * (float) sr;
        for (int i = 0; i < n; ++i)
        {
            lfo += lfoRate / (float) sr; if (lfo > 1.f) lfo -= 1.f;
            const float wobble = modDepth * (std::sin (6.2831853f * lfo) + 0.5f * drift.next());
            const float d = timeS.next() + wobble;
            const float inL = L[i], inR = R[i];
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                float y = s.line.readCubic (d);
                y = s.hp.process (s.lp2.process (s.lp1.process (y)));
                y = tanhFast (y * 1.1f) / 1.1f + ageNoise * noise.white();
                s.last = y;
                out[c] = y;
            }
            if (ping)
            {
                const float mono = 0.5f * (inL + inR);
                ch[0].line.push (flush (mono + feedback * ch[1].last));
                ch[1].line.push (flush (feedback * ch[0].last));
            }
            else
            {
                ch[0].line.push (flush (inL + feedback * ch[0].last));
                ch[1].line.push (flush (inR + feedback * ch[1].last));
            }
            L[i] = inL + (out[0] - inL) * mixAmt;
            R[i] = inR + (out[1] - inR) * mixAmt;
        }
    }

private:
    struct Chan { DelayLine line; Biquad lp1, lp2, hp; float last = 0; };
    void update (const float* p)
    {
        constexpr Type T = Type::Echo;
        sync = idx (T, p, kSync);
        freeMs = val (T, p, kTime);
        feedback = val (T, p, kFeedback) / 100.f;
        const float age = p[kAge];
        const double tone = val (T, p, kTone) * (1.0 - 0.5 * age);
        modDepth = p[kMod] * (float) sr * 0.0018f;
        lfoRate = 0.4f + 2.5f * p[kMod];
        ageNoise = age > 0.001f ? dbToGain (-82.f + 30.f * age) : 0.f;
        ping = idx (T, p, kPing) == 1;
        mixAmt = p[kMix];
        for (auto& c : ch) { c.lp1.lowpass (sr, tone, 0.6); c.lp2.lowpass (sr, tone * 1.4, 0.55); c.hp.highpass (sr, 80 + 200 * age, 0.6); }
    }
    std::array<Chan, 2> ch;
    ParamCache cache;
    Smooth timeS;
    Drift drift;
    Noise noise;
    int sync = 5;
    bool ping = false;
    float freeMs = 300, feedback = 0.4f, modDepth = 0, lfoRate = 0.5f, ageNoise = 0, mixAmt = 0.3f, lfo = 0;
};

//=============================================================================================
// Reverb: pre-delay, input diffusion (four allpasses), then an 8-line feedback delay network with
// a Hadamard matrix, per-line damping and gain set from the decay time, two modulated lines.
class Reverb : public Module
{
public:
    enum { kType, kSize, kDecay, kDamping, kPredelay, kMod, kWidth, kMix };
    static constexpr int N = 8;
    void prepare (double s) override
    {
        sr = s;
        for (auto& l : lines) l.setMax ((int) (sr * 0.2));
        pre.setMax ((int) (sr * 0.21));
        const int apLen[4] = { 142, 107, 379, 277 };
        for (int i = 0; i < 4; ++i) diff[i].setup ((int) (apLen[i] * sr / 44100.0), 0.7f);
        inHp.highpass (sr, 70, 0.7);
        cache.first = true;
        reset();
    }
    void reset() override
    {
        for (auto& l : lines) l.reset();
        for (auto& d : damp) d.reset();
        for (auto& a : diff) a.reset();
        pre.reset(); inHp.reset(); phase = 0;
    }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams)) update (p);
        for (int i = 0; i < n; ++i)
        {
            phase += 0.73f / (float) sr; if (phase > 1.f) phase -= 1.f;
            const float m0 = modDepth * std::sin (6.2831853f * phase), m1 = modDepth * std::sin (6.2831853f * (phase + 0.37f));
            pre.push (flush (0.5f * (L[i] + R[i])));
            float x = inHp.process (pre.read (predelay));
            for (auto& a : diff) x = a.process (x);
            float y[N], tapOut[N];
            for (int k = 0; k < N; ++k)
            {
                const float d = len[k] + (k == 0 ? m0 : (k == 4 ? m1 : 0.f));
                tapOut[k] = damp[k].lp (lines[(size_t) k].readCubic (d));
                y[k] = tapOut[k] * gain[k];
            }
            // fast Walsh-Hadamard transform (orthogonal mixing)
            for (int h = 1; h < N; h <<= 1)
                for (int a = 0; a < N; a += h * 2)
                    for (int b = a; b < a + h; ++b) { const float u = y[b], v = y[b + h]; y[b] = u + v; y[b + h] = u - v; }
            constexpr float norm = 0.35355339f; // 1/sqrt(8)
            for (int k = 0; k < N; ++k) lines[(size_t) k].push (flush (y[k] * norm + (k & 1 ? -x : x) * inGain));
            static const float outL[N] = { 1.f, -1.f, 1.f, 1.f, -0.7f, 0.7f, -0.7f, 0.7f };
            static const float outR[N] = { 0.7f, 0.7f, -0.7f, 0.7f, 1.f, 1.f, -1.f, -1.f };
            float l = 0, r = 0;
            for (int k = 0; k < N; ++k) { l += outL[k] * tapOut[k]; r += outR[k] * tapOut[k]; }
            l *= 0.6f; r *= 0.6f; // about unity energy at 100 % wet
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
            L[i] += (mid + side - L[i]) * mixAmt;
            R[i] += (mid - side - R[i]) * mixAmt;
        }
    }

private:
    void update (const float* p)
    {
        constexpr Type T = Type::Reverb;
        static const float base[4][N] = {
            { 13.7f, 17.9f, 21.1f, 25.3f, 29.9f, 33.1f, 37.7f, 41.3f },
            { 9.3f, 11.7f, 13.9f, 16.1f, 18.3f, 21.7f, 24.1f, 27.9f },
            { 29.7f, 37.1f, 41.1f, 47.3f, 53.9f, 61.7f, 67.1f, 73.3f },
            { 5.3f, 6.7f, 7.9f, 9.1f, 10.3f, 11.9f, 13.1f, 14.7f } };
        const int type = idx (T, p, kType);
        const float size = 0.6f + p[kSize];
        const float t60 = val (T, p, kDecay) * (type == 3 ? 0.4f : 1.f);
        const double dampF = 18000.0 * (1.0 - 0.85 * p[kDamping]);
        for (int k = 0; k < N; ++k)
        {
            len[k] = (float) (base[type][k] * size * 0.001 * sr);
            gain[k] = std::pow (10.f, -3.f * len[k] / (t60 * (float) sr));
            damp[k].setLowpass (sr, dampF);
        }
        inGain = type == 0 ? 0.55f : 0.45f;
        for (auto& a : diff) a.g = type == 0 ? 0.72f : (type == 3 ? 0.55f : 0.65f);
        predelay = val (T, p, kPredelay) * 0.001f * (float) sr;
        modDepth = p[kMod] * (float) sr * 0.0007f;
        width = p[kWidth];
        mixAmt = p[kMix];
    }
    std::array<DelayLine, N> lines;
    std::array<OnePole, N> damp;
    std::array<Allpass, 4> diff;
    DelayLine pre;
    Biquad inHp;
    ParamCache cache;
    float len[N] {}, gain[N] {}, inGain = 0.5f, predelay = 0, modDepth = 0, width = 1, mixAmt = 0.25f, phase = 0;
};

//=============================================================================================
// Spring: per spring a delay loop with a cascade of "stretched" allpasses (z^-2) that disperse
// high frequencies (the chirp), a tone low-pass and a high-pass (springs carry little bass)
class Spring : public Module
{
public:
    enum { kSprings, kDecay, kTension, kDrip, kTone, kMix };
    static constexpr int kStages = 14;
    void prepare (double s) override
    {
        sr = s;
        for (auto& sp : springs) sp.line.setMax ((int) (sr * 0.08));
        inHp.highpass (sr, 140, 0.7);
        cache.first = true;
        reset();
    }
    void reset() override
    {
        for (auto& sp : springs) { sp.line.reset(); sp.lp.reset(); sp.hp.reset(); for (auto& a : sp.ap) a = {}; sp.last = 0; }
        inHp.reset();
    }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams)) update (p);
        for (int i = 0; i < n; ++i)
        {
            const float in = inHp.process (0.5f * (L[i] + R[i])) * 0.7f;
            float outs[3] {};
            for (int k = 0; k < count; ++k)
            {
                auto& sp = springs[(size_t) k];
                float y = sp.line.readCubic (sp.len);
                for (int j = 0; j < activeStages; ++j) y = sp.ap[(size_t) j].process (y, apCoef);
                y = sp.hp.process (sp.lp.process (y));
                sp.last = y;
                sp.line.push (flush (tanhFast (in + fb * y)));
                outs[k] = y;
            }
            float wl = outs[0], wr = count > 1 ? outs[1] : outs[0];
            if (count > 2) { wl += 0.6f * outs[2]; wr += 0.6f * outs[2]; }
            wl *= 1.6f; wr *= 1.6f; // wet level comparable to the dry signal
            L[i] += (wl - L[i]) * mixAmt;
            R[i] += (wr - R[i]) * mixAmt;
        }
    }

private:
    struct Ap2 // y = a x + x[n-2] - a y[n-2]
    {
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float process (float x, float a) { const float y = a * x + x2 - a * y2; x2 = x1; x1 = x; y2 = y1; y1 = flush (y); return y; }
    };
    struct SpringLine { DelayLine line; std::array<Ap2, kStages> ap; Biquad lp, hp; float len = 1000, last = 0; };
    void update (const float* p)
    {
        constexpr Type T = Type::Spring;
        count = idx (T, p, kSprings) + 1;
        const float tension = p[kTension];
        static const float baseMs[3] = { 33.7f, 41.9f, 37.3f };
        const float t60 = val (T, p, kDecay);
        for (int k = 0; k < 3; ++k)
        {
            auto& sp = springs[(size_t) k];
            sp.len = baseMs[k] * (1.25f - 0.5f * tension) * 0.001f * (float) sr;
            sp.lp.lowpass (sr, val (T, p, kTone), 0.7);
            sp.hp.highpass (sr, 180, 0.6);
        }
        const float loopMs = baseMs[1] * (1.25f - 0.5f * tension);
        fb = std::pow (10.f, -3.f * loopMs * 0.001f / t60);
        apCoef = -(0.45f + 0.35f * tension);
        activeStages = 4 + (int) std::lround (p[kDrip] * (kStages - 4));
        mixAmt = p[kMix];
    }
    std::array<SpringLine, 3> springs;
    Biquad inHp;
    ParamCache cache;
    int count = 2, activeStages = 10;
    float fb = 0.8f, apCoef = -0.6f, mixAmt = 0.3f;
};

//=============================================================================================
// Chorus: modulated short delays with a BBD-style low-pass on the wet signal
class Chorus : public Module
{
public:
    enum { kMode, kRate, kDepth, kWidth, kTone, kMix };
    void prepare (double s) override { sr = s; for (auto& l : line) l.setMax ((int) (sr * 0.05)); cache.first = true; reset(); }
    void reset() override { for (auto& l : line) l.reset(); for (auto& f : tone) f.reset(); phase = 0; phase2 = 0; }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams))
        {
            constexpr Type T = Type::Chorus;
            mode = idx (T, p, kMode);
            rate = val (T, p, kRate) * (mode == 1 ? 1.6f : 1.f);
            depth = p[kDepth] * (mode == 1 ? 1.4f : 1.f) * (float) sr * 0.003f;
            base = (float) sr * (mode == 3 ? 0.010f : 0.007f);
            width = p[kWidth];
            for (auto& f : tone) f.lowpass (sr, val (T, p, kTone), 0.6);
            mixAmt = p[kMix];
        }
        auto tri = [] (float ph) { ph -= std::floor (ph); return ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph; };
        for (int i = 0; i < n; ++i)
        {
            phase += rate / (float) sr; if (phase > 1.f) phase -= 1.f;
            phase2 += 6.1f / (float) sr; if (phase2 > 1.f) phase2 -= 1.f;
            line[0].push (L[i]); line[1].push (R[i]);
            float wl, wr;
            if (mode == 3) // ensemble: three voices 120 degrees apart plus a fast shimmer
            {
                const float sh = 0.12f * depth * std::sin (6.2831853f * phase2);
                float accL = 0, accR = 0;
                for (int v = 0; v < 3; ++v)
                {
                    const float ph = phase + (float) v / 3.f;
                    accL += line[0].readCubic (base + depth * tri (ph) + sh);
                    accR += line[1].readCubic (base + depth * tri (ph + 0.5f) - sh);
                }
                wl = accL / 3.f; wr = accR / 3.f;
            }
            else
            {
                float m = tri (phase);
                if (mode == 2) m = 0.6f * m + 0.4f * std::sin (6.2831853f * phase * 7.f);
                wl = line[0].readCubic (base + depth * m);
                wr = line[1].readCubic (base - depth * m);
            }
            wl = tone[0].process (wl); wr = tone[1].process (wr);
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            L[i] += (mid + side - L[i]) * mixAmt;
            R[i] += (mid - side - R[i]) * mixAmt;
        }
    }

private:
    std::array<DelayLine, 2> line;
    std::array<Biquad, 2> tone;
    ParamCache cache;
    int mode = 0;
    float rate = 0.5f, depth = 1, base = 300, width = 1, mixAmt = 0.5f, phase = 0, phase2 = 0;
};
} // namespace

std::unique_ptr<Module> makeEcho() { return std::make_unique<Echo>(); }
std::unique_ptr<Module> makeReverb() { return std::make_unique<Reverb>(); }
std::unique_ptr<Module> makeSpring() { return std::make_unique<Spring>(); }
std::unique_ptr<Module> makeChorus() { return std::make_unique<Chorus>(); }
} // namespace ferrum::fx
