// compare.cpp - renders the same notes through the original LXR-02 voice code (compiled for the
// host, see build_and_run.sh) and through the Ferrum 8 port, then compares the 16-bit outputs.
#include "fw_glue.h"
#include "../../Source/dsp/LxrVoice.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <utility>

using namespace ferrum;

struct Setting { FwParam fw; int value; };

static void applyMine (Voice& v, FwParam p, int x)
{
    auto ch = [&] (int id, int n) { v.setParam (id, choiceValue (x, n)); };
    auto c = [&] (int id) { v.setParam (id, x / 127.f); };
    switch (p)
    {
        case FW_OSC_WAVE: ch (P_OSC_WAVE, 6); break;
        case FW_COARSE: c (P_OSC_COARSE); break;
        case FW_FINE: c (P_OSC_FINE); break;
        case FW_MOD_WAVE: ch (P_MOD_WAVE, 6); break;
        case FW_MOD_COARSE: c (P_MOD_COARSE); break;
        case FW_MOD_GAIN: c (P_MOD_GAIN); break;
        case FW_MOD2_WAVE: ch (P_MOD2_WAVE, 6); break;
        case FW_MOD2_COARSE: c (P_MOD2_COARSE); break;
        case FW_MOD2_GAIN: c (P_MOD2_GAIN); break;
        case FW_MIX_MODE: ch (P_FM_MODE, 2); break;
        case FW_NOISE_FREQ: c (P_NOISE_FREQ); break;
        case FW_NOISE_MIX: c (P_NOISE_MIX); break;
        case FW_PITCH_AMT: c (P_PITCH_AMT); break;
        case FW_PITCH_DECAY: c (P_PITCH_DECAY); break;
        case FW_PITCH_SLOPE: c (P_PITCH_SLOPE); break;
        case FW_ATTACK: c (P_ATTACK); break;
        case FW_DECAY: c (P_DECAY); break;
        case FW_SLOPE: c (P_SLOPE); break;
        case FW_REPEAT: v.setParam (P_REPEAT, std::sqrt (x / 127.f)); break;
        case FW_FILTER_TYPE: ch (P_FILTER_TYPE, 8); break;
        case FW_FILTER_FREQ: c (P_FILTER_FREQ); break;
        case FW_FILTER_RESO: c (P_FILTER_RESO); break;
        case FW_FILTER_DRIVE: c (P_FILTER_DRIVE); break;
        case FW_TRANS_WAVE: ch (P_TRANS_WAVE, 14); break;
        case FW_TRANS_VOL: c (P_TRANS_VOL); break;
        case FW_TRANS_FREQ: c (P_TRANS_FREQ); break;
        case FW_DRIVE: c (P_DRIVE); break;
        case FW_VOL: c (P_LEVEL); break;
        case FW_PWM: c (P_PWM); break;
        case FW_VOLMOD: v.setParam (P_VEL_VOL, x ? 1.f : 0.f); break;
    }
}

struct Scenario { const char* name; int fwVoice; Model model; std::vector<Setting> s; int vel = 127, note = 60, blocks = 400; };

