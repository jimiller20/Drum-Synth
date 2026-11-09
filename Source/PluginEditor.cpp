// PluginEditor.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PluginEditor.h"
#include "ui/Clipboard.h"

using namespace juce;
using namespace ferrum;

Ferrum8Editor::Ferrum8Editor (Ferrum8Processor& p)
    : AudioProcessorEditor (&p), proc (p), toolbar (p), channels (p), patch (p), pattern (p), global (p), fxPanel (p), modPanel (p)
{
    setLookAndFeel (&lnf);
    LookAndFeel::setDefaultLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.onPaint = [this] (Graphics& g) { drawTabs (g); drawStatus (g); };
    for (auto* c : std::initializer_list<Component*> { &toolbar, &channels, &patch, &pattern, &global, &drumsTab, &fxTab, &modTab })
        content.addAndMakeVisible (c);
    content.addChildComponent (fxPanel);
    content.addChildComponent (modPanel);
    modTab.setTooltip ("Modulation: LFOs, envelopes, sample and hold, peak controllers, random drift, step lanes and macros (Tab switches)");
    modTab.onClick = [this] { setTab (2); };
    modPanel.showStatus = [this] (const String& s) { status (s); };
    drumsTab.setTooltip ("Drum patches and patterns (Tab switches)");
    fxTab.setTooltip ("The FX rack: FX 1, FX 2 and Master buses, lanes in parallel, slots in series (Tab switches)");
    drumsTab.onClick = [this] { setTab (0); };
    fxTab.onClick = [this] { setTab (1); };
    fxPanel.showStatus = [this] (const String& s) { status (s); };
    global.onOpenFx = [this] (int b) { fxPanel.showBus (b); setTab (1); };
    global.onOpenFxMorph = [this] { showFxMorphPanel(); };
    global.getMorphPad().showStatus = [this] (const String& s) { status (s); };
    fxPanel.onOpenMorph = [this] { showFxMorphPanel(); };

    toolbar.mainMenuBtn.onClick = [this] { showMainMenu (ModifierKeys::currentModifiers.isShiftDown()); };
    // the dice change by the amount under the mouse (left edge ~1 %, right edge 100 %)
    toolbar.soundsBtn.onClick = [this] { status (proc.rollSounds (toolbar.soundsBtn.clickAmount())); selectChannel (proc.editorChannel); };
    toolbar.beatBtn.onClick = [this] { status (proc.rollBeat (toolbar.beatBtn.clickAmount())); };
    toolbar.allBtn.onClick = [this] { randomizeAll (toolbar.allBtn.clickAmount()); };
    toolbar.diceMenuBtn.onClick = [this] { showDicePanel(); };
    channels.onStatus = [this] (const String& s) { status (s); };
    toolbar.onStepPreset = [this] (int d) { stepPreset (d); };
    toolbar.onPresetList = [this] { showPresetList(); };
    toolbar.onMorphMenu = [this] { showMorphMenu(); };
    channels.onSelect = [this] (int ch) { selectChannel (ch); };
    channels.onPreview = [this] (int ch, bool acc) { preview (ch, acc); };
    channels.onChannelMenu = [this] (int ch) { showChannelMenu (ch); };
    pattern.onSelectChannel = [this] (int ch) { selectChannel (ch); };
    pattern.onPreview = [this] (int ch, bool acc) { preview (ch, acc); };
    pattern.showStatus = [this] (const String& s) { status (s); };
    patch.showStatus = [this] (const String& s) { status (s); };
    patch.onOpenMod = [this] (int m) { modPanel.select (m); setTab (2); };
    global.onPlay = [this] { pressPlay(); };
    global.onStop = [this] { pressStop(); };

    proc.uiChanges.addChangeListener (this);
    selectChannel (proc.editorChannel);
    setTab (0);

    float z = 1.0f;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay())
        if (d->userBounds.getHeight() < kDesignH + 80) z = jmax (0.6f, (float) (d->userBounds.getHeight() - 80) / (float) kDesignH);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    setResizeLimits ((int) (kDesignW * 0.6f), (int) (kDesignH * 0.6f), kDesignW * 2, kDesignH * 2);
    setZoom (z);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
    if (autoTutorial && ! ui::Tutorial::hasBeenSeen()) showTutorial (0);
}

