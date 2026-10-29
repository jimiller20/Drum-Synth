// DicePanel.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "DicePanel.h"

namespace ui
{
using namespace juce;
using namespace ferrum;

static void styleSlider (Slider& s, const String& suffix)
{
    s.setSliderStyle (Slider::LinearBar);
    s.setRange (0.0, 100.0, 1.0);
    s.setTextValueSuffix (suffix);
    s.setColour (Slider::trackColourId, col::accent.withAlpha (0.55f));
    s.setColour (Slider::backgroundColourId, col::raised);
    s.setColour (Slider::textBoxTextColourId, col::text);
    s.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
}

DicePanel::DicePanel (Ferrum8Processor& p, std::function<void (const String&)> statusFn) : proc (p), status (std::move (statusFn))
{
    for (int i = 0; i < (int) BeatStyle::Count; ++i) style.addItem (kBeatStyleNames[i], i + 1);
    for (int i = 0; i < (int) KitCharacter::Count; ++i) character.addItem (kKitCharacterNames[i], i + 1);
    key.addItem ("Off (free tuning)", 1);
    for (int i = 0; i < 12; ++i) key.addItem (String (kKeyNames[i]) + " minor pentatonic", i + 2);
    evolve.addItem ("Off", 1); evolve.addItem ("Orbit (variations around the original)", 2); evolve.addItem ("Drift (keeps wandering)", 3);
    for (int n : { 1, 2, 4, 8 }) every.addItem ("every " + String (n) + (n == 1 ? " loop" : " loops"), n);

    for (auto* s : { &density, &complexity, &expression, &mutation }) { styleSlider (*s, " %"); addAndMakeVisible (s); }
    for (auto* c : { &style, &character, &key, &evolve, &every }) addAndMakeVisible (c);
    for (auto* b : { &scopeOne, &scopeAll, &soundsBtn, &beatBtn, &allBtn, &morphBtn, &historyBtn, &keepBtn }) addAndMakeVisible (b);
    scopeOne.setClickingTogglesState (true); scopeAll.setClickingTogglesState (true);
    scopeOne.setRadioGroupId (771); scopeAll.setRadioGroupId (771);
    for (auto* b : { &soundsBtn, &beatBtn, &allBtn }) b->setColour (TextButton::buttonColourId, col::accent.withAlpha (0.85f)), b->setColour (TextButton::textColourOffId, Colours::black);

    style.setTooltip ("Rhythm style for Beat and All");
    density.setTooltip ("Sparse .. busy");
    complexity.setTooltip ("Straight .. syncopated (off-beats, displaced hits, more accents)");
    expression.setTooltip ("How much ghosting, nudging, chance and rolls the beat gets");
    mutation.setTooltip ("Size of the changes Evolve and the variation patterns make");
    character.setTooltip ("A character every new sound shares, so the kit belongs together");
    key.setTooltip ("Pitched drums (kicks, toms, percussion, snare bodies) snap to this key");
    evolve.setTooltip ("While playing, change the pattern every few loops. Orbit always starts from the original "
                       "(turning Evolve off brings it back); Drift keeps every change.");
    keepBtn.setTooltip ("Orbit: make the variation you are hearing the new centre");
    soundsBtn.setTooltip ("New sounds on every unlocked channel, patterns stay");
    beatBtn.setTooltip ("New steps for every unlocked channel, sounds stay");
    allBtn.setTooltip ("New sounds and patterns A-L");
    morphBtn.setTooltip ("Random morph B ends for every unlocked channel");
    historyBtn.setTooltip ("Bring back one of the last 16 rolls");

    style.onChange = [this] { proc.dice.style = (BeatStyle) (style.getSelectedId() - 1); };
    character.onChange = [this] { proc.dice.character = (KitCharacter) (character.getSelectedId() - 1); };
    key.onChange = [this] { proc.dice.key = key.getSelectedId() - 2; };
    evolve.onChange = [this] { proc.setEvolve (evolve.getSelectedId() - 1); keepBtn.setEnabled (proc.dice.evolve == 1); };
    every.onChange = [this] { proc.dice.evolveEvery = every.getSelectedId(); };
    density.onValueChange = [this] { proc.dice.density = (float) density.getValue() / 100.f; };
    complexity.onValueChange = [this] { proc.dice.complexity = (float) complexity.getValue() / 100.f; };
    expression.onValueChange = [this] { proc.dice.expression = (float) expression.getValue() / 100.f; };
    mutation.onValueChange = [this] { proc.dice.mutation = (float) mutation.getValue() / 100.f; };
    scopeOne.onClick = [this] { proc.dice.allPatterns = false; };
    scopeAll.onClick = [this] { proc.dice.allPatterns = true; };
    soundsBtn.onClick = [this] { roll (0); };
    beatBtn.onClick = [this] { roll (1); };
    allBtn.onClick = [this] { roll (2); };
    morphBtn.onClick = [this] { roll (3); };
    keepBtn.onClick = [this] { proc.keepEvolvedAsHome(); if (status) status ("Evolve now orbits around the variation you just heard"); };
    historyBtn.onClick = [this] { showHistory(); };
    sync();
    setSize (kW, kH);
}

void DicePanel::sync()
{
    const auto& d = proc.dice;
    style.setSelectedId ((int) d.style + 1, dontSendNotification);
    character.setSelectedId ((int) d.character + 1, dontSendNotification);
    key.setSelectedId (d.key + 2, dontSendNotification);
    evolve.setSelectedId (d.evolve + 1, dontSendNotification);
    every.setSelectedId (d.evolveEvery, dontSendNotification);
    density.setValue (d.density * 100.0, dontSendNotification);
    complexity.setValue (d.complexity * 100.0, dontSendNotification);
    expression.setValue (d.expression * 100.0, dontSendNotification);
    mutation.setValue (d.mutation * 100.0, dontSendNotification);
    scopeOne.setToggleState (! d.allPatterns, dontSendNotification);
    scopeAll.setToggleState (d.allPatterns, dontSendNotification);
    keepBtn.setEnabled (d.evolve == 1);
}

void DicePanel::roll (int kind)
{
    String msg;
    switch (kind)
    {
        case 0: msg = proc.rollSounds(); break;
        case 1: msg = proc.rollBeat(); break;
        case 2: msg = proc.rollAll(); break;
        default: msg = proc.rollMorphTargets(); break;
    }
    if (status) status (msg);
}

void DicePanel::showHistory()
{
    const auto& h = proc.getRollHistory();
    PopupMenu m;
    m.addSectionHeader ("Recent rolls (newest first)");
    if (h.empty()) m.addItem (-1, "Nothing rolled yet", false);
    for (int i = 0; i < (int) h.size(); ++i)
        m.addItem (i + 1, h[(size_t) i].when.formatted ("%H:%M:%S") + "   " + h[(size_t) i].label);
    SafePointer<DicePanel> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&historyBtn), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        safe->proc.restoreRoll (r - 1);
        if (safe->status) safe->status ("Brought back: " + safe->proc.getRollHistory()[(size_t) (r - 1)].label);
    });
}

