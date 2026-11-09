// Controls.h - knobs, choice boxes, segmented selectors and icon buttons bound to host parameters
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"

namespace ui
{
// A rotary knob. Drag up/down (Shift = fine), Ctrl/Cmd-click or double-click = default,
// right-click = menu with "Enter value" and "Reset", mouse wheel nudges.
class Knob : public juce::Component, public juce::SettableTooltipClient, public juce::DragAndDropTarget
{
public:
    Knob();
    ~Knob() override;
    void bind (juce::RangedAudioParameter* p);
    juce::RangedAudioParameter* getParameter() const { return param; }

    // Free mode: the knob edits a value that is not a host parameter (the FX rack modules).
    // Values are normalised 0..1; get() is read again by refresh().
    struct Free
    {
        juce::String name;
        float def = 0.f;
        std::function<float()> get;
        std::function<void (float)> set;
        std::function<juce::String (float)> text;
        std::function<bool (const juce::String&, float&)> parse; // optional: typed value -> normalised
    };
    void bindFree (Free f);
    void refresh();
    bool isBound() const { return param != nullptr || free.set != nullptr; }

    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setBipolar (bool b) { bipolar = b; repaint(); }
    // modulation: the range a modulator moves this knob by (signed, 0 = none), drawn as an outer arc
    void setModRange (float depth) { if (depth != modRange) { modRange = depth; repaint(); } }
    // extra items in the right-click menu (ids from 1000 up go to onExtendedMenu)
    std::function<void (juce::PopupMenu&)> extendMenu;
    std::function<void (int)> onExtendedMenu;
    void setInactive (bool b) { inactive = b; repaint(); }
    // a modulator dragged from the MOD strip ("ferrum-mod:<n>") and dropped here; set by panels whose
    // knobs can be modulated
    std::function<void (int modulator)> onModDrop;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

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
    Free free;
    float modRange = 0.f;
    float norm = 0.f, dragStartNorm = 0.f, accumDrag = 0.f;
    int lastY = 0;
    bool dragging = false, hover = false, bipolar = false, inactive = false, dropHover = false;
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
    // free mode: a list that is not a host parameter (setIndex() updates the display)
    void bindFree (const juce::String& name, const juce::StringArray& choices, int index);
    void setIndex (int i) { index = i; repaint(); }
    // shown instead of the parameter's own choice names (same count and order)
    void setDisplayNames (const juce::StringArray& n) { displayNames = n; repaint(); }
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
    juce::StringArray names() const;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> att;
    juce::String freeName;
    juce::StringArray freeChoices, displayNames;
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
    void bindFree (const juce::StringArray& options, int index);   // not a host parameter: use onUserChoice
    void setIndex (int i) { index = i; repaint(); }
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
    bool vertical = false, isFree = false;
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

    // Positional dice: where the button is clicked sets how much it changes, from ~1 % at the left
    // edge to 100 % at the right edge (the last 8 % of the width is a full 100 %). The amount is shown
    // while hovering; read it in onClick with clickAmount().
    void setPositional (bool p) { positional = p; repaint(); }
    bool isPositional() const { return positional; }
    float clickAmount() const { return amount; }
    static float amountAt (float x, float width);
    static juce::String amountText (float a) { return juce::String (juce::roundToInt (a * 100.f)) + " %"; }
    // right-click: called instead of a click (for buttons with a settings menu)
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    Icon icon;
    juce::Colour accent = col::accent;
    bool blink = false, positional = false, rightDown = false;
    float amount = 1.f, hoverX = -1.f;
};
} // namespace ui
