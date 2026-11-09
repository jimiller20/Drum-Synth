// PatchPanel.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PatchPanel.h"
#include "Clipboard.h"

namespace ui
{
using namespace juce;
using namespace ferrum;

static constexpr int kCellW = 62, kCellH = 66, kTitleH = 20, kGroupGap = 14, kRowGap = 8, kHeaderH = 38;

PatchPanel::PatchPanel (Ferrum8Processor& p) : proc (p), modStrip (p)
{
    buildControls();
    addAndMakeVisible (modStrip);
    modStrip.onOpenMod = [this] (int m) { if (onOpenMod) onOpenMod (m); };
    modStrip.showStatus = [this] (const String& t) { if (showStatus) showStatus (t); };
    modStrip.onDepthChanged = [this] { updateModRings(); };

    addAndMakeVisible (modelSel);
    modelSel.setTooltip ("Voice model: the LXR's drum, snare, cymbal or hi-hat engine");
    modelSel.onGestureStart = [this] { proc.pushUndo(); };
    modelSel.onUserChoice = [this] (int idx)
    {
        // a new model starts from that model's init sound, keeping level, pan and routing
        const Patch cur = proc.getCurrentPatch (channel);
        Patch fresh = defaultPatch ((Model) idx);
        for (int k : { (int) P_LEVEL, (int) P_PAN, (int) P_OUTPUT, (int) P_CHOKE }) fresh[(size_t) k] = cur[(size_t) k];
        proc.setPatch (channel, fresh, true);
        proc.setChannelName (channel, kModelNames[idx]);
        refresh();
    };

    for (auto* b : { &menuBtn, &prevBtn, &nextBtn, &openBtn, &saveBtn, &diceBtn, &flavorBtn, &alterBtn, &editAllBtn })
        addAndMakeVisible (b);
    menuBtn.setTooltip ("Drum patch menu (Shift-click repeats the last item)");
    prevBtn.setTooltip ("Previous drum patch in folder");
    nextBtn.setTooltip ("Next drum patch in folder");
    openBtn.setTooltip ("Open drum patch...");
    saveBtn.setTooltip ("Save drum patch...");
    diceBtn.setTooltip ("Random patch that fits this channel's model (in the Dice panel's sound genre). It goes to the morph end "
                        "the Sound Morph slider is nearest to; the other end keeps its sound.");
    flavorBtn.setTooltip ("Random sound in a genre (a Drum & Bass snare, a Trap kick ...), a cymbal type, or another kind of sound");
    flavorBtn.onClick = [this] { showRandomMenu(); };
    alterBtn.setTooltip ("Alter: random changes to every sound parameter of this patch, as big as where you click");
    editAllBtn.setTooltip ("Edit All: knob moves apply to every unmuted channel");
    addAndMakeVisible (mutateBtn);
    mutateBtn.setTooltip ("Sound mutation for this channel: every few loops its sound moves a little (pitch, decay, tone, filter, "
                          "noise, transient, drive). Each channel has its own rate and depth, so the kick can mutate while the rest stays.");
    mutateBtn.onClick = [this] { showMutatePanel(); };
    editAllBtn.setClickingTogglesState (true);
    editAllBtn.onClick = [this] { editAll = editAllBtn.getToggleState(); repaint(); };

    menuBtn.onClick = [this] { showPatchMenu (ModifierKeys::currentModifiers.isShiftDown()); };
    prevBtn.onClick = [this] { stepPatchFile (-1); };
    nextBtn.onClick = [this] { stepPatchFile (1); };
    openBtn.onClick = [this] { runPatchMenuItem (1); };
    saveBtn.onClick = [this] { runPatchMenuItem (2); };
    // positional dice: the click position sets how far the sound moves (left ~1 %, right edge 100 %)
    diceBtn.setPositional (true);
    alterBtn.setPositional (true);
    diceBtn.onClick = [this] { diceAmount = diceBtn.clickAmount(); runPatchMenuItem (10); diceAmount = -1.f; };
    alterBtn.onClick = [this] { diceAmount = alterBtn.clickAmount(); runPatchMenuItem (9); diceAmount = -1.f; };

    addAndMakeVisible (nameBox);
    nameBox.setColour (TextButton::buttonColourId, col::bg);
    nameBox.setTooltip ("Drum patch name - click for the patches in this folder");
    nameBox.onClick = [this] { showPatchList(); };

    setChannel (proc.editorChannel);
}

PatchPanel::~PatchPanel()
{
    for (auto& c : controls) c.reset();
}

void PatchPanel::buildControls()
{
    for (int p = 0; p < NUM_PATCH_PARAMS; ++p)
    {
        if (p == P_MODEL) continue;
        const auto& d = paramDef (p);
        if (p == P_CHOKE || p == P_FM_MODE)
        {
            auto s = std::make_unique<Segmented>();
            s->setLabel (d.label);
            s->onGestureStart = [this] { proc.pushUndo(); };
            s->onUserChoice = [this, p] (int idx) { if (editAll) applyEditAllChoice (p, idx); };
            controls[(size_t) p] = std::move (s);
        }
        else if (d.numChoices > 0)
        {
            auto c = std::make_unique<ChoiceBox>();
            c->setLabel (d.label);
            if (p == P_OSC_WAVE || p == P_MOD_WAVE || p == P_MOD2_WAVE) c->setStyle (ChoiceBox::Style::Waves);
            if (p == P_LFO_WAVE) c->setStyle (ChoiceBox::Style::LfoWaves);
            c->onGestureStart = [this] { proc.pushUndo(); };
            c->onUserChoice = [this, p] (int idx) { if (editAll) applyEditAllChoice (p, idx); };
            controls[(size_t) p] = std::move (c);
        }
        else
        {
            auto k = std::make_unique<Knob>();
            k->setLabel (d.label);
            k->setBipolar (p == P_PAN || p == P_OSC_FINE || p == P_PITCH_SLOPE || p == P_SLOPE);
            k->onGestureStart = [this] { proc.pushUndo(); };
            k->onUserDelta = [this, p] (float delta) { if (editAll) applyEditAll (p, delta); };
            // right-click > Modulate with > MOD n: a route from that modulator to this parameter
            k->extendMenu = [this, p] (PopupMenu& m)
            {
                PopupMenu sub;
                const auto& ms = proc.getMods();
                for (int i = 0; i < mod::kMods; ++i)
                    sub.addItem (1000 + i, "MOD " + String (i + 1) + "  (" + (ms[(size_t) i].type == mod::Type::Off ? String ("off - becomes an LFO") : String (mod::kTypeNames[(int) ms[(size_t) i].type])) + ")");
                m.addSeparator();
                m.addSubMenu ("Modulate with", sub);
                mod::Route r; r.kind = mod::Route::Channel; r.ch = channel; r.param = p;
                if (proc.modDepthOn (r) != 0.f) m.addItem (1100, "Remove modulation");
            };
            k->onExtendedMenu = [this, p] (int id)
            {
                mod::Route r; r.kind = mod::Route::Channel; r.ch = channel; r.param = p;
                proc.pushUndo();
                if (id == 1100)
                {
                    auto ms = proc.getMods();
                    for (auto& s : ms)
                        for (auto& rt : s.routes) if (rt.kind != mod::Route::None && rt.sameTarget (r)) rt = {};
                    proc.setMods (ms);
                    if (showStatus) showStatus (String (paramDef (p).name) + ": modulation removed");
                }
                else assignMod (p, id - 1000, false);
                proc.sendUiChange();
            };
            // a modulator tile dragged from the MOD strip
            k->onModDrop = [this, p] (int m) { proc.pushUndo(); assignMod (p, m, true); proc.sendUiChange(); };
            controls[(size_t) p] = std::move (k);
        }
        String tip (d.name);
        if (p == P_GHOST) tip = "Ghost Depth: how much softer, darker and rounder the GHOST lane levels 1-3 play (50% = normal, 0% = off)";
        if (p == P_HUMANIZE) tip = "Humanize: timing and dynamics that wander from hit to hit like a real player";
        if (p == P_VARIANCE) tip = "Variance: random per-hit changes to decay, pitch, tone, filter and transient (happy accidents)";
        if (p == P_OUTPUT) tip = "Output: plugin output A (main) or B, through the FX 1 / FX 2 bus of the FX rack, or 'Ch n Out': this channel's "
                                 "own plugin output, so it can go to its own mixer track (sidechain, separate processing). In FL Studio map the "
                                 "outputs to mixer tracks in the plugin's wrapper settings (Processing tab). Main menu > Outputs routes every channel at once.";
        if (auto* tc = dynamic_cast<SettableTooltipClient*> (controls[(size_t) p].get())) tc->setTooltip (tip);
        addAndMakeVisible (*controls[(size_t) p]);
    }

    groups = {
        { "OSCILLATOR", { P_OSC_WAVE, P_OSC_COARSE, P_OSC_FINE, P_PWM }, {} },
        { "PITCH ENVELOPE", { P_PITCH_AMT, P_PITCH_DECAY, P_PITCH_SLOPE }, {} },
        { "AMP ENVELOPE", { P_ATTACK, P_DECAY, P_SLOPE, P_REPEAT }, {} },
        { "OUTPUT", { P_LEVEL, P_PAN, P_DRIVE, P_DECIMATE }, {} },
        { "MOD", { P_FM_MODE, P_MOD_WAVE, P_MOD_COARSE, P_MOD_GAIN, P_MOD2_WAVE, P_MOD2_COARSE, P_MOD2_GAIN }, {} },
        { "NOISE", { P_NOISE_FREQ, P_NOISE_MIX }, {} },
        { "FILTER", { P_FILTER_TYPE, P_FILTER_FREQ, P_FILTER_RESO, P_FILTER_DRIVE }, {} },
        { "TRANSIENT", { P_TRANS_WAVE, P_TRANS_VOL, P_TRANS_FREQ }, {} },
        { "ROUTING", { P_OUTPUT, P_CHOKE }, {} },
        { "LFO", { P_LFO_WAVE, P_LFO_FREQ, P_LFO_SYNC, P_LFO_RETRIG, P_LFO_PHASE, P_LFO_AMT, P_LFO_DEST }, {} },
        { "VELOCITY", { P_VEL_VOL, P_VEL_AMT, P_VEL_DEST }, {} },
        { "FEEL", { P_GHOST, P_HUMANIZE, P_VARIANCE }, {} },
    };
}

String PatchPanel::groupTitle (const Group& g) const
{
    const Model m = (Model) choiceIndex (proc.getPatchValue (channel, P_MODEL), 4);
    if (g.title == "MOD")
    {
        switch (m)
        {
            case Model::Drum:   return "MODULATOR OSC  (FM or mix)";
            case Model::Snare:  return "MODULATOR  (not used by the snare)";
            default:            return "FM MODULATORS 1 + 2";
        }
    }
    if (g.title == "FILTER" && m == Model::Snare) return "NOISE FILTER";
    if (g.title == "PITCH ENVELOPE" && (m == Model::Cymbal || m == Model::HiHat)) return "PITCH ENV  (drum / snare only)";
    if (g.title == "NOISE" && m != Model::Snare) return "NOISE  (snare)";
    return g.title;
}

void PatchPanel::setChannel (int ch)
{
    channel = jlimit (0, Ferrum8Processor::NC - 1, ch);
    proc.editorChannel = channel;
    modStrip.setChannel (channel);
    for (int p = 0; p < NUM_PATCH_PARAMS; ++p)
    {
        auto* c = controls[(size_t) p].get();
        if (c == nullptr) continue;
        auto* prm = proc.patchParam (channel, p);
        if (auto* k = dynamic_cast<Knob*> (c)) { k->bind (prm); k->setAccent (col::channel (channel)); }
        else if (auto* cb = dynamic_cast<ChoiceBox*> (c)) { cb->bind (prm); cb->setAccent (col::channel (channel)); }
        else if (auto* s = dynamic_cast<Segmented*> (c)) { s->bind (prm); s->setAccent (col::channel (channel)); }
    }
    modelSel.bind (proc.patchParam (channel, P_MODEL));
    modelSel.setAccent (col::channel (channel));
    refresh();
}

void PatchPanel::assignMod (int p, int m, bool dropped)
{
    mod::Route r; r.kind = mod::Route::Channel; r.ch = channel; r.param = p; r.depth = 0.3f;
    if (proc.addModRoute (m, r) < 0) { if (showStatus) showStatus ("MOD " + String (m + 1) + " already has 8 targets"); return; }
    if (showStatus) showStatus ("MOD " + String (m + 1) + " now moves " + paramDef (p).name + " on channel " + String (channel + 1)
                                + " (30%). " + (dropped ? "Set the depth in the MOD strip, the shape on the MOD tab." : "Set its shape and depth on the MOD tab."));
}

void PatchPanel::updateModRings()
{
    for (int p = 0; p < NUM_PATCH_PARAMS; ++p) // modulation ranges on the knobs
        if (auto* k = dynamic_cast<Knob*> (controls[(size_t) p].get()))
        {
            mod::Route r; r.kind = mod::Route::Channel; r.ch = channel; r.param = p;
            k->setModRange (proc.modDepthOn (r));
        }
}

void PatchPanel::refresh()
{
    updateMutateButton();
    modStrip.refresh();
    if (auto* box = dynamic_cast<ChoiceBox*> (controls[(size_t) P_OUTPUT].get())) // name the channel's own output
        box->setDisplayNames ({ "A", "B", "FX 1", "FX 2", "Ch " + String (channel + 1) + " Out" });
    updateModRings();
    const Model m = (Model) choiceIndex (proc.getPatchValue (channel, P_MODEL), 4);
    for (int p = 0; p < NUM_PATCH_PARAMS; ++p)
    {
        auto* c = controls[(size_t) p].get();
        if (c == nullptr) continue;
        const bool used = isUsedByModel (p, m);
        if (auto* k = dynamic_cast<Knob*> (c)) k->setInactive (! used);
        else if (auto* cb = dynamic_cast<ChoiceBox*> (c)) cb->setInactive (! used);
        c->setAlpha (used ? 1.0f : 0.55f);
    }
    // relabel the modulator knobs for the selected model
    auto relabel = [this] (int p, const char* t) { if (auto* k = dynamic_cast<Knob*> (controls[(size_t) p].get())) k->setLabel (t); else if (auto* c = dynamic_cast<ChoiceBox*> (controls[(size_t) p].get())) c->setLabel (t); };
    const bool dual = m == Model::Cymbal || m == Model::HiHat;
    relabel (P_MOD_WAVE, dual ? "Wave 1" : "Mod Wave");
    relabel (P_MOD_COARSE, dual ? "Pitch 1" : "Mod Pitch");
    relabel (P_MOD_GAIN, dual ? "Amount 1" : (m == Model::Drum ? "FM / Mix" : "Mod Amt"));
    relabel (P_MOD2_WAVE, "Wave 2");
    relabel (P_MOD2_COARSE, "Pitch 2");
    relabel (P_MOD2_GAIN, "Amount 2");
    if (auto* k = dynamic_cast<Knob*> (controls[P_MOD_GAIN].get())) k->setLabel (m == Model::Drum ? "Amount" : (dual ? "Amount 1" : "Mod Amt"));

    String name = proc.getChannelName (channel);
    if (name.isEmpty()) name = kModelNames[(int) m];
    nameBox.setButtonText (name);
    repaint();
}

void PatchPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    headerArea = r.removeFromTop (kHeaderH);
    auto h = headerArea.reduced (0, 4);
    menuBtn.setBounds (h.removeFromLeft (30)); h.removeFromLeft (6);
    prevBtn.setBounds (h.removeFromLeft (24)); h.removeFromLeft (2);
    nextBtn.setBounds (h.removeFromLeft (24)); h.removeFromLeft (6);
    nameArea = h.removeFromLeft (230);
    nameBox.setBounds (nameArea);
    h.removeFromLeft (6);
    openBtn.setBounds (h.removeFromLeft (30)); h.removeFromLeft (4);
    saveBtn.setBounds (h.removeFromLeft (30)); h.removeFromLeft (24);
    modelSel.setBounds (h.removeFromLeft (270)); h.removeFromLeft (14);
    diceBtn.setBounds (h.removeFromLeft (96)); h.removeFromLeft (1);
    flavorBtn.setBounds (h.removeFromLeft (24)); h.removeFromLeft (6);
    alterBtn.setBounds (h.removeFromLeft (84)); h.removeFromLeft (6);
    mutateBtn.setBounds (h.removeFromLeft (142));
    editAllBtn.setBounds (h.removeFromRight (90));
    // the MOD strip: right of the sound groups, full body height
    modStrip.setBounds (getLocalBounds().reduced (10, 6).withTrimmedTop (kHeaderH + 6).removeFromRight (148));
    layoutGroups();
}

