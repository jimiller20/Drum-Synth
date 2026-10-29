// LookAndFeel.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "LookAndFeel.h"
#include "BinaryData.h"

namespace ui
{
juce::Colour col::channel (int ch)
{
    static const juce::uint32 c[8] = { 0xffff6b57, 0xffffa62b, 0xffd9cf3a, 0xff63d27c,
                                       0xff3fc6d0, 0xff5b8cff, 0xffa77bff, 0xffff5fae };
    return juce::Colour (c[(size_t) juce::jlimit (0, 7, ch)]);
}

juce::Font font (float height, bool bold)
{
    static juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedMedium_ttf,
                                                                                 BinaryData::BarlowCondensedMedium_ttfSize);
    static juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf,
                                                                                   BinaryData::BarlowCondensedSemiBold_ttfSize);
    auto tf = bold ? semibold : medium;
    if (tf == nullptr) return juce::Font (juce::FontOptions (height));
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

void drawWaveIcon (juce::Graphics& g, juce::Rectangle<float> r, int wave, juce::Colour c, float th)
{
    juce::Path p;
    const float x0 = r.getX(), w = r.getWidth(), yc = r.getCentreY(), a = r.getHeight() * 0.42f;
    switch (wave)
    {
        case 0: // sine
            for (int i = 0; i <= 32; ++i)
            {
                const float t = i / 32.f, y = yc - a * std::sin (t * juce::MathConstants<float>::twoPi);
                if (i == 0) p.startNewSubPath (x0 + t * w, y); else p.lineTo (x0 + t * w, y);
            }
            break;
        case 1: // triangle
            p.startNewSubPath (x0, yc); p.lineTo (x0 + w * 0.25f, yc - a); p.lineTo (x0 + w * 0.75f, yc + a); p.lineTo (x0 + w, yc);
            break;
        case 2: // saw
            p.startNewSubPath (x0, yc + a); p.lineTo (x0 + w * 0.5f, yc - a); p.lineTo (x0 + w * 0.5f, yc + a); p.lineTo (x0 + w, yc - a);
            break;
        case 3: // square
            p.startNewSubPath (x0, yc + a); p.lineTo (x0, yc - a); p.lineTo (x0 + w * 0.5f, yc - a);
            p.lineTo (x0 + w * 0.5f, yc + a); p.lineTo (x0 + w, yc + a); p.lineTo (x0 + w, yc - a);
            break;
        case 4: // noise
        {
            static const float n[] = { 0.1f, -0.8f, 0.6f, -0.3f, 0.9f, -0.6f, 0.2f, -0.9f, 0.7f, -0.1f, 0.5f, -0.7f, 0.3f };
            for (int i = 0; i < 13; ++i)
            {
                const float x = x0 + w * i / 12.f, y = yc - a * n[i];
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            break;
        }
        default: // pwm: narrow pulses
            p.startNewSubPath (x0, yc + a); p.lineTo (x0 + w * 0.15f, yc + a); p.lineTo (x0 + w * 0.15f, yc - a);
            p.lineTo (x0 + w * 0.3f, yc - a); p.lineTo (x0 + w * 0.3f, yc + a); p.lineTo (x0 + w * 0.65f, yc + a);
            p.lineTo (x0 + w * 0.65f, yc - a); p.lineTo (x0 + w * 0.8f, yc - a); p.lineTo (x0 + w * 0.8f, yc + a); p.lineTo (x0 + w, yc + a);
            break;
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawLfoIcon (juce::Graphics& g, juce::Rectangle<float> r, int wave, juce::Colour c)
{
    juce::Path p;
    const float x0 = r.getX(), w = r.getWidth(), y0 = r.getY(), h = r.getHeight();
    auto fn = [wave] (float t) -> float
    {
        switch (wave)
        {
            case 0: return 0.5f + 0.5f * std::sin (t * juce::MathConstants<float>::twoPi);
            case 1: return 1.f - std::abs (t * 2 - 1);
            case 2: return t;
            case 3: return 1.f - t;
            case 4: return t < 0.5f ? 1.f : 0.f;
            case 5: { static const float s[] = { 0.3f, 0.9f, 0.1f, 0.6f, 0.75f }; return s[(int) std::min (4.f, t * 5)]; }
            case 6: return t * t * t;
            default: return (1 - t) * (1 - t) * (1 - t);
        }
    };
    for (int i = 0; i <= 40; ++i)
    {
        const float t = i / 40.f;
        const float x = x0 + t * w, y = y0 + h - fn (std::min (t, 0.999f)) * h;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, col::panel2);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::headerTextColourId, col::dim);
    setColour (juce::TextButton::buttonColourId, col::raised);
    setColour (juce::TextButton::buttonOnColourId, col::accent);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, col::raised);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::dim);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::line);
    setColour (juce::TextEditor::backgroundColourId, col::bg);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::ToggleButton::tickColourId, col::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, col::faint);
    setColour (juce::TooltipWindow::backgroundColourId, col::raised);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::ResizableWindow::backgroundColourId, col::panel);
    setColour (juce::ListBox::backgroundColourId, col::bg);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgc, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? col::accent : bgc;
    if (down) c = c.brighter (0.15f); else if (over) c = c.brighter (0.07f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.f);
    g.setColour (col::line.brighter (0.1f));
    g.drawRoundedRectangle (r, 3.f, 1.f);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::panel2);
    g.setColour (col::line.brighter (0.2f));
    g.drawRect (0, 0, w, h);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (col::raised);
    g.setColour (col::line.brighter (0.3f));
    g.drawRect (0, 0, w, h);
    g.setColour (col::text);
    g.setFont (font (15.f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (font (15.f), tipText, 0, 0);
    const int w = juce::jmin (360, (int) ga.getBoundingBox (0, -1, true).getWidth() + 16);
    const int h = 24;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}
} // namespace ui