void Ferrum8Editor::showTutorial (int page)
{
    if (tutorial == nullptr)
    {
        tutorial = std::make_unique<ui::Tutorial>();
        content.addAndMakeVisible (*tutorial);
        tutorial->setBounds (0, 0, kDesignW, kDesignH);
        SafePointer<Ferrum8Editor> safe (this);
        tutorial->onClose = [safe]
        {
            ui::Tutorial::markSeen();
            // not from inside the tutorial's own button callback
            MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->tutorial.reset(); safe->grabKeyboardFocus(); } });
        };
    }
    tutorial->showPage (page);
    tutorial->toFront (true);
    if (isShowing()) tutorial->grabKeyboardFocus();
}

Ferrum8Editor::~Ferrum8Editor()
{
    stopTimer();
    proc.uiChanges.removeChangeListener (this);
    LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void Ferrum8Editor::setZoom (float z)
{
    zoom = jlimit (0.6f, 2.0f, z);
    setSize (roundToInt (kDesignW * zoom), roundToInt (kDesignH * zoom));
}

void Ferrum8Editor::paint (Graphics& g) { g.fillAll (ui::col::bg); }

void Ferrum8Editor::resized()
{
    zoom = (float) getWidth() / (float) kDesignW;
    content.setBounds (0, 0, kDesignW, kDesignH);
    content.setTransform (AffineTransform::scale (zoom));
    toolbar.setBounds (0, 0, kDesignW, 50);
    channels.setBounds (0, 50, kDesignW, 64);
    tabArea = { 0, 114, kDesignW, 34 };
    drumsTab.setBounds (12, 118, 118, 26);
    fxTab.setBounds (134, 118, 118, 26);
    modTab.setBounds (256, 118, 118, 26);
    patch.setBounds (0, 148, kDesignW, 354);
    pattern.setBounds (0, 502, kDesignW, 236);
    fxPanel.setBounds (0, 148, kDesignW, 590);
    modPanel.setBounds (0, 148, kDesignW, 590);
    global.setBounds (0, 738, kDesignW, 112);
    statusArea = { 0, 850, kDesignW, 32 };
    if (tutorial != nullptr) tutorial->setBounds (0, 0, kDesignW, kDesignH);
    content.repaint();
}

void Ferrum8Editor::timerCallback()
{
    toolbar.tick();
    channels.tick();
    pattern.tick();
    global.tick();
    patch.tick();
    if (tab == 1) fxPanel.tick();
    if (tab == 2) modPanel.tick();
    if (statusTimer > 0 && --statusTimer == 0) statusText.clear();
    static int frame = 0;
    if ((++frame % 10) == 0) content.repaint (statusArea);
}

void Ferrum8Editor::changeListenerCallback (ChangeBroadcaster*)
{
    patch.refresh();
    toolbar.refresh();
    channels.repaint();
    pattern.updateDiceButton();
    pattern.repaint();
    fxPanel.refresh();
    modPanel.refresh();
    global.repaint();
    content.repaint (tabArea);
    if (patch.getChannel() != proc.editorChannel) selectChannel (proc.editorChannel);
    pattern.setMatrix (proc.matrixView);
}

void Ferrum8Editor::selectChannel (int ch)
{
    ch = jlimit (0, Ferrum8Processor::NC - 1, ch);
    patch.setChannel (ch);
    pattern.setChannel (ch);
    channels.setSelected (ch);
}

void Ferrum8Editor::preview (int ch, bool accent)
{
    proc.getEngine().previewTrigger (ch, accent ? 1.0f : 64.f / 127.f);
}

void Ferrum8Editor::setTab (int t)
{
    tab = jlimit (0, 2, t);
    patch.setVisible (tab == 0);
    pattern.setVisible (tab == 0);
    fxPanel.setVisible (tab == 1);
    modPanel.setVisible (tab == 2);
    if (tab == 1) fxPanel.refresh();
    if (tab == 2) modPanel.refresh();
    drumsTab.setToggleState (tab == 0, dontSendNotification);
    fxTab.setToggleState (tab == 1, dontSendNotification);
    modTab.setToggleState (tab == 2, dontSendNotification);
    content.repaint (tabArea);
}

void Ferrum8Editor::drawTabs (Graphics& g)
{
    g.setColour (ui::col::bg);
    g.fillRect (tabArea);
    g.setColour (ui::col::line);
    g.drawHorizontalLine (tabArea.getBottom() - 1, 0.f, (float) kDesignW);
    // routing summary: which channels play through which bus
    auto& rack = proc.getRack();
    const int n = paramDef (P_OUTPUT).numChoices;
    String text;
    for (int b = 0; b < fx::kBuses; ++b)
    {
        StringArray chans, mods;
        if (b != fx::kMasterBus)
            for (int ch = 0; ch < Ferrum8Processor::NC; ++ch)
                if (choiceIndex (proc.getPatchValue (ch, P_OUTPUT), n) == b + 2) chans.add (String (ch + 1));
        for (int l = 0; l < fx::kLanes; ++l)
            for (int s = 0; s < fx::kSlots; ++s)
                if (rack.moduleType (b, l, s) != fx::Type::None) mods.add (fx::info (rack.moduleType (b, l, s)).name);
        if (b == fx::kMasterBus && mods.isEmpty()) continue;
        if (b != fx::kMasterBus && chans.isEmpty() && mods.isEmpty()) continue;
        if (text.isNotEmpty()) text << "     ";
        text << String (fx::busName (b)).toUpperCase() << ": ";
        if (b != fx::kMasterBus) text << (chans.isEmpty() ? String ("no channels") : "ch " + chans.joinIntoString (" ")) << "  >  ";
        text << (mods.isEmpty() ? String ("dry") : mods.joinIntoString (" > "));
    }
    g.setFont (ui::font (14.f));
    g.setColour (ui::col::dim);
    g.drawFittedText (text.isEmpty() ? String ("No effects: set a channel's Output to FX 1 or FX 2, or open the FX RACK tab") : text,
                      tabArea.withTrimmedLeft (390).withTrimmedRight (14), Justification::centredRight, 1, 0.8f);
}

void Ferrum8Editor::drawStatus (Graphics& g)
{
    auto r = statusArea;
    g.setColour (ui::col::bg);
    g.fillRect (r);
    g.setColour (ui::col::line);
    g.drawHorizontalLine (r.getY(), 0.f, (float) r.getWidth());
    r = r.reduced (14, 0);
    g.setFont (ui::font (15.f));
    const int state = proc.getEngine().uiState.load();
    static const char* states[4] = { "stopped", "waiting for sync", "playing", "stopping at pattern end" };
    const bool standalone = proc.wrapperType == AudioProcessor::wrapperType_Standalone;
    String right = String (standalone ? "Internal clock " : "Host ") + String (proc.lastHostBpm, 1) + " BPM  |  engine " + states[jlimit (0, 3, state)];
    g.setColour (ui::col::dim);
    g.drawText (right, r, Justification::centredRight);
    if (statusText.isNotEmpty()) { g.setColour (ui::col::accent); g.drawText (statusText, r, Justification::centredLeft); }
    else
    {
        g.setColour (ui::col::faint);
        g.drawText ("Drag knobs (Shift = fine)  |  Ctrl-click resets  |  right-click to type a value  |  click a selected channel to audition  |  keys 1-8, a-l, P, S, Tab",
                    r, Justification::centredLeft);
    }
}

void Ferrum8Editor::status (const String& s)
{
    statusText = s;
    statusTimer = 30 * 4;
    content.repaint (statusArea);
}

//==============================================================================================
void Ferrum8Editor::pressPlay()
{
    auto* playP = proc.apvts.getParameter ("play");
    const int state = proc.getEngine().uiState.load();
    if (playP->getValue() < 0.5f) playP->setValueNotifyingHost (1.f);
    else if (state == 1 || state == 0) { proc.getEngine().requestImmediateStart(); status ("Started immediately"); }
}

void Ferrum8Editor::pressStop()
{
    auto* playP = proc.apvts.getParameter ("play");
    if (playP->getValue() > 0.5f) playP->setValueNotifyingHost (0.f);
    else proc.getEngine().requestImmediateStop();
}

void Ferrum8Editor::randomizeAll (float amount)
{
    status (proc.rollAll (amount));
    selectChannel (proc.editorChannel);
}

void Ferrum8Editor::showFxMorphPanel()
{
    SafePointer<Ferrum8Editor> safe (this);
    auto panel = std::make_unique<ui::FxMorphPanel> (proc, [safe] (const String& s) { if (safe != nullptr) safe->status (s); });
    auto area = getLocalArea (&global, global.getMorphPad().getBounds());
    if (tab == 1) area = getLocalArea (&fxPanel, fxPanel.morphButtonBounds());
    CallOutBox::launchAsynchronously (std::move (panel), area, this);
}

void Ferrum8Editor::showDicePanel()
{
    SafePointer<Ferrum8Editor> safe (this);
    auto panel = std::make_unique<ui::DicePanel> (proc, [safe] (const String& s)
    {
        if (safe == nullptr) return;
        safe->status (s);
        safe->selectChannel (safe->proc.editorChannel);
    });
    const auto area = getLocalArea (&toolbar.diceMenuBtn, toolbar.diceMenuBtn.getLocalBounds());
    CallOutBox::launchAsynchronously (std::move (panel), area, this);
}

//==============================================================================================
void Ferrum8Editor::showMainMenu (bool repeatLast)
{
    if (repeatLast && lastMainMenuItem != 0) { runMainMenuItem (lastMainMenuItem); return; }
    PopupMenu m, kits, zoomMenu, morph, load;
    const auto factory = factoryKits();
    for (int i = 0; i < (int) factory.size(); ++i) kits.addItem (200 + i, factory[(size_t) i].name);
    for (int z : { 60, 75, 90, 100, 125, 150, 175, 200 }) zoomMenu.addItem (300 + z, String (z) + "%", true, std::abs (zoom * 100 - z) < 1.0f);
    morph.addItem (14, "Copy Current Sound To Both Ends");
    morph.addItem (15, "Swap Morph Ends");
    morph.addItem (16, "Randomize Morph Targets (all channels)");
    load.addItem (19, "Load Drum Patches Only...");
    load.addItem (20, "Load Patterns Only...");

    m.addItem (1, "Undo", proc.canUndo());
    m.addItem (2, "Redo", proc.canRedo());
    m.addSeparator();
    m.addItem (3, "Open Preset...");
    m.addSubMenu ("Open Part Of A Preset", load);
    m.addItem (4, "Save Preset As...");
    m.addItem (5, "Revert To Saved", proc.presetFile.existsAsFile());
    m.addSubMenu ("Factory Kits", kits);
    m.addSeparator();
    m.addItem (6, "Cut Preset");
    m.addItem (7, "Copy Preset");
    m.addItem (8, "Paste Preset", ui::Clipboard::preset.has_value());
    m.addItem (9, "Initialize Preset");
    m.addItem (10, "Exchange Preset / Clipboard", ui::Clipboard::preset.has_value());
    m.addSeparator();
    m.addItem (11, "Transpose Preset...");
    m.addItem (12, "Alter Drum Patches");
    m.addItem (40, "Randomize Sounds (keep patterns)");
    m.addItem (41, "Randomize Beat (keep sounds)");
    m.addItem (13, "Randomize All");
    m.addItem (42, "Dice Settings...");
    m.addSubMenu ("Sound Morph", morph);
    PopupMenu outs;
    outs.addItem (50, "Every Channel To Its Own Output (Ch 1 - Ch 8)");
    outs.addItem (51, "Every Channel Back To Output A");
    outs.addSeparator();
    outs.addItem (52, "How Multiple Outputs Work...");
    m.addSubMenu ("Outputs (multi-out)", outs);
    m.addSeparator();
    m.addItem (53, "Show Tutorial");
    m.addItem (17, "MIDI Config...");
    m.addSubMenu ("Zoom", zoomMenu);
    m.addItem (18, "Show Preset Folder");
    m.addItem (21, "About Ferrum 8");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&toolbar.mainMenuBtn), [this] (int r) { if (r > 0) runMainMenuItem (r); });
}

