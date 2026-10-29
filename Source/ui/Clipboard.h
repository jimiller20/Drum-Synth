// Clipboard.h - internal clipboard shared by all editor panels (and plug-in instances)
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../engine/Patterns.h"
#include "../dsp/PatchParams.h"
#include <juce_data_structures/juce_data_structures.h>
#include <optional>

namespace ui
{
struct PatchClip { ferrum::Patch a {}, b {}; juce::String name; };
struct ChannelClip { PatchClip patch; std::array<ferrum::PatternData::Channel, ferrum::kNumPatterns> steps {}; };
struct PatternClip { ferrum::PatternData data; bool channelOnly = false; int channel = 0; };

struct Clipboard
{
    static inline std::optional<PatchClip> patch;
    static inline std::optional<ChannelClip> channel;
    static inline std::optional<PatternClip> pattern;
    static inline std::optional<juce::ValueTree> preset;
};
} // namespace ui