void PatchPanel::layoutGroups()
{
    auto body = getLocalBounds().reduced (10, 6).withTrimmedTop (kHeaderH + 4);
    const int rows[3][5] = { { 0, 1, 2, 3, -1 }, { 4, 5, 6, 7, -1 }, { 8, 9, 10, 11, -1 } };
    int y = body.getY();
    for (auto& row : rows)
    {
        int x = body.getX();
        for (int gi : row)
        {
            if (gi < 0) break;
            auto& g = groups[(size_t) gi];
            const int w = (int) g.params.size() * kCellW + 8;
            g.bounds = { x, y, w, kTitleH + kCellH + 6 };
            int cx = x + 4;
            for (int p : g.params)
            {
                if (auto* c = controls[(size_t) p].get())
                {
                    if (p == P_CHOKE || p == P_FM_MODE)
                        c->setBounds (cx + 3, y + kTitleH + 14, kCellW - 6, kCellH - 18);
                    else
                        c->setBounds (cx, y + kTitleH + 2, kCellW, kCellH);
                }
                cx += kCellW;
            }
            x += w + kGroupGap;
        }
        y += kTitleH + kCellH + 6 + kRowGap;
    }
}

void PatchPanel::paint (Graphics& g)
{
    g.fillAll (col::panel);
    const auto cc = col::channel (channel);
    g.setColour (cc);
    g.fillRect (0, 0, 4, getHeight());
    g.setColour (col::line);
    g.drawHorizontalLine (headerArea.getBottom() + 2, 10.f, (float) getWidth() - 10.f);
    g.setColour (cc);
    g.setFont (font (17.f, true));
    g.drawText ("CH " + String (channel + 1), editAllBtn.getX() - 70, headerArea.getY(), 60, headerArea.getHeight(), Justification::centredRight);
    if (editAll)
    {
        g.setColour (col::accent.withAlpha (0.08f));
        g.fillRect (getLocalBounds().withTrimmedTop (headerArea.getBottom() + 3));
    }
    for (const auto& gr : groups)
    {
        auto b = gr.bounds.toFloat();
        g.setColour (col::panel2);
        g.fillRoundedRectangle (b, 5.f);
        g.setColour (col::line);
        g.drawRoundedRectangle (b.reduced (0.5f), 5.f, 1.f);
        g.setColour (col::dim);
        g.setFont (font (14.f, true));
        g.drawText (groupTitle (gr), gr.bounds.withHeight (kTitleH).reduced (8, 0), Justification::centredLeft);
    }
}