void Ferrum8Editor::runMainMenuItem (int id)
{
    if (id != 3 && id != 4 && id != 19 && id != 20) lastMainMenuItem = id;
    auto openPreset = [this] (bool patches, bool patterns)
    {
        chooser = std::make_unique<FileChooser> ("Open preset", Ferrum8Processor::presetFolder(), "*.f8preset");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, patches, patterns] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
            {
                if (! proc.loadPreset (fc.getResult(), patches, patterns)) status ("Not a Ferrum 8 preset: " + fc.getResult().getFileName());
                else status ("Loaded " + fc.getResult().getFileName());
            }
        });
    };
    switch (id)
    {
        case 1: proc.undo(); break;
        case 2: proc.redo(); break;
        case 3: openPreset (true, true); break;
        case 19: openPreset (true, false); break;
        case 20: openPreset (false, true); break;
        case 4:
        {
            auto start = Ferrum8Processor::presetFolder().getChildFile (File::createLegalFileName (proc.presetName) + ".f8preset");
            chooser = std::make_unique<FileChooser> ("Save preset", start, "*.f8preset");
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == File()) return;
                if (! f.hasFileExtension ("f8preset")) f = f.withFileExtension ("f8preset");
                status (proc.savePreset (f) ? "Saved " + f.getFileName() : "Could not save " + f.getFullPathName());
            });
            break;
        }
        case 5: if (proc.presetFile.existsAsFile()) proc.loadPreset (proc.presetFile); break;
        case 6: case 7:
            ui::Clipboard::preset = proc.createPresetTree();
            if (id == 6) { proc.pushUndo(); runMainMenuItem (9); }
            status (id == 6 ? "Preset cut to clipboard" : "Preset copied");
            break;
        case 8: if (ui::Clipboard::preset) { proc.pushUndo(); proc.applyPresetTree (*ui::Clipboard::preset); } break;
        case 9:
        {
            proc.pushUndo();
            KitData k = initKit();
            for (auto& pd : k.patterns) pd = PatternData {};
            k.name = "Init";
            proc.loadKit (k);
            break;
        }
        case 10:
            if (ui::Clipboard::preset)
            {
                proc.pushUndo();
                auto mine = proc.createPresetTree();
                proc.applyPresetTree (*ui::Clipboard::preset);
                ui::Clipboard::preset = mine;
            }
            break;
        case 11:
        {
            auto* w = new AlertWindow ("Transpose preset", "Semitones for all eight drum patches (decimals allowed)", MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("st", "1");
            w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            SafePointer<Ferrum8Editor> safe (this);
            w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int res)
            {
                if (res != 1 || safe == nullptr) return;
                safe->proc.pushUndo();
                const float st = w->getTextEditorContents ("st").getFloatValue();
                for (int ch = 0; ch < 8; ++ch) safe->proc.transposeChannel (ch, st);
            }), true);
            break;
        }
        case 12:
            proc.pushUndo();
            for (int ch = 0; ch < 8; ++ch) proc.setPatchAtActiveEnd (ch, proc.randomizer.alter (proc.getActiveEnd (ch)));
            status ("Altered all drum patches on morph end " + Ferrum8Processor::endName (proc.activeEnd()) + " (Shift-click the menu to repeat)");
            break;
        case 13: randomizeAll(); break;
        case 40: status (proc.rollSounds()); selectChannel (proc.editorChannel); break;
        case 41: status (proc.rollBeat()); break;
        case 42: showDicePanel(); break;
        case 14: proc.pushUndo(); proc.copyCurrentToBothEnds(); status ("Both morph ends now hold the current sound"); break;
        case 15: proc.pushUndo(); proc.swapMorphEnds(); break;
        case 16:
            proc.pushUndo();
            for (int ch = 0; ch < 8; ++ch)
            {
                const Patch cur = proc.getCurrentPatch (ch);
                Patch b = proc.randomizer.patch (Randomizer::guessFlavor (cur), cur);
                b[P_MODEL] = cur[P_MODEL];
                proc.setMorphTarget (ch, b);
            }
            status ("New B end for every channel - sweep Sound Morph to explore");
            break;
        case 17: showMidiConfig(); break;
        case 18: Ferrum8Processor::presetFolder().startAsProcess(); break;
        case 21: showAbout(); break;
        case 50: proc.routeAllChannels (true); status ("Every channel now plays on its own output: Ch 1 - Ch 8 (give them mixer tracks in your DAW)"); break;
        case 51: proc.routeAllChannels (false); status ("Every channel back on output A"); break;
        case 52: showTutorial (5); break;
        case 53: showTutorial (0); break;
        default:
            if (id >= 200 && id < 300)
            {
                const auto factory = factoryKits();
                if (id - 200 < (int) factory.size()) { proc.pushUndo(); proc.loadKit (factory[(size_t) (id - 200)]); status ("Loaded factory kit " + proc.presetName); }
            }
            else if (id >= 300) setZoom ((float) (id - 300) / 100.f);
            break;
    }
    selectChannel (proc.editorChannel);
}

