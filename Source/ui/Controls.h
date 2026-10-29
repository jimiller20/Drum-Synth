// Controls.h - knobs, choice boxes, segmented selectors and icon buttons bound to host parameters
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"

namespace ui
{
// A rotary knob. Drag up/down (Shift = fine), Ctrl/Cmd-click or double-click = default,
// right-click = menu with "Enter value" and "Reset", mouse wheel nudges.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob();
    ~Knob() override;
    void bind (juce::RangedAudioParameter* p);
    juce::RangedAudioParameter* getParameter() const { return param; }
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setBipolar (bool b) { bipolar = b; repaint(); }
    void setInactive (bool b) { inactive = b; repaint(); }

    std::function<void()> onGestureStart;
    std::function<void (float)> onUserDelta;              // normalised delta from the user (Edit All)
    std::function<juce::String (float)> formatter;        // optional override of the parameter's text

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    void setNorm (float n, bool gesture);
    void resetToDefault();
    juce::String valueText() const;

    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> att;
    float norm = 0.f, dragStartNorm = 0.f, accumDrag = 0.f;
    int lastY = 0;
    bool dragging = false, hover = false, bipolar = false, inactive = false;
    juce::String label;
    juce::Colour accent = col::accent;
};

// A choice parameter shown as a box (text or waveform icon); click for the list, wheel/drag to step.
class ChoiceBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style { Text, Waves, LfoWaves };
    ChoiceBox();
    ~ChoiceBox() override;
    void bind (juce::RangedAudioParameter* p);
    juce::RangedAudioParameter* getParameter() const { return param; }
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setStyle (Style s) { style = s; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setInactive (bool b) { inactive = b; repaint(); }
    int getIndex() const { return index; }
    std::function<void()> onGestureStart;
    std::function<void (int)> onUserChoice;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void choose (int idx);
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> att;
    int index = 0, dragStartIndex = 0;
    bool dragged = false, inactive = false;
    float wheelAccum = 0.f;
    Style style = Style::Text;
    juce::String label;
    juce::Colour accent = col::accent;
};

// A row of buttons selecting one option of a choice (or bool) parameter.
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented();
    ~Segmented() override;
    void bind (juce::RangedAudioParameter* p, juce::StringArray displayNames = {});
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setVertical (bool v) { vertical = v; repaint(); }
    int getIndex() const { return index; }
    std::function<void()> onGestureStart;
    std::function<void (int)> onUserChoice;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> segmentArea() const;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> att;
    juce::StringArray names;
    int index = 0;
    bool vertical = false;
    juce::String label;
    juce::Colour accent = col::accent;
};

// Flat button with a drawn icon or short text
class IconButton : public juce::Button
{
public:
    enum class Icon { None, Menu, Folder, Disk, Dice, Undo, Redo, Prev, Next, Play, Stop, Copy, Paste, Matrix, Midi, Alter, ChainLeft, ChainRight, Edit, Down, Lock, Unlock };
    IconButton (Icon i = Icon::None, const juce::String& text = {});
    void setIcon (Icon i) { icon = i; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setBlink (bool b) { blink = b; repaint(); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    static void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);

private:
    Icon icon;
    juce::Colour accent = col::accent;
    bool blink = false;
};
} // namespace ui