//==============================================================================================
void PatchPanel::applyEditAll (int param, float delta)
{
    for (int c = 0; c < Ferrum8Processor::NC; ++c)
    {
        if (c == channel) continue;
        if (proc.getEngine().uiMidiMute[c].load()) continue;
        if (auto* mp = proc.apvts.getRawParameterValue ("mute" + String (c + 1)); mp != nullptr && mp->load() > 0.5f) continue;
        proc.setPatchValue (c, param, proc.getPatchValue (c, param) + delta);
    }
}

void PatchPanel::applyEditAllChoice (int param, int index)
{
    const int n = paramDef (param).numChoices;
    for (int c = 0; c < Ferrum8Processor::NC; ++c)
    {
        if (c == channel) continue;
        if (auto* mp = proc.apvts.getRawParameterValue ("mute" + String (c + 1)); mp != nullptr && mp->load() > 0.5f) continue;
        proc.setPatchValue (c, param, choiceValue (index, n));
    }
}

void PatchPanel::showPatchMenu (bool repeatLast)
{
    if (repeatLast && lastPatchMenuItem != 0) { runPatchMenuItem (lastPatchMenuItem); return; }
    PopupMenu m, rnd;
    for (int f = 0; f < (int) Flavor::Count; ++f) rnd.addItem (100 + f, String ("Random ") + kFlavorNames[f]);
    m.addSectionHeader ("Drum patch - channel " + String (channel + 1));
    m.addItem (1, "Open Drum Patch...");
    m.addItem (2, "Save Drum Patch...");
    m.addSeparator();
    m.addItem (3, "Cut Drum Patch");
    m.addItem (4, "Copy Drum Patch");
    m.addItem (5, "Paste Drum Patch", Clipboard::patch.has_value());
    m.addItem (6, "Initialize Drum Patch");
    m.addItem (7, "Exchange Drum Patch / Clipboard", Clipboard::patch.has_value());
    m.addSeparator();
    m.addItem (8, "Transpose Drum Patch...");
    m.addItem (9, "Alter Drum Patch");
    m.addItem (10, "Randomize Drum Patch");
    m.addSubMenu ("Randomize As", rnd);
    m.addItem (11, "Randomize Morph Target (B end)");
    m.addSeparator();
    m.addItem (12, "Rename Channel...");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&menuBtn), [this] (int r) { if (r > 0) runPatchMenuItem (r); });
}

