// TopPanels.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TopPanels.h"

namespace ui
{
using namespace juce;
using namespace ferrum;

//==============================================================================================
void MorphSlider::bind (RangedAudioParameter* p)
{
    att.reset();
    param = p;
    if (param != nullptr)
    {
        att = std::make_unique<ParameterAttachment> (*param, [this] (float v) { value = param->convertTo0to1 (v); repaint(); });
        att->sendInitialUpdate();
    }
}

float MorphSlider::valueAt (float x) const
{
    const auto b = getLocalBounds().toFloat().reduced (10.f, 0.f);
    return jlimit (0.f, 1.f, (x - b.getX()) / b.getWidth());
}

void MorphSlider::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float cy = b.getCentreY();
    auto track = Rectangle<float> (b.getX() + 10.f, cy - 3.f, b.getWidth() - 20.f, 6.f);
    g.setColour (col::raised);
    g.fillRoundedRectangle (track, 3.f);
    g.setColour (col::line.brighter (0.2f));
    for (int i = 0; i <= 4; ++i)
    {
        const float tx = track.getX() + track.getWidth() * (float) i / 4.f;
        g.drawVerticalLine ((int) tx, cy + 6.f, cy + 9.f);
    }
    g.setGradientFill (ColourGradient (col::select.withAlpha (0.8f), track.getX(), cy, col::accent, track.getRight(), cy, false));
    g.fillRoundedRectangle (track.withWidth (track.getWidth() * value), 3.f);
    const float x = track.getX() + track.getWidth() * value;
    g.setColour (col::text);
    g.fillEllipse (x - 8.f, cy - 8.f, 16.f, 16.f);
    g.setColour (col::bg);
    g.drawEllipse (x - 8.f, cy - 8.f, 16.f, 16.f, 1.5f);
    if (dragging || isMouseOver())
    {
        g.setColour (col::text);
        g.setFont (font (13.f, true));
        g.drawText (String (roundToInt (value * 100)) + "%", Rectangle<float> (x - 20.f, b.getY() - 2.f, 40.f, 12.f), Justification::centred);
    }
}

void MorphSlider::mouseDown (const MouseEvent& e)
{
    if (param == nullptr) return;
    dragging = true;
    if (onGestureStart) onGestureStart();
    att->beginGesture();
    value = valueAt (e.position.x);
    att->setValueAsPartOfGesture (param->convertFrom0to1 (value));
    repaint();
}

void MorphSlider::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    value = valueAt (e.position.x);
    att->setValueAsPartOfGesture (param->convertFrom0to1 (value));
    repaint();
}

void MorphSlider::mouseUp (const MouseEvent&)
{
    if (dragging && att != nullptr) att->endGesture();
    dragging = false;
    repaint();
}

void MorphSlider::mouseDoubleClick (const MouseEvent&)
{
    if (att != nullptr) att->setValueAsCompleteGesture (param->convertFrom0to1 (0.f));
}

//==============================================================================================
Toolbar::Toolbar (Ferrum8Processor& p) : proc (p)
{
    for (auto* b : { &mainMenuBtn, &prevBtn, &nextBtn, &morphMenuBtn, &undoBtn, &redoBtn, &soundsBtn, &beatBtn, &allBtn, &diceMenuBtn }) addAndMakeVisible (b);
    addAndMakeVisible (presetBox);
    addAndMakeVisible (morph);
    presetBox.setColour (TextButton::buttonColourId, col::bg);
    mainMenuBtn.setTooltip ("Main menu (Shift-click repeats the last item)");
    prevBtn.setTooltip ("Previous preset");
    nextBtn.setTooltip ("Next preset");
    presetBox.setTooltip ("Preset - click for factory kits and your saved presets");
    morphMenuBtn.setTooltip ("Morph menu: set end points, swap, randomise targets");
    undoBtn.setTooltip ("Undo (Ctrl+Z)");
    redoBtn.setTooltip ("Redo (Ctrl+Y)");
    soundsBtn.setTooltip ("Randomize Sounds: new drum sounds on every unlocked channel, the patterns stay");
    beatBtn.setTooltip ("Randomize Beat: new steps for every unlocked channel, the sounds stay (style, density ... in the Dice menu)");
    allBtn.setTooltip ("Randomize All: new sounds and patterns A-L");
    diceMenuBtn.setTooltip ("Dice settings: beat style, density, complexity, kit character, key, Evolve, roll history");
    undoBtn.onClick = [this] { proc.undo(); };
    redoBtn.onClick = [this] { proc.redo(); };
    prevBtn.onClick = [this] { if (onStepPreset) onStepPreset (-1); };
    nextBtn.onClick = [this] { if (onStepPreset) onStepPreset (1); };
    presetBox.onClick = [this] { if (onPresetList) onPresetList(); };
    morphMenuBtn.onClick = [this] { if (onMorphMenu) onMorphMenu(); };
    morph.bind (proc.apvts.getParameter ("morph"));
    refresh();
}

