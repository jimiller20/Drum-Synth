// FxDrive.cpp - Ferric (vintage saturation after Felt Instruments' Brut), Amp, Cabinet, Tape, Crusher
// SPDX-License-Identifier: GPL-3.0-only
#include "FxFactories.h"
#include "FxDsp.h"
#include <array>
#include <cstring>

namespace ferrum::fx
{
using namespace dsp;

namespace
{
// remembers the last parameter block so coefficients are only recomputed when something moved
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
const ParamSpec& spec (Type t, int i) { return info (t).params[(size_t) i]; }
float val (Type t, const float* p, int i) { return paramValue (spec (t, i), p[i]); }
int idx (Type t, const float* p, int i) { return choiceIndex (spec (t, i), p[i]); }

// TPT state-variable filter (stable at any setting), optional soft saturation of the band state
struct Svf
{
    float g = 0.1f, k = 1.4f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set (double sr, double f, float res)
    {
        g = (float) std::tan (kPi * std::clamp (f, 10.0, sr * 0.45) / sr);
        k = 2.f - 1.96f * std::clamp (res, 0.f, 1.f);
        a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void tick (float x, float& lp, float& bp, float& hp, bool saturate = false)
    {
        const float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        if (saturate) v1 = tanhFast (v1 * 0.8f) * 1.25f; // the bandpass' own non-linearity
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.f * v1 - ic1); ic2 = flush (2.f * v2 - ic2);
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0; }
};

//=============================================================================================
// Ferric. Six preamp characters (each a pre-EQ, a transfer curve, a post-EQ and its own dynamics),
// an optional resonant highpass or bandpass after the preamp, an analog-style feedback loop around the
// preamp (see loopStage), and a tilt EQ at the very end.
class Ferric : public Module
{
public:
    enum { kPreamp, kDrive, kFilter, kCutoff, kReso, kFeedback, kTilt, kMix };
    void prepare (double s) override
    {
        sr = s;
        for (auto& c : ch) { c.dry.set (Oversampler2x::kLatency); c.dc.set (sr, 12.0); }
        // feedback loop filters run at the oversampled rate: a coupling high-pass and a band limit
        const double osr = 2.0 * sr;
        loopHpA = (float) std::exp (-2.0 * 3.14159265358979 * 70.0 / osr);
        loopLpA = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 4200.0 / osr));
        cache.first = true;
        reset();
    }
    void reset() override
    {
        for (auto& c : ch)
        {
            c.os.reset(); c.dry.reset(); c.dc.reset(); c.svf.reset();
            c.fbState = c.fbHpX = c.fbHpY = c.fbLp = 0.f;
            c.fbRes.reset();
            c.pre1.reset(); c.pre2.reset(); c.post1.reset(); c.post2.reset(); c.env.reset(); c.slow.reset();
            c.slew = 0; c.tilt.split.reset();
        }
    }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams)) update (p);
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[c];
            float* x = io[c];
            for (int i = 0; i < n; ++i)
            {
                const float in = x[i];
                const float dry = s.dry.process (in);
                float u = s.pre2.process (s.pre1.process (in));
                // preamp dynamics
                const float e = s.env.process (u), slowE = s.slow.process (u);
                float g = drive;
                if (preamp == 1) g /= 1.f + 3.f * e * drive * 0.05f;              // Lab: squashes as it is driven
                if (preamp == 4) g *= 1.f / (1.f + 0.8f * slowE) + 0.35f * e;    // Germanium: punchy, sustain sags
                u *= g;
                float a, b;
                s.os.upsample (u, a, b);
                if (loopGain > 0.f) { a = loopStage (a, s); b = loopStage (b, s); }
                else { a = curve (a, s); b = curve (b, s); }
                float v = s.os.downsample (a, b);
                v = s.post2.process (s.post1.process (v));
                if (filterMode > 0)
                {
                    float lp, bp, hp;
                    s.svf.tick (v, lp, bp, hp, filterMode == 2);
                    v = filterMode == 1 ? hp : bp * bpGain;
                }
                v *= makeup * fbMakeup;
                const float out = s.dc.process (s.tilt.process (v));
                x[i] = dry + (out - dry) * mixAmt;
            }
        }
    }

