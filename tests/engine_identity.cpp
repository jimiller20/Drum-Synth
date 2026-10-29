// engine_identity.cpp - proves the sequencer adds nothing to the LXR sound when the new features
// are off: every channel of a pattern is rendered through the full Engine (scheduler, lookahead,
// expression lanes, feel, mixer) and again by triggering the same LXR voice directly at the same
// samples. With every expression lane at 0 and Human / Vary at 0 the two must be bit-identical.
// A last check turns one ghost note on to show the test does notice a change.
// Build (from the project folder, one line):
//   g++ -O2 -std=c++17 tests/engine_identity.cpp Source/engine/{Engine,Groove,Kits,Randomizer}.cpp Source/dsp/{LxrVoice,PatchParams,LxrTables,FxBus}.cpp -o engine_identity
// SPDX-License-Identifier: GPL-3.0-only
#include "../Source/engine/Engine.h"
#include "../Source/engine/Kits.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <string>

using namespace ferrum;

struct Result { double maxDiff; double peak; int hits; };

static Result compare (const KitData& kit, int pattern, int ch, double sr, int block, const PatternData* override = nullptr)
{
    const double bpm = 120.0, pps = bpm / 60.0 / sr;
    const int total = (int) std::ceil (2 * 4.0 / pps); // two bars

    // 1) the full engine, only this channel unmuted
    Engine e;
    e.prepare (sr, block);
    for (int c = 0; c < kNumChannels; ++c)
        for (int i = 0; i < NUM_PATCH_PARAMS; ++i) e.setPatchParam (c, i, kit.a[(size_t) c][(size_t) i]);
    for (int p = 0; p < kNumPatterns; ++p) e.patterns.set (p, kit.patterns[(size_t) p], true);
    if (override) e.patterns.set (pattern, *override, true);
    for (int p = 0; p < kNumPatterns; ++p) e.patterns.setChainNext (p, false);
    Globals g;
    g.playOn = true; g.selectedPattern = pattern; g.rate = StepRate::Sixteenth; g.swing = 0.f;
    g.fillRate = kit.fillRate; g.master = 1.f;
    for (int c = 0; c < kNumChannels; ++c) g.mutes[c] = c != ch;
    e.setGlobals (g);
    MidiConfig mc; mc.transmitPatternNotes = false;
    e.setMidiConfig (mc);
    Transport t; t.hostHasTransport = true; t.hostPlaying = true; t.bpm = bpm; t.ppq = 0;
    std::vector<float> engL ((size_t) total), l ((size_t) block), r ((size_t) block);
    std::vector<MidiIn> none;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        e.process (l.data(), r.data(), nullptr, nullptr, n, t, none, nullptr);
        for (int i = 0; i < n; ++i) engL[(size_t) (pos + i)] = l[(size_t) i];
        t.ppq += n * pps;
    }

    // 2) the same LXR voice triggered directly (same set-up order and noise seed as the engine)
    static const Model engineDefaults[kNumChannels] = { Model::Drum, Model::Drum, Model::Drum, Model::Snare,
                                                        Model::Cymbal, Model::HiHat, Model::HiHat, Model::Snare };
    Voice v;
    { const Patch d = defaultPatch (engineDefaults[ch]); for (int i = 0; i < NUM_PATCH_PARAMS; ++i) v.setParam (i, d[(size_t) i]); }
    v.prepare (sr, (uint32_t) (ch + 1) * 7919u);
    for (int i = 0; i < NUM_PATCH_PARAMS; ++i) v.setParam (i, kit.a[(size_t) ch][(size_t) i]);
    v.setBpm (bpm);
    const PatternData& pd = override ? *override : kit.patterns[(size_t) pattern];
    struct Hit { double ppq; float vel; };
    std::vector<Hit> hits;
    for (int bar = 0; bar < 3; ++bar)
        for (int s = 0; s < pd.length; ++s)
        {
            if (! ((pd.trig[(size_t) ch] >> s) & 1u)) continue;
            const bool acc = (pd.acc[(size_t) ch] >> s) & 1u, fill = (pd.fill[(size_t) ch] >> s) & 1u;
            // the original Microtonic rules, written out by hand (not taken from the engine)
            const double t0 = (bar * pd.length + s) * 0.25;
            if (fill)
            {
                const float rate = std::fmin (std::fmax (kit.fillRate, 1.f), 8.f);
                const int count = std::max (1, (int) std::ceil (rate - 1e-4f));
                const float vs = acc ? 127.f : 64.f, ve = acc ? 64.f : 0.f;
                for (int j = 0; j < count; ++j)
                    hits.push_back ({ t0 + j * 0.25 / rate, std::fmax ((vs - (vs - ve) * (float) j / (float) count) / 127.f, 1.f / 127.f) });
            }
            else hits.push_back ({ t0, acc ? 1.0f : 64.f / 127.f });
        }
    std::vector<float> refL ((size_t) total), mono ((size_t) block);
    double ppq0 = 0;
    size_t next = 0;
    int fired = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        const double ppq1 = ppq0 + n * pps;
        int at = 0;
        auto renderTo = [&] (int end)
        {
            if (end <= at) return;
            v.render (mono.data(), end - at);
            for (int i = 0; i < end - at; ++i) refL[(size_t) (pos + at + i)] = v.isSilent() && mono[(size_t) i] == 0.f ? 0.f : mono[(size_t) i] * v.panLeft();
            at = end;
        };
        while (next < hits.size() && hits[next].ppq < ppq1)
        {
            const int off = std::min (std::max ((int) std::floor ((hits[next].ppq - ppq0) / pps), 0), n - 1);
            renderTo (off);
            v.trigger (hits[next].vel, 60);
            ++next; ++fired;
        }
        renderTo (n);
        ppq0 += n * pps;
    }

    Result res { 0.0, 0.0, fired };
    if (std::getenv ("DEBUG_FIRST"))
        for (int i = 0; i < total; ++i)
            if (engL[(size_t) i] != refL[(size_t) i])
            {
                std::printf ("first diff at sample %d (ppq %.4f): engine %.7f ref %.7f\n", i, i * pps, engL[(size_t) i], refL[(size_t) i]);
                for (auto& h : hits) if (std::fabs (h.ppq - i * pps) < 0.5) std::printf ("   hit at ppq %.4f -> sample %.1f\n", h.ppq, h.ppq / pps);
                break;
            }
    for (int i = 0; i < total; ++i)
    {
        res.maxDiff = std::fmax (res.maxDiff, std::fabs ((double) engL[(size_t) i] - (double) refL[(size_t) i]));
        res.peak = std::fmax (res.peak, std::fabs ((double) refL[(size_t) i]));
    }
    return res;
}

