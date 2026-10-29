// FxBus.h - the LXR-02 master FX bus (FxBus.c and FX/*.c): tube / fold / "rat" distortion,
// ring modulator, compressor and (ping-pong) delay. Channels whose output is set to "FX" are
// summed (with 16-bit saturation, as on the hardware) and processed here.
// Copyright (C) 2012-2026 Erica Synths; port (C) 2026 Ferrum 8 contributors.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LxrCore.h"
#include <string>
#include <vector>

namespace ferrum
{
enum class FxType : int { Off = 0, Distortion, RingMod, Compressor, Delay, Count };
enum class DistType : int { Tube = 0, Fold, Rat };

struct FxSettings
{
    FxType type = FxType::Off;
    float dryWet = 0.5f;               // 0..1
    float p[4] { 0.5f, 0.5f, 0.5f, 0.5f };
    DistType distType = DistType::Tube;
    int ratio = 2;                     // index into 1,2,3,4,6,8
    int ringWave = lxr::WAVE_REC;
    bool pingPong = false;
};

class FxBus
{
public:
    void prepare (double sampleRate);
    void reset();
    void setSettings (const FxSettings& s);
    // in: summed FX send (int16 scale), out: added to outL/outR (full scale = 1.0)
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    static const char* paramName (FxType t, int idx); // nullptr when the slot is unused
    static std::string formatParam (FxType t, int idx, float v);

private:
    void processDistortion (float& l, float& r);
    void setDelayTime (float t);
    float calcDelayTime (float timeCv) const;
    static float toHalf (float x);

    double sr = 44100.0;
    float srRatio = 1.0f;
    FxSettings s;
    bool first = true;

    // distortion state
    double hp1[2] {}, hp2[2] {}, hp3[2] {}, lp1[2] {}, lp2[2] {}, lp3[2] {}, slew[2] {}, lastOut[2] {};
    double cHp1 = 0, cLp1 = 0, cHp2 = 0, cHp3 = 0, cLp2 = 0;

    // ring modulator
    lxr::Osc ringOsc;
    lxr::Rng rng;

    // compressor
    double env = 0, attack = 0, release = 0, thresh = 0, ratioV = 0.5, markup = 1;

    // delay
    std::vector<float> buf;
    uint32_t mask = 0, oldRead = 0, newRead = 0, write = 0;
    float fracOld = 0, fracNew = 0, interpValue = 0, interpStep = 0;
    float feedback = 0.7f, range = 0, delayTime = 0, pendingTime = 0;
    bool timePending = false;
    lxr::OnePole tone, tone2;
};
} // namespace ferrum
