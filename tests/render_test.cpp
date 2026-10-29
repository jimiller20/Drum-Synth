// render_test.cpp - framework-free checks of the engine: renders each default voice and a pattern
// to WAV, and prints peak / RMS / NaN checks. Build: see tests/CMakeLists.txt
#include "../Source/engine/Engine.h"
#include "../Source/engine/Kits.h"
#include "../Source/engine/Randomizer.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <memory>

using namespace ferrum;

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) L.size();
    const uint32_t dataBytes = n * 2 * 2;
    auto w32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        for (float s : { L[i], R[i] })
        {
            const int v = (int) std::lround (std::max (-1.f, std::min (1.f, s)) * 32767.f);
            w16 ((uint16_t) (int16_t) v);
        }
    }
    std::fclose (f);
}

struct Stats { float peak = 0; double rms = 0; bool nan = false; };
static Stats stats (const std::vector<float>& L, const std::vector<float>& R)
{
    Stats s; double acc = 0;
    for (size_t i = 0; i < L.size(); ++i)
    {
        for (float v : { L[i], R[i] })
        {
            if (! std::isfinite (v)) s.nan = true;
            s.peak = std::max (s.peak, std::fabs (v));
            acc += (double) v * v;
        }
    }
    s.rms = std::sqrt (acc / (2.0 * (double) L.size()));
    return s;
}