void PatchPanel::showRandomMenu()
{
    const Patch cur = proc.getActiveEnd (channel);
    const Flavor role = Randomizer::guessFlavor (cur);
    const String end = Ferrum8Processor::endName (proc.activeEnd());
    PopupMenu m;
    m.addSectionHeader ("Random " + String (kFlavorNames[(int) role]) + " in the style of...   (writes morph end " + end + ")");
    for (int g = 1; g < (int) Genre::Count; ++g)
    {
        if (g == 8) m.addColumnBreak();
        m.addItem (3000 + g, kGenreNames[g]);
    }
    if (role == Flavor::Cymbal)
    {
        m.addSeparator();
        m.addSectionHeader ("Cymbal type");
        for (int t = 0; t < (int) CymbalType::Count; ++t) m.addItem (2000 + (int) Flavor::Cymbal * 32 + 20 + t, kCymbalTypeNames[t]);
    }
    m.addColumnBreak();
    m.addSectionHeader ("Random as...");
    for (int f = 0; f < (int) Flavor::Chaos; ++f)
    {
        PopupMenu sub;
        sub.addItem (2000 + f * 32, "Any genre");
        sub.addSeparator();
        for (int g = 1; g < (int) Genre::Count; ++g) sub.addItem (2000 + f * 32 + g, kGenreNames[g]);
        if (f == (int) Flavor::Cymbal)
        {
            sub.addSeparator();
            sub.addSectionHeader ("Cymbal type");
            for (int t = 0; t < (int) CymbalType::Count; ++t) sub.addItem (2000 + f * 32 + 20 + t, kCymbalTypeNames[t]);
        }
        m.addSubMenu (kFlavorNames[f], sub);
    }
    m.addItem (100 + (int) Flavor::Chaos, "Chaos  (anything goes)");
    m.addSeparator();
    m.addItem (11, "Random morph target (B end)");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&diceBtn), [this] (int r) { if (r > 0) runPatchMenuItem (r); });
}