static Array<File> userPresets()
{
    auto files = Ferrum8Processor::presetFolder().findChildFiles (File::findFiles, true, "*.f8preset");
    files.sort();
    return files;
}

void Ferrum8Editor::showPresetList()
{
    PopupMenu m;
    const auto factory = factoryKits();
    m.addSectionHeader ("Factory kits");
    for (int i = 0; i < (int) factory.size(); ++i)
        m.addItem (200 + i, factory[(size_t) i].name, true, ! proc.presetFile.existsAsFile() && proc.presetName == String (factory[(size_t) i].name));
    const auto files = userPresets();
    m.addSectionHeader ("Your presets");
    for (int i = 0; i < files.size(); ++i) m.addItem (1000 + i, files[i].getFileNameWithoutExtension(), true, files[i] == proc.presetFile);
    if (files.isEmpty()) m.addItem (-1, "(none saved yet)", false);
    m.addSeparator();
    m.addItem (3, "Open...");
    m.addItem (4, "Save Preset As...");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&toolbar.presetBox), [this, files] (int r)
    {
        if (r >= 1000) { proc.loadPreset (files[r - 1000]); status ("Loaded " + files[r - 1000].getFileName()); }
        else if (r > 0) runMainMenuItem (r);
    });
}

void Ferrum8Editor::stepPreset (int dir)
{
    const auto factory = factoryKits();
    const auto files = userPresets();
    const int total = (int) factory.size() + files.size();
    int idx = -1;
    if (proc.presetFile.existsAsFile()) { const int f = files.indexOf (proc.presetFile); if (f >= 0) idx = (int) factory.size() + f; }
    else for (int i = 0; i < (int) factory.size(); ++i) if (proc.presetName == String (factory[(size_t) i].name)) idx = i;
    idx = idx < 0 ? 0 : (idx + dir + total) % total;
    proc.pushUndo();
    if (idx < (int) factory.size()) proc.loadKit (factory[(size_t) idx]);
    else proc.loadPreset (files[idx - (int) factory.size()]);
    status ("Preset: " + proc.presetName);
}