void Toolbar::refresh()
{
    presetBox.setButtonText (proc.presetName + (proc.presetDirty ? " *" : ""));
}

void Toolbar::tick()
{
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    const String t = proc.presetName + (proc.presetDirty ? " *" : "");
    if (presetBox.getButtonText() != t) presetBox.setButtonText (t);
}

void Toolbar::resized()
{
    auto r = getLocalBounds().reduced (10, 9);
    mainMenuBtn.setBounds (r.removeFromLeft (34)); r.removeFromLeft (10);
    logoArea = r.removeFromLeft (120); r.removeFromLeft (8);
    prevBtn.setBounds (r.removeFromLeft (24)); r.removeFromLeft (2);
    nextBtn.setBounds (r.removeFromLeft (24)); r.removeFromLeft (6);
    presetBox.setBounds (r.removeFromLeft (210)); r.removeFromLeft (20);
    diceMenuBtn.setBounds (r.removeFromRight (26)); r.removeFromRight (3);
    allBtn.setBounds (r.removeFromRight (70)); r.removeFromRight (4);
    beatBtn.setBounds (r.removeFromRight (80)); r.removeFromRight (4);
    soundsBtn.setBounds (r.removeFromRight (98)); r.removeFromRight (14);
    midiArea = r.removeFromRight (64); r.removeFromRight (14);
    redoBtn.setBounds (r.removeFromRight (34)); r.removeFromRight (4);
    undoBtn.setBounds (r.removeFromRight (34)); r.removeFromRight (20);
    morphLabelArea = r.removeFromLeft (74);
    morphMenuBtn.setBounds (r.removeFromRight (24));
    r.removeFromRight (4);
    auto m = r.reduced (16, 0);
    morph.setBounds (m);
}

void Toolbar::paint (Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::line);
    g.drawHorizontalLine (getHeight() - 1, 0.f, (float) getWidth());
    g.setFont (font (25.f, true));
    g.setColour (col::text);
    g.drawText ("FERRUM", logoArea.withTrimmedRight (26), Justification::centredLeft);
    g.setColour (col::accent);
    g.drawText ("8", logoArea.withTrimmedLeft (logoArea.getWidth() - 34), Justification::centredLeft);
    g.setFont (font (14.f, true));
    g.setColour (col::dim);
    g.drawText ("SOUND MORPH", morphLabelArea, Justification::centredLeft);
    auto mb = morph.getBounds();
    g.setFont (font (15.f, true));
    g.setColour (col::select);
    g.drawText ("A", mb.getX() - 16, mb.getY(), 14, mb.getHeight(), Justification::centred);
    g.setColour (col::accent);
    g.drawText ("B", mb.getRight() + 2, mb.getY(), 14, mb.getHeight(), Justification::centred);
    auto mi = midiArea.toFloat();
    g.setColour (col::raised);
    g.fillRoundedRectangle (mi, 4.f);
    g.setColour (col::line.brighter (0.15f));
    g.drawRoundedRectangle (mi.reduced (0.5f), 4.f, 1.f);
    IconButton::drawIcon (g, IconButton::Icon::Midi, mi.withWidth (mi.getHeight()).reduced (7.f), col::text);
    g.setColour (col::dim);
    g.setFont (font (13.5f, true));
    g.drawText ("DRAG", mi.withTrimmedLeft (mi.getHeight() - 4.f), Justification::centredLeft);
}

