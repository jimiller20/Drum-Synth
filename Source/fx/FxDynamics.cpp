// FxDynamics.cpp - Glue (bus compressor), Shaper (transients), Ladder (filter), Program EQ, Utility
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
// Glue: feed-forward VCA compressor, stereo-linked, soft knee, side-chain HPF, auto release
class Glue : public Module
{
public:
    enum { kThresh, kRatio, kAttack, kRelease, kMakeup, kScHpf, kMix };
    void prepare (double s) override { sr = s; cache.first = true; reset(); }
    void reset() override { gr = 0; avgGr = 0; sc.reset(); }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams))
        {
            constexpr Type T = Type::Glue;
            thresh = val (T, p, kThresh);
            static const float ratios[3] = { 2, 4, 10 };
            slope = 1.f / ratios[idx (T, p, kRatio)] - 1.f;
            static const float attMs[6] = { 0.1f, 0.3f, 1, 3, 10, 30 };
            static const float relS[5] = { 0.1f, 0.3f, 0.6f, 1.2f, 0.f };
            att = (float) std::exp (-1.0 / (sr * attMs[idx (T, p, kAttack)] * 0.001));
            autoRel = idx (T, p, kRelease) == 4;
            rel = (float) std::exp (-1.0 / (sr * (autoRel ? 0.3 : relS[idx (T, p, kRelease)])));
            makeup = val (T, p, kMakeup);
            static const float hpf[4] = { 0, 60, 90, 150 };
            scOn = idx (T, p, kScHpf) > 0;
            if (scOn) sc.highpass (sr, hpf[idx (T, p, kScHpf)], 0.7);
            mixAmt = p[kMix];
            avgCoef = (float) std::exp (-1.0 / (sr * 0.5));
        }
        constexpr float knee = 6.f;
        for (int i = 0; i < n; ++i)
        {
            float x = 0.5f * (L[i] + R[i]);
            if (scOn) x = sc.process (x);
            const float level = gainToDb (std::fabs (x) + 1.0e-9f);
            const float over = level - thresh;
            float target = 0.f;
            if (2.f * over > knee) target = slope * over;
            else if (2.f * over > -knee) target = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.f * knee);
            float r = rel;
            if (autoRel) // longer release the harder it works (program dependent)
            {
                avgGr = avgCoef * avgGr + (1.f - avgCoef) * gr;
                const float secs = 0.12f + 1.1f * std::clamp (-avgGr / 12.f, 0.f, 1.f);
                r = (float) std::exp (-1.0 / (sr * secs));
            }
            gr = target < gr ? target + att * (gr - target) : target + r * (gr - target);
            const float g = dbToGain (gr + makeup);
            L[i] += (L[i] * g - L[i]) * mixAmt;
            R[i] += (R[i] * g - R[i]) * mixAmt;
        }
    }
    float gainReductionDb() const { return gr; }

private:
    ParamCache cache;
    Biquad sc;
    bool scOn = true, autoRel = true;
    float thresh = -16, slope = -0.5f, att = 0.99f, rel = 0.999f, makeup = 0, mixAmt = 1, gr = 0, avgGr = 0, avgCoef = 0.9999f;
};

//=============================================================================================
// Shaper: compares a fast and a slow envelope to find attacks, and a held envelope for sustain
class Shaper : public Module
{
public:
    enum { kAttack, kSustain, kSpeed, kOutput, kMix };
    void prepare (double s) override { sr = s; cache.first = true; reset(); }
    void reset() override { fast.reset(); slow.reset(); hold.reset(); gs.snap (1.f); }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams))
        {
            constexpr Type T = Type::Shaper;
            attackAmt = val (T, p, kAttack) / 100.f;
            sustainAmt = val (T, p, kSustain) / 100.f;
            static const float spd[3] = { 0.5f, 1.f, 2.f };
            const float k = spd[idx (T, p, kSpeed)];
            fast.set (sr, 0.3 * k, 40.0 * k);
            slow.set (sr, 18.0 * k, 40.0 * k);
            hold.set (sr, 0.3 * k, 350.0 * k);
            out = dbToGain (val (T, p, kOutput));
            mixAmt = p[kMix];
            gs.setTime (sr, 1.0);
        }
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::fabs (L[i]), std::fabs (R[i]));
            const float f = fast.process (x), s = slow.process (x), h = hold.process (x);
            const float transient = std::clamp ((f - s) / (f + 1.0e-6f), 0.f, 1.f);
            const float tail = std::clamp ((h - f) / (h + 1.0e-6f), 0.f, 1.f);
            gs.target = dbToGain (attackAmt * 18.f * transient + sustainAmt * 18.f * tail);
            const float g = gs.next() * out;
            L[i] += (L[i] * g - L[i]) * mixAmt;
            R[i] += (R[i] * g - R[i]) * mixAmt;
        }
    }

