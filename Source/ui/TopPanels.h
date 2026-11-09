// TopPanels.h - toolbar (presets, morph, undo, MIDI drag, randomise all), channel strip and
// the global section (transport, step rate, swing, fill rate, FX rack overview, master)
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Controls.h"
#include "FxMorph.h"
#include "../PluginProcessor.h"

namespace ui
{
class MorphSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    MorphSlider() { setTooltip ("Sound Morph: blends every drum patch between its A and B end points"); }
    ~MorphSlider() override { att.reset(); }
    void bind (juce::RangedAudioParameter* p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    std::function<void()> onGestureStart;
private:
    float valueAt (float x) const;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> att;
    float value = 0.f;
    bool dragging = false;
};

class Toolbar : public juce::Component
{
public:
    explicit Toolbar (Ferrum8Processor& p);
    void tick();
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    IconButton mainMenuBtn { IconButton::Icon::Menu }, prevBtn { IconButton::Icon::Prev }, nextBtn { IconButton::Icon::Next },
               morphMenuBtn { IconButton::Icon::Down }, undoBtn { IconButton::Icon::Undo }, redoBtn { IconButton::Icon::Redo },
               soundsBtn { IconButton::Icon::Dice, "Sounds" }, beatBtn { IconButton::Icon::Dice, "Beat" },
               allBtn { IconButton::Icon::Dice, "All" }, diceMenuBtn { IconButton::Icon::Down };
    juce::TextButton presetBox;
    std::function<void (int)> onStepPreset;
    std::function<void()> onPresetList, onMorphMenu;

private:
    Ferrum8Processor& proc;
    MorphSlider morph;
    juce::Rectangle<int> logoArea, midiArea, morphLabelArea;
    bool midiDragStarted = false;
};

class ChannelStrip : public juce::Component
{
public:
    explicit ChannelStrip (Ferrum8Processor& p);
    void setSelected (int ch) { selected = ch; repaint(); }
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onSelect;
    std::function<void (int, bool)> onPreview;
    std::function<void (int)> onChannelMenu;
    std::function<void (const juce::String&)> onStatus;

private:
    juce::Rectangle<int> cell (int ch) const;
    juce::Rectangle<int> muteButton (int ch) const;
    juce::Rectangle<int> lockButton (int ch) const;
    Ferrum8Processor& proc;
    int selected = 0;
    std::array<uint32_t, 8> seenTriggers {};
    std::array<float, 8> flash {};
};

class GlobalPanel : public juce::Component
{
public:
    explicit GlobalPanel (Ferrum8Processor& p);
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    std::function<void()> onPlay, onStop;
    std::function<void (int)> onOpenFx;   // a bus card was clicked: show the FX tab on that bus
    std::function<void()> onOpenFxMorph;  // the FX Morph pad's title was clicked
    FxMorphPad& getMorphPad() { return morphPad; }

private:
    juce::Rectangle<int> busCard (int b) const;
    juce::Rectangle<int> morphCard() const;
    Ferrum8Processor& proc;
    IconButton stopBtn { IconButton::Icon::Stop }, playBtn { IconButton::Icon::Play };
    FxMorphPad morphPad;
    Segmented rate;
    Knob swing, fillRate, tempo, master;
    int blink = 0, hoverCard = -1;
    std::array<float, 3> meters {};
    juce::Rectangle<int> transportArea, seqArea, fxArea, masterArea;
};
} // namespace ui