void Toolbar::mouseDown (const MouseEvent& e)
{
    midiDragStarted = false;
    if (midiArea.contains (e.getPosition()) && e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addItem (1, "Drag this icon into your DAW to drop the selected pattern as a MIDI clip", false);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
    }
}

void Toolbar::mouseDrag (const MouseEvent& e)
{
    if (midiDragStarted || ! midiArea.contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 4) return;
    midiDragStarted = true;
    const File f = proc.createTempMidiForDrag (proc.getSelectedPattern());
    if (f.existsAsFile()) DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

//==============================================================================================
ChannelStrip::ChannelStrip (Ferrum8Processor& p) : proc (p)
{
    for (int c = 0; c < 8; ++c) seenTriggers[(size_t) c] = proc.getEngine().uiTriggerCount[c].load();
}

Rectangle<int> ChannelStrip::cell (int ch) const
{
    const auto r = getLocalBounds().reduced (10, 6);
    const int gap = 6;
    const int w = (r.getWidth() - gap * 7) / 8;
    return { r.getX() + ch * (w + gap), r.getY(), w, r.getHeight() };
}

Rectangle<int> ChannelStrip::muteButton (int ch) const
{
    auto c = cell (ch);
    return { c.getRight() - 30, c.getY() + 5, 24, c.getHeight() / 2 - 7 };
}

Rectangle<int> ChannelStrip::lockButton (int ch) const
{
    auto c = cell (ch);
    return { c.getRight() - 30, c.getCentreY() + 2, 24, c.getHeight() / 2 - 7 };
}

void ChannelStrip::tick()
{
    bool any = false;
    for (int c = 0; c < 8; ++c)
    {
        const uint32_t t = proc.getEngine().uiTriggerCount[c].load();
        if (t != seenTriggers[(size_t) c]) { seenTriggers[(size_t) c] = t; flash[(size_t) c] = 1.f; any = true; }
        else if (flash[(size_t) c] > 0.f) { flash[(size_t) c] = jmax (0.f, flash[(size_t) c] - 0.18f); any = true; }
    }
    static int slow = 0;
    if (any || (++slow % 15) == 0) repaint();
}

void ChannelStrip::paint (Graphics& g)
{
    g.fillAll (col::bg);
    for (int ch = 0; ch < 8; ++ch)
    {
        auto c = cell (ch).toFloat();
        const auto cc = col::channel (ch);
        const bool sel = ch == selected;
        const bool muted = proc.apvts.getRawParameterValue ("mute" + String (ch + 1))->load() > 0.5f;
        const bool midiMuted = proc.getEngine().uiMidiMute[ch].load();
        g.setColour (sel ? col::panel2.brighter (0.06f) : col::panel);
        g.fillRoundedRectangle (c, 6.f);
        if (flash[(size_t) ch] > 0.f)
        {
            g.setColour (cc.withAlpha (0.35f * flash[(size_t) ch]));
            g.fillRoundedRectangle (c, 6.f);
        }
        g.setColour (sel ? cc : col::line);
        g.drawRoundedRectangle (c.reduced (0.5f), 6.f, sel ? 2.f : 1.f);
        g.setColour (cc);
        g.fillRoundedRectangle (c.getX() + 8.f, c.getY() + 9.f, 4.f, c.getHeight() - 18.f, 2.f);

        auto t = c.toNearestInt().withTrimmedLeft (18).withTrimmedRight (34);
        g.setColour (sel ? cc : col::text);
        g.setFont (font (22.f, true));
        g.drawText (String (ch + 1), t.removeFromLeft (16), Justification::centredLeft);
        t.removeFromLeft (6);
        const Model m = (Model) choiceIndex (proc.getPatchValue (ch, P_MODEL), 4);
        String name = proc.getChannelName (ch);
        if (name.isEmpty()) name = kModelNames[(int) m];
        g.setFont (font (16.f, true));
        g.setColour (muted ? col::faint : col::text);
        g.drawFittedText (name, t.withTrimmedBottom (t.getHeight() / 2 - 2), Justification::bottomLeft, 1, 0.8f);
        g.setFont (font (13.5f));
        g.setColour (col::dim);
        String info = String (kModelNames[(int) m]).toUpperCase();
        if (proc.getPatchValue (ch, P_CHOKE) > 0.5f) info << "  CHOKE";
        const int out = choiceIndex (proc.getPatchValue (ch, P_OUTPUT), paramDef (P_OUTPUT).numChoices);
        if (out == 1) info << "  OUT B"; else if (out == 2) info << "  > FX 1"; else if (out == 3) info << "  > FX 2";
        g.drawFittedText (info, t.withTrimmedTop (t.getHeight() / 2 + 1), Justification::topLeft, 1, 0.8f);

        auto mb = muteButton (ch).toFloat();
        g.setColour (muted ? col::mute : (midiMuted ? col::accent : col::raised));
        g.fillRoundedRectangle (mb, 4.f);
        g.setColour (muted || midiMuted ? Colours::black : col::dim);
        g.setFont (font (15.f, true));
        g.drawText ("M", mb, Justification::centred);
        const bool lk = proc.locked[(size_t) ch];
        auto lb = lockButton (ch).toFloat();
        g.setColour (lk ? col::select : col::raised);
        g.fillRoundedRectangle (lb, 4.f);
        IconButton::drawIcon (g, lk ? IconButton::Icon::Lock : IconButton::Icon::Unlock, lb.withSizeKeepingCentre (14.f, 14.f), lk ? Colours::black : col::dim);
    }
}

void ChannelStrip::mouseDown (const MouseEvent& e)
{
    for (int ch = 0; ch < 8; ++ch)
    {
        if (! cell (ch).contains (e.getPosition())) continue;
        if (lockButton (ch).contains (e.getPosition()))
        {
            proc.locked[(size_t) ch] = ! proc.locked[(size_t) ch];
            if (onStatus) onStatus (proc.locked[(size_t) ch] ? "Channel " + String (ch + 1) + " locked: the dice leave its sound and steps alone"
                                                            : "Channel " + String (ch + 1) + " unlocked");
            repaint();
            return;
        }
        if (muteButton (ch).contains (e.getPosition()))
        {
            if (e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isAltDown())
            {
                bool soloed = true;
                for (int c = 0; c < 8; ++c)
                    if ((proc.apvts.getRawParameterValue ("mute" + String (c + 1))->load() > 0.5f) != (c != ch)) soloed = false;
                for (int c = 0; c < 8; ++c)
                    proc.apvts.getParameter ("mute" + String (c + 1))->setValueNotifyingHost (soloed ? 0.f : (c == ch ? 0.f : 1.f));
                if (onSelect && ! soloed) onSelect (ch);
            }
            else
            {
                auto* mp = proc.apvts.getParameter ("mute" + String (ch + 1));
                mp->setValueNotifyingHost (mp->getValue() > 0.5f ? 0.f : 1.f);
            }
            repaint();
            return;
        }
        if (e.mods.isPopupMenu())
        {
            if (onSelect) onSelect (ch);
            if (onChannelMenu) onChannelMenu (ch);
            return;
        }
        if (ch == selected) { if (onPreview) onPreview (ch, e.mods.isCtrlDown() || e.mods.isAltDown() || e.mods.isCommandDown()); }
        else if (onSelect) onSelect (ch);
        repaint();
        return;
    }
}

//==============================================================================================
GlobalPanel::GlobalPanel (Ferrum8Processor& p) : proc (p)
{
    for (auto* c : std::initializer_list<Component*> { &stopBtn, &playBtn, &rate, &swing, &fillRate, &tempo, &master })
        addAndMakeVisible (c);

    stopBtn.setTooltip ("Stop the pattern engine at the end of the pattern (click again: stop now)");
    playBtn.setTooltip ("Start the pattern engine in sync with the host (click again: start now)");
    stopBtn.onClick = [this] { if (onStop) onStop(); };
    playBtn.onClick = [this] { if (onPlay) onPlay(); };
    playBtn.setAccent (col::play);

    auto& a = proc.apvts;
    auto undo = [this] { proc.pushUndo(); };
    rate.bind (a.getParameter ("rate"));
    rate.setLabel ("STEP RATE");
    rate.onGestureStart = undo;
    swing.bind (a.getParameter ("swing")); swing.setLabel ("Swing"); swing.onGestureStart = undo;
    fillRate.bind (a.getParameter ("fillRate")); fillRate.setLabel ("Fill Rate"); fillRate.onGestureStart = undo;
    tempo.bind (a.getParameter ("tempo"));
    const bool standalone = proc.wrapperType == AudioProcessor::wrapperType_Standalone;
    tempo.setLabel (standalone ? "Tempo" : "Tempo*");
    tempo.setTooltip (standalone ? "Tempo of the pattern engine" : "Tempo when the host does not report one (the host tempo is used while it plays)");
    master.bind (a.getParameter ("master")); master.setLabel ("Master");
    master.setAccent (col::text);
}

void GlobalPanel::tick()
{
    const int state = proc.getEngine().uiState.load();
    ++blink;
    const bool on = (blink / 8) % 2 == 0;
    playBtn.setToggleState (state == 2 || state == 3, dontSendNotification);
    playBtn.setBlink (state == 1 && on);
    stopBtn.setBlink (state == 3 && on);
    stopBtn.setToggleState (false, dontSendNotification);
    bool changed = false;
    for (int b = 0; b < fx::kBuses; ++b)
    {
        float& m = meters[(size_t) b];
        const float next = jmax (proc.getRack().meter (b), m * 0.82f);
        if (std::abs (next - m) > 0.002f) { m = next; changed = true; }
    }
    if (changed) repaint (fxArea);
}

Rectangle<int> GlobalPanel::busCard (int b) const
{
    auto r = fxArea.reduced (8, 0).withTrimmedTop (24).withTrimmedBottom (8);
    const int w = (r.getWidth() - 2 * 6) / 3;
    return { r.getX() + b * (w + 6), r.getY(), w, r.getHeight() };
}

void GlobalPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    transportArea = r.removeFromLeft (124);
    stopBtn.setBounds (transportArea.getX() + 6, transportArea.getY() + 24, 52, 42);
    playBtn.setBounds (transportArea.getX() + 64, transportArea.getY() + 24, 52, 42);
    r.removeFromLeft (12);
    seqArea = r.removeFromLeft (410);
    auto s = seqArea.reduced (8, 0);
    rate.setBounds (s.removeFromLeft (212).withTrimmedTop (26).withHeight (50));
    s.removeFromLeft (8);
    swing.setBounds (s.removeFromLeft (60).withTrimmedTop (20).withHeight (68));
    fillRate.setBounds (s.removeFromLeft (60).withTrimmedTop (20).withHeight (68));
    tempo.setBounds (s.removeFromLeft (60).withTrimmedTop (20).withHeight (68));
    r.removeFromLeft (12);
    masterArea = r.removeFromRight (86);
    master.setBounds (masterArea.withTrimmedTop (20).withHeight (68).withSizeKeepingCentre (70, 68));
    r.removeFromRight (12);
    fxArea = r;
}