private:
    struct Chan
    {
        Oversampler2x os;
        LatencyDelay dry;
        DcBlocker dc;
        float fbState = 0, fbHpX = 0, fbHpY = 0, fbLp = 0; // the analog-style feedback loop
        Svf fbRes;                                         // its resonance (feedback above 80 %)
        Svf svf;
        Biquad pre1, pre2, post1, post2;
        Envelope env, slow;
        Tilt tilt;
        float slew = 0;
    };
    // Analog-style feedback: the preamp's output is fed back to its own input with (almost) no delay -
    // one sample at the oversampled rate, like a feedback resistor around a gain stage. The loop runs
    // through a coupling high-pass (70 Hz) and a band limit (4.2 kHz), and the returned signal is soft-
    // limited the way an op-amp's rails would. Its gain is normalised by the preamp curve's small-signal
    // slope, so the loop gain stays below 1 at every frequency: feedback thickens and sustains the drive
    // (more gain at low levels, hysteresis, compression as the curve saturates) instead of ringing or
    // howling at a comb frequency. The user filter sits outside the loop, so feedback never pumps its
    // resonance.
    float loopStage (float x, Chan& s) const
    {
        const float y = curve (x + loopGain * s.fbState, s);
        // coupling high-pass (one pole) then band limit (one pole)
        const float hp = loopHpA * (s.fbHpY + y - s.fbHpX);
        s.fbHpX = y;
        s.fbHpY = flush (hp);
        s.fbLp = flush (s.fbLp + loopLpA * (hp - s.fbLp));
        float loop = s.fbLp;
        if (resMix > 0.f) // above 80 % feedback: part of the loop goes through a resonant band-pass
        {
            float lp, bp, hpo;
            s.fbRes.tick (loop, lp, bp, hpo);
            loop = (1.f - resMix) * loop + resMix * bp * resNorm; // unity at the peak: loop gain stays below 1
        }
        s.fbState = tanhFast (loop * 1.25f) * 0.8f;
        return y;
    }
    float curve (float x, Chan& s) const
    {
        switch (preamp)
        {
            case 0: { constexpr float b = 0.22f; return (tanhFast (x + b) - tanhFast (b)) * 1.05f; }        // Glow: bright tube, even harmonics
            case 1: { const float s1 = tanhFast (1.6f * x + 0.12f) - tanhFast (0.12f); return tanhFast (2.2f * s1); } // Lab: two hot stages
            case 2:                                                                                         // Dark: crossover grit, lopsided
            {
                const float grit = x + 0.06f * (x > 0 ? 1.f : -1.f) * (1.f - std::exp (-std::fabs (x) * 24.f));
                return grit > 0 ? tanhFast (1.25f * grit) : 1.12f * tanhFast (0.9f * grit);
            }
            case 3:                                                                                         // Meter: mostly clean, 2nd + 3rd
            {
                const float y = x + 0.14f * x * x - 0.05f * x * x * x;
                return tanhFast (y * 0.8f) * 1.25f;
            }
            case 4: { const float ax = std::fabs (x); return x / std::pow (1.f + std::pow (ax, 2.5f), 0.4f); } // Germanium: round, symmetrical
            case 5:                                                                                         // Soviet: hard clip + slew
            default:
            {
                float y = std::clamp (1.6f * x, -1.f, 1.f);
                if (std::fabs (x) > 1.3f) y -= 0.25f * (x > 0 ? 1.f : -1.f) * std::min (1.f, std::fabs (x) - 1.3f);
                const float d = std::clamp (y - s.slew, -0.32f, 0.32f);
                s.slew += d;
                return s.slew;
            }
        }
    }
    void update (const float* p)
    {
        constexpr Type T = Type::Ferric;
        preamp = idx (T, p, kPreamp);
        filterMode = idx (T, p, kFilter);
        const float driveDb = val (T, p, kDrive);
        static const float gainScale[6] = { 1.f, 2.f, 1.6f, 0.5f, 1.2f, 1.4f };
        drive = dbToGain (driveDb) * gainScale[preamp];
        makeup = 1.f / (1.f + 0.32f * std::log2 (std::max (1.f, drive)));
        if (preamp == 3) makeup *= 1.1f;
        // loop gain: up to 0.9 at 100 %, divided by the curve's slope around zero (Lab's two stages are steep)
        static const float slope0[6] = { 1.0f, 3.5f, 3.1f, 1.0f, 1.0f, 1.6f };
        const float fb = p[kFeedback];
        loopGain = 0.9f * std::pow (fb, 0.85f) / slope0[preamp];
        fbMakeup = 1.f / (1.f + 0.45f * fb);  // positive feedback adds gain: keep the level close
        // Above 80 % a little resonance comes through: up to 40 % of the loop passes a band-pass (Q ~ 4)
        // tuned by Cutoff. Its peak gain is 1, so the loop gain at that frequency stays at loopGain (< 1):
        // the band rings and sings, the rest of the loop gets less, nothing runs away.
        resMix = 0.4f * std::clamp ((fb - 0.8f) / 0.2f, 0.f, 1.f);
        mixAmt = p[kMix];
        const double fc = val (T, p, kCutoff);
        const float res = p[kReso];
        bpGain = 2.f - 1.96f * res; // unity at the bandpass peak
        for (auto& c : ch)
        {
            switch (preamp)
            {
                case 0: c.pre1.highShelf (sr, 3000, 3); c.pre2.bypass(); c.post1.lowpass (sr, 15000, 0.7); c.post2.bypass(); break;
                case 1: c.pre1.highpass (sr, 250, 0.7); c.pre2.highpass (sr, 180, 0.6); c.post1.lowpass (sr, 9000, 0.7); c.post2.peak (sr, 1800, 0.9, 2.5); break;
                case 2: c.pre1.lowpass (sr, 3200, 0.7); c.pre2.lowShelf (sr, 120, 3); c.post1.lowpass (sr, 5000, 0.6); c.post2.bypass(); break;
                case 3: c.pre1.bypass(); c.pre2.bypass(); c.post1.lowpass (sr, 18000, 0.7); c.post2.bypass(); break;
                case 4: c.pre1.lowShelf (sr, 100, 2); c.pre2.peak (sr, 2000, 0.8, 1.5); c.post1.lowpass (sr, 12000, 0.7); c.post2.bypass(); break;
                default: c.pre1.peak (sr, 1200, 0.8, 4); c.pre2.highpass (sr, 60, 0.7); c.post1.lowpass (sr, 8000, 0.7); c.post2.bypass(); break;
            }
            c.svf.set (sr, fc, res);
            c.fbRes.set (2.0 * sr, std::clamp (fc, 150.0, 6000.0), 0.88f);
            c.tilt.set (sr, 800, val (T, p, kTilt));
            c.env.set (sr, 1.0, 60.0);
            c.slow.set (sr, 30.0, 250.0);
        }
    }
    std::array<Chan, 2> ch;
    ParamCache cache;
    int preamp = 0, filterMode = 0;
    float drive = 1, makeup = 1, loopGain = 0, fbMakeup = 1, loopHpA = 0.99f, loopLpA = 0.3f, mixAmt = 1, bpGain = 1;
    float resMix = 0, resNorm = 2.f - 1.96f * 0.88f; // band-pass output * k = unity at the peak
};