private:
    ParamCache cache;
    Envelope fast, slow, hold;
    Smooth gs;
    float attackAmt = 0, sustainAmt = 0, out = 1, mixAmt = 1;
};

//=============================================================================================
// Ladder: zero-delay-feedback 4-pole ladder with a tanh input stage and pole-mixing modes
class Ladder : public Module
{
public:
    enum { kMode, kCutoff, kReso, kDrive, kEnv, kEnvSpeed, kMix };
    void prepare (double s) override { sr = s; cache.first = true; reset(); }
    void reset() override { for (auto& c : st) for (auto& v : c) v = 0; env.reset(); counter = 0; }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams))
        {
            constexpr Type T = Type::Ladder;
            mode = idx (T, p, kMode);
            baseFc = val (T, p, kCutoff);
            k = 3.9f * p[kReso];
            drive = dbToGain (val (T, p, kDrive));
            envAmt = val (T, p, kEnv) / 100.f;
            static const float att[3] = { 1, 5, 20 }, rel[3] = { 60, 180, 600 };
            const int sp = idx (T, p, kEnvSpeed);
            env.set (sr, att[sp], rel[sp]);
            mixAmt = p[kMix];
            counter = 0;
        }
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            const float e = env.process (0.5f * (L[i] + R[i]));
            if (counter-- <= 0) // update the cutoff every 16 samples
            {
                counter = 15;
                const double fc = std::clamp (baseFc * std::pow (2.0, (double) envAmt * 5.0 * std::min (1.f, e * 2.f)), 20.0, sr * 0.45);
                const double g = std::tan (kPi * fc / sr);
                G = (float) (g / (1.0 + g));
            }
            for (int c = 0; c < 2; ++c)
            {
                auto& s = st[c];
                const float x = io[c][i];
                const float in = x * drive;
                const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
                const float sigma = (1.f - G) * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);
                const float y4est = (G4 * in + sigma) / (1.f + k * G4);
                float u = tanhFast (in - k * y4est);
                float y[4];
                for (int j = 0; j < 4; ++j)
                {
                    const float v = (u - s[j]) * G;
                    y[j] = v + s[j];
                    s[j] = flush (y[j] + v);
                    u = y[j];
                }
                const float uIn = tanhFast (in - k * y[3]);
                float out;
                switch (mode)
                {
                    case 0: out = y[3] * (1.f + k * 0.35f); break;
                    case 1: out = y[1] * (1.f + k * 0.25f); break;
                    case 2: out = 2.f * (y[0] - y[1]) * (1.f + k * 0.2f); break;
                    default: out = uIn - 2.f * y[0] + y[1]; break;
                }
                out /= std::max (1.f, std::sqrt (drive));
                io[c][i] = x + (out - x) * mixAmt;
            }
        }
    }

private:
    ParamCache cache;
    Envelope env;
    float st[2][4] {};
    int mode = 0, counter = 0;
    double baseFc = 2000;
    float k = 0, drive = 1, envAmt = 0, mixAmt = 1, G = 0.2f;
};