void PatchPanel::runPatchMenuItem (int id)
{
    lastPatchMenuItem = id;
    const int ch = channel;
    // sound dice write the morph end the slider is nearest to (a new model goes to both ends)
    auto rollTo = [this, ch] (const Patch& p, const String& name)
    {
        const int end = proc.setPatchAtActiveEnd (ch, p);
        proc.setChannelName (ch, name);
        if (showStatus) showStatus (name + " on channel " + String (ch + 1) + "  (morph end " + Ferrum8Processor::endName (end) + ")");
    };
    auto clipFromChannel = [this, ch]
    {
        PatchClip c; c.a = proc.getEndA (ch); c.b = proc.getEndB (ch); c.name = proc.getChannelName (ch); return c;
    };
    auto pasteClip = [this, ch] (const PatchClip& c)
    {
        Patch a = c.a, b = c.b;
        const Patch cur = proc.getCurrentPatch (ch);
        a[P_OUTPUT] = b[P_OUTPUT] = cur[P_OUTPUT]; a[P_CHOKE] = b[P_CHOKE] = cur[P_CHOKE];
        proc.setPatch (ch, a, true);
        proc.setMorphTarget (ch, b);
        proc.setChannelName (ch, c.name);
    };
    switch (id)
    {
        case 1:
        {
            chooser = std::make_unique<FileChooser> ("Open drum patch", proc.getChannelFile (ch).existsAsFile() ? proc.getChannelFile (ch) : Ferrum8Processor::patchFolder(), "*.f8drum");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, ch] (const FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) { proc.loadPatch (ch, fc.getResult()); refresh(); }
            });
            return;
        }
        case 2:
        {
            auto start = Ferrum8Processor::patchFolder().getChildFile (File::createLegalFileName (proc.getChannelName (ch).isEmpty() ? "Patch" : proc.getChannelName (ch)) + ".f8drum");
            chooser = std::make_unique<FileChooser> ("Save drum patch", start, "*.f8drum");
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this, ch] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == File()) return;
                if (! f.hasFileExtension ("f8drum")) f = f.withFileExtension ("f8drum");
                proc.savePatch (ch, f);
                refresh();
            });
            return;
        }
        case 3: Clipboard::patch = clipFromChannel(); proc.pushUndo(); proc.setPatch (ch, defaultPatch ((Model) choiceIndex (proc.getPatchValue (ch, P_MODEL), 4)), true, true); break;
        case 4: Clipboard::patch = clipFromChannel(); break;
        case 5: if (Clipboard::patch) { proc.pushUndo(); pasteClip (*Clipboard::patch); } break;
        case 6:
        {
            proc.pushUndo();
            const Model m = (Model) choiceIndex (proc.getPatchValue (ch, P_MODEL), 4);
            proc.setPatch (ch, defaultPatch (m), true, true);
            proc.setChannelName (ch, kModelNames[(int) m]);
            break;
        }
        case 7:
            if (Clipboard::patch)
            {
                proc.pushUndo();
                const PatchClip mine = clipFromChannel();
                pasteClip (*Clipboard::patch);
                Clipboard::patch = mine;
            }
            break;
        case 8:
        {
            auto* w = new AlertWindow ("Transpose drum patch", "Semitones (decimals allowed, negative = down)", MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("st", "12");
            w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            SafePointer<PatchPanel> safe (this);
            w->enterModalState (true, ModalCallbackFunction::create ([safe, w, ch] (int res)
            {
                if (res != 1 || safe == nullptr) return;
                safe->proc.pushUndo();
                safe->proc.transposeChannel (ch, w->getTextEditorContents ("st").getFloatValue());
            }), true);
            return;
        }
        case 9:
        {
            proc.pushUndo();
            // the menu's Alter is the classic small one (12 %); the button alters by where it was clicked
            const float amount = diceAmount >= 0.f ? diceAmount : 0.12f;
            const int end = proc.setPatchAtActiveEnd (ch, proc.randomizer.alter (proc.getActiveEnd (ch), amount));
            if (showStatus) showStatus ("Altered channel " + String (ch + 1) + " by " + IconButton::amountText (amount)
                                        + "  (morph end " + Ferrum8Processor::endName (end) + ")");
            break;
        }
        case 10:
        {
            proc.pushUndo();
            const Genre g = proc.randomizer.genreFor (proc.dice);
            const Patch cur = proc.getActiveEnd (ch);
            Patch p = proc.randomizer.patchForModel (cur, g);
            if (diceAmount >= 0.f && diceAmount < 0.98f)
            {
                // part of the way to a new sound of the same model; the channel keeps its name
                p = proc.randomizer.blendToward (cur, p, diceAmount);
                if (proc.dice.key >= 0) Randomizer::tuneToKey (p, proc.dice.key);
                const int end = proc.setPatchAtActiveEnd (ch, p);
                if (showStatus) showStatus ("Channel " + String (ch + 1) + " moved " + IconButton::amountText (diceAmount)
                                            + " towards a new sound  (morph end " + Ferrum8Processor::endName (end) + ", click further right for more)");
                break;
            }
            const Flavor f = Randomizer::guessFlavor (p);
            rollTo (p, (g == Genre::Any ? String ("Rnd") : String (kGenreNames[(int) g])) + " " + kFlavorNames[(int) f]);
            break;
        }
        case 11:
        {
            proc.pushUndo();
            const Patch cur = proc.getEndA (ch);
            Patch b = proc.randomizer.patch (Randomizer::guessFlavor (cur), cur);
            b[P_MODEL] = cur[P_MODEL];
            proc.setMorphTarget (ch, b);
            if (showStatus) showStatus ("New morph target on channel " + String (ch + 1) + " - move Sound Morph to hear it");
            break;
        }
        case 12:
        {
            auto* w = new AlertWindow ("Rename channel " + String (ch + 1), {}, MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("n", proc.getChannelName (ch));
            w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            SafePointer<PatchPanel> safe (this);
            w->enterModalState (true, ModalCallbackFunction::create ([safe, w, ch] (int res)
            {
                if (res == 1 && safe != nullptr) { safe->proc.setChannelName (ch, w->getTextEditorContents ("n")); safe->refresh(); }
            }), true);
            return;
        }
        default:
            if (id >= 100 && id < 100 + (int) Flavor::Count)
            {
                proc.pushUndo();
                const Flavor f = (Flavor) (id - 100);
                rollTo (proc.randomizer.patch (f, proc.getActiveEnd (ch)), String ("Rnd ") + kFlavorNames[(int) f]);
            }
            else if (id >= 2000 && id < 2000 + (int) Flavor::Count * 32)
            {
                proc.pushUndo();
                const Flavor f = (Flavor) ((id - 2000) / 32);
                const int k = (id - 2000) % 32;
                const Patch cur = proc.getActiveEnd (ch);
                if (k >= 20 && k < 20 + (int) CymbalType::Count)
                    rollTo (proc.randomizer.cymbal ((CymbalType) (k - 20), cur, Genre::Any), kCymbalTypeNames[k - 20]);
                else
                {
                    const Genre g = (Genre) std::clamp (k, 0, (int) Genre::Count - 1);
                    rollTo (proc.randomizer.patch (f, cur, g), (g == Genre::Any ? String ("Rnd") : String (kGenreNames[(int) g])) + " " + kFlavorNames[(int) f]);
                }
            }
            else if (id > 3000 && id < 3000 + (int) Genre::Count)
            {
                proc.pushUndo();
                const Genre g = (Genre) (id - 3000);
                const Patch cur = proc.getActiveEnd (ch);
                const Flavor f = Randomizer::guessFlavor (cur);
                rollTo (proc.randomizer.patch (f, cur, g), String (kGenreNames[(int) g]) + " " + kFlavorNames[(int) f]);
            }
            break;
    }
    refresh();
}