//=============================================================================================
// Amp: input high-pass, 1-3 triode stages (2x oversampled) with coupling high-passes and
// inter-stage low-passes, a tone stack, a power amp that sags with the envelope, presence, master.
class Amp : public Module
{
public:
    enum { kModel, kGain, kBass, kMid, kTreble, kPresence, kSag, kMaster };
    void prepare (double s) override { sr = s; for (auto& c : ch) c.dc.set (sr, 10.0); cache.first = true; reset(); }
    void reset() override
    {
        for (auto& c : ch)
        {
            c.os.reset(); c.inHp.reset(); c.bass.reset(); c.mid.reset(); c.treble.reset(); c.pres.reset(); c.dc.reset(); c.env.reset();
            for (auto& st : c.couple) st.reset();
            for (auto& st : c.inter) st.reset();
        }
    }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams)) update (p);
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[c];
            for (int i = 0; i < n; ++i)
            {
                float x = s.inHp.process (io[c][i]);
                float a, b;
                s.os.upsample (x, a, b);
                a = stages (a, s); b = stages (b, s);
                float y = s.os.downsample (a, b);
                y = s.treble.process (s.mid.process (s.bass.process (y)));
                const float e = s.env.process (y);
                const float g = 1.f / (1.f + sag * e * 2.5f);
                y = tanhFast (1.3f * g * y) / 1.3f;
                y = s.pres.process (y);
                io[c][i] = s.dc.process (y * master);
            }
        }
    }

