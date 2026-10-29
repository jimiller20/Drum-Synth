// DicePanel.h - the randomizer settings pop-over: beat style, density, complexity, expression,
// kit character and key, Evolve, roll history and the roll buttons.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ui
{
class DicePanel : public juce::Component
{
public:
    DicePanel (Ferrum8Processor& p, std::function<void (const juce::String&)> statusFn);
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int kW = 440, kH = 532;

private:
    void sync();                                   // controls <- processor
    void roll (int kind);                          // 0 sounds, 1 beat, 2 all, 3 morph targets
    void showHistory();

    Ferrum8Processor& proc;
    std::function<void (const juce::String&)> status;
    juce::ComboBox style, genre, character, key, evolve, every;
    juce::Slider density, complexity, expression, mutation;
    juce::TextButton scopeOne { "Selected pattern" }, scopeAll { "All patterns A-L" };
    juce::TextButton soundsBtn { "Sounds" }, beatBtn { "Beat" }, allBtn { "All" }, morphBtn { "Morph B" };
    juce::TextButton historyBtn { "History" }, keepBtn { "Keep variation" };
    juce::Rectangle<int> beatHeader, kitHeader, evolveHeader, rollHeader;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
};
} // namespace ui
