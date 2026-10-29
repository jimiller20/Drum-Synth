// Kits.cpp - factory kits
// SPDX-License-Identifier: GPL-3.0-only
#include "Kits.h"
#include "../dsp/LxrCore.h"

namespace ferrum
{
namespace
{
struct P
{
    Patch p;
    explicit P (Model m) : p (defaultPatch (m)) {}
    P& set (int id, float v127) { p[(size_t) id] = lxr::clamp01 (v127 / 127.f); return *this; }
    P& norm (int id, float v) { p[(size_t) id] = lxr::clamp01 (v); return *this; }
    P& ch (int id, int idx) { p[(size_t) id] = choiceValue (idx, paramDef (id).numChoices); return *this; }
    P& repeat (int n) { p[P_REPEAT] = std::sqrt ((float) n / 127.f); return *this; }
};

// "x" trigger, "X" accent, "f" fill, "F" accent + fill, "." rest
void lane (PatternData& d, int ch, const char* s)
{
    for (int i = 0; i < kMaxSteps && s[i]; ++i)
    {
        const char c = s[i];
        const uint16_t bit = (uint16_t) (1u << i);
        if (c == '.') continue;
        d.trig[(size_t) ch] |= bit;
        if (c == 'X' || c == 'F') d.acc[(size_t) ch] |= bit;
        if (c == 'f' || c == 'F') d.fill[(size_t) ch] |= bit;
    }
}

KitData makeKit (const char* name, std::initializer_list<std::pair<const char*, Patch>> chans)
{
    KitData k;
    k.name = name;
    int i = 0;
    for (auto& c : chans)
    {
        if (i >= kNumChannels) break;
        k.channelNames[(size_t) i] = c.first;
        k.a[(size_t) i] = k.b[(size_t) i] = c.second;
        ++i;
    }
    return k;
}

Patch clapPatch()
{
    return P (Model::Snare).ch (P_OSC_WAVE, lxr::WAVE_TRI).set (P_OSC_COARSE, 60)
        .set (P_NOISE_MIX, 127).set (P_NOISE_FREQ, 127).set (P_PITCH_AMT, 0)
        .set (P_ATTACK, 8).repeat (3).set (P_DECAY, 12).set (P_SLOPE, 15)
        .ch (P_FILTER_TYPE, 2).set (P_FILTER_FREQ, 72).set (P_FILTER_RESO, 25).p;
}
}

KitData initKit()
{
    // voices 1-6 straight from the LXR-02 init kit, the open hat uses the init kit's "open"
    // decay, channel 8 adds a clap built on the snare model
    const Patch kick = defaultPatch (Model::Drum);
    const Patch tom = P (Model::Drum).set (P_OSC_COARSE, 41).set (P_OSC_FINE, 63.5f).set (P_DECAY, 13)
                          .set (P_PITCH_DECAY, 93).set (P_PITCH_AMT, 14).set (P_PITCH_SLOPE, 10).set (P_SLOPE, 11).p;
    const Patch zap = P (Model::Drum).set (P_OSC_COARSE, 58).set (P_OSC_FINE, 63.5f).set (P_DECAY, 3)
                          .set (P_PITCH_DECAY, 7).set (P_PITCH_AMT, 127).set (P_PITCH_SLOPE, 14).set (P_SLOPE, 4).p;
    const Patch closedHat = P (Model::HiHat).ch (P_CHOKE, 1).p;
    const Patch openHat = P (Model::HiHat).set (P_DECAY, 17).ch (P_CHOKE, 1).p;
    KitData k = makeKit ("LXR Init Kit", { { "Kick", kick }, { "Tom", tom }, { "Zap", zap },
                                           { "Snare", defaultPatch (Model::Snare) }, { "Cymbal", defaultPatch (Model::Cymbal) },
                                           { "Closed Hat", closedHat }, { "Open Hat", openHat }, { "Clap", clapPatch() } });
    PatternData& a = k.patterns[0];
    lane (a, 0, "X...x...x...x...");
    lane (a, 3, "....X.......X...");
    lane (a, 5, "x.X.x.X.x.X.x...");
    lane (a, 6, "..............x.");
    PatternData& b = k.patterns[1];
    lane (b, 0, "X...x...x..x..x.");
    lane (b, 1, "...........x..xf");
    lane (b, 3, "....X.......X..x");
    lane (b, 5, "xxXxxxXxxxXxxx..");
    lane (b, 6, "..............x.");
    lane (b, 7, "............X...");
    return k;
}

static KitData concrete()
{
    KitData k = makeKit ("Concrete", {
        { "Kick", P (Model::Drum).set (P_OSC_COARSE, 28).set (P_PITCH_AMT, 58).set (P_PITCH_DECAY, 18).set (P_PITCH_SLOPE, 30)
                      .set (P_DECAY, 30).set (P_SLOPE, 60).set (P_DRIVE, 85).ch (P_TRANS_WAVE, 2).norm (P_TRANS_VOL, 0.35f)
                      .set (P_TRANS_FREQ, 40).set (P_FILTER_FREQ, 118).set (P_FILTER_DRIVE, 20).p },
        { "Rumble", P (Model::Drum).ch (P_OSC_WAVE, lxr::WAVE_TRI).set (P_OSC_COARSE, 33).set (P_PITCH_AMT, 20)
                        .set (P_PITCH_DECAY, 40).set (P_DECAY, 40).set (P_SLOPE, 80).set (P_DRIVE, 70).norm (P_LEVEL, 0.7f)
                        .ch (P_OUTPUT, 2).p },
        { "Perc", P (Model::Drum).ch (P_OSC_WAVE, lxr::WAVE_REC).set (P_OSC_COARSE, 79).ch (P_MOD_WAVE, lxr::WAVE_SINE)
                      .set (P_MOD_COARSE, 101).norm (P_MOD_GAIN, 0.45f).set (P_PITCH_AMT, 10).set (P_PITCH_DECAY, 8)
                      .set (P_DECAY, 7).set (P_SLOPE, 20).ch (P_FILTER_TYPE, 2).set (P_FILTER_FREQ, 95).set (P_FILTER_RESO, 40)
                      .set (P_DRIVE, 60).norm (P_PAN, 0.62f).norm (P_LEVEL, 0.7f).p },
        { "Snare", P (Model::Snare).set (P_OSC_COARSE, 52).set (P_NOISE_MIX, 70).set (P_NOISE_FREQ, 120).set (P_PITCH_AMT, 50)
                       .set (P_PITCH_DECAY, 10).set (P_DECAY, 12).set (P_SLOPE, 25).ch (P_FILTER_TYPE, 1).set (P_FILTER_FREQ, 40)
                       .ch (P_TRANS_WAVE, 11).norm (P_TRANS_VOL, 0.4f).set (P_TRANS_FREQ, 34).set (P_DRIVE, 80).p },
        { "Ride", P (Model::Cymbal).ch (P_OSC_WAVE, lxr::WAVE_REC).set (P_OSC_COARSE, 105).ch (P_MOD_WAVE, lxr::WAVE_SINE)
                      .set (P_MOD_COARSE, 88).set (P_MOD_GAIN, 90).ch (P_MOD2_WAVE, lxr::WAVE_SINE).set (P_MOD2_COARSE, 117)
                      .set (P_MOD2_GAIN, 60).set (P_DECAY, 70).set (P_SLOPE, 20).ch (P_FILTER_TYPE, 1).set (P_FILTER_FREQ, 80)
                      .set (P_FILTER_RESO, 20).set (P_DRIVE, 60).norm (P_LEVEL, 0.55f).norm (P_PAN, 0.4f).p },
        { "Closed Hat", P (Model::HiHat).ch (P_OSC_WAVE, lxr::WAVE_REC).set (P_OSC_COARSE, 62).set (P_MOD_COARSE, 74)
                            .set (P_MOD_GAIN, 127).ch (P_MOD2_WAVE, lxr::WAVE_NOISE).set (P_MOD2_COARSE, 127).set (P_MOD2_GAIN, 90)
                            .set (P_DECAY, 4).set (P_SLOPE, 10).set (P_FILTER_FREQ, 100).ch (P_CHOKE, 1).norm (P_PAN, 0.56f).p },
        { "Open Hat", P (Model::HiHat).ch (P_OSC_WAVE, lxr::WAVE_REC).set (P_OSC_COARSE, 62).set (P_MOD_COARSE, 74)
                          .set (P_MOD_GAIN, 127).ch (P_MOD2_WAVE, lxr::WAVE_NOISE).set (P_MOD2_COARSE, 127).set (P_MOD2_GAIN, 90)
                          .set (P_DECAY, 20).set (P_SLOPE, 30).set (P_FILTER_FREQ, 100).ch (P_CHOKE, 1).norm (P_PAN, 0.56f).p },
        { "Clap", P (Model::Snare).set (P_NOISE_MIX, 127).set (P_NOISE_FREQ, 127).set (P_PITCH_AMT, 0).set (P_ATTACK, 7)
                      .repeat (4).set (P_DECAY, 15).set (P_SLOPE, 15).ch (P_FILTER_TYPE, 2).set (P_FILTER_FREQ, 78)
                      .set (P_FILTER_RESO, 30).p } });
    {
        FxSettings fs; fs.type = FxType::Delay; fs.dryWet = 0.35f; fs.p[0] = 0.55f; fs.p[1] = 0.7f; fs.p[2] = 0.5f; fs.p[3] = 0.45f;
        k.rack = fx::FxRack::fromLegacy (fs, 0);
    }
    k.fillRate = 4.f;
    PatternData& a = k.patterns[0];
    lane (a, 0, "X...x...x...x...");
    lane (a, 2, "..x..x...x..x..x");
    lane (a, 3, "....X.......X...");
    lane (a, 5, "xx.xxx.xxx.xxx.x");
    lane (a, 6, "..X...X...X...X.");
    lane (a, 7, "............x...");
    PatternData& b = k.patterns[1];
    lane (b, 0, "X...x...x...x.x.");
    lane (b, 1, ".......x.......x");
    lane (b, 2, "..x..x...x..x.xf");
    lane (b, 3, "....X.......X...");
    lane (b, 4, "x.x.x.x.x.x.x.x.");
    lane (b, 5, "xx.xxx.xxx.xxx.x");
    lane (b, 6, "..X...X...X...X.");
    lane (b, 7, "....x.......X..f");
    a.chainNext = true; // A -> B
    return k;
}

static KitData bitElectro()
{
    KitData k = makeKit ("Bit Electro", {
        { "Boom", P (Model::Drum).set (P_OSC_COARSE, 30).set (P_PITCH_AMT, 40).set (P_PITCH_DECAY, 22).set (P_DECAY, 55)
                      .set (P_SLOPE, 70).set (P_DRIVE, 95).set (P_DECIMATE, 105).p },
        { "Zap", P (Model::Drum).ch (P_OSC_WAVE, lxr::WAVE_SAW).set (P_OSC_COARSE, 50).set (P_PITCH_AMT, 120).set (P_PITCH_DECAY, 6)
                     .set (P_DECAY, 9).set (P_FILTER_FREQ, 90).set (P_FILTER_RESO, 60).set (P_DECIMATE, 90).norm (P_LEVEL, 0.7f)
                     .norm (P_PAN, 0.35f).p },
        { "Cowbell", P (Model::Drum).ch (P_OSC_WAVE, lxr::WAVE_REC).set (P_OSC_COARSE, 75).ch (P_FM_MODE, 1).ch (P_MOD_WAVE, lxr::WAVE_REC)
                         .set (P_MOD_COARSE, 82).norm (P_MOD_GAIN, 0.5f).set (P_PITCH_AMT, 0).set (P_DECAY, 10).set (P_SLOPE, 30)
                         .ch (P_FILTER_TYPE, 2).set (P_FILTER_FREQ, 85).set (P_FILTER_RESO, 30).norm (P_LEVEL, 0.6f).norm (P_PAN, 0.65f).p },
        { "Snare", P (Model::Snare).ch (P_OSC_WAVE, lxr::WAVE_REC).set (P_OSC_COARSE, 55).set (P_NOISE_MIX, 60).set (P_DECAY, 11)
                       .set (P_FILTER_FREQ, 100).ch (P_TRANS_WAVE, 6).norm (P_TRANS_VOL, 0.5f).set (P_TRANS_FREQ, 30)
                       .set (P_DECIMATE, 95).set (P_DRIVE, 90).ch (P_OUTPUT, 2).p },
        { "Crash", P (Model::Cymbal).set (P_DECAY, 85).set (P_SLOPE, 10).set (P_FILTER_FREQ, 70).norm (P_LEVEL, 0.6f).p },
        { "Closed Hat", P (Model::HiHat).set (P_OSC_COARSE, 70).ch (P_MOD_WAVE, lxr::WAVE_REC).set (P_MOD_COARSE, 96)
                            .ch (P_MOD2_WAVE, lxr::WAVE_NOISE).set (P_MOD2_GAIN, 70).set (P_DECAY, 3).set (P_FILTER_FREQ, 105)
                            .set (P_DECIMATE, 110).ch (P_CHOKE, 1).p },
        { "Open Hat", P (Model::HiHat).set (P_OSC_COARSE, 70).ch (P_MOD_WAVE, lxr::WAVE_REC).set (P_MOD_COARSE, 96)
                          .ch (P_MOD2_WAVE, lxr::WAVE_NOISE).set (P_MOD2_GAIN, 70).set (P_DECAY, 16).set (P_FILTER_FREQ, 105)
                          .set (P_DECIMATE, 110).ch (P_CHOKE, 1).p },
        { "Clap", P (Model::Snare).set (P_NOISE_MIX, 127).set (P_PITCH_AMT, 0).set (P_ATTACK, 9).repeat (3).set (P_DECAY, 13)
                      .ch (P_FILTER_TYPE, 2).set (P_FILTER_FREQ, 70).set (P_FILTER_RESO, 20).set (P_DECIMATE, 100).p } });
    {
        FxSettings fs; fs.type = FxType::Distortion; fs.distType = DistType::Rat; fs.dryWet = 0.5f;
        fs.p[0] = 0.25f; fs.p[1] = 0.2f; fs.p[2] = 0.6f; fs.p[3] = 0.7f;
        k.rack = fx::FxRack::fromLegacy (fs, 0);
    }
    k.swing = 0.35f; k.fillRate = 3.f;
    PatternData& a = k.patterns[0];
    lane (a, 0, "X..x......x.....");
    lane (a, 1, ".......x.......x");
    lane (a, 2, "x..x..x...x..x..");
    lane (a, 3, "....X.......X...");
    lane (a, 5, "x.xxx.x.x.xxx.x.");
    lane (a, 6, ".x.......x......");
    lane (a, 7, "............x..f");
    return k;
}

static KitData metalPerc()
{
    KitData k = makeKit ("Metal Perc", {
        { "Kick", P (Model::Drum).set (P_OSC_COARSE, 26).ch (P_MOD_WAVE, lxr::WAVE_TRI).set (P_MOD_COARSE, 64).norm (P_MOD_GAIN, 0.2f)
                      .set (P_PITCH_AMT, 70).set (P_PITCH_DECAY, 14).set (P_DECAY, 26).set (P_SLOPE, 50).set (P_DRIVE, 75).p },
        { "Bell", P (Model::Drum).set (P_OSC_COARSE, 84).ch (P_MOD_WAVE, lxr::WAVE_SINE).set (P_MOD_COARSE, 99).norm (P_MOD_GAIN, 0.7f)
                      .set (P_PITCH_AMT, 0).set (P_DECAY, 40).set (P_SLOPE, 30).norm (P_PAN, 0.35f).norm (P_LEVEL, 0.6f).p },
        { "Metal", P (Model::Drum).ch (P_OSC_WAVE, lxr::WAVE_REC).set (P_OSC_COARSE, 66).ch (P_MOD_WAVE, lxr::WAVE_SAW)
                       .set (P_MOD_COARSE, 87).norm (P_MOD_GAIN, 0.55f).set (P_PITCH_AMT, 5).set (P_DECAY, 14).ch (P_FILTER_TYPE, 1)
                       .set (P_FILTER_FREQ, 60).set (P_FILTER_RESO, 50).norm (P_PAN, 0.65f).norm (P_LEVEL, 0.65f).ch (P_OUTPUT, 2).p },
        { "Snare", P (Model::Snare).ch (P_OSC_WAVE, lxr::WAVE_SINE).set (P_OSC_COARSE, 48).set (P_NOISE_MIX, 55).set (P_NOISE_FREQ, 110)
                       .set (P_PITCH_AMT, 70).set (P_PITCH_DECAY, 12).set (P_DECAY, 14).ch (P_FILTER_TYPE, 2).set (P_FILTER_FREQ, 80)
                       .set (P_FILTER_RESO, 20).p },
        { "Gong", P (Model::Cymbal).ch (P_OSC_WAVE, lxr::WAVE_SINE).set (P_OSC_COARSE, 70).ch (P_MOD_WAVE, lxr::WAVE_SINE)
                      .set (P_MOD_COARSE, 77).set (P_MOD_GAIN, 100).ch (P_MOD2_WAVE, lxr::WAVE_TRI).set (P_MOD2_COARSE, 53)
                      .set (P_MOD2_GAIN, 80).set (P_DECAY, 90).set (P_SLOPE, 30).set (P_FILTER_FREQ, 30).norm (P_LEVEL, 0.5f).p },
        { "Tick", P (Model::HiHat).set (P_OSC_COARSE, 88).set (P_MOD_COARSE, 113).ch (P_MOD2_WAVE, lxr::WAVE_REC).set (P_MOD2_COARSE, 101)
                      .set (P_DECAY, 2).set (P_FILTER_FREQ, 110).ch (P_CHOKE, 1).p },
        { "Shaker", P (Model::HiHat).ch (P_OSC_WAVE, lxr::WAVE_NOISE).set (P_OSC_COARSE, 127).set (P_ATTACK, 10).set (P_DECAY, 9)
                        .set (P_SLOPE, 50).set (P_FILTER_FREQ, 95).norm (P_PAN, 0.6f).norm (P_LEVEL, 0.7f).p },
        { "Clap", clapPatch() } });
    {
        FxSettings fs; fs.type = FxType::RingMod; fs.dryWet = 0.5f; fs.p[0] = 0.4f;
        k.rack = fx::FxRack::fromLegacy (fs, 0);
    }
    k.swing = 0.15f;
    PatternData& a = k.patterns[0];
    lane (a, 0, "X......x..x.....");
    lane (a, 1, "..x.....x.....x.");
    lane (a, 2, "...x..x....x..x.");
    lane (a, 3, "....X.......X...");
    lane (a, 5, "x.x.x.x.x.x.x.x.");
    lane (a, 6, ".xx..xx..xx..xx.");
    return k;
}

// one expression lane, step by step (0 = neutral)
static void xl (PatternData& d, int ch, int lane, std::initializer_list<int> v)
{
    int st = 0;
    for (int x : v) if (st < kMaxSteps) d.expr[(size_t) ch][(size_t) lane][(size_t) st++] = (int8_t) x;
}

// shows off the expression lanes: ghosted DnB snares, laid-back hats, flams, drags and chance
static KitData jungleGhosts()
{
    KitData k = makeKit ("Jungle Ghosts", {
        { "Kick", P (Model::Drum).set (P_OSC_COARSE, 29).set (P_PITCH_AMT, 52).set (P_PITCH_DECAY, 20).set (P_DECAY, 24)
                      .set (P_SLOPE, 40).set (P_DRIVE, 70).norm (P_HUMANIZE, 0.15f).p },
        { "Tom", P (Model::Drum).set (P_OSC_COARSE, 45).set (P_DECAY, 16).set (P_PITCH_DECAY, 60).set (P_PITCH_AMT, 20)
                     .norm (P_PAN, 0.38f).norm (P_LEVEL, 0.7f).norm (P_VARIANCE, 0.2f).p },
        { "Rim", P (Model::Drum).set (P_OSC_COARSE, 80).set (P_DECAY, 3).set (P_PITCH_AMT, 30).set (P_PITCH_DECAY, 5)
                     .ch (P_TRANS_WAVE, 6).norm (P_TRANS_VOL, 0.6f).norm (P_PAN, 0.62f).norm (P_LEVEL, 0.6f).p },
        { "Snare", P (Model::Snare).set (P_OSC_COARSE, 52).set (P_NOISE_MIX, 72).set (P_NOISE_FREQ, 120).set (P_PITCH_AMT, 78)
                       .set (P_PITCH_DECAY, 12).set (P_DECAY, 15).set (P_SLOPE, 20).ch (P_FILTER_TYPE, 1).set (P_FILTER_FREQ, 30)
                       .norm (P_GHOST, 0.55f).norm (P_HUMANIZE, 0.35f).norm (P_VARIANCE, 0.1f).p },
        { "Ride", P (Model::Cymbal).set (P_DECAY, 70).set (P_FILTER_FREQ, 80).norm (P_PAN, 0.6f).norm (P_LEVEL, 0.45f).p },
        { "Closed Hat", P (Model::HiHat).set (P_DECAY, 4).ch (P_CHOKE, 1).norm (P_PAN, 0.56f)
                            .norm (P_HUMANIZE, 0.3f).norm (P_VARIANCE, 0.15f).p },
        { "Open Hat", P (Model::HiHat).set (P_DECAY, 18).ch (P_CHOKE, 1).norm (P_PAN, 0.56f).norm (P_HUMANIZE, 0.2f).p },
        { "Clap", clapPatch() } });
    k.fillRate = 3.f;

    // A: two-step with ghosted snares
    PatternData& a = k.patterns[0];
    lane (a, 0, "X.........x.....");
    lane (a, 3, "....X.x..x..X.xx");
    xl (a, 3, X_GHOST,  { 0,0,0,0, 0,0,2,0, 0,3,0,0, 0,0,2,1 });
    xl (a, 3, X_NUDGE,  { 0,0,0,0, 0,0,2,0, 0,3,0,0, 0,0,1,2 });
    xl (a, 3, X_TONE,   { 0,0,0,0, 0,0,-1,0, 0,-3,0,0, 0,0,-1,0 });
    xl (a, 3, X_CHANCE, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,2 });
    lane (a, 5, "x.xxx.x.x.xxx.x.");
    xl (a, 5, X_GHOST,  { 0,0,0,1, 0,0,0,0, 0,0,0,1, 0,0,0,0 });
    xl (a, 5, X_NUDGE,  { 0,0,2,2, 0,0,2,0, 0,0,2,2, 0,0,2,0 });
    xl (a, 5, X_TONE,   { 0,0,-1,-2, 0,0,-1,0, 0,0,-1,-2, 0,0,-1,0 });
    lane (a, 6, "..............x.");
    xl (a, 6, X_CHANCE, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,8,0 });   // every other loop
    lane (a, 4, "X...............");
    xl (a, 4, X_CHANCE, { 10,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 });  // first of every four loops

    // B: the turnaround: drags, a flam, ratchets and a tom run
    PatternData& b = k.patterns[1];
    b = a;
    lane (b, 3, "....X.x..x.xX.xx");
    xl (b, 3, X_GHOST,  { 0,0,0,0, 0,0,2,0, 0,3,0,2, 0,0,2,0 });
    xl (b, 3, X_ROLL,   { 0,0,0,0, 1,0,0,0, 0,0,0,0, 2,0,0,4 });
    lane (b, 1, ".........x.x.x..");
    xl (b, 1, X_PITCH,  { 0,0,0,0, 0,0,0,0, 0,5,0,2, 0,-3,0,0 });
    xl (b, 1, X_GHOST,  { 0,0,0,0, 0,0,0,0, 0,0,0,1, 0,1,0,0 });
    lane (b, 2, "..x.....x.....x.");
    xl (b, 2, X_CHANCE, { 0,0,4,0, 0,0,0,0, 0,0,0,0, 0,0,3,0 });
    xl (b, 2, X_GHOST,  { 0,0,1,0, 0,0,0,0, 2,0,0,0, 0,0,1,0 });
    a.chainNext = true;
    return k;
}

std::vector<KitData> factoryKits()
{
    return { initKit(), concrete(), bitElectro(), metalPerc(), jungleGhosts() };
}
} // namespace ferrum
