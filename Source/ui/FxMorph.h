// FxMorph.h - the FX Morph pad (four FX scenes in the corners, a puck that blends them) and the
// panel around it: Morph on/off, and Store / Edit / Clear for each scene. A small pad also sits in
// the FX RACK section at the bottom of the window, so the morph can be played from every tab.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ui
{
class FxMorphPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    FxMorphPad (Ferrum8Processor& p, bool mini);
    std::function<void()> onOpenPanel;          // mini pad: click on its title (or with no scenes yet)
    std::function<void (const juce::String&)> showStatus;
    void tick();                                // 30 Hz: follows automation
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    static juce::Colour sceneColour (int i);

private:
    juce::Rectangle<float> padArea() const;
    juce::Rectangle<float> titleArea() const;
    void moveTo (juce::Point<float> p);
    Ferrum8Processor& proc;
    bool mini, dragging = false;
    float shownX = -1.f, shownY = -1.f;
    bool shownOn = false;
    int shownUsed = -1;
};

class FxMorphPanel : public juce::Component
{
public:
    FxMorphPanel (Ferrum8Processor& p, std::function<void (const juce::String&)> status);
    ~FxMorphPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void sync();

private:
    class Ticker : public juce::Timer { public: std::function<void()> f; void timerCallback() override { if (f) f(); } } ticker;
    Ferrum8Processor& proc;
    std::function<void (const juce::String&)> status;
    FxMorphPad pad;
    IconButton onBtn { IconButton::Icon::None, "FX MORPH: OFF" };
    std::array<IconButton, 4> storeBtns, editBtns, clearBtns;
    std::array<juce::Rectangle<int>, 4> rowArea;
    juce::Rectangle<int> helpArea;
};
} // namespace ui
