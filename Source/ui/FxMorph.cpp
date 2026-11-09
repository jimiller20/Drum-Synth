// FxMorph.cpp - see FxMorph.h
// SPDX-License-Identifier: GPL-3.0-only
#include "FxMorph.h"

namespace ui
{
using namespace juce;
using namespace ferrum;

static const char* const kCornerNames[4] = { "top left", "top right", "bottom left", "bottom right" };

Colour FxMorphPad::sceneColour (int i)
{
    static const Colour c[4] = { Colour (0xffff7a1a), Colour (0xff4fd0ff), Colour (0xff44d39a), Colour (0xffd77bff) };
    return c[jlimit (0, 3, i)];
}

FxMorphPad::FxMorphPad (Ferrum8Processor& p, bool m) : proc (p), mini (m)
{
    setTooltip (mini ? "FX Morph: drag to blend the four FX scenes (click the title to store and edit scenes)"
                     : "Drag the puck: the FX blend between the scenes in the corners (host automation: FX Morph X / Y)");
}

Rectangle<float> FxMorphPad::titleArea() const
{
    return mini ? getLocalBounds().toFloat().removeFromTop (17.f) : Rectangle<float>();
}

Rectangle<float> FxMorphPad::padArea() const
{
    auto r = getLocalBounds().toFloat().reduced (1.f);
    if (mini) r.removeFromTop (18.f);
    return r.reduced (mini ? 7.f : 14.f, mini ? 5.f : 14.f);
}

void FxMorphPad::tick()
{
    const float x = proc.fxMorphX(), y = proc.fxMorphY();
    int used = 0;
    for (int i = 0; i < 4; ++i) used |= (proc.fxSceneUsed (i) ? 1 : 0) << i;
    used |= (proc.fxEditScene() + 1) << 4;
    if (std::abs (x - shownX) > 1e-4f || std::abs (y - shownY) > 1e-4f || proc.isFxMorphOn() != shownOn || used != shownUsed)
    {
        shownX = x; shownY = y; shownOn = proc.isFxMorphOn(); shownUsed = used;
        repaint();
    }
}

void FxMorphPad::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const bool on = proc.isFxMorphOn();
    g.setColour (mini ? col::raised : col::panel);
    g.fillRoundedRectangle (b.reduced (0.5f), 5.f);
    g.setColour (on ? col::accent.withAlpha (0.6f) : col::line.brighter (0.1f));
    g.drawRoundedRectangle (b.reduced (0.5f), 5.f, 1.f);
    if (mini)
    {
        auto t = titleArea().reduced (8.f, 0.f).translated (0.f, 2.f);
        g.setFont (font (14.5f, true));
        g.setColour (col::text);
        g.drawText ("MORPH", t, Justification::centredLeft);
        g.setFont (font (12.5f, true));
        g.setColour (on ? col::accent : col::faint);
        g.drawText (on ? "ON" : "OFF", t, Justification::centredRight);
    }
    const auto pa = padArea();
    g.setColour (col::bg);
    g.fillRoundedRectangle (pa, 4.f);
    // grid
    g.setColour (col::line);
    for (int i = 1; i < 4; ++i)
    {
        g.drawHorizontalLine ((int) (pa.getY() + pa.getHeight() * (float) i / 4.f), pa.getX(), pa.getRight());
        g.drawVerticalLine ((int) (pa.getX() + pa.getWidth() * (float) i / 4.f), pa.getY(), pa.getBottom());
    }
    const float x = jlimit (0.f, 1.f, proc.fxMorphX()), y = jlimit (0.f, 1.f, proc.fxMorphY());
    const Point<float> puck (pa.getX() + x * pa.getWidth(), pa.getY() + y * pa.getHeight());
    std::array<bool, 4> used {};
    for (int i = 0; i < 4; ++i) used[(size_t) i] = proc.fxSceneUsed (i);
    const auto w = fx::FxRack::morphWeights (x, y, used);
    const float cr = mini ? 4.5f : 11.f;
    for (int i = 0; i < 4; ++i)
    {
        const Point<float> c (i % 2 == 0 ? pa.getX() : pa.getRight(), i < 2 ? pa.getY() : pa.getBottom());
        const auto sc = sceneColour (i);
        if (used[(size_t) i] && on && w[(size_t) i] > 0.001f)
        {
            g.setColour (sc.withAlpha (0.15f + 0.6f * w[(size_t) i]));
            g.drawLine (Line<float> (c, puck), 1.f + 2.5f * w[(size_t) i]);
        }
        g.setColour (used[(size_t) i] ? sc : col::panel2);
        g.fillEllipse (c.x - cr, c.y - cr, cr * 2.f, cr * 2.f);
        g.setColour (used[(size_t) i] ? sc.brighter (0.3f) : col::faint);
        g.drawEllipse (c.x - cr, c.y - cr, cr * 2.f, cr * 2.f, 1.f);
        if (proc.fxEditScene() == i)
        {
            g.setColour (Colours::white.withAlpha (0.85f));
            g.drawEllipse (c.x - cr - 3.f, c.y - cr - 3.f, (cr + 3.f) * 2.f, (cr + 3.f) * 2.f, 1.5f);
        }
        if (! mini)
        {
            g.setColour (used[(size_t) i] ? Colours::black : col::dim);
            g.setFont (font (14.f, true));
            g.drawText (String (i + 1), Rectangle<float> (c.x - cr, c.y - cr, cr * 2.f, cr * 2.f), Justification::centred);
        }
    }
    const float pr = mini ? 5.f : 9.f;
    g.setColour (on ? col::accent : col::dim);
    g.fillEllipse (puck.x - pr, puck.y - pr, pr * 2.f, pr * 2.f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawEllipse (puck.x - pr, puck.y - pr, pr * 2.f, pr * 2.f, 1.f);
    if (! mini && ! on)
    {
        g.setColour (col::dim);
        g.setFont (font (15.f, true));
        g.drawText ("Morph is off", pa.withTrimmedTop (pa.getHeight() * 0.5f - 30.f).withHeight (20.f), Justification::centred);
    }
}

void FxMorphPad::moveTo (Point<float> p)
{
    const auto pa = padArea();
    proc.setFxMorphXY ((p.x - pa.getX()) / pa.getWidth(), (p.y - pa.getY()) / pa.getHeight());
    tick();
}

void FxMorphPad::mouseDown (const MouseEvent& e)
{
    bool any = false;
    for (int i = 0; i < 4; ++i) any |= proc.fxSceneUsed (i);
    if (mini && (titleArea().contains (e.position) || ! any || e.mods.isPopupMenu()))
    {
        if (onOpenPanel) onOpenPanel();
        return;
    }
    dragging = true;
    proc.fxMorphGesture (true);
    moveTo (e.position);
    if (! proc.isFxMorphOn() && showStatus) showStatus ("FX Morph is off - turn it on in the FX Morph panel to hear the blend");
}

void FxMorphPad::mouseDrag (const MouseEvent& e) { if (dragging) moveTo (e.position); }

void FxMorphPad::mouseUp (const MouseEvent&)
{
    if (dragging) proc.fxMorphGesture (false);
    dragging = false;
}

//==============================================================================================
FxMorphPanel::FxMorphPanel (Ferrum8Processor& p, std::function<void (const String&)> s) : proc (p), status (std::move (s)), pad (p, false)
{
    addAndMakeVisible (pad);
    addAndMakeVisible (onBtn);
    pad.showStatus = status;
    onBtn.setTooltip ("While Morph is on the FX rack plays the blend of the stored scenes (where the puck is). "
                      "The rack shows the edit scene; your edits go into it.");
    onBtn.onClick = [this]
    {
        proc.pushUndo();
        proc.setFxMorphOn (! proc.isFxMorphOn());
        if (status) status (proc.isFxMorphOn() ? "FX Morph on: the rack plays the blend of the scenes (drag the puck or automate FX Morph X / Y)"
                                               : "FX Morph off: the rack plays as it is shown");
        sync();
    };
    for (int i = 0; i < 4; ++i)
    {
        for (auto* b : { &storeBtns[(size_t) i], &editBtns[(size_t) i], &clearBtns[(size_t) i] }) addAndMakeVisible (b);
        storeBtns[(size_t) i].setButtonText ("Store");
        editBtns[(size_t) i].setButtonText ("Edit");
        clearBtns[(size_t) i].setButtonText ("Clear");
        storeBtns[(size_t) i].setTooltip ("Store the FX rack's modules (and their settings) as scene " + String (i + 1));
        editBtns[(size_t) i].setTooltip ("Load scene " + String (i + 1) + " into the FX rack to edit it (with Morph on, edits go straight into the scene)");
        clearBtns[(size_t) i].setTooltip ("Empty scene " + String (i + 1));
        storeBtns[(size_t) i].onClick = [this, i]
        {
            proc.pushUndo();
            proc.storeFxScene (i);
            if (status) status ("Scene " + String (i + 1) + " (" + kCornerNames[i] + ") = " + proc.fxSceneSummary (i));
            sync();
        };
        editBtns[(size_t) i].onClick = [this, i]
        {
            proc.pushUndo();
            proc.recallFxScene (i);
            if (status) status ("Editing scene " + String (i + 1) + ": the FX rack shows it" + (proc.isFxMorphOn() ? ", and your edits go into it" : String()));
            sync();
        };
        clearBtns[(size_t) i].onClick = [this, i]
        {
            proc.pushUndo();
            proc.clearFxScene (i);
            sync();
        };
    }
    ticker.f = [this] { pad.tick(); };
    ticker.startTimerHz (30);
    sync();
    setSize (600, 380);
}

FxMorphPanel::~FxMorphPanel() { ticker.stopTimer(); }

void FxMorphPanel::sync()
{
    const bool on = proc.isFxMorphOn();
    onBtn.setButtonText (on ? "FX MORPH: ON" : "FX MORPH: OFF");
    onBtn.setToggleState (on, dontSendNotification);
    for (int i = 0; i < 4; ++i)
    {
        editBtns[(size_t) i].setEnabled (proc.fxSceneUsed (i));
        clearBtns[(size_t) i].setEnabled (proc.fxSceneUsed (i));
        editBtns[(size_t) i].setToggleState (proc.fxEditScene() == i, dontSendNotification);
    }
    pad.tick();
    repaint();
}

void FxMorphPanel::resized()
{
    auto r = getLocalBounds().reduced (14);
    pad.setBounds (r.removeFromLeft (250).withHeight (250).translated (0, 30));
    r.removeFromLeft (16);
    onBtn.setBounds (r.removeFromTop (30));
    r.removeFromTop (10);
    for (int i = 0; i < 4; ++i)
    {
        auto row = r.removeFromTop (52);
        rowArea[(size_t) i] = row;
        auto btns = row.removeFromBottom (24);
        clearBtns[(size_t) i].setBounds (btns.removeFromRight (64));
        btns.removeFromRight (4);
        editBtns[(size_t) i].setBounds (btns.removeFromRight (64));
        btns.removeFromRight (4);
        storeBtns[(size_t) i].setBounds (btns.removeFromRight (64));
        r.removeFromTop (6);
    }
    helpArea = r;
}

void FxMorphPanel::paint (Graphics& g)
{
    g.fillAll (col::panel2);
    g.setColour (col::accent);
    g.setFont (font (20.f, true));
    g.drawText ("FX MORPH", 14, 10, 250, 24, Justification::centredLeft);
    for (int i = 0; i < 4; ++i)
    {
        auto row = rowArea[(size_t) i];
        auto top = row.removeFromTop (22);
        g.setColour (FxMorphPad::sceneColour (i));
        g.fillEllipse ((float) top.getX(), (float) top.getCentreY() - 6.f, 12.f, 12.f);
        g.setColour (col::text);
        g.setFont (font (15.f, true));
        g.drawText ("Scene " + String (i + 1) + "  " + kCornerNames[i] + (proc.fxEditScene() == i ? "  (editing)" : ""),
                    top.withTrimmedLeft (18), Justification::centredLeft);
        g.setColour (proc.fxSceneUsed (i) ? col::dim : col::faint);
        g.setFont (font (12.5f));
        auto sum = row.withTrimmedRight (3 * 68).withTrimmedLeft (18);
        g.drawFittedText (proc.fxSceneSummary (i), sum, Justification::centredLeft, 2, 0.8f);
    }
    g.setColour (col::dim);
    g.setFont (font (13.f));
    g.drawFittedText ("Store up to four FX rack setups in the corners, turn Morph on and move the puck: settings blend and modules "
                      "fade in and out between the scenes. Dry, Out, lane levels and bus outputs stay the rack's own.",
                      helpArea.withTrimmedTop (2), Justification::topLeft, 4, 0.85f);
}
} // namespace ui