static Array<File> patchFilesNear (const File& current)
{
    const File dir = current.existsAsFile() ? current.getParentDirectory() : Ferrum8Processor::patchFolder();
    auto files = dir.findChildFiles (File::findFiles, false, "*.f8drum");
    files.sort();
    return files;
}

void PatchPanel::stepPatchFile (int dir)
{
    auto files = patchFilesNear (proc.getChannelFile (channel));
    if (files.isEmpty()) { if (showStatus) showStatus ("No saved drum patches yet - use Save to create some"); return; }
    int idx = files.indexOf (proc.getChannelFile (channel));
    idx = idx < 0 ? (dir > 0 ? 0 : files.size() - 1) : (idx + dir + files.size()) % files.size();
    proc.loadPatch (channel, files[idx]);
    refresh();
}

void PatchPanel::showPatchList()
{
    auto files = patchFilesNear (proc.getChannelFile (channel));
    PopupMenu m;
    m.addSectionHeader ("Drum patches");
    for (int i = 0; i < files.size(); ++i) m.addItem (1000 + i, files[i].getFileNameWithoutExtension(), true, files[i] == proc.getChannelFile (channel));
    if (files.isEmpty()) m.addItem (-1, "(no saved patches yet)", false);
    m.addSeparator();
    m.addItem (1, "Open...");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&nameBox), [this, files] (int r)
    {
        if (r == 1) runPatchMenuItem (1);
        else if (r >= 1000) { proc.loadPatch (channel, files[r - 1000]); refresh(); }
    });
}