static std::vector<Setting> drumBase()
{
    return { { FW_OSC_WAVE, 0 }, { FW_COARSE, 31 }, { FW_FINE, 63 }, { FW_MOD_WAVE, 0 }, { FW_MOD_COARSE, 0 }, { FW_MOD_GAIN, 0 },
             { FW_MIX_MODE, 0 }, { FW_PITCH_AMT, 44 }, { FW_PITCH_DECAY, 26 }, { FW_PITCH_SLOPE, 22 }, { FW_ATTACK, 0 },
             { FW_DECAY, 21 }, { FW_SLOPE, 46 }, { FW_REPEAT, 0 }, { FW_FILTER_TYPE, 0 }, { FW_FILTER_FREQ, 127 },
             { FW_FILTER_RESO, 0 }, { FW_FILTER_DRIVE, 0 }, { FW_TRANS_WAVE, 0 }, { FW_TRANS_VOL, 0 }, { FW_TRANS_FREQ, 0 },
             { FW_DRIVE, 63 }, { FW_VOL, 127 }, { FW_PWM, 0 }, { FW_VOLMOD, 1 } };
}
static std::vector<Setting> snareBase()
{
    return { { FW_OSC_WAVE, 1 }, { FW_COARSE, 45 }, { FW_FINE, 64 }, { FW_NOISE_FREQ, 127 }, { FW_NOISE_MIX, 61 },
             { FW_PITCH_AMT, 81 }, { FW_PITCH_DECAY, 14 }, { FW_PITCH_SLOPE, 15 }, { FW_ATTACK, 0 }, { FW_DECAY, 10 },
             { FW_SLOPE, 10 }, { FW_REPEAT, 0 }, { FW_FILTER_TYPE, 0 }, { FW_FILTER_FREQ, 127 }, { FW_FILTER_RESO, 0 },
             { FW_FILTER_DRIVE, 0 }, { FW_TRANS_WAVE, 0 }, { FW_TRANS_VOL, 0 }, { FW_TRANS_FREQ, 0 }, { FW_DRIVE, 63 },
             { FW_VOL, 127 }, { FW_PWM, 0 }, { FW_VOLMOD, 1 } };
}
static std::vector<Setting> metalBase (bool hat)
{
    if (hat)
        return { { FW_OSC_WAVE, 0 }, { FW_COARSE, 50 }, { FW_FINE, 63 }, { FW_MOD_WAVE, 0 }, { FW_MOD_COARSE, 56 }, { FW_MOD_GAIN, 127 },
                 { FW_MOD2_WAVE, 0 }, { FW_MOD2_COARSE, 127 }, { FW_MOD2_GAIN, 127 }, { FW_ATTACK, 0 }, { FW_DECAY, 5 },
                 { FW_SLOPE, 6 }, { FW_REPEAT, 0 }, { FW_FILTER_TYPE, 1 }, { FW_FILTER_FREQ, 104 }, { FW_FILTER_RESO, 0 },
                 { FW_FILTER_DRIVE, 0 }, { FW_TRANS_WAVE, 0 }, { FW_TRANS_VOL, 0 }, { FW_TRANS_FREQ, 0 }, { FW_DRIVE, 63 },
                 { FW_VOL, 127 }, { FW_PWM, 0 }, { FW_VOLMOD, 1 } };
    return { { FW_OSC_WAVE, 4 }, { FW_COARSE, 127 }, { FW_FINE, 63 }, { FW_MOD_WAVE, 0 }, { FW_MOD_COARSE, 0 }, { FW_MOD_GAIN, 0 },
             { FW_MOD2_WAVE, 0 }, { FW_MOD2_COARSE, 0 }, { FW_MOD2_GAIN, 0 }, { FW_ATTACK, 0 }, { FW_DECAY, 107 },
             { FW_SLOPE, 0 }, { FW_REPEAT, 0 }, { FW_FILTER_TYPE, 1 }, { FW_FILTER_FREQ, 68 }, { FW_FILTER_RESO, 0 },
             { FW_FILTER_DRIVE, 0 }, { FW_TRANS_WAVE, 0 }, { FW_TRANS_VOL, 0 }, { FW_TRANS_FREQ, 0 }, { FW_DRIVE, 63 },
             { FW_VOL, 127 }, { FW_PWM, 0 }, { FW_VOLMOD, 1 } };
}
static std::vector<Setting> with (std::vector<Setting> b, std::initializer_list<Setting> o)
{
    for (auto& x : o) { bool found = false; for (auto& y : b) if (y.fw == x.fw) { y.value = x.value; found = true; } if (! found) b.push_back (x); }
    return b;
}