//=============================================================================================
// Program EQ: broad low shelf boost and a slightly higher attenuation shelf (the bump-and-dip
// trick), a peaking high boost with bandwidth, a high attenuation shelf, gentle tube make-up
class Eq : public Module
{
public:
    enum { kLowFreq, kLowBoost, kLowAtten, kHighFreq, kHighBoost, kBandwidth, kHighAtten, kOutput };
    void prepare (double s) override { sr = s; cache.first = true; reset(); }
    void reset() override { for (auto& c : f) for (auto& b : c) b.reset(); }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams))
        {
            constexpr Type T = Type::Eq;
            static const float lowF[4] = { 20, 30, 60, 100 }, highF[7] = { 3000, 4000, 5000, 8000, 10000, 12000, 16000 };
            const float lf = lowF[idx (T, p, kLowFreq)], hf = highF[idx (T, p, kHighFreq)];
            const float q = 2.2f - 1.8f * p[kBandwidth];
            out = dbToGain (val (T, p, kOutput));
            for (auto& c : f)
            {
                c[0].lowShelf (sr, lf, 1.35 * val (T, p, kLowBoost), 0.55);
                c[1].lowShelf (sr, lf * 1.8, -1.75 * val (T, p, kLowAtten), 0.7);
                c[2].peak (sr, hf, q, 1.8 * val (T, p, kHighBoost));
                c[3].highShelf (sr, 10000, -2.0 * val (T, p, kHighAtten), 0.7);
            }
        }
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                float y = io[c][i];
                for (auto& b : f[c]) y = b.process (y);
                io[c][i] = 2.f * tanhFast (0.5f * y * out); // transparent at normal levels, rounds the peaks
            }
    }

private:
    ParamCache cache;
    std::array<std::array<Biquad, 4>, 2> f;
    float out = 1;
};

//=============================================================================================
class Utility : public Module
{
public:
    enum { kGain, kPan, kWidth, kLowCut, kHighCut, kMonoBelow, kPolarity };
    void prepare (double s) override { sr = s; cache.first = true; primed = false; reset(); gainS.setTime (sr, 10); gainS.snap (1.f); }
    void reset() override { for (auto& b : lo) b.reset(); for (auto& b : hi) b.reset(); for (auto& b : mono) b.reset(); }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams))
        {
            constexpr Type T = Type::Utility;
            gainS.target = dbToGain (val (T, p, kGain));
            const float pan = val (T, p, kPan) / 100.f;
            panL = std::min (1.f, 1.f - pan); panR = std::min (1.f, 1.f + pan);
            width = val (T, p, kWidth) / 100.f;
            loOn = p[kLowCut] > 0.005f; hiOn = p[kHighCut] < 0.995f; monoOn = p[kMonoBelow] > 0.005f;
            for (auto& b : lo) b.highpass (sr, val (T, p, kLowCut), 0.707);
            for (auto& b : hi) b.lowpass (sr, val (T, p, kHighCut), 0.707);
            for (auto& b : mono) b.lowpass (sr, val (T, p, kMonoBelow), 0.707);
            const int pol = idx (T, p, kPolarity);
            polL = (pol == 1 || pol == 3) ? -1.f : 1.f;
            polR = (pol == 2 || pol == 3) ? -1.f : 1.f;
            if (! primed) { gainS.snap (gainS.target); primed = true; } // a new Utility starts at its gain, no fade
        }
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (loOn) { l = lo[0].process (l); r = lo[1].process (r); }
            if (hiOn) { l = hi[0].process (l); r = hi[1].process (r); }
            if (monoOn)
            {
                const float ll = mono[0].process (l), lr = mono[1].process (r), m = 0.5f * (ll + lr);
                l = l - ll + m; r = r - lr + m;
            }
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
            const float g = gainS.next();
            L[i] = (mid + side) * g * panL * polL;
            R[i] = (mid - side) * g * panR * polR;
        }
    }

private:
    ParamCache cache;
    Smooth gainS;
    bool primed = false;
    Biquad lo[2], hi[2], mono[2];
    bool loOn = false, hiOn = false, monoOn = false;
    float panL = 1, panR = 1, width = 1, polL = 1, polR = 1;
};
} // namespace

std::unique_ptr<Module> makeGlue() { return std::make_unique<Glue>(); }
std::unique_ptr<Module> makeShaper() { return std::make_unique<Shaper>(); }
std::unique_ptr<Module> makeLadder() { return std::make_unique<Ladder>(); }
std::unique_ptr<Module> makeEq() { return std::make_unique<Eq>(); }
std::unique_ptr<Module> makeUtility() { return std::make_unique<Utility>(); }
} // namespace ferrum::fx