//==============================================================================================
// Sound mutation settings for one channel, in a call-out bubble
namespace
{
class MutatePanel : public Component
{
public:
    MutatePanel (Ferrum8Processor& p, int ch, std::function<void (const String&)> status, std::function<void()> changed)
        : proc (p), channel (ch), showStatus (std::move (status)), onChanged (std::move (changed))
    {
        for (auto* c : std::initializer_list<Component*> { &every, &depth, &mode, &nowBtn, &keepBtn, &homeBtn }) addAndMakeVisible (c);
        const auto m = proc.getSoundMutation (channel);
        int idx = 0;
        for (int i = 0; i < 6; ++i) if (Ferrum8Processor::kMutateEvery[i] == m.every) idx = i;
        every.setLabel ("EVERY  (pattern loops)");
        every.bindFree ({ "Off", "1", "2", "4", "8", "16" }, idx);
        every.setAccent (col::accent);
        every.onUserChoice = [this] (int i) { auto s = proc.getSoundMutation (channel); s.every = Ferrum8Processor::kMutateEvery[i]; apply (s); };
        mode.setLabel ("MODE");
        mode.bindFree ({ "Orbit", "Drift" }, m.mode);
        mode.onUserChoice = [this] (int i) { auto s = proc.getSoundMutation (channel); s.mode = i; apply (s); };
        mode.setTooltip ("Orbit: every mutation starts from the home sound, so it wanders around it.\nDrift: every mutation starts from the last one, so the sound travels.");
        depth.setLabel ("Depth");
        depth.setAccent (col::accent);
        depth.bindFree ({ "Depth", 0.3f, [this] { return proc.getSoundMutation (channel).depth; },
                          [this] (float v) { auto s = proc.getSoundMutation (channel); s.depth = v; apply (s); },
                          [] (float v) { return String (roundToInt (v * 100.f)) + "%"; },
                          [] (const String& t, float& n) { n = jlimit (0.f, 1.f, t.getFloatValue() / 100.f); return true; } });
        depth.setTooltip ("How far each mutation moves the sound (pitch up to 2 semitones, decay, tone, filter, noise, FM depth, transient, drive)");
        nowBtn.setPositional (true);
        nowBtn.onClick = [this]
        {
            proc.pushUndo();
            proc.mutateSoundNow (channel, nowBtn.clickAmount());
            if (showStatus) showStatus ("Mutated channel " + String (channel + 1) + " by " + IconButton::amountText (nowBtn.clickAmount()));
        };
        keepBtn.onClick = [this] { proc.keepMutationHome (channel); if (showStatus) showStatus ("Channel " + String (channel + 1) + ": this sound is now the home Orbit returns to"); };
        homeBtn.onClick = [this] { proc.restoreMutationHome (channel); if (showStatus) showStatus ("Channel " + String (channel + 1) + " back to its home sound"); };
        nowBtn.setTooltip ("One mutation step now, as big as where you click (Ctrl+Z undoes it)");
        keepBtn.setTooltip ("Make the current sound the home sound (Orbit mutates around it)");
        homeBtn.setTooltip ("Go back to the home sound");
        setSize (360, 250);
    }
    void paint (Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::text);
        g.setFont (font (16.f, true));
        g.drawText ("SOUND MUTATION  -  CHANNEL " + String (channel + 1), 14, 8, getWidth() - 28, 20, Justification::centredLeft);
        g.setColour (col::dim);
        g.setFont (font (13.f));
        g.drawFittedText ("While the pattern plays, this channel's sound moves a little every few loops. "
                          "Other channels keep their own setting. Changes go to the morph end the slider is nearest to.",
                          Rectangle<int> (14, 196, getWidth() - 28, 48), Justification::topLeft, 3);
    }
    void resized() override
    {
        every.setBounds (14, 34, getWidth() - 28, 50);
        mode.setBounds (14, 94, 150, 50);
        depth.setBounds (176, 88, 64, 72);
        nowBtn.setBounds (252, 92, 94, 26);
        keepBtn.setBounds (252, 122, 94, 26);
        homeBtn.setBounds (252, 152, 94, 26);
    }

private:
    void apply (const Ferrum8Processor::SoundMutation& s)
    {
        proc.setSoundMutation (channel, s);
        if (onChanged) onChanged();
    }
    Ferrum8Processor& proc;
    int channel;
    std::function<void (const String&)> showStatus;
    std::function<void()> onChanged;
    Segmented every, mode;
    Knob depth;
    IconButton nowBtn { IconButton::Icon::Dice, "Now" }, keepBtn { IconButton::Icon::None, "Keep" }, homeBtn { IconButton::Icon::None, "Home" };
};
} // namespace

void PatchPanel::showMutatePanel()
{
    auto panel = std::make_unique<MutatePanel> (proc, channel, showStatus, [this] { updateMutateButton(); });
    auto area = mutateBtn.getScreenBounds();
    if (auto* top = getTopLevelComponent()) area = top->getLocalArea (nullptr, area);
    CallOutBox::launchAsynchronously (std::move (panel), area, getTopLevelComponent());
}

void PatchPanel::updateMutateButton()
{
    const auto m = proc.getSoundMutation (channel);
    mutateBtn.setButtonText (m.every == 0 ? String ("Mutate: Off")
                                    : "Mutate: " + String (m.every) + (m.every == 1 ? " loop" : " loops") + "  " + String (roundToInt (m.depth * 100.f)) + "%");
    mutateBtn.setToggleState (m.every > 0, dontSendNotification);
    lastMutCount = proc.mutationCount (channel);
    repaint();
}

void PatchPanel::tick()
{
    modStrip.tick();
    const uint32_t c = proc.mutationCount (channel);
    if (c != lastMutCount) { lastMutCount = c; mutateBtn.setBlink (true); blinkFrames = 6; }
    else if (blinkFrames > 0 && --blinkFrames == 0) mutateBtn.setBlink (false);
}

} // namespace ui
