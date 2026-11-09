// ModPanel.h - the MOD tab: six modulators (LFO, envelope, sample and hold, peak controller, random
// drift, step lane, macro) with a live scope, their settings and up to eight routes each.
// Routes can also be made from any sound or FX knob: right-click > Modulate with.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ui
{
// bipolar depth bar (-100 % .. +100 %), drag to set, double-click to zero
class DepthBar : public juce::Component
{
public:
    std::function<void (float)> onChange;
    std::function<void()> onGestureStart;
    void setValue (float v) { value = juce::jlimit (-1.f, 1.f, v); repaint(); }
    float getValue() const { return value; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    float value = 0.f, startValue = 0.f;
};

class ModPanel : public juce::Component, public juce::TooltipClient
{
public:
    explicit ModPanel (Ferrum8Processor& p);
    void refresh();          // processor state changed
    void tick();             // 30 Hz: scopes
    void select (int m);
    std::function<void (const juce::String&)> showStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static juce::String targetName (const Ferrum8Processor& p, const ferrum::mod::Route& r);
    // fills a menu with every target (ids from base up); decode with routeFromMenu
    static void addTargetMenu (const Ferrum8Processor& p, juce::PopupMenu& m, int base);
    static bool routeFromMenu (const Ferrum8Processor& p, int id, int base, ferrum::mod::Route& r);

private:
    juce::Rectangle<int> card (int m) const;
    juce::Rectangle<int> stepArea() const;
    void edit (const std::function<void (ferrum::mod::ModState&)>& f, bool undo = false);
    void syncControls();
    void layoutControls();
    void pickTarget (int route);

    Ferrum8Processor& proc;
    int sel = 0;
    Segmented typeSel;
    std::array<Knob, ferrum::mod::kKnobs> knobs;
    std::array<ChoiceBox, ferrum::mod::kKnobs> choices;
    ChoiceBox trigger, source;
    Knob macro;
    std::array<juce::TextButton, ferrum::mod::kRoutes> routeBtns;
    std::array<DepthBar, ferrum::mod::kRoutes> depthBars;
    std::array<IconButton, ferrum::mod::kRoutes> removeBtns;
    IconButton addBtn { IconButton::Icon::None, "+ Add target" };
    std::array<std::array<float, 120>, ferrum::mod::kMods> history {};
    int histPos = 0;
    juce::Rectangle<int> scope, controlsArea, routesArea, header;
    int dragStep = -1;
};
} // namespace ui