void Ferrum8Editor::showMorphMenu()
{
    PopupMenu m;
    m.addSectionHeader ("Sound Morph");
    m.addItem (14, "Copy Current Sound To Both Ends");
    m.addItem (15, "Swap Morph Ends");
    m.addItem (16, "Randomize Morph Targets (all channels)");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&toolbar.morphMenuBtn), [this] (int r) { if (r > 0) runMainMenuItem (r); });
}

void Ferrum8Editor::showChannelMenu (int ch)
{
    PopupMenu m;
    m.addSectionHeader ("Channel " + String (ch + 1) + "  -  sound and its steps in every pattern");
    m.addItem (1, "Cut Drum Channel");
    m.addItem (2, "Copy Drum Channel");
    m.addItem (3, "Paste Drum Channel", ui::Clipboard::channel.has_value());
    m.addItem (4, "Exchange Drum Channel / Clipboard", ui::Clipboard::channel.has_value());
    m.addSeparator();
    m.addItem (5, "Randomize Drum Patch");
    m.addItem (6, "Randomize This Channel's Steps (current pattern)");
    m.addItem (7, "Clear This Channel's Steps (all patterns)");
    m.showMenuAsync (PopupMenu::Options(), [this, ch] (int r)
    {
        auto grab = [this, ch]
        {
            ui::ChannelClip c;
            c.patch.a = proc.getEndA (ch); c.patch.b = proc.getEndB (ch); c.patch.name = proc.getChannelName (ch);
            for (int p = 0; p < kNumPatterns; ++p)
            {
                c.steps[(size_t) p] = proc.getPattern (p).channel (ch);
            }
            return c;
        };
        auto put = [this, ch] (const ui::ChannelClip& c)
        {
            proc.setPatch (ch, c.patch.a, true);
            proc.setMorphTarget (ch, c.patch.b);
            proc.setChannelName (ch, c.patch.name);
            for (int p = 0; p < kNumPatterns; ++p)
            {
                auto d = proc.getPattern (p);
                d.setChannel (ch, c.steps[(size_t) p]);
                proc.setPattern (p, d);
            }
        };
        switch (r)
        {
            case 1: case 2:
                ui::Clipboard::channel = grab();
                if (r == 1)
                {
                    proc.pushUndo();
                    ui::ChannelClip blank;
                    blank.patch.a = blank.patch.b = defaultPatch ((Model) choiceIndex (proc.getPatchValue (ch, P_MODEL), 4));
                    blank.patch.name = kModelNames[choiceIndex (proc.getPatchValue (ch, P_MODEL), 4)];
                    put (blank);
                }
                break;
            case 3: if (ui::Clipboard::channel) { proc.pushUndo(); put (*ui::Clipboard::channel); } break;
            case 4: if (ui::Clipboard::channel) { proc.pushUndo(); auto mine = grab(); put (*ui::Clipboard::channel); ui::Clipboard::channel = mine; } break;
            case 5:
            {
                proc.pushUndo();
                const Patch p = proc.randomizer.patchForModel (proc.getActiveEnd (ch), proc.randomizer.genreFor (proc.dice));
                const int end = proc.setPatchAtActiveEnd (ch, p);
                proc.setChannelName (ch, String ("Rnd ") + kFlavorNames[(int) Randomizer::guessFlavor (p)]);
                status ("New sound on channel " + String (ch + 1) + "  (morph end " + Ferrum8Processor::endName (end) + ")");
                break;
            }
            case 6: pattern.runMenuItem (12, true); break;
            case 7:
                proc.pushUndo();
                for (int p = 0; p < kNumPatterns; ++p)
                {
                    auto d = proc.getPattern (p);
                    d.clearChannel (ch);
                    proc.setPattern (p, d);
                }
                break;
            default: break;
        }
        patch.refresh();
    });
}