static void render (Engine& e, int sr, double seconds, const Transport& t, std::vector<float>& L, std::vector<float>& R,
                    const std::vector<MidiIn>& firstBlockMidi = {})
{
    const int block = 256;
    const int total = (int) (seconds * sr);
    L.assign ((size_t) total, 0.f); R.assign ((size_t) total, 0.f);
    std::vector<float> bl (block), br (block), al (block), ar (block);
    Transport tr = t;
    std::vector<MidiOut> mo;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        std::vector<MidiIn> mi = pos == 0 ? firstBlockMidi : std::vector<MidiIn> {};
        mo.clear();
        e.process (al.data(), ar.data(), nullptr, nullptr, n, tr, mi, &mo);
        for (int i = 0; i < n; ++i) { L[(size_t) (pos + i)] = al[(size_t) i]; R[(size_t) (pos + i)] = ar[(size_t) i]; }
        if (tr.hostPlaying) tr.ppq += n * tr.bpm / 60.0 / sr;
    }
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    int failures = 0;
    for (int sr : { 44100, 48000, 96000 })
    {
        auto e = std::make_unique<Engine>();
        e->prepare (sr, 512);
        Transport idle; idle.hostHasTransport = true; idle.hostPlaying = false; idle.bpm = 120;
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            e->reset();
            std::vector<MidiIn> mi { { 0, 0x90, (uint8_t) (36 + ch), 127 } };
            std::vector<float> L, R;
            render (*e, sr, 1.5, idle, L, R, mi);
            const Stats s = stats (L, R);
            std::printf ("sr %6d  ch %d  model %-6s peak %.3f  rms %.4f %s\n", sr, ch + 1,
                         kModelNames[(int) e->getPatchParam (ch, P_MODEL) * 4 >= 4 ? 3 : (int) (e->getPatchParam (ch, P_MODEL) * 4)],
                         s.peak, s.rms, s.nan ? "NaN!" : "");
            if (s.nan || s.peak < 0.01f) ++failures;
            if (sr == 48000) writeWav (outDir + "/voice" + std::to_string (ch + 1) + ".wav", L, R, sr);
        }
    }

    // pattern test: a basic beat on pattern A, host transport playing at 120 bpm
    {
        const int sr = 48000;
        auto e = std::make_unique<Engine>();
        e->prepare (sr, 512);
        auto& pb = e->patterns;
        for (int s = 0; s < 16; s += 4) pb.setTrig (0, 0, s, true);                   // kick on beats
        pb.setTrig (0, 3, 4, true); pb.setTrig (0, 3, 12, true); pb.setAcc (0, 3, 12, true); // snare 2 & 4
        for (int s = 0; s < 16; s += 2) pb.setTrig (0, 5, s, true);                   // closed hat 8ths
        pb.setTrig (0, 6, 14, true);                                                   // open hat
        pb.setTrig (0, 7, 15, true); pb.setFill (0, 7, 15, true);                      // clap roll
        Globals g; g.playOn = true; g.swing = 0.3f; g.fillRate = 3.0f;
        e->setGlobals (g);
        Transport t; t.hostHasTransport = true; t.hostPlaying = true; t.ppq = 0; t.bpm = 120;
        std::vector<float> L, R;
        render (*e, sr, 4.0, t, L, R);
        const Stats s = stats (L, R);
        std::printf ("pattern  peak %.3f  rms %.4f %s\n", s.peak, s.rms, s.nan ? "NaN!" : "");
        if (s.nan || s.peak < 0.01f) ++failures;
        writeWav (outDir + "/pattern.wav", L, R, sr);

        // standalone-style internal clock + FX delay on the snare
        auto e2 = std::make_unique<Engine>();
        e2->prepare (sr, 512);
        e2->patterns.set (0, pb.get (0));
        e2->setPatchParam (3, P_OUTPUT, choiceValue (2, 3));
        Globals g2 = g; g2.fx.type = FxType::Delay; g2.fx.dryWet = 0.4f; g2.fx.p[0] = 0.6f; g2.fx.p[1] = 1.0f; g2.fx.p[3] = 0.5f;
        e2->setGlobals (g2);
        Transport none; none.hostHasTransport = false;
        render (*e2, sr, 4.0, none, L, R);
        const Stats s2 = stats (L, R);
        std::printf ("internal clock + delay  peak %.3f  rms %.4f %s\n", s2.peak, s2.rms, s2.nan ? "NaN!" : "");
        if (s2.nan || s2.peak < 0.01f) ++failures;
        writeWav (outDir + "/pattern_delay.wav", L, R, sr);
    }
    // factory kits
    for (const auto& kit : factoryKits())
    {
        const int sr = 48000;
        auto e = std::make_unique<Engine>();
        e->prepare (sr, 512);
        for (int c = 0; c < kNumChannels; ++c)
            for (int p = 0; p < NUM_PATCH_PARAMS; ++p) e->setPatchParam (c, p, kit.a[(size_t) c][(size_t) p]);
        for (int p = 0; p < kNumPatterns; ++p) e->patterns.set (p, kit.patterns[(size_t) p], true);
        Globals g; g.playOn = true; g.swing = kit.swing; g.fillRate = kit.fillRate; g.rate = kit.rate; g.fx = kit.fx;
        e->setGlobals (g);
        Transport t; t.hostHasTransport = true; t.hostPlaying = true; t.bpm = 124;
        std::vector<float> L, R;
        render (*e, sr, 8.0, t, L, R);
        const Stats s = stats (L, R);
        std::printf ("kit %-14s peak %.3f rms %.4f %s\n", kit.name.c_str(), s.peak, s.rms, s.nan ? "NaN!" : "");
        if (s.nan || s.peak < 0.05f || s.peak > 4.f) ++failures;
        std::string fn = kit.name; for (auto& ch : fn) if (ch == ' ') ch = '_';
        writeWav (outDir + "/kit_" + fn + ".wav", L, R, sr);
    }
    // random patches: every flavour must be audible, finite and not ring forever
    {
        const int sr = 48000;
        Randomizer rnd (1234);
        int silentCount = 0, longCount = 0;
        for (int f = 0; f < (int) Flavor::Count; ++f)
        {
            for (int k = 0; k < 40; ++k)
            {
                auto e = std::make_unique<Engine>();
                e->prepare (sr, 512);
                const Patch cur = defaultPatch (Model::Drum);
                const Patch p = rnd.patch ((Flavor) f, cur);
                for (int i = 0; i < NUM_PATCH_PARAMS; ++i) e->setPatchParam (0, i, p[(size_t) i]);
                Transport idle; idle.hostHasTransport = true;
                std::vector<float> L, R;
                render (*e, sr, 3.0, idle, L, R, { { 0, 0x90, 36, 100 } });
                const Stats s = stats (L, R);
                float tail = 0; for (size_t i = L.size() - 4800; i < L.size(); ++i) tail = std::max (tail, std::fabs (L[i]));
                if (s.nan) { std::printf ("NaN in %s #%d\n", kFlavorNames[f], k); ++failures; }
                if (s.peak < 0.02f) ++silentCount;
                if (f != (int) Flavor::Cymbal && f != (int) Flavor::Chaos && tail > 0.01f) ++longCount;
                if (k == 0) writeWav (outDir + "/random_" + std::string (kFlavorNames[f]) + ".wav", L, R, sr);
            }
        }
        std::printf ("random patches: %d near-silent, %d still ringing after 3 s (of %d)\n", silentCount, longCount, 40 * (int) Flavor::Count);
    }
    std::printf ("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