int main (int argc, char** argv)
{
    if (argc > 4) // debug one case: kit pattern channel(0-based) samplerate
    {
        const auto kits = factoryKits();
        const Result r = compare (kits[(size_t) std::atoi (argv[1])], std::atoi (argv[2]), std::atoi (argv[3]), std::atof (argv[4]), 512);
        std::printf ("max diff %.7f\n", r.maxDiff);
        return 0;
    }
    int failures = 0, checks = 0;
    const auto kits = factoryKits();
    std::printf ("%-14s %-3s %-4s %-7s %6s %9s %s\n", "kit", "pat", "ch", "rate", "hits", "peak", "max difference");
    for (int k : { 0, 1, 2, 3 })
        for (int pat : { 0, 1 })
            for (double sr : { 44100.0, 48000.0, 96000.0 })
                for (int ch = 0; ch < kNumChannels; ++ch)
                {
                    const auto& kit = kits[(size_t) k];
                    if (kit.patterns[(size_t) pat].trig[(size_t) ch] == 0) continue;
                    if (kit.a[(size_t) ch][P_CHOKE] > 0.5f) continue; // choke partners are tested in plugin_tests
                    if (choiceIndex (kit.a[(size_t) ch][P_OUTPUT], 3) == 2) continue; // routed through the FX bus: not a dry voice
                    const Result r = compare (kit, pat, ch, sr, 512);
                    ++checks;
                    const bool ok = r.maxDiff == 0.0 && r.hits > 0;
                    if (! ok) ++failures;
                    if (! ok || sr == 48000.0)
                        std::printf ("%-14.14s %-3c %-4d %-7.0f %6d %9.4f %s\n", kit.name.c_str(), 'A' + pat, ch + 1, sr, r.hits, r.peak,
                                     ok ? "0 (bit-identical)" : std::to_string (r.maxDiff).c_str());
                }
    // sanity: one ghost note must change the output, or the test would prove nothing
    PatternData ghosted = kits[0].patterns[0];
    ghosted.expr[3][X_GHOST][4] = 2;
    const Result g = compare (kits[0], 0, 3, 48000.0, 512, &ghosted);
    std::printf ("\nsanity: snare with one ghost note -> max difference %.4f (%s)\n", g.maxDiff, g.maxDiff > 0.01 ? "detected, as it should be" : "NOT DETECTED");
    if (g.maxDiff <= 0.01) ++failures;
    std::printf ("%d channel renders compared, %s\n", checks, failures ? "FAILURES" : "all bit-identical to the plain LXR voices");
    return failures ? 1 : 0;
}