private:
    struct Chan
    {
        Oversampler2x os;
        Biquad inHp, bass, mid, treble, pres;
        std::array<OnePole, 3> couple, inter;
        DcBlocker dc;
        Envelope env;
    };
    static float triode (float x) { return x >= 0 ? tanhFast (x) : tanhFast (1.45f * x) / 1.45f; }
    float stages (float x, Chan& s)
    {
        for (int k = 0; k < nStages; ++k)
        {
            x = triode (x * stageGain[k] + 0.08f) - triode (0.08f);
            x = s.couple[(size_t) k].hp (x);
            x = s.inter[(size_t) k].lp (x);
        }
        return x * stageOut;
    }
    void update (const float* p)
    {
        constexpr Type T = Type::Amp;
        const int model = idx (T, p, kModel);
        const float gain = p[kGain];
        struct M { int stages; float g0, g0range, g1, g2, hp, lp, midF, midOffset, out; };
        static const M models[4] = {
            { 1, 1.0f, 6.f, 1.f, 1.f, 70.f, 9000.f, 650.f, 0.f, 1.0f },
            { 2, 2.0f, 14.f, 2.f, 1.f, 90.f, 7000.f, 700.f, 1.f, 0.8f },
            { 3, 4.0f, 30.f, 3.f, 2.f, 110.f, 6000.f, 650.f, -3.f, 0.7f },
            { 2, 1.5f, 8.f, 1.5f, 1.f, 35.f, 5000.f, 450.f, 0.f, 0.9f } };
        const M& m = models[model];
        nStages = m.stages;
        stageGain[0] = m.g0 + m.g0range * gain * gain;
        stageGain[1] = m.g1; stageGain[2] = m.g2;
        stageOut = m.out;
        sag = p[kSag];
        master = dbToGain (val (T, p, kMaster));
        const double fast = sr * 2;
        for (auto& c : ch)
        {
            c.inHp.highpass (sr, model == 3 ? 30 : 60, 0.7);
            for (int k = 0; k < 3; ++k)
            {
                c.couple[(size_t) k].setLowpass (fast, k == 0 ? m.hp : m.hp * 0.6);
                c.inter[(size_t) k].setLowpass (fast, m.lp * (k == 0 ? 1.4 : 1.0));
            }
            c.bass.lowShelf (sr, model == 3 ? 90 : 120, val (T, p, kBass));
            c.mid.peak (sr, m.midF, 0.7, val (T, p, kMid) + m.midOffset);
            c.treble.highShelf (sr, 2800, val (T, p, kTreble));
            c.pres.highShelf (sr, 3800, val (T, p, kPresence));
            c.env.set (sr, 5.0, 120.0);
        }
    }
    std::array<Chan, 2> ch;
    ParamCache cache;
    int nStages = 1;
    float stageGain[3] { 1, 1, 1 }, stageOut = 1, sag = 0, master = 1;
};

//=============================================================================================
// Cabinet: a cascade of filters per box (resonance high-pass, low body, cone break-up mids, a
// notch, two steep low-passes, presence) plus mic position and distance (an early reflection).
class Cabinet : public Module
{
public:
    enum { kCab, kMic, kDistance, kLowCut, kHighCut, kMix };
    void prepare (double s) override { sr = s; for (auto& c : ch) c.room.setMax ((int) (sr * 0.006)); cache.first = true; reset(); }
    void reset() override { for (auto& c : ch) { for (auto& f : c.f) f.reset(); c.room.reset(); } }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams)) update (p);
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[c];
            for (int i = 0; i < n; ++i)
            {
                const float dry = io[c][i];
                float y = dry;
                for (auto& f : s.f) y = f.process (y);
                s.room.push (y);
                y = (y + roomMix * s.room.read (roomDelay)) * makeup;
                io[c][i] = dry + (y - dry) * mixAmt;
            }
        }
    }

