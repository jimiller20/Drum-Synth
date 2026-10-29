// Kits.h - factory presets: eight drum patches (with morph end points), twelve patterns and
// the pattern-engine settings.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../dsp/FxBus.h"
#include "../dsp/PatchParams.h"
#include "Engine.h"
#include "Patterns.h"
#include <string>
#include <vector>

namespace ferrum
{
struct KitData
{
    std::string name;
    std::array<std::string, kNumChannels> channelNames;
    std::array<Patch, kNumChannels> a, b;            // morph end points (a == b means no morph)
    std::array<PatternData, kNumPatterns> patterns;
    StepRate rate = StepRate::Sixteenth;
    float swing = 0.f, fillRate = 2.f, morph = 0.f;
    fx::RackState rack;                              // FX 1, FX 2 and Master (empty = no effects)
};

KitData initKit();                      // the LXR-02 init kit mapped onto eight channels
std::vector<KitData> factoryKits();     // initKit() first
} // namespace ferrum
