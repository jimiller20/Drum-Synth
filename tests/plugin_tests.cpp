// plugin_tests.cpp - headless checks of the JUCE processor + editor snapshots
// Run: Ferrum8Tests <outputDir>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/ui/DicePanel.h"
#include "../Source/fx/FxChains.h"
#include <cstdio>
#include <complex>
#include <functional>
#include <set>

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
    CHECK (proc->getParameters().size() == 8 * NUM_PATCH_PARAMS + 16, "parameter count");

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


    // ---- sound dice: morph ends, genres, curated cymbals, per-hit variation on metals --------
    {
        auto p = std::make_unique<Ferrum8Processor>();
        p->setRateAndBufferSizeDetails (48000, 512);
        p->prepareToPlay (48000, 512);
        auto same = [] (const Patch& a, const Patch& b) { for (int i = 0; i < NUM_PATCH_PARAMS; ++i) if (std::abs (a[(size_t) i] - b[(size_t) i]) > 1e-6f) return false; return true; };
        const int ch = 4; // the cymbal channel of the init kit
        const Patch a0 = p->getEndA (ch), b0 = p->getEndB (ch);
        p->setParamValue ("morph", 0.f); runBlocks (*p, 2, nullptr);
        Randomizer r (5);
        const int e1 = p->setPatchAtActiveEnd (ch, r.cymbal (CymbalType::Ride, p->getActiveEnd (ch)));
        CHECK (e1 == 0 && ! same (p->getEndA (ch), a0) && same (p->getEndB (ch), b0), "morph at A: a channel's dice change only end A");
        p->setParamValue ("morph", 1.f); runBlocks (*p, 2, nullptr);
        const Patch a1 = p->getEndA (ch);
        const int e2 = p->setPatchAtActiveEnd (ch, r.cymbal (CymbalType::Crash, p->getActiveEnd (ch)));
        CHECK (e2 == 1 && same (p->getEndA (ch), a1) && ! same (p->getEndB (ch), b0), "morph at B: a channel's dice change only end B");
        const int e3 = p->setPatchAtActiveEnd (ch, r.patch (Flavor::Snare, p->getActiveEnd (ch)));
        CHECK (e3 == 2 && choiceIndex (p->getEndA (ch)[P_MODEL], 4) == (int) Model::Snare && choiceIndex (p->getEndB (ch)[P_MODEL], 4) == (int) Model::Snare,
               "a new voice model goes to both ends");
        // Sounds roll at B keeps every A end of the same model
        p->loadKit (factoryKits()[0]);
        p->setParamValue ("morph", 1.f); runBlocks (*p, 2, nullptr);
        std::array<Patch, 8> before;
        for (int c = 0; c < 8; ++c) before[(size_t) c] = p->getEndA (c);
        p->rollSounds();
        int keptA = 0, changedB = 0;
        auto sameMorphable = [] (const Patch& x, const Patch& y)
        {
            for (int i = 0; i < NUM_PATCH_PARAMS; ++i) if (paramDef (i).morphable && std::abs (x[(size_t) i] - y[(size_t) i]) > 1e-6f) return false;
            return true;
        };
        for (int c = 0; c < 8; ++c)
        {
            if (sameMorphable (p->getEndA (c), before[(size_t) c])) ++keptA;
            if (! same (p->getEndB (c), before[(size_t) c])) ++changedB;
        }
        std::printf ("     Sounds at B: %d A ends kept, %d B ends new\n", keptA, changedB);
        CHECK (keptA == 8 && changedB == 8, "Sounds with the morph at B rolls the B ends and keeps A");
        p->setParamValue ("morph", 0.f); runBlocks (*p, 2, nullptr);

        // genres shape the recipes
        auto stat = [&r] (Flavor f, Genre g, int param, bool wantMax)
        {
            float v = wantMax ? -1.f : 2.f;
            for (int i = 0; i < 60; ++i)
            {
                const Patch q = r.patch (f, defaultPatch (Model::Drum), g);
                v = wantMax ? std::max (v, q[(size_t) param]) : std::min (v, q[(size_t) param]);
            }
            return v * 127.f;
        };
        CHECK (stat (Flavor::Snare, Genre::DnB, P_OSC_COARSE, false) >= 54.9f && stat (Flavor::Snare, Genre::DnB, P_DECAY, true) <= 15.1f,
               "Drum & Bass snares are high-tuned and tight");
        CHECK (stat (Flavor::Kick, Genre::Trap, P_DECAY, false) >= 59.9f, "Trap kicks are long 808s");
        CHECK (stat (Flavor::ClosedHat, Genre::LoFi, P_DECIMATE, true) <= 95.1f, "Lo-Fi hats are decimated");
        {
            DiceSettings d; d.style = BeatStyle::DnB;
            bool dnbish = true;
            for (int i = 0; i < 20; ++i) { const Genre g = r.genreFor (d); dnbish &= g == Genre::DnB || g == Genre::Jungle; }
            d.genre = (int) Genre::Techno;
            CHECK (dnbish && r.genreFor (d) == Genre::Techno, "the sound genre follows the beat style unless one is chosen");
        }
        // every genre x flavor plays and stays finite
        {
            bool ok = true;
            for (int g = 0; g < (int) Genre::Count; ++g)
                for (int f = 0; f < (int) Flavor::Chaos; ++f)
                {
                    const Patch q = r.patch ((Flavor) f, defaultPatch (Model::Drum), (Genre) g);
                    Voice v; v.prepare (48000, 3);
                    for (int i = 0; i < NUM_PATCH_PARAMS; ++i) v.setParam (i, q[(size_t) i]);
                    v.trigger (1.f, 60);
                    std::vector<float> x (48000);
                    v.render (x.data(), (int) x.size());
                    float pk = 0; for (float s2 : x) pk = std::max (pk, std::isfinite (s2) ? std::abs (s2) : 99.f);
                    if (pk < 0.02f || pk > 1.5f) { ok = false; std::printf ("     %s %s peak %.3f\n", kGenreNames[g], kFlavorNames[f], pk); }
                }
            CHECK (ok, "every genre makes every kind of sound audible and in range");
        }
        // curated cymbals: no single whistling partial, no low mud
        {
            auto spectrum = [] (const std::vector<float>& x, double& domFrac, double& lowFrac)
            {
                const int N = 8192;
                std::vector<std::complex<double>> a ((size_t) N);
                for (int i = 0; i < N; ++i) a[(size_t) i] = (double) x[(size_t) i] * (0.5 - 0.5 * std::cos (2 * MathConstants<double>::pi * i / (N - 1)));
                for (size_t i = 1, j = 0; i < (size_t) N; ++i) { size_t bit = (size_t) N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
                for (size_t len = 2; len <= (size_t) N; len <<= 1)
                {
                    const double ang = -2 * MathConstants<double>::pi / (double) len;
                    const std::complex<double> wl (std::cos (ang), std::sin (ang));
                    for (size_t i = 0; i < (size_t) N; i += len)
                    {
                        std::complex<double> w (1);
                        for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; }
                    }
                }
                double all = 0, best = 0, low = 0, tot = 0; int imax = 0;
                for (int i = 1; i < N / 2; ++i)
                {
                    const double f = i * 48000.0 / N, pw = std::norm (a[(size_t) i]);
                    tot += pw; if (f < 400) low += pw;
                    if (f >= 1000 && f <= 16000) { all += pw; if (pw > best) { best = pw; imax = i; } }
                }
                double around = 0; for (int i = std::max (1, imax - 8); i <= std::min (N / 2 - 1, imax + 8); ++i) around += std::norm (a[(size_t) i]);
                domFrac = all > 0 ? around / all : 1; lowFrac = tot > 0 ? low / tot : 1;
            };
            bool clean = true;
            for (int t = 0; t < (int) CymbalType::Count; ++t)
            {
                if ((CymbalType) t == CymbalType::Bell) continue; // a bell is meant to ring on a few partials
                int whistles = 0;
                for (int i = 0; i < 40; ++i)
                {
                    const Patch q = r.cymbal ((CymbalType) t, defaultPatch (Model::Cymbal));
                    Voice v; v.prepare (48000, 9);
                    for (int k = 0; k < NUM_PATCH_PARAMS; ++k) v.setParam (k, q[(size_t) k]);
                    v.trigger (1.f, 60);
                    std::vector<float> x (8192);
                    v.render (x.data(), (int) x.size());
                    double dom = 0, low = 0;
                    spectrum (x, dom, low);
                    if (dom > 0.4 || low > 0.02) ++whistles;
                }
                if (whistles > 2) { clean = false; std::printf ("     %s: %d of 40 whistle or rumble\n", kCymbalTypeNames[t], whistles); }
            }
            CHECK (clean, "curated cymbals have a dense metallic spectrum (no single whistling partial, no low rumble)");
        }
        // Vary on metals: the brightness of a cymbal changes from hit to hit (zero-crossing rate as a
        // brightness measure, spread over 12 hits); the LXR's free-running carrier alone varies little
        {
            auto spread = [] (float vary)
            {
                Randomizer rr (11);
                Patch q = rr.cymbal (CymbalType::Cymbal808, defaultPatch (Model::Cymbal));
                q[P_VARIANCE] = vary;
                Voice v; v.prepare (48000, 4);
                for (int k = 0; k < NUM_PATCH_PARAMS; ++k) v.setParam (k, q[(size_t) k]);
                std::vector<float> h (4096), gap (48000);
                std::vector<double> z;
                for (int hit = 0; hit < 12; ++hit)
                {
                    v.trigger (1.f, 60); v.render (h.data(), 4096);
                    int zc = 0; for (int i = 1; i < 4096; ++i) zc += (h[(size_t) i] >= 0) != (h[(size_t) i - 1] >= 0);
                    z.push_back (zc);
                    for (int s2 = 0; s2 < 20 && ! v.isFinished(); ++s2) v.render (gap.data(), 48000);
                }
                double m = 0; for (double x : z) m += x; m /= (double) z.size();
                double var = 0; for (double x : z) var += (x - m) * (x - m);
                return std::sqrt (var / (double) z.size()) / m;
            };
            const double s0 = spread (0.f), s1 = spread (0.6f);
            std::printf ("     cymbal brightness spread over 12 hits: vary 0 -> %.3f, vary 60%% -> %.3f\n", s0, s1);
            CHECK (s1 > 2.0 * s0 && s1 > 0.02, "Vary changes the metal on every cymbal hit");
        }
    }

    // ---- FX rack ---------------------------------------------------------------------------
    {
        namespace F = ferrum::fx;
        auto noise = [] (std::vector<float>& L, std::vector<float>& R, int n, uint32_t seed)
        {
            L.resize ((size_t) n); R.resize ((size_t) n);
            Random rnd ((int64) seed);
            for (int i = 0; i < n; ++i)
            {
                const float env = std::exp (-(float) (i % 6000) / 1200.f); // drum-like bursts
                L[(size_t) i] = (rnd.nextFloat() * 2.f - 1.f) * 0.5f * env;
                R[(size_t) i] = (rnd.nextFloat() * 2.f - 1.f) * 0.5f * env;
            }
        };
        auto runBus = [] (F::FxRack& r, int bus, std::vector<float> L, std::vector<float> R)
        {
            for (size_t i = 0; i < L.size(); i += 480) r.process (bus, L.data() + i, R.data() + i, (int) std::min<size_t> (480, L.size() - i));
            return std::make_pair (L, R);
        };
        auto maxDiff = [] (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
        {
            float d = 0; for (size_t i = from; i < a.size(); ++i) d = std::max (d, std::abs (a[i] - b[i])); return d;
        };
        auto rms = [] (const std::vector<float>& a, size_t from = 0)
        {
            double s = 0; for (size_t i = from; i < a.size(); ++i) s += (double) a[i] * a[i]; return (float) std::sqrt (s / (double) std::max<size_t> (1, a.size() - from));
        };
        std::vector<float> L, R;
        noise (L, R, 48000, 7);
        {
            F::FxRack r; r.prepare (48000, 512);
            const auto out = runBus (r, 0, L, R);
            CHECK (maxDiff (out.first, L) == 0.f && maxDiff (out.second, R) == 0.f, "an empty bus passes its input through untouched");
        }
        {
            F::FxRack r; r.prepare (48000, 512);
            F::RackState st;
            F::setSlot (st.buses[0].lanes[0].slots[0], F::Type::Utility, { { "Gain", 6 } });
            F::setSlot (st.buses[0].lanes[0].slots[1], F::Type::Utility, { { "Gain", -6 } });
            r.setState (st);
            const auto out = runBus (r, 0, L, R);
            CHECK (maxDiff (out.first, L, 4800) < 0.01f, "slots in a lane run in series (+6 dB then -6 dB = unity)");
            st.buses[0].dry = 1.f;
            F::setSlot (st.buses[0].lanes[0].slots[1], F::Type::None);
            F::setSlot (st.buses[0].lanes[0].slots[0], F::Type::Utility, { { "Gain", 0 } });
            F::setSlot (st.buses[0].lanes[1].slots[0], F::Type::Utility, { { "Gain", 0 } });
            r.setState (st);
            runBus (r, 0, L, R); // let the level smoothing settle
            const auto par = runBus (r, 0, L, R);
            CHECK (std::abs (rms (par.first, 4800) / rms (L, 4800) - 3.f) < 0.05f, "lanes run in parallel and sum with the dry signal (dry + 2 lanes = 3x)");
            r.setLaneMute (0, 1, true);
            runBus (r, 0, L, R);
            const auto muted = runBus (r, 0, L, R);
            CHECK (std::abs (rms (muted.first, 4800) / rms (L, 4800) - 2.f) < 0.05f, "a muted lane drops out of the mix");
            r.setBypass (0, 0, 0, true);
            r.setLaneMute (0, 1, false);
            r.setDry (0, 0.f);
            r.setLaneLevel (0, 1, 0.f);
            runBus (r, 0, L, R);
            const auto byp = runBus (r, 0, L, R);
            std::printf ("     bypass: max difference %.5f (the bus DC blocker only)\n", maxDiff (byp.first, L, 4800));
            CHECK (maxDiff (byp.first, L, 4800) < 0.01f, "a bypassed module passes the lane through");
            r.collectGarbage();
        }
        // every module at three sample rates: finite, bounded, quiet afterwards
        bool allFinite = true, allBounded = true, allSettle = true, allChange = true;
        String bad;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int t = 1; t < (int) F::Type::Count; ++t)
            {
                F::FxRack r; r.prepare (sr, 512);
                F::RackState st;
                F::setSlot (st.buses[0].lanes[0].slots[0], (F::Type) t);
                r.setState (st);
                std::vector<float> a, b;
                noise (a, b, (int) sr, 3);
                a.resize ((size_t) sr * 9, 0.f); b.resize ((size_t) sr * 9, 0.f); // 1 s of hits, then 8 s of silence
                const auto out = runBus (r, 0, a, b);
                float peak = 0, tail = 0;
                for (size_t i = 0; i < out.first.size(); ++i)
                {
                    const float v = std::max (std::abs (out.first[i]), std::abs (out.second[i]));
                    if (! std::isfinite (v)) allFinite = false;
                    peak = std::max (peak, v);
                    if (i > out.first.size() - (size_t) sr / 2) tail = std::max (tail, v);
                }
                const String name (F::info ((F::Type) t).name);
                if (peak > 6.f) { allBounded = false; bad << name << " peak " << peak << "; "; }
                if (tail > 0.003f) { allSettle = false; bad << name << " tail " << tail << "; "; }
                if (maxDiff (out.first, a) < 1.0e-4f) { allChange = false; bad << name << " does nothing; "; }
            }
        CHECK (allFinite, "every FX module stays finite at 44.1, 48 and 96 kHz");
        CHECK (allBounded, ("every FX module keeps its level in range " + bad).toRawUTF8());
        CHECK (allSettle, ("every FX module falls silent after its input stops " + bad).toRawUTF8());
        CHECK (allChange, ("every FX module changes the sound at its defaults " + bad).toRawUTF8());
        {
            // Ferric: the six preamps sound different, and feedback adds harmonics
            std::vector<float> ref;
            float minDiff = 1e9f;
            for (int pre = 0; pre < 6; ++pre)
            {
                F::FxRack r; r.prepare (48000, 512);
                F::RackState st;
                F::setSlot (st.buses[0].lanes[0].slots[0], F::Type::Ferric, { { "Preamp", (float) pre }, { "Drive", 18 } });
                r.setState (st);
                const auto out = runBus (r, 0, L, R);
                if (pre > 0) minDiff = std::min (minDiff, maxDiff (out.first, ref));
                ref = out.first;
            }
            CHECK (minDiff > 0.01f, "Ferric's six preamps each sound different");
        }
        // chains and the chain dice
        {
            bool ok = true;
            for (const auto& c : F::chains())
            {
                F::RackState st;
                c.build (st.buses[0]);
                int mods = 0;
                for (auto& l : st.buses[0].lanes) for (auto& sl : l.slots) mods += sl.type != F::Type::None;
                F::FxRack r; r.prepare (48000, 512); r.setState (st);
                const auto out = runBus (r, 0, L, R);
                float pk = 0; for (float v : out.first) pk = std::max (pk, std::isfinite (v) ? std::abs (v) : 99.f);
                if (mods == 0 || pk > 4.f || pk < 0.01f) { ok = false; std::printf ("     chain %s: %d modules, peak %.3f\n", c.name, mods, pk); }
            }
            CHECK (ok, "every ready-made chain builds and plays at a sane level");
            bool rolled = true;
            std::set<String> distinct;
            for (uint32_t seed = 1; seed <= 40; ++seed)
            {
                const auto b = F::randomChain (seed, seed % 5 == 0);
                F::RackState st; st.buses[0] = b;
                String sig;
                for (auto& l : b.lanes) for (auto& sl : l.slots) if (sl.type != F::Type::None) sig << F::info (sl.type).id << ",";
                distinct.insert (sig);
                F::FxRack r; r.prepare (48000, 512); r.setState (st);
                const auto out = runBus (r, 0, L, R);
                float pk = 0; for (float v : out.first) pk = std::max (pk, std::isfinite (v) ? std::abs (v) : 99.f);
                if (sig.isEmpty() || pk > 4.f || pk < 0.005f) { rolled = false; std::printf ("     seed %u: %s peak %.3f\n", seed, sig.toRawUTF8(), pk); }
            }
            CHECK (rolled, "rolled chains are never empty, silent or blown up");
            CHECK (distinct.size() > 15, "rolled chains vary (more than 15 different chains in 40 rolls)");
        }
        // processor: routing to the buses, presets, legacy files
        {
            auto p = std::make_unique<Ferrum8Processor>();
            p->setRateAndBufferSizeDetails (48000, 512);
            p->prepareToPlay (48000, 512);
            MidiBuffer m; m.addEvent (MidiMessage::noteOn (10, 36, (uint8) 127), 0);
            AudioBuffer<float> dry (2, 512 * 30), wet (2, 512 * 30);
            auto render = [&p, &m] (AudioBuffer<float>& out)
            {
                p->getEngine().reset();
                AudioBuffer<float> buf (2, 512);
                for (int b = 0; b < 30; ++b)
                {
                    buf.clear();
                    MidiBuffer mm; if (b == 0) mm = m;
                    p->processBlock (buf, mm);
                    for (int c = 0; c < 2; ++c) out.copyFrom (c, b * 512, buf, c, 0, 512);
                }
            };
            render (dry);
            F::RackState st;
            F::chains()[0].build (st.buses[0]); // Brut Crunch on FX 1
            p->getRack().setState (st);
            render (wet);
            CHECK (dry.getMagnitude (0, 0, dry.getNumSamples()) > 0.05f, "kick plays");
            float d = 0;
            for (int i = 0; i < dry.getNumSamples(); ++i) d = std::max (d, std::abs (dry.getSample (0, i) - wet.getSample (0, i)));
            CHECK (d == 0.f, "FX 1 does not touch a channel that plays on output A");
            p->setPatchValue (0, P_OUTPUT, choiceValue (2, paramDef (P_OUTPUT).numChoices));
            render (wet);
            d = 0;
            for (int i = 0; i < dry.getNumSamples(); ++i) d = std::max (d, std::abs (dry.getSample (0, i) - wet.getSample (0, i)));
            CHECK (d > 0.02f, "a channel sent to FX 1 plays through its chain");
            // master bus processes output A
            F::RackState st2 = p->getRack().getState();
            F::setSlot (st2.buses[F::kMasterBus].lanes[0].slots[0], F::Type::Utility, { { "Gain", -12 } });
            p->getRack().setState (st2);
            AudioBuffer<float> mast (2, 512 * 30);
            render (mast);
            render (mast);
            const float ratio = mast.getRMSLevel (0, 2048, mast.getNumSamples() - 2048) / wet.getRMSLevel (0, 2048, wet.getNumSamples() - 2048);
            std::printf ("     master -12 dB: level ratio %.3f\n", ratio);
            CHECK (std::abs (ratio - 0.251f) < 0.03f, "the Master bus processes output A");
            // preset round trip
            const auto before = p->getRack().getState();
            const auto tree = p->createPresetTree();
            p->getRack().setState ({});
            p->applyPresetTree (tree);
            const auto after = p->getRack().getState();
            bool same = true;
            for (int b = 0; b < F::kBuses; ++b)
                for (int l = 0; l < F::kLanes; ++l)
                    for (int s2 = 0; s2 < F::kSlots; ++s2)
                    {
                        const auto& x = before.buses[(size_t) b].lanes[(size_t) l].slots[(size_t) s2];
                        const auto& y = after.buses[(size_t) b].lanes[(size_t) l].slots[(size_t) s2];
                        if (x.type != y.type || x.bypass != y.bypass) same = false;
                        for (int i = 0; i < F::kMaxParams && x.type != F::Type::None; ++i)
                            if (i < (int) F::info (x.type).params.size() && std::abs (x.p[i] - y.p[i]) > 1.0e-5f) same = false;
                    }
            CHECK (same, "the FX rack is saved and loaded with the preset");
            MemoryBlock mb; p->getStateInformation (mb);
            auto p2 = std::make_unique<Ferrum8Processor>();
            p2->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (p2->getRack().moduleType (0, 0, 0) == F::Type::Ferric && p2->getRack().moduleType (F::kMasterBus, 0, 0) == F::Type::Utility
                       && choiceIndex (p2->getPatchValue (0, P_OUTPUT), paramDef (P_OUTPUT).numChoices) == 2,
                   "the host project restores the rack and the FX routing");
            // a version-1 preset: single FX bus in GLOBALS, channel output "FX"
            ValueTree old ("FERRUM8_PRESET");
            old.setProperty ("version", 1, nullptr);
            ValueTree glob ("GLOBALS");
            glob.setProperty ("fxType", 4, nullptr); glob.setProperty ("fxMix", 0.4f, nullptr); glob.setProperty ("fxP1", 0.6f, nullptr);
            glob.setProperty ("fxPingPong", 1.f, nullptr); glob.setProperty ("fxOut", 1, nullptr);
            old.appendChild (glob, nullptr);
            ValueTree chans ("CHANNELS");
            ValueTree c0 ("FERRUM8_PATCH"); c0.setProperty ("index", 2, nullptr);
            ValueTree a ("A"); a.setProperty ("output", "FX", nullptr); c0.appendChild (a, nullptr);
            chans.appendChild (c0, nullptr);
            old.appendChild (chans, nullptr);
            p->applyPresetTree (old);
            const auto& lin = F::info (F::Type::LxrDelay);
            CHECK (p->getRack().moduleType (0, 0, 0) == F::Type::LxrDelay && p->getRack().output (0) == 1
                       && std::abs (p->getRack().param (0, 0, 0, 5) - 0.4f) < 1e-5f
                       && F::choiceIndex (lin.params[4], p->getRack().param (0, 0, 0, 4)) == 1
                       && ! p->getRack().hasModules (F::kMasterBus),
                   "an old preset's FX bus becomes FX 1 with the LXR delay");
            CHECK (choiceIndex (p->getPatchValue (2, P_OUTPUT), paramDef (P_OUTPUT).numChoices) == 2, "an old patch's Output 'FX' becomes FX 1");
            p->loadKit (factoryKits()[1]);
            const bool k1 = p->getRack().moduleType (0, 0, 0) == F::Type::LxrDelay;
            p->loadKit (factoryKits()[2]);
            CHECK (k1 && p->getRack().moduleType (0, 0, 0) == F::Type::LxrDist, "factory kits bring their FX rack");
            p->getRack().collectGarbage();
        }
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
        {
            // the FX tab: Brut Crunch on FX 1 (snare + clap), Dirt + Space on FX 2 (cymbal), Drum Bus on the master
            namespace F = ferrum::fx;
            F::RackState st;
            for (const auto& c : F::chains())
            {
                if (String (c.name) == "Brut Crunch") c.build (st.buses[0]);
                if (String (c.name) == "Dirt + Space") c.build (st.buses[1]);
                if (String (c.name) == "Drum Bus") c.build (st.buses[2]);
            }
            F::setSlot (st.buses[0].lanes[0].slots[1], F::Type::Cabinet);
            proc->getRack().setState (st);
            const int nOut = paramDef (P_OUTPUT).numChoices;
            proc->setPatchValue (3, P_OUTPUT, choiceValue (2, nOut));
            proc->setPatchValue (7, P_OUTPUT, choiceValue (2, nOut));
            proc->setPatchValue (4, P_OUTPUT, choiceValue (3, nOut));
            proc->matrixView = false; proc->sendUiChange(); pump (100);
            if (auto* fe = dynamic_cast<Ferrum8Editor*> (ed.get()))
            {
                fe->getFxPanel().showBus (0);
                fe->setTab (1);
                pump (150);
                snapshot (*ed, outDir.getChildFile ("ui_fx_bus1.png"));
                fe->getFxPanel().showBus (1);
                pump (100);
                snapshot (*ed, outDir.getChildFile ("ui_fx_bus2.png"));
                fe->getFxPanel().showBus (2);
                pump (100);
                snapshot (*ed, outDir.getChildFile ("ui_fx_master.png"));
                fe->setTab (0);
                pump (100);
                snapshot (*ed, outDir.getChildFile ("ui_fx_overview.png"));
            }
        }
        ed.reset();
    }
    std::printf ("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