private:
    struct Chan { std::array<Biquad, 10> f; DelayLine room; };
    void update (const float* p)
    {
        constexpr Type T = Type::Cabinet;
        struct Cab { float hpF, hpQ, lowF, lowDb, midF, midDb, dipF, dipDb, notchF, lp1, lp2, presF, presDb; };
        static const Cab cabs[7] = {
            { 75, 0.9f, 110, 2.f, 1800, 3.f, 500, -1.f, 3500, 5200, 6500, 2600, 2.f },
            { 85, 1.2f, 120, 3.f, 1500, 2.5f, 450, -2.f, 4200, 5500, 6800, 2800, 2.f },
            { 70, 1.4f, 100, 4.5f, 1200, 2.f, 400, -3.f, 3800, 4800, 6000, 2400, 3.f },
            { 45, 1.2f, 90, 3.f, 900, 2.f, 300, -1.5f, 2800, 4200, 5200, 2000, 1.f },
            { 38, 1.3f, 70, 4.f, 700, 1.5f, 250, -1.f, 2200, 3000, 3800, 1600, 0.f },
            { 250, 1.5f, 400, 4.f, 1500, 4.f, 800, -2.f, 6000, 3500, 4500, 2000, 3.f },
            { 400, 0.8f, 900, 3.f, 2000, 5.f, 1200, -1.f, 7000, 3200, 3400, 2500, 2.f } };
        const Cab& c = cabs[idx (T, p, kCab)];
        const float mic = p[kMic], dist = p[kDistance];
        const float lpScale = (1.25f - 0.55f * mic) * (1.f - 0.3f * dist);
        roomMix = 0.35f * dist;
        roomDelay = (float) (sr * (0.0006 + 0.003 * dist));
        mixAmt = p[kMix];
        makeup = dbToGain (2.f) / (1.f + roomMix * 0.5f);
        for (auto& s : ch)
        {
            s.f[0].highpass (sr, c.hpF, c.hpQ);
            s.f[1].peak (sr, c.lowF, 1.0, c.lowDb);
            s.f[2].peak (sr, c.midF, 1.2, c.midDb);
            s.f[3].peak (sr, c.dipF, 0.8, c.dipDb);
            s.f[4].notch (sr, c.notchF, 2.0);
            s.f[5].lowpass (sr, c.lp1 * lpScale, 0.75);
            s.f[6].lowpass (sr, c.lp2 * lpScale, 0.6);
            s.f[7].peak (sr, c.presF, 1.0, c.presDb + 2.5f - 5.f * mic);
            if (p[kLowCut] > 0.005f) s.f[8].highpass (sr, val (T, p, kLowCut), 0.7); else s.f[8].bypass();
            if (p[kHighCut] < 0.995f) s.f[9].lowpass (sr, val (T, p, kHighCut), 0.7); else s.f[9].bypass();
        }
    }
    std::array<Chan, 2> ch;
    ParamCache cache;
    float roomMix = 0, roomDelay = 10, mixAmt = 1, makeup = 1;
};

//=============================================================================================
// Tape: pre-emphasis, magnetic saturation with a little memory, de-emphasis, head bump, speed- and
// age-dependent HF loss, wow and flutter (a modulated delay shared by both channels, as on a real
// transport) and hiss.
class Tape : public Module
{
public:
    enum { kDrive, kBias, kSpeed, kWow, kFlutter, kHiss, kAge, kMix };
    void prepare (double s) override
    {
        sr = s;
        for (auto& c : ch) { c.line.setMax ((int) (sr * 0.02)); c.dry.set ((int) (sr * kBaseDelay)); c.dc.set (sr, 10.0); }
        wowDrift.set (sr, 0.4);
        hissHp.setLowpass (sr, 1000.0);
        cache.first = true;
        reset();
    }
    void reset() override
    {
        for (auto& c : ch) { c.line.reset(); c.dry.reset(); c.dc.reset(); c.pre.reset(); c.de.reset(); c.bump.reset(); c.dip.reset(); c.lp1.reset(); c.lp2.reset(); c.mem = 0; }
        wowPhase = flutPhase = 0;
    }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams)) update (p);
        for (int i = 0; i < n; ++i)
        {
            // one transport for both channels
            wowPhase += wowRate / (float) sr; if (wowPhase > 1) wowPhase -= 1;
            flutPhase += 7.3f / (float) sr; if (flutPhase > 1) flutPhase -= 1;
            const float drift = wowDrift.next();
            const float mod = wow * (float) sr * 0.0022f * (0.7f * std::sin (6.2831853f * wowPhase) + 0.3f * drift)
                            + flutter * (float) sr * 0.00013f * std::sin (6.2831853f * flutPhase);
            const float delay = (float) (sr * kBaseDelay) + mod;
            const float ampMod = 1.f - age * 0.06f * (0.5f + 0.5f * drift);
            const float hiss = noise.white();
            const float hissHigh = hiss - hissHp.lp (hiss);
            float* io[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                const float in = io[c][i];
                const float dry = s.dry.process (in);
                float x = s.pre.process (in) * g;
                float y = tanhFast (x + biasOff) - biasRef;
                y = 0.85f * y + 0.15f * s.mem; // a touch of magnetic memory
                s.mem = y;
                y = s.de.process (y) * comp;
                y = s.dip.process (s.bump.process (y));
                y = s.lp2.process (s.lp1.process (y));
                s.line.push (flush (y));
                float wet = s.line.readCubic (delay) * ampMod + hissHigh * hissLevel;
                wet = s.dc.process (wet);
                io[c][i] = dry + (wet - dry) * mixAmt;
            }
        }
    }