void DicePanel::resized()
{
    labels.clear();
    auto r = getLocalBounds().reduced (16, 12);
    auto top = r.removeFromTop (30);
    historyBtn.setBounds (top.removeFromRight (96).reduced (0, 2));
    r.removeFromTop (6);
    auto row = [&] (const String& label, int h = 26)
    {
        auto line = r.removeFromTop (h);
        labels.push_back ({ line.removeFromLeft (110), label });
        r.removeFromTop (6);
        return line;
    };
    beatHeader = r.removeFromTop (20);
    style.setBounds (row ("Style"));
    density.setBounds (row ("Density"));
    complexity.setBounds (row ("Complexity"));
    expression.setBounds (row ("Expression"));
    {
        auto line = row ("Beat writes");
        scopeOne.setBounds (line.removeFromLeft (line.getWidth() / 2).reduced (0, 0).withTrimmedRight (3));
        scopeAll.setBounds (line.withTrimmedLeft (3));
    }
    r.removeFromTop (6);
    kitHeader = r.removeFromTop (20);
    character.setBounds (row ("Character"));
    key.setBounds (row ("Key"));
    r.removeFromTop (6);
    evolveHeader = r.removeFromTop (20);
    {
        auto line = row ("Mode");
        every.setBounds (line.removeFromRight (110));
        line.removeFromRight (6);
        evolve.setBounds (line);
    }
    {
        auto line = row ("Mutation");
        keepBtn.setBounds (line.removeFromRight (110));
        line.removeFromRight (6);
        mutation.setBounds (line);
    }
    r.removeFromTop (8);
    rollHeader = r.removeFromTop (20);
    auto btns = r.removeFromTop (34);
    const int w = (btns.getWidth() - 18) / 4;
    for (auto* b : { &soundsBtn, &beatBtn, &allBtn, &morphBtn }) { b->setBounds (btns.removeFromLeft (w)); btns.removeFromLeft (6); }
}

void DicePanel::paint (Graphics& g)
{
    g.fillAll (col::panel);
    auto r = getLocalBounds().reduced (16, 12);
    g.setColour (col::accent);
    g.setFont (font (21.f, true));
    auto title = r.removeFromTop (30);
    IconButton::drawIcon (g, IconButton::Icon::Dice, title.removeFromLeft (26).toFloat().reduced (2.f, 4.f), col::accent);
    title.removeFromLeft (8);
    g.drawText ("DICE", title, Justification::centredLeft);
    auto header = [&] (Rectangle<int> h, const String& t)
    {
        g.setColour (col::dim);
        g.setFont (font (13.f, true));
        g.drawText (t, h, Justification::centredLeft);
        g.setColour (col::line);
        g.fillRect (h.getX() + 120, h.getCentreY(), h.getWidth() - 120, 1);
    };
    header (beatHeader, "BEAT");
    header (kitHeader, "KIT SOUNDS");
    header (evolveHeader, "EVOLVE WHILE PLAYING");
    header (rollHeader, "ROLL  (locked channels are skipped)");
    g.setFont (font (15.f));
    g.setColour (col::text);
    for (auto& [rect, text] : labels) g.drawText (text, rect, Justification::centredLeft);
}
} // namespace ui
