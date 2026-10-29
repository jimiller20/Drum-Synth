// plugin_tests.cpp - headless checks of the JUCE processor + editor snapshots
// Run: Ferrum8Tests <outputDir>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/ui/DicePanel.h"
#include <cstdio>
#include <functional>

using namespace juce;
using namespace ferrum;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } else std::printf ("ok   %s\n", msg); } while (0)

struct FakePlayHead : AudioPlayHead
{
    double ppq = 0, bpm = 120;
    bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        return p;
    }
};

static float runBlocks (Ferrum8Processor& p, int blocks, FakePlayHead* ph, MidiBuffer firstMidi = {}, int* midiOutCount = nullptr)
{
    AudioBuffer<float> buf (2, 512);
    float peak = 0;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        MidiBuffer m;
        if (b == 0) m = firstMidi;
        p.processBlock (buf, m);
        if (midiOutCount) *midiOutCount += m.getNumEvents();
        peak = jmax (peak, buf.getMagnitude (0, 512));
        if (ph && ph->playing) ph->ppq += 512 * ph->bpm / 60.0 / 48000.0;
    }
    return peak;
}

static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void snapshot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream out (f);
    PNGImageFormat png;
    png.writeImageToStream (img, out);
    std::printf ("     wrote %s (%dx%d)\n", f.getFileName().toRawUTF8(), img.getWidth(), img.getHeight());
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const File outDir = argc > 1 ? File (String (argv[1])) : File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    auto proc = std::make_unique<Ferrum8Processor>();
    proc->setRateAndBufferSizeDetails (48000, 512);
    proc->prepareToPlay (48000, 512);
    CHECK (proc->getParameters().size() == 8 * NUM_PATCH_PARAMS + 27, "parameter count");

    // MIDI note C1 triggers channel 1
    {
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (10, 36, (uint8) 127), 0);
        const float peak = runBlocks (*proc, 40, nullptr, m);
        CHECK (peak > 0.05f && peak < 2.f, "MIDI note 36 plays channel 1");
    }
    // pattern engine with a host transport
    {
        FakePlayHead ph;
        proc->setPlayHead (&ph);
        proc->setParamValue ("play", 1.f);
        int midiOut = 0;
        const uint32_t before = proc->getEngine().uiTriggerCount[0].load();
        const float peak = runBlocks (*proc, 200, &ph, {}, &midiOut);
        const uint32_t trig = proc->getEngine().uiTriggerCount[0].load() - before;
        CHECK (peak > 0.05f, "pattern A plays with host transport");
        CHECK (trig >= 4 && trig <= 6, "kick fires on every beat (about 1 bar rendered)");
        CHECK (midiOut > 0, "pattern engine transmits MIDI notes");
        proc->setParamValue ("play", 0.f);
        runBlocks (*proc, 200, &ph);
        CHECK (proc->getEngine().uiState.load() == 0, "stop at pattern end");
        proc->setPlayHead (nullptr);
    }
    // morph: A at 0, B at 1, editing in the middle moves both ends
    {
        const Patch a = proc->getEndA (0);
        Patch b = a; b[P_OSC_COARSE] = 0.8f; b[P_DECAY] = 0.5f;
        proc->setMorphTarget (0, b);
        proc->setParamValue ("morph", 1.f); pump (120);
        CHECK (std::abs (proc->getPatchValue (0, P_OSC_COARSE) - 0.8f) < 1e-4f, "morph 100% = B end");
        proc->setParamValue ("morph", 0.f); pump (120);
        CHECK (std::abs (proc->getPatchValue (0, P_OSC_COARSE) - a[P_OSC_COARSE]) < 1e-4f, "morph 0% = A end");
        proc->setParamValue ("morph", 0.5f); pump (120);
        const float mid = proc->getPatchValue (0, P_DECAY);
        proc->setPatchValue (0, P_DECAY, mid + 0.05f);
        CHECK (std::abs (proc->getEndA (0)[P_DECAY] - (a[P_DECAY] + 0.05f)) < 1e-3f && std::abs (proc->getEndB (0)[P_DECAY] - 0.55f) < 1e-3f,
               "edit at 50% moves both ends equally");
        proc->setParamValue ("morph", 0.f); pump (120);
    }
    // undo
    {
        const Patch before = proc->getCurrentPatch (3);
        proc->pushUndo();
        proc->setPatch (3, proc->randomizer.patch (Flavor::Chaos, before), true);
        proc->undo();
        bool same = true;
        for (int i = 0; i < NUM_PATCH_PARAMS; ++i) if (std::abs (proc->getCurrentPatch (3)[(size_t) i] - before[(size_t) i]) > 1e-4f) same = false;
        CHECK (same, "undo restores a randomised patch");
    }
    // state round trip (with a morph target and an edited pattern)
    {
        proc->loadKit (factoryKits()[1]);
        Patch b = proc->getEndA (2); b[P_FILTER_FREQ] = 0.3f; proc->setMorphTarget (2, b);
        proc->getEngine().patterns.setTrig (5, 4, 9, true);
        proc->setChannelName (4, "My Ride");
        MemoryBlock mb;
        proc->getStateInformation (mb);
        auto p2 = std::make_unique<Ferrum8Processor>();
        p2->setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int c = 0; c < 8; ++c)
            for (int i = 0; i < NUM_PATCH_PARAMS; ++i)
                if (std::abs (p2->getPatchValue (c, i) - proc->getPatchValue (c, i)) > 1e-5f) same = false;
        CHECK (same, "state restores every patch parameter");
        CHECK (std::abs (p2->getEndB (2)[P_FILTER_FREQ] - 0.3f) < 1e-5f, "state restores morph end B");
        CHECK (p2->getEngine().patterns.trig (5, 4, 9), "state restores pattern steps");
        CHECK (p2->getChannelName (4) == "My Ride", "state restores channel names");
        CHECK (p2->presetName == proc->presetName, "state restores preset name");
    }
    // preset + patch files
    {
        const File pf = outDir.getChildFile ("test.f8preset");
        CHECK (proc->savePreset (pf), "save preset");
        auto p3 = std::make_unique<Ferrum8Processor>();
        CHECK (p3->loadPreset (pf), "load preset");
        CHECK (std::abs (p3->getPatchValue (0, P_DECAY) - proc->getPatchValue (0, P_DECAY)) < 1e-5f, "preset keeps sounds");
        const File df = outDir.getChildFile ("test.f8drum");
        CHECK (proc->savePatch (3, df), "save drum patch");
        CHECK (p3->loadPatch (7, df), "load drum patch into another channel");
        CHECK (std::abs (p3->getPatchValue (7, P_OSC_COARSE) - proc->getPatchValue (3, P_OSC_COARSE)) < 1e-5f, "drum patch keeps its sound");
    }
    // exports
    {
        const File mid = outDir.getChildFile ("export.mid");
        CHECK (proc->exportPatternMidi (mid, 0) && mid.getSize() > 50, "export pattern chain to MIDI");
        const File wav = outDir.getChildFile ("export.wav");
        CHECK (proc->exportPatternAudio (wav, 0, 24, 48000, 1) && wav.getSize() > 100000, "export pattern to WAV");
        CHECK (proc->createTempMidiForDrag (0).existsAsFile(), "MIDI drag file");
    }
    // move channel
    {
        const auto nameBefore = proc->getChannelName (0);
        const bool stepBefore = proc->getEngine().patterns.trig (0, 0, 0);
        proc->moveChannel (0, 3);
        CHECK (proc->getChannelName (3) == nameBefore && proc->getEngine().patterns.trig (0, 3, 0) == stepBefore, "matrix channel reorder moves sound and steps");
        proc->undo();
        CHECK (proc->getChannelName (0) == nameBefore, "undo channel move");
    }

    // ---- expression lanes and feel ----------------------------------------------------------
    {
        // a ghosted snare is softer and darker than a normal one
        auto hit = [] (Articulation art, float& hf)
        {
            Voice v; v.prepare (48000, 11);
            const Patch p = defaultPatch (Model::Snare);
            for (int i = 0; i < NUM_PATCH_PARAMS; ++i) v.setParam (i, p[(size_t) i]);
            v.trigger (64.f / 127.f, 60, art);
            std::vector<float> b (9600);
            v.render (b.data(), (int) b.size());
            float peak = 0, prev = 0; double lo = 0, hi = 0;
            for (float x : b) { peak = std::max (peak, std::abs (x)); hi += (x - prev) * (x - prev); lo += x * x; prev = x; }
            hf = (float) (hi / std::max (1e-12, lo));
            return peak;
        };
        float hfFull = 0, hfG1 = 0, hfG3 = 0;
        Articulation g1; g1.ghost = 1;
        Articulation g3; g3.ghost = 3;
        const float full = hit ({}, hfFull), p1 = hit (g1, hfG1), p3 = hit (g3, hfG3);
        std::printf ("     ghost peaks: full %.3f  G1 %.3f  G3 %.3f | brightness %.3f %.3f %.3f\n", full, p1, p3, hfFull, hfG1, hfG3);
        CHECK (p1 < full && p3 < p1 && p3 < full * 0.35f, "ghost levels get progressively softer");
        CHECK (hfG3 < hfFull * 0.8f, "ghost notes are darker");
    }
    {
        // engine-level checks: chance, cycle conditions, rolls and nudge timing
        auto countHits = [] (std::function<void (PatternBank&)> setup, int bars, std::vector<int>* firstHitSample = nullptr)
        {
            Engine e; e.prepare (48000, 64);
            for (int p = 0; p < kNumPatterns; ++p) e.patterns.set (p, PatternData {}, true);
            setup (e.patterns);
            Globals g; g.playOn = true; e.setGlobals (g);
            Transport t; t.hostHasTransport = true; t.hostPlaying = true; t.bpm = 120;
            std::vector<MidiIn> none; std::vector<float> l (64), r (64);
            const int blocks = (int) (bars * 2.0 * 48000 / 64);
            uint32_t last = 0;
            for (int b = 0; b < blocks; ++b)
            {
                e.process (l.data(), r.data(), nullptr, nullptr, 64, t, none, nullptr);
                const uint32_t now = e.uiTriggerCount[0].load();
                if (firstHitSample && now != last) firstHitSample->push_back (b * 64);
                last = now;
                t.ppq += 64 * 2.0 / 48000;
            }
            return (int) e.uiTriggerCount[0].load();
        };
        const int half = countHits ([] (PatternBank& pb) { for (int s = 0; s < 16; ++s) { pb.setTrig (0, 0, s, true); pb.setExpr (0, 0, X_CHANCE, s, 4); } }, 8);
        std::printf ("     50%% chance: %d of 128 steps\n", half);
        CHECK (half > 40 && half < 88, "CHANCE 50% plays about half the steps");
        const int cond = countHits ([] (PatternBank& pb) { pb.setTrig (0, 0, 4, true); pb.setExpr (0, 0, X_CHANCE, 4, 8); }, 4);
        CHECK (cond == 2, "CHANCE 1:2 plays every other loop");
        const int rolls = countHits ([] (PatternBank& pb) { pb.setTrig (0, 0, 4, true); pb.setExpr (0, 0, X_ROLL, 4, 4); }, 1);
        CHECK (rolls == 3, "ROLL x3 plays three hits");
        const int flam = countHits ([] (PatternBank& pb) { pb.setTrig (0, 0, 4, true); pb.setExpr (0, 0, X_ROLL, 4, 1); }, 1);
        CHECK (flam == 2, "ROLL flam plays a grace note and the main hit");
        std::vector<int> onGrid, early;
        countHits ([] (PatternBank& pb) { pb.setTrig (0, 0, 4, true); }, 1, &onGrid);
        countHits ([] (PatternBank& pb) { pb.setTrig (0, 0, 4, true); pb.setExpr (0, 0, X_NUDGE, 4, -12); }, 1, &early);
        const int diff = (! onGrid.empty() && ! early.empty()) ? onGrid[0] - early[0] : 0;
        std::printf ("     nudge -12: %d samples early (expected about 2000)\n", diff);
        CHECK (diff > 1800 && diff < 2200, "NUDGE moves a hit a third of a step early");
    }
    {
        // expression survives state, presets, MIDI export and channel moves
        proc->loadKit (factoryKits()[4]);
        CHECK (proc->presetName == "Jungle Ghosts", "Jungle Ghosts factory kit loads");
        proc->getEngine().patterns.setExpr (2, 6, X_PITCH, 3, -7);
        MemoryBlock mb; proc->getStateInformation (mb);
        auto p2 = std::make_unique<Ferrum8Processor>();
        p2->setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int p = 0; p < kNumPatterns; ++p)
            for (int c = 0; c < 8; ++c)
                for (int l = 0; l < kNumExpr; ++l)
                    for (int st = 0; st < 16; ++st)
                        if (p2->getEngine().patterns.expr (p, c, l, st) != proc->getEngine().patterns.expr (p, c, l, st)) same = false;
        CHECK (same, "state restores every expression lane");
        CHECK (std::abs (p2->getPatchValue (3, P_HUMANIZE) - proc->getPatchValue (3, P_HUMANIZE)) < 1e-5f && proc->getPatchValue (3, P_HUMANIZE) > 0.3f,
               "state restores feel parameters");
        const File mid = outDir.getChildFile ("ghosts.mid");
        CHECK (proc->exportPatternMidi (mid, 1), "export ghosted chain to MIDI");
        {
            FileInputStream in (mid); MidiFile mf; mf.readFrom (in);
            int notes = 0, soft = 0;
            for (int i = 0; i < mf.getTrack (0)->getNumEvents(); ++i)
            {
                const auto& m = mf.getTrack (0)->getEventPointer (i)->message;
                if (m.isNoteOn() && m.getNoteNumber() == 39) { ++notes; if (m.getVelocity() < 40) ++soft; }
            }
            std::printf ("     snare notes in MIDI: %d (%d ghosted)\n", notes, soft);
            CHECK (notes >= 14 && soft >= 5, "MIDI export carries ghost velocities and rolls");
        }
        const int ghostBefore = proc->getEngine().patterns.expr (0, 3, X_GHOST, 9);
        proc->moveChannel (3, 0);
        CHECK (proc->getEngine().patterns.expr (0, 0, X_GHOST, 9) == ghostBefore && ghostBefore == 3, "channel move keeps expression");
        proc->undo();
        // pattern randomizer + ghost generator
        PatternData d = proc->getPattern (5);
        d.clearChannel (3);
        d.trig[3] = (uint16_t) ((1u << 4) | (1u << 12));
        const int added = proc->randomizer.addGhostNotes (d, proc->channelRoles());
        int ghosts = 0;
        for (int st = 0; st < 16; ++st) if (((d.trig[3] >> st) & 1u) && d.expr[3][X_GHOST][(size_t) st] > 0) ++ghosts;
        CHECK (added > 0 && ghosts == added && (d.trig[3] & (1u << 4)), "Add Ghost Notes fills the snare channel with ghosts");
        // audio export of the jungle kit renders
        const File wav = outDir.getChildFile ("jungle.wav");
        CHECK (proc->exportPatternAudio (wav, 0, 24, 48000, 2), "export jungle pattern to WAV");
    }

    // ---- randomizer -----------------------------------------------------------------------
    {
        auto allPatches = [&] { std::array<Patch, 8> a; for (int c = 0; c < 8; ++c) a[(size_t) c] = proc->getCurrentPatch (c); return a; };
        auto allPatterns = [&] { std::array<PatternData, kNumPatterns> a; for (int p = 0; p < kNumPatterns; ++p) a[(size_t) p] = proc->getPattern (p); return a; };
        auto samePat = [] (const PatternData& a, const PatternData& b)
        {
            return a.trig == b.trig && a.acc == b.acc && a.fill == b.fill && a.expr == b.expr && a.length == b.length;
        };
        proc->loadKit (factoryKits()[0]);
        proc->locked = {};
        proc->dice = DiceSettings {};
        // Beat keeps sounds
        auto soundsBefore = allPatches();
        auto patsBefore = allPatterns();
        proc->dice.style = BeatStyle::DnB;
        proc->rollBeat();
        CHECK (allPatches() == soundsBefore, "Randomize Beat keeps every sound");
        CHECK (! samePat (proc->getPattern (0), patsBefore[0]), "Randomize Beat changes the selected pattern");
        CHECK (samePat (proc->getPattern (1), patsBefore[1]), "Randomize Beat leaves the other patterns (selected-pattern mode)");
        // Sounds keeps patterns
        patsBefore = allPatterns();
        proc->rollSounds();
        bool patsSame = true;
        for (int p = 0; p < kNumPatterns; ++p) patsSame &= samePat (proc->getPattern (p), patsBefore[(size_t) p]);
        CHECK (patsSame, "Randomize Sounds keeps every pattern");
        CHECK (allPatches() != soundsBefore, "Randomize Sounds changes the sounds");
        // locks
        proc->locked[0] = true; proc->locked[3] = true;
        soundsBefore = allPatches(); patsBefore = allPatterns();
        proc->rollAll();
        bool kept = proc->getCurrentPatch (0) == soundsBefore[0] && proc->getCurrentPatch (3) == soundsBefore[3];
        for (int p = 0; p < kNumPatterns; ++p)
        {
            const auto now = proc->getPattern (p).channel (0), was = patsBefore[(size_t) p].channel (0);
            kept &= now.trig == was.trig && now.expr == was.expr;
        }
        CHECK (kept, "locked channels keep sound and steps through Randomize All");
        CHECK (proc->getCurrentPatch (1) != soundsBefore[1], "unlocked channels change");
        proc->locked = {};
        proc->rollAll();
        // set slots
        {
            const auto roles = proc->channelRoles();
            int kickCh = -1, snareCh = -1;
            for (int c = 0; c < 8; ++c) { if (roles[(size_t) c] == Flavor::Kick && kickCh < 0) kickCh = c; if (roles[(size_t) c] == Flavor::Snare && snareCh < 0) snareCh = c; }
            CHECK (kickCh >= 0 && snareCh >= 0, "random kit has a kick and a snare");
            const auto e = proc->getPattern (4), f = proc->getPattern (5);
            CHECK (kickCh >= 0 && e.trig[(size_t) kickCh] == 0, "set pattern E is a breakdown (no kick)");
            CHECK (kickCh >= 0 && (f.trig[(size_t) kickCh] & 0x1111) == 0x1111, "set pattern F build-up has four on the floor");
        }
        // styles and density
        Randomizer rz (1234);
        std::array<Flavor, 8> roles = { Flavor::Kick, Flavor::Tom, Flavor::Perc, Flavor::Snare, Flavor::Cymbal, Flavor::ClosedHat, Flavor::OpenHat, Flavor::Clap };
        bool anchorsOk = true;
        for (int st = 1; st < (int) BeatStyle::Count; ++st)
        {
            DiceSettings ds; ds.style = (BeatStyle) st;
            PatternData d; rz.generateBeat (d, roles, ds, ChannelMask {});
            const bool kick0 = d.trig[0] & 1u;
            if (! kick0 && ds.style != BeatStyle::Euclid) { anchorsOk = false; std::printf ("     style %s: no kick on 1\n", kBeatStyleNames[st]); }
            if (ds.style == BeatStyle::House && (d.trig[0] & 0x1111) != 0x1111) anchorsOk = false;
            if (ds.style == BeatStyle::DnB && (d.trig[3] & 0x1010) != 0x1010) anchorsOk = false;
            if (ds.style == BeatStyle::Trap && ! (d.trig[3] & 0x0100)) anchorsOk = false;
        }
        CHECK (anchorsOk, "every style keeps its anchors (house 4/4 kick, DnB backbeats, trap snare on 3)");
        auto hitsAt = [&] (float density)
        {
            int total = 0;
            for (int i = 0; i < 60; ++i)
            {
                DiceSettings ds; ds.style = BeatStyle::Breakbeat; ds.density = density;
                PatternData d; rz.generateBeat (d, roles, ds, ChannelMask {});
                for (auto t : d.trig) total += __builtin_popcount (t);
            }
            return total / 60.0;
        };
        const double sparse = hitsAt (0.1f), busy = hitsAt (0.9f);
        std::printf ("     breakbeat hits per pattern: density 10%% = %.1f, 90%% = %.1f\n", sparse, busy);
        CHECK (busy > sparse * 1.3, "density makes beats busier");
        // tuning
        Patch tp = defaultPatch (Model::Drum); tp[P_OSC_COARSE] = 32.f / 127.f;
        Randomizer::tuneToKey (tp, 9); // A minor pentatonic: A C D E G
        const int note = (int) std::lround (tp[P_OSC_COARSE] * 127.f);
        CHECK (note == 33 || note == 31, "Key snaps a drum to the scale");
        // history
        const int hBefore = (int) proc->getRollHistory().size();
        const auto pBefore = proc->getPattern (0);
        proc->rollBeat();
        CHECK ((int) proc->getRollHistory().size() == hBefore + 1, "rolls are kept in the history");
        proc->restoreRoll (1);
        CHECK (samePat (proc->getPattern (0), pBefore), "an earlier roll can be brought back");
        // evolve (orbit)
        {
            FakePlayHead ph;
            proc->setPlayHead (&ph);
            proc->setParamValue ("play", 1.f);
            proc->dice.mutation = 0.8f; proc->dice.evolveEvery = 1;
            pump (60);
            proc->setEvolve (1);
            const auto home = proc->getPattern (0);
            bool changed = false;
            for (int k = 0; k < 6 && ! changed; ++k) { runBlocks (*proc, 200, &ph); pump (60); changed = ! samePat (proc->getPattern (0), home); }
            CHECK (changed, "Evolve changes the pattern while playing");
            proc->setEvolve (0);
            CHECK (samePat (proc->getPattern (0), home), "turning Orbit off brings the original back");
            proc->setParamValue ("play", 0.f);
            runBlocks (*proc, 200, &ph);
            proc->setPlayHead (nullptr);
        }
        // state keeps dice settings and locks
        proc->dice.style = BeatStyle::Footwork; proc->dice.key = 4; proc->locked[6] = true;
        MemoryBlock mb; proc->getStateInformation (mb);
        auto p3 = std::make_unique<Ferrum8Processor>();
        p3->setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (p3->dice.style == BeatStyle::Footwork && p3->dice.key == 4 && p3->locked[6] && ! p3->locked[5], "state keeps dice settings and locks");
        proc->locked = {};
        ui::LookAndFeel lnf;
        ui::DicePanel dp (*proc, {});
        dp.setLookAndFeel (&lnf);
        snapshot (dp, outDir.getChildFile ("ui_dice.png"));
        dp.setLookAndFeel (nullptr);
    }

    // editor snapshots
    {
        proc->loadKit (factoryKits()[0]);
        std::unique_ptr<AudioProcessorEditor> ed (proc->createEditor());
        ed->setSize (Ferrum8Editor::kDesignW, Ferrum8Editor::kDesignH);
        pump (150);
        snapshot (*ed, outDir.getChildFile ("ui_kick.png"));
        proc->editorChannel = 3; proc->sendUiChange(); pump (150);
        snapshot (*ed, outDir.getChildFile ("ui_snare.png"));
        proc->editorChannel = 5; proc->matrixView = true; proc->sendUiChange(); pump (150);
        snapshot (*ed, outDir.getChildFile ("ui_hat_matrix.png"));
        proc->loadKit (factoryKits()[1]);
        proc->editorChannel = 4; proc->matrixView = false; proc->sendUiChange(); pump (150);
        snapshot (*ed, outDir.getChildFile ("ui_concrete_cymbal.png"));
        ed->setSize (Ferrum8Editor::kDesignW * 3 / 4, Ferrum8Editor::kDesignH * 3 / 4); pump (100);
        snapshot (*ed, outDir.getChildFile ("ui_zoom75.png"));
        ed->setSize (Ferrum8Editor::kDesignW, Ferrum8Editor::kDesignH); pump (100);
        proc->loadKit (factoryKits()[4]);
        proc->editorChannel = 3; proc->matrixView = false; proc->exprLane = X_GHOST; proc->sendUiChange(); pump (150);
        snapshot (*ed, outDir.getChildFile ("ui_jungle_ghost.png"));
        proc->exprLane = X_NUDGE; proc->editorChannel = 5; proc->sendUiChange(); pump (150);
        snapshot (*ed, outDir.getChildFile ("ui_jungle_nudge.png"));
        proc->matrixView = true; proc->sendUiChange(); pump (150);
        snapshot (*ed, outDir.getChildFile ("ui_jungle_matrix.png"));
        ed.reset();
    }
    std::printf ("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