private:
    static constexpr double kBaseDelay = 0.004;
    struct Chan { DelayLine line; LatencyDelay dry; DcBlocker dc; Biquad pre, de, bump, dip, lp1, lp2; float mem = 0; };
    void update (const float* p)
    {
        constexpr Type T = Type::Tape;
        g = dbToGain (val (T, p, kDrive));
        biasOff = 0.3f * p[kBias];
        biasRef = tanhFast (biasOff);
        comp = 1.f / (1.f + 0.25f * std::log2 (std::max (1.f, g)));
        const int speed = idx (T, p, kSpeed);
        static const float bumpF[3] = { 45, 70, 110 }, lpF[3] = { 9000, 14000, 19000 }, emph[3] = { 6, 4, 3 };
        age = p[kAge];
        wow = p[kWow]; flutter = p[kFlutter];
        wowRate = 0.45f + 0.5f * wow;
        hissLevel = p[kHiss] > 0.001f ? dbToGain (-90.f + 45.f * p[kHiss]) : 0.f;
        mixAmt = p[kMix];
        const float lp = lpF[speed] * (1.f - 0.55f * age);
        for (auto& c : ch)
        {
            c.pre.highShelf (sr, 3000, emph[speed]);
            c.de.highShelf (sr, 3000, -emph[speed]);
            c.bump.peak (sr, bumpF[speed], 1.2, 3.0);
            c.dip.peak (sr, bumpF[speed] * 0.5, 1.0, -2.0);
            c.lp1.lowpass (sr, lp, 0.6);
            c.lp2.lowpass (sr, lp * 1.3, 0.55);
        }
    }
    std::array<Chan, 2> ch;
    ParamCache cache;
    Drift wowDrift;
    Noise noise;
    OnePole hissHp;
    float g = 1, biasOff = 0, biasRef = 0, comp = 1, age = 0, wow = 0, flutter = 0, wowRate = 0.5f, hissLevel = 0, mixAmt = 1;
    float wowPhase = 0, flutPhase = 0;
};

//=============================================================================================
class Crusher : public Module
{
public:
    enum { kBits, kRate, kJitter, kTone, kMix };
    void prepare (double s) override { sr = s; cache.first = true; reset(); }
    void reset() override { phase = 1.f; held[0] = held[1] = 0; for (auto& t : tone) t.reset(); }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (cache.changed (p, kMaxParams))
        {
            constexpr Type T = Type::Crusher;
            q = std::pow (2.f, val (T, p, kBits) - 1.f);
            const bool rateOff = p[kRate] > 0.995f;
            inc = rateOff ? 1.f : std::min (1.f, val (T, p, kRate) / (float) sr);
            jitter = p[kJitter];
            toneOn = p[kTone] < 0.995f;
            for (auto& t : tone) t.lowpass (sr, val (T, p, kTone), 0.707);
            mixAmt = p[kMix];
        }
        for (int i = 0; i < n; ++i)
        {
            phase += inc * (1.f + jitter * 0.9f * noise.white());
            if (phase >= 1.f)
            {
                phase -= std::floor (phase);
                held[0] = std::round (L[i] * q) / q;
                held[1] = std::round (R[i] * q) / q;
            }
            float l = held[0], r = held[1];
            if (toneOn) { l = tone[0].process (l); r = tone[1].process (r); }
            L[i] += (l - L[i]) * mixAmt;
            R[i] += (r - R[i]) * mixAmt;
        }
    }

private:
    ParamCache cache;
    Noise noise;
    Biquad tone[2];
    float q = 128, inc = 1, jitter = 0, phase = 1, held[2] {}, mixAmt = 1;
    bool toneOn = false;
};
} // namespace

std::unique_ptr<Module> makeFerric() { return std::make_unique<Ferric>(); }
std::unique_ptr<Module> makeAmp() { return std::make_unique<Amp>(); }
std::unique_ptr<Module> makeCabinet() { return std::make_unique<Cabinet>(); }
std::unique_ptr<Module> makeTape() { return std::make_unique<Tape>(); }
std::unique_ptr<Module> makeCrusher() { return std::make_unique<Crusher>(); }
} // namespace ferrum::fx