void GlobalPanel::mouseDown (const MouseEvent& e)
{
    for (int b = 0; b < fx::kBuses; ++b)
        if (busCard (b).contains (e.getPosition()) && onOpenFx) { onOpenFx (b); return; }
}

void GlobalPanel::mouseMove (const MouseEvent& e)
{
    int h = -1;
    for (int b = 0; b < fx::kBuses; ++b) if (busCard (b).contains (e.getPosition())) h = b;
    if (h != hoverCard) { hoverCard = h; repaint (fxArea); }
}

void GlobalPanel::mouseExit (const MouseEvent&)
{
    if (hoverCard >= 0) { hoverCard = -1; repaint (fxArea); }
}

void GlobalPanel::paint (Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.drawHorizontalLine (0, 0.f, (float) getWidth());
    auto frame = [&g] (Rectangle<int> r, const String& title)
    {
        g.setColour (col::panel2);
        g.fillRoundedRectangle (r.toFloat(), 5.f);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.f, 1.f);
        g.setColour (col::dim);
        g.setFont (font (14.f, true));
        g.drawText (title, r.withHeight (18).reduced (8, 0).translated (0, 2), Justification::centredLeft);
    };
    frame (transportArea, "PATTERN ENGINE");
    frame (seqArea, "SEQUENCER");
    frame (fxArea, "FX RACK  (click a bus to edit it)");
    frame (masterArea, "OUTPUT");
    g.setFont (font (13.f));
    g.setColour (col::dim);
    g.drawText ("STOP", stopBtn.getBounds().translated (0, 44).withHeight (14), Justification::centred);
    g.drawText ("PLAY", playBtn.getBounds().translated (0, 44).withHeight (14), Justification::centred);

    auto& rack = proc.getRack();
    const int n = paramDef (P_OUTPUT).numChoices;
    for (int b = 0; b < fx::kBuses; ++b)
    {
        auto r = busCard (b).toFloat();
        g.setColour (hoverCard == b ? col::raised.brighter (0.06f) : col::raised);
        g.fillRoundedRectangle (r, 5.f);
        g.setColour (hoverCard == b ? col::accent : col::line.brighter (0.1f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        auto t = r.reduced (9.f, 5.f);
        auto top = t.removeFromTop (19.f);
        g.setColour (col::text);
        g.setFont (font (15.5f, true));
        g.drawText (String (fx::busName (b)).toUpperCase(), top, Justification::centredLeft);
        // channels sent here, as coloured dots
        if (b != fx::kMasterBus)
        {
            float x = top.getRight() - 8.f;
            for (int ch = Ferrum8Processor::NC - 1; ch >= 0; --ch)
                if (choiceIndex (proc.getPatchValue (ch, P_OUTPUT), n) == b + 2)
                {
                    g.setColour (col::channel (ch));
                    g.fillEllipse (x - 3.f, top.getCentreY() - 3.f, 7.f, 7.f);
                    x -= 10.f;
                }
        }
        StringArray names;
        for (int l = 0; l < fx::kLanes; ++l)
            for (int s = 0; s < fx::kSlots; ++s)
                if (rack.moduleType (b, l, s) != fx::Type::None) names.add (fx::info (rack.moduleType (b, l, s)).name);
        g.setFont (font (13.5f));
        g.setColour (names.isEmpty() ? col::faint : col::dim);
        g.drawFittedText (names.isEmpty() ? String ("empty") : names.joinIntoString (" > "), t.removeFromTop (34.f).toNearestInt(),
                          Justification::topLeft, 2, 0.8f);
        auto meter = Rectangle<float> (r.getX() + 9.f, r.getBottom() - 9.f, r.getWidth() - 18.f, 3.f);
        g.setColour (col::bg);
        g.fillRoundedRectangle (meter, 1.5f);
        const float lvl = jlimit (0.f, 1.f, (Decibels::gainToDecibels (meters[(size_t) b], -60.f) + 60.f) / 66.f);
        g.setColour (meters[(size_t) b] > 0.98f ? col::mute : col::play);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * lvl), 1.5f);
    }
}
} // namespace ui
