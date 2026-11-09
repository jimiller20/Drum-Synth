// LookAndFeel.h - colours, fonts and drawing helpers for the Ferrum 8 editor
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
namespace col
{
    const juce::Colour bg       { 0xff111317 };
    const juce::Colour panel    { 0xff191c21 };
    const juce::Colour panel2   { 0xff20242a };
    const juce::Colour raised   { 0xff272b33 };
    const juce::Colour line     { 0xff2c313a };
    const juce::Colour text     { 0xffd8dce2 };
    const juce::Colour dim      { 0xff858d99 };
    const juce::Colour faint    { 0xff4b525d };
    const juce::Colour accent   { 0xffff7a1a };   // signal orange
    const juce::Colour play     { 0xff44d39a };   // playing / running
    const juce::Colour mod      { 0xff4fd0ff };   // modulation
    const juce::Colour mute     { 0xffff4f4f };
    const juce::Colour select   { 0xff4ea3ff };   // selected pattern
    juce::Colour channel (int ch);
}

juce::Font font (float height, bool bold = false);
void drawWaveIcon (juce::Graphics& g, juce::Rectangle<float> r, int wave, juce::Colour c, float thickness = 1.4f);
void drawLfoIcon (juce::Graphics& g, juce::Rectangle<float> r, int wave, juce::Colour c);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Font getPopupMenuFont() override { return font (15.f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (std::min (15.f, h * 0.62f), true); }
    juce::Font getLabelFont (juce::Label&) override { return font (15.f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (15.f); }
    juce::Font getAlertWindowMessageFont() override { return font (16.f); }
    juce::Font getAlertWindowTitleFont() override { return font (19.f, true); }
    juce::Font getAlertWindowFont() override { return font (15.f); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};
} // namespace ui