int main()
{
    std::vector<Scenario> sc = {
        { "kick (init kit voice 1)", 0, Model::Drum, drumBase() },
        { "tom (init kit voice 2)", 1, Model::Drum, with (drumBase(), { { FW_COARSE, 41 }, { FW_DECAY, 13 }, { FW_PITCH_DECAY, 93 }, { FW_PITCH_AMT, 14 }, { FW_PITCH_SLOPE, 10 }, { FW_SLOPE, 11 } }) },
        { "zap (init kit voice 3)", 2, Model::Drum, with (drumBase(), { { FW_COARSE, 58 }, { FW_DECAY, 3 }, { FW_PITCH_DECAY, 7 }, { FW_PITCH_AMT, 127 }, { FW_PITCH_SLOPE, 14 }, { FW_SLOPE, 4 } }) },
        { "drum FM sine/sine", 0, Model::Drum, with (drumBase(), { { FW_COARSE, 48 }, { FW_MOD_COARSE, 64 }, { FW_MOD_GAIN, 80 }, { FW_DECAY, 30 } }) },
        { "drum FM tri carrier / square mod", 0, Model::Drum, with (drumBase(), { { FW_OSC_WAVE, 1 }, { FW_MOD_WAVE, 3 }, { FW_COARSE, 55 }, { FW_MOD_COARSE, 70 }, { FW_MOD_GAIN, 50 } }) },
        { "drum mix saw + square", 1, Model::Drum, with (drumBase(), { { FW_OSC_WAVE, 2 }, { FW_MOD_WAVE, 3 }, { FW_COARSE, 50 }, { FW_MOD_COARSE, 62 }, { FW_MOD_GAIN, 60 }, { FW_MIX_MODE, 1 } }) },
        { "drum PWM + BP reso + filter drive", 2, Model::Drum, with (drumBase(), { { FW_OSC_WAVE, 5 }, { FW_PWM, 64 }, { FW_COARSE, 60 }, { FW_FILTER_TYPE, 2 }, { FW_FILTER_FREQ, 80 }, { FW_FILTER_RESO, 90 }, { FW_FILTER_DRIVE, 60 } }) },
        { "drum kick sample transient + attack + LP2", 0, Model::Drum, with (drumBase(), { { FW_TRANS_WAVE, 5 }, { FW_TRANS_VOL, 90 }, { FW_TRANS_FREQ, 30 }, { FW_ATTACK, 10 }, { FW_FILTER_TYPE, 6 }, { FW_FILTER_FREQ, 90 }, { FW_FILTER_RESO, 50 } }) },
        { "drum snap transient", 0, Model::Drum, with (drumBase(), { { FW_TRANS_WAVE, 0 }, { FW_TRANS_VOL, 80 }, { FW_TRANS_FREQ, 60 } }) },
        { "drum offset transient", 0, Model::Drum, with (drumBase(), { { FW_TRANS_WAVE, 1 }, { FW_TRANS_VOL, 64 } }) },
        { "drum noise osc, notch filter", 1, Model::Drum, with (drumBase(), { { FW_OSC_WAVE, 4 }, { FW_COARSE, 100 }, { FW_FILTER_TYPE, 4 }, { FW_FILTER_FREQ, 90 }, { FW_FILTER_RESO, 60 } }) },
        { "drum peak + unity BP", 2, Model::Drum, with (drumBase(), { { FW_OSC_WAVE, 2 }, { FW_FILTER_TYPE, 5 }, { FW_FILTER_FREQ, 70 }, { FW_FILTER_RESO, 80 } }) },
        { "drum velocity 64, note 48", 0, Model::Drum, drumBase(), 64, 48 },
        { "snare (init kit)", 3, Model::Snare, snareBase() },
        { "snare BP reso + snare sample", 3, Model::Snare, with (snareBase(), { { FW_FILTER_TYPE, 2 }, { FW_FILTER_FREQ, 85 }, { FW_FILTER_RESO, 70 }, { FW_NOISE_FREQ, 90 }, { FW_TRANS_WAVE, 11 }, { FW_TRANS_VOL, 60 }, { FW_TRANS_FREQ, 34 } }) },
        { "snare noise rate 90", 3, Model::Snare, with (snareBase(), { { FW_NOISE_FREQ, 90 } }) },
        { "snare BP reso", 3, Model::Snare, with (snareBase(), { { FW_FILTER_TYPE, 2 }, { FW_FILTER_FREQ, 85 }, { FW_FILTER_RESO, 70 } }) },
        { "snare sample transient", 3, Model::Snare, with (snareBase(), { { FW_TRANS_WAVE, 11 }, { FW_TRANS_VOL, 60 }, { FW_TRANS_FREQ, 34 } }) },
        { "snare repeat 3", 3, Model::Snare, with (snareBase(), { { FW_ATTACK, 8 }, { FW_REPEAT, 3 } }) },
        { "clap (snare, repeat 3)", 3, Model::Snare, with (snareBase(), { { FW_NOISE_MIX, 127 }, { FW_PITCH_AMT, 0 }, { FW_ATTACK, 8 }, { FW_REPEAT, 3 }, { FW_DECAY, 12 }, { FW_SLOPE, 15 }, { FW_FILTER_TYPE, 2 }, { FW_FILTER_FREQ, 72 }, { FW_FILTER_RESO, 25 } }) },
        { "cymbal (init kit)", 4, Model::Cymbal, metalBase (false), 127, 60, 900 },
        { "cymbal FM sine / square + sine", 4, Model::Cymbal, with (metalBase (false), { { FW_OSC_WAVE, 0 }, { FW_COARSE, 100 }, { FW_MOD_WAVE, 3 }, { FW_MOD_COARSE, 88 }, { FW_MOD_GAIN, 90 }, { FW_MOD2_WAVE, 0 }, { FW_MOD2_COARSE, 117 }, { FW_MOD2_GAIN, 60 }, { FW_DECAY, 70 } }), 127, 60, 600 },
        { "hi-hat (init kit)", 5, Model::HiHat, metalBase (true) },
        { "hi-hat square / noise mod, velocity 90", 5, Model::HiHat, with (metalBase (true), { { FW_OSC_WAVE, 3 }, { FW_COARSE, 62 }, { FW_MOD_COARSE, 74 }, { FW_MOD2_WAVE, 4 }, { FW_MOD2_GAIN, 90 }, { FW_DECAY, 20 }, { FW_SLOPE, 30 }, { FW_FILTER_FREQ, 100 } }), 90 },
        { "hi-hat PWM carrier", 5, Model::HiHat, with (metalBase (true), { { FW_OSC_WAVE, 5 }, { FW_PWM, 40 }, { FW_DECAY, 12 } }) },
    };

    int failed = 0;
    std::printf ("%-44s %8s %8s %8s %10s\n", "scenario", "peak", "maxDiff", ">2 LSB", "corr");
    for (const auto& s : sc)
    {
        const uint32_t seed = 12345;
        fw_init (seed * 2654435761u + 1u);
        for (const auto& x : s.s) fw_set (s.fwVoice, x.fw, x.value);
        fw_settle (s.fwVoice);

        Voice mine;
        mine.setParam (P_MODEL, choiceValue ((int) s.model, 4));
        for (int i = 0; i < NUM_PATCH_PARAMS; ++i) if (i != P_MODEL) mine.setParam (i, i == P_DECIMATE ? 1.f : (i == P_PAN ? 0.5f : 0.f));
        for (const auto& x : s.s) applyMine (mine, x.fw, x.value);
        mine.prepare (lxr::kLxrFs, seed);

        fw_trigger (s.fwVoice, s.vel, s.note);
        mine.trigger (s.vel / 127.f, s.note);

        int maxDiff = 0, over = 0; int16_t peak = 0;
        double sxy = 0, sxx = 0, syy = 0;
        int16_t a[64]; float b[64];
        for (int blk = 0; blk < s.blocks; ++blk)
        {
            fw_render (s.fwVoice, a);
            mine.render (b, 64);
            for (int i = 0; i < 64; ++i)
            {
                const int y = (int) std::lround (b[i] * 32768.f);
                const int d = std::abs (y - a[i]);
                maxDiff = std::max (maxDiff, d);
                if (d > 2) { ++over; if (std::getenv ("FW_DEBUG") && d > 50 && over <= 400) std::printf ("    diff blk %d i %d fw %d port %d\n", blk, i, a[i], y); }
                peak = std::max<int16_t> (peak, (int16_t) std::abs ((int) a[i]));
                sxy += (double) a[i] * y; sxx += (double) a[i] * a[i]; syy += (double) y * y;
            }
        }
        const double corr = (sxx > 0 && syy > 0) ? sxy / std::sqrt (sxx * syy) : (sxx == syy ? 1.0 : 0.0);
        const bool ok = corr > 0.999 && peak > 100;
        if (! ok) ++failed;
        std::printf ("%-44s %8d %8d %8d %10.6f %s\n", s.name, peak, maxDiff, over, corr, ok ? "" : "  <-- MISMATCH");
    }
    std::printf ("%s\n", failed ? "SOME SCENARIOS DIFFER" : "port matches the firmware in every scenario");
    return failed ? 1 : 0;
}