void Ferrum8Editor::showMidiConfig()
{
    const auto c = proc.getMidiConfig();
    auto* w = new AlertWindow ("MIDI config",
                               "Standard mode: C1-G1 trigger channels 1-8, C2-G2 mute while held, C3-B3 select pattern a-l, C4 stops (C3 = note 60).\n"
                               "Pitched mode: MIDI channels 1-8 play drum channels 1-8 chromatically (C3 = original pitch); channel 10 keeps the standard map.",
                               MessageBoxIconType::NoIcon, this);
    w->addComboBox ("launch", { "Switch Next", "Switch Directly", "Retrigger", "Retrigger Gated" }, "Pattern MIDI notes");
    w->getComboBoxComponent ("launch")->setSelectedItemIndex ((int) c.launch);
    w->addComboBox ("pitched", { "Standard", "Pitched (MIDI channels 1-8 and 10)" }, "Mode");
    w->getComboBoxComponent ("pitched")->setSelectedItemIndex (c.pitchedMode ? 1 : 0);
    w->addComboBox ("noteoff", { "Off", "On" }, "Stop voices with MIDI note off");
    w->getComboBoxComponent ("noteoff")->setSelectedItemIndex (c.noteOffStops ? 1 : 0);
    w->addComboBox ("tx", { "Off", "On" }, "Pattern engine transmits MIDI notes");
    w->getComboBoxComponent ("tx")->setSelectedItemIndex (c.transmitPatternNotes ? 1 : 0);
    static const int bends[6] = { 0, 1, 2, 7, 12, 24 };
    w->addComboBox ("bend", { "Disabled", "1 semitone", "2 semitones", "7 semitones", "1 octave", "2 octaves" }, "Pitch wheel range");
    int bi = 4; for (int i = 0; i < 6; ++i) if (bends[i] == c.pitchBendSemis) bi = i;
    w->getComboBoxComponent ("bend")->setSelectedItemIndex (bi);
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    SafePointer<Ferrum8Editor> safe (this);
    w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int res)
    {
        if (res != 1 || safe == nullptr) return;
        MidiConfig n;
        n.launch = (PatternLaunch) w->getComboBoxComponent ("launch")->getSelectedItemIndex();
        n.pitchedMode = w->getComboBoxComponent ("pitched")->getSelectedItemIndex() == 1;
        n.noteOffStops = w->getComboBoxComponent ("noteoff")->getSelectedItemIndex() == 1;
        n.transmitPatternNotes = w->getComboBoxComponent ("tx")->getSelectedItemIndex() == 1;
        n.pitchBendSemis = bends[jlimit (0, 5, w->getComboBoxComponent ("bend")->getSelectedItemIndex())];
        safe->proc.setMidiConfig (n);
    }), true);
}

