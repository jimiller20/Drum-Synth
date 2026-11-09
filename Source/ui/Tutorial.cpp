// Tutorial.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Tutorial.h"

namespace ui
{
using namespace juce;

static std::unique_ptr<PropertiesFile> settingsFile()
{
    PropertiesFile::Options o;
    o.applicationName = "Ferrum8";
    o.filenameSuffix = ".settings";
    o.folderName = "Ferrum 8";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<PropertiesFile> (o);
}

bool Tutorial::hasBeenSeen() { return settingsFile()->getBoolValue ("tutorialSeen", false); }
void Tutorial::markSeen() { auto f = settingsFile(); f->setValue ("tutorialSeen", true); f->saveIfNeeded(); }

// the dice picture: one wide button, the amount under the mouse, and the scale under it
static void drawDicePicture (Graphics& g, Rectangle<float> r)
{
    auto b = r.withSizeKeepingCentre (jmin (420.f, r.getWidth() - 40.f), 40.f).withY (r.getY() + 14.f);
    g.setColour (col::raised); g.fillRoundedRectangle (b, 5.f);
    g.setColour (col::line.brighter (0.2f)); g.drawRoundedRectangle (b, 5.f, 1.f);
    const float x = b.getX() + b.getWidth() * 0.36f;
    ColourGradient grad (col::accent.withAlpha (0.25f), b.getX(), 0.f, col::accent, b.getRight(), 0.f, false);
    g.setGradientFill (grad);
    g.fillRect (b.getX() + 3.f, b.getBottom() - 5.f, x - b.getX() - 3.f, 3.f);
    IconButton::drawIcon (g, IconButton::Icon::Dice, b.withWidth (b.getHeight()).reduced (10.f), col::text);
    g.setColour (col::text); g.setFont (font (17.f, true));
    g.drawText ("Sounds  36 %", b.withTrimmedLeft (b.getHeight()), Justification::centredLeft);
    // the mouse
    Path arrow;
    arrow.startNewSubPath (x, b.getCentreY()); arrow.lineTo (x, b.getCentreY() + 20.f); arrow.lineTo (x + 5.f, b.getCentreY() + 15.f);
    arrow.lineTo (x + 10.f, b.getCentreY() + 23.f); arrow.lineTo (x + 13.f, b.getCentreY() + 21.f); arrow.lineTo (x + 8.f, b.getCentreY() + 13.f);
    arrow.lineTo (x + 14.f, b.getCentreY() + 13.f); arrow.closeSubPath();
    g.setColour (Colours::white); g.fillPath (arrow);
    g.setColour (Colours::black); g.strokePath (arrow, PathStrokeType (1.f));
    // the scale
    auto s = b.withY (b.getBottom() + 18.f).withHeight (8.f);
    g.setGradientFill (ColourGradient (col::accent.withAlpha (0.15f), s.getX(), 0.f, col::accent, s.getRight(), 0.f, false));
    g.fillRoundedRectangle (s, 3.f);
    g.setColour (col::dim); g.setFont (font (14.f));
    g.drawText ("1 %  a nudge", s.withY (s.getBottom() + 4.f).withHeight (18.f), Justification::centredLeft);
    g.drawText ("50 %", s.withY (s.getBottom() + 4.f).withHeight (18.f), Justification::centred);
    g.drawText ("100 %  all new", s.withY (s.getBottom() + 4.f).withHeight (18.f), Justification::centredRight);
}

Tutorial::Tutorial()
{
    setWantsKeyboardFocus (true);
    pages.push_back ({ "Welcome to Ferrum 8",
        "Eight drum channels with the LXR-02 sound engine, a Microtonic-style pattern sequencer, an FX rack and modulation.\n\n"
        "Top: presets, Sound Morph (blends every channel between its A and B sound) and the dice.\n"
        "Channel strip: click a channel to edit it (and hear it), M mutes, the lock keeps it out of the dice.\n"
        "Tabs: DRUMS (sound + pattern), FX RACK and MOD. The Tab key switches between them.\n\n"
        "Hover anything for a tip. The line at the very bottom says what just happened. Ctrl+Z undoes almost everything.", {} });
    pages.push_back ({ "Dice: where you click sets how much",
        "Every dice button changes things by the amount under the mouse: click near the LEFT edge for about 1 % (a nudge), "
        "the middle for about 50 %, the RIGHT edge for 100 % (a completely new roll). The button shows the amount while you hover.\n\n"
        "This works on Sounds, Beat and All, Random and Alter on a sound, Roll Chain on the FX rack, the step dice and the "
        "expression-lane dice of the pattern, Mutate Now and the Dice panel's buttons (the arrow next to All). Its History button brings back the last 16 rolls.",
        drawDicePicture });
    pages.push_back ({ "Sound: morph, mutation and keys",
        "Each channel has two sounds, A and B. Sound Morph blends between them; the dice write the end the slider is nearest to.\n\n"
        "Mutate (in the sound header) moves one channel's sound a little every few loops, each channel at its own rate. "
        "Orbit wanders around the home sound, Drift keeps travelling. Mutations stay in the Dice panel's key and keep the "
        "volume even: when the drive goes up, the level comes down.", {} });
    pages.push_back ({ "Modulation",
        "The MOD strip at the right of the sound section holds the six modulators. Drag one onto any knob to make it move "
        "that knob (30 % to start); the blue ring around the knob shows the range. Drag on a route bar in the strip to set "
        "its depth, double-click it to remove it.\n\n"
        "Click a modulator to open it on the MOD tab: LFO, envelope, sample and hold, peak controller (follows a channel or "
        "the main mix, like FL's Peak Controller), random drift, a step lane or a macro. Right-click a knob > Modulate with "
        "does the same from the knob.", {} });
    pages.push_back ({ "FX rack and FX Morph",
        "Three buses: FX 1, FX 2 and Master. In each bus the three lanes run side by side (parallel) and the five slots of "
        "a lane run one after another (series). Dry Thru is how much of the untouched signal passes next to the lanes. "
        "Send a channel to a bus with its Output (FX 1 / FX 2) or the channel chips on the routing map.\n\n"
        "FX Morph keeps four FX scenes in the corners of an XY pad. Store the rack in a corner, change it, store the next "
        "one, then move the puck (or automate FX Morph X / Y): settings blend and effects fade in and out between the scenes.", {} });
    pages.push_back ({ "Channels on their own mixer tracks",
        "Ferrum 8 has 12 stereo outputs: A (main), B, Ch 1 - Ch 8 and FX 1 / FX 2.\n\n"
        "1.  Send a channel to its own output: DRUMS > Routing > Output > 'Ch n Out'. Main menu > Outputs sends every "
        "channel to its own output in one go (and back to A).\n"
        "2.  In FL Studio open the plugin's wrapper settings, Processing tab, and give each output a mixer track in the "
        "output connections (or use any track's input menu).\n\n"
        "The channel strip shows '> CH n OUT' on channels that use their own output. Outputs the host leaves "
        "switched off fall back to A, so nothing goes silent.", {} });
    pages.push_back ({ "Step variations per channel",
        "The dice left of the pattern grid rolls new steps for the selected channel. Right-click it to make that channel "
        "vary by itself: re-roll every 1, 2, 4, 8 or 16 loops, by how much, around the original steps (Orbit) or wandering "
        "away (Drift). Each channel keeps its own setting, so the hats can change every bar while the kick stays put.\n\n"
        "Back To Original in the same menu restores the steps.", {} });

    for (auto* b : { &backBtn, &nextBtn, &closeBtn }) addAndMakeVisible (b);
    backBtn.onClick = [this] { showPage (page - 1); };
    nextBtn.onClick = [this] { if (page + 1 < pageCount()) showPage (page + 1); else if (onClose) onClose(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    showPage (0);
}

void Tutorial::showPage (int p)
{
    page = jlimit (0, pageCount() - 1, p);
    backBtn.setEnabled (page > 0);
    nextBtn.setButtonText (page + 1 < pageCount() ? "Next" : "Done");
    nextBtn.setIcon (page + 1 < pageCount() ? IconButton::Icon::Next : IconButton::Icon::None);
    repaint();
}

Rectangle<int> Tutorial::card() const { return getLocalBounds().withSizeKeepingCentre (680, 470); }

void Tutorial::resized()
{
    auto c = card().reduced (22, 18);
    auto bottom = c.removeFromBottom (32);
    closeBtn.setBounds (bottom.removeFromLeft (90));
    nextBtn.setBounds (bottom.removeFromRight (100));
    bottom.removeFromRight (8);
    backBtn.setBounds (bottom.removeFromRight (100));
}

void Tutorial::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.62f));
    const auto c = card();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (c.toFloat(), 8.f);
    g.setColour (col::accent.withAlpha (0.7f));
    g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 8.f, 1.5f);
    const auto& pg = pages[(size_t) page];
    auto r = c.reduced (26, 20);
    g.setColour (col::dim);
    g.setFont (font (14.f, true));
    g.drawText ("TOUR  " + String (page + 1) + " / " + String (pageCount()), r.removeFromTop (18), Justification::centredLeft);
    g.setColour (col::accent);
    g.setFont (font (26.f, true));
    g.drawText (pg.title, r.removeFromTop (36), Justification::centredLeft);
    r.removeFromTop (6);
    r.removeFromBottom (44);
    AttributedString a;
    a.append (pg.body, font (17.f), col::text);
    a.setLineSpacing (3.f);
    a.setWordWrap (AttributedString::byWord);
    TextLayout tl;
    tl.createLayout (a, (float) r.getWidth());
    tl.draw (g, r.toFloat());
    if (pg.picture) // right under the text
        pg.picture (g, r.toFloat().withTrimmedTop (jmin ((float) r.getHeight() - 110.f, tl.getHeight() + 22.f)).withHeight (110.f));
    // page dots
    const float dotsW = (float) pageCount() * 14.f;
    float x = (float) c.getCentreX() - dotsW * 0.5f;
    for (int i = 0; i < pageCount(); ++i, x += 14.f)
    {
        g.setColour (i == page ? col::accent : col::faint);
        g.fillEllipse (x + 3.f, (float) c.getBottom() - 38.f, 7.f, 7.f);
    }
}

void Tutorial::mouseDown (const MouseEvent& e)
{
    if (! card().contains (e.getPosition()) && onClose) onClose();
}

bool Tutorial::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k == KeyPress::rightKey || k == KeyPress::returnKey) { nextBtn.triggerClick(); return true; }
    if (k == KeyPress::leftKey) { showPage (page - 1); return true; }
    return false;
}
} // namespace ui