void Ferrum8Editor::showAbout()
{
    AlertWindow::showMessageBoxAsync (MessageBoxIconType::NoIcon, "Ferrum 8 v" JucePlugin_VersionString,
        "An eight-channel drum synthesizer.\n\n"
        "Sound engine: a C++ port of the open-source firmware of the Erica Synths LXR-02 "
        "(GPL-3.0, Copyright 2012-2026 Erica Synths): drum, snare, cymbal and hi-hat voices, transient "
        "samples, ZDF filter, FX bus.\n"
        "Workflow modelled on Sonic Charge Microtonic: 12 patterns, chains, accents, fills, swing, morph.\n\n"
        "Not affiliated with or endorsed by Erica Synths or Sonic Charge.\n"
        "Licence: GPL-3.0. Uses JUCE (AGPLv3) and Barlow Condensed (SIL OFL).", "OK", this);
}

//==============================================================================================
bool Ferrum8Editor::keyPressed (const KeyPress& k)
{
    const auto mods = k.getModifiers();
    const bool cmd = mods.isCommandDown() || mods.isCtrlDown();
    const int code = k.getKeyCode();
    if (cmd && ! mods.isShiftDown())
    {
        switch (code)
        {
            case 'Z': proc.undo(); return true;
            case 'Y': proc.redo(); return true;
            case 'X': pattern.runMenuItem (1, false); return true;
            case 'C': pattern.runMenuItem (2, false); return true;
            case 'V': pattern.runMenuItem (3, false); return true;
            case 'J': pattern.runMenuItem (7, false); return true;
            case 'K': pattern.runMenuItem (8, false); return true;
            case 'L': pattern.runMenuItem (10, false); return true;
            default: break;
        }
    }
    if (cmd && mods.isShiftDown() && code == 'Z') { proc.redo(); return true; }
    if (k == KeyPress::tabKey) { setTab ((tab + 1) % 3); return true; }
    if (code >= '1' && code <= '8')
    {
        const int ch = code - '1';
        if (mods.isShiftDown() && cmd)
        {
            bool soloed = true;
            for (int c = 0; c < 8; ++c)
                if ((proc.apvts.getRawParameterValue ("mute" + String (c + 1))->load() > 0.5f) != (c != ch)) soloed = false;
            for (int c = 0; c < 8; ++c) proc.apvts.getParameter ("mute" + String (c + 1))->setValueNotifyingHost (soloed ? 0.f : (c == ch ? 0.f : 1.f));
        }
        else if (mods.isShiftDown())
        {
            auto* mp = proc.apvts.getParameter ("mute" + String (ch + 1));
            mp->setValueNotifyingHost (mp->getValue() > 0.5f ? 0.f : 1.f);
        }
        else { selectChannel (ch); preview (ch, cmd); }
        channels.repaint();
        return true;
    }
    if (! cmd && ! mods.isAltDown())
    {
        const juce_wchar ch = CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (ch >= 'a' && ch <= 'l') { proc.selectPattern ((int) (ch - 'a')); pattern.repaint(); return true; }
        if (ch == 's') { pressStop(); return true; }
        if (ch == 'p') { pressPlay(); return true; }
    }
    return false;
}
