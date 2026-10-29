// Patterns.h - Microtonic-style pattern memory: 12 patterns (A-L) of up to 16 steps, each step
// holding trigger / accent / fill flags per channel, plus pattern length and chain links.
// On top of that every step carries a set of "expression" values (ghost level, decay, tone, pitch,
// nudge, chance, roll) that shape how that one hit is played.
// Written from the message thread, read lock-free by the audio thread.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ferrum
{
using std::size_t;
constexpr int kNumChannels = 8;
constexpr int kNumPatterns = 12;
constexpr int kMaxSteps = 16;

// per-step expression lanes
enum ExprLane : int { X_GHOST = 0, X_DECAY, X_TONE, X_PITCH, X_NUDGE, X_CHANCE, X_ROLL, kNumExpr };

struct ExprInfo
{
    const char* id;      // file id
    const char* name;    // tab name
    int min, max;        // value range (0 = neutral, always inside the range)
    bool bipolar;
    const char* help;
};

inline const ExprInfo& exprInfo (int lane)
{
    static const ExprInfo t[kNumExpr] = {
        { "ghost",  "GHOST",  0, 3, false,  "Ghost level: 1-3 = softer, darker, rounder hits (depth set by the channel's Ghost knob)" },
        { "decay",  "DECAY",  -8, 8, true,  "Per-step decay: shorter (down) or longer (up) than the patch" },
        { "tone",   "TONE",   -8, 8, true,  "Per-step tone: darker (down) or brighter (up)" },
        { "pitch",  "PITCH",  -12, 12, true, "Per-step pitch in semitones" },
        { "nudge",  "NUDGE",  -12, 12, true, "Micro-timing: push the hit early (down) or lay it back (up), up to a third of a step" },
        { "chance", "CHANCE", 0, 13, false, "Probability (100% .. 13%) or cycle condition (1:2 = first of every 2 loops ...)" },
        { "roll",   "ROLL",   0, 5, false,  "Flam, drag (two grace notes) or ratchet x2 / x3 / x4 inside the step" },
    };
    return t[lane < 0 ? 0 : (lane >= kNumExpr ? kNumExpr - 1 : lane)];
}

using StepValues = std::array<int8_t, kMaxSteps>;

struct PatternData // plain copy of one pattern, used by edit operations and files
{
    std::array<uint16_t, kNumChannels> trig {}, acc {}, fill {};
    std::array<std::array<StepValues, kNumExpr>, kNumChannels> expr {}; // [channel][lane][step]
    int length = 16;
    bool chainNext = false;

    // everything one channel plays in this pattern
    struct Channel { uint16_t trig = 0, acc = 0, fill = 0; std::array<StepValues, kNumExpr> expr {}; };
    Channel channel (int c) const { return { trig[(size_t) c], acc[(size_t) c], fill[(size_t) c], expr[(size_t) c] }; }
    void setChannel (int c, const Channel& x)
    {
        trig[(size_t) c] = x.trig; acc[(size_t) c] = x.acc; fill[(size_t) c] = x.fill; expr[(size_t) c] = x.expr;
    }
    void clearChannel (int c) { setChannel (c, Channel {}); }

    // moves step 'from' of channel c onto step 'to' (all lanes); the source keeps its content
    void copyStep (int c, int from, int to)
    {
        PatternData s; s.setChannel (0, channel (c));
        place (c, to, s, from);
    }
    void swapSteps (int c, int s1, int s2)
    {
        PatternData s; s.setChannel (0, channel (c));
        place (c, s1, s, s2);
        place (c, s2, s, s1);
    }
    // rotate (dir = -1 left, +1 right) or reverse the first 'length' steps of channel c, all lanes
    void rotateChannel (int c, int dir)
    {
        const Channel src = channel (c);
        PatternData s; s.setChannel (0, src);
        for (int i = 0; i < length; ++i) { const int from = ((i - dir) % length + length) % length; place (c, i, s, from); }
    }
    void reverseChannel (int c)
    {
        PatternData s; s.setChannel (0, channel (c));
        for (int i = 0; i < length; ++i) place (c, i, s, length - 1 - i);
    }
    bool hasExpression (int c) const
    {
        for (auto& lane : expr[(size_t) c]) for (auto v : lane) if (v != 0) return true;
        return false;
    }

private:
    void place (int c, int to, const PatternData& s, int from) // s holds the source in channel 0
    {
        auto bit = [&] (uint16_t& v, uint16_t src) { const bool b = (src >> from) & 1u; v = b ? (uint16_t) (v | (1u << to)) : (uint16_t) (v & ~(1u << to)); };
        bit (trig[(size_t) c], s.trig[0]); bit (acc[(size_t) c], s.acc[0]); bit (fill[(size_t) c], s.fill[0]);
        for (int l = 0; l < kNumExpr; ++l) expr[(size_t) c][(size_t) l][(size_t) to] = s.expr[0][(size_t) l][(size_t) from];
    }
};

class PatternBank
{
public:
    PatternBank() { clearAll(); }

    bool trig (int p, int ch, int s) const { return (trigBits[p][ch].load (std::memory_order_relaxed) >> s) & 1u; }
    bool acc (int p, int ch, int s) const  { return (accBits[p][ch].load (std::memory_order_relaxed) >> s) & 1u; }
    bool fill (int p, int ch, int s) const { return (fillBits[p][ch].load (std::memory_order_relaxed) >> s) & 1u; }
    int expr (int p, int ch, int lane, int s) const { return exprVals[p][ch][lane][s].load (std::memory_order_relaxed); }
    int length (int p) const { return lengths[p].load (std::memory_order_relaxed); }
    bool chainNext (int p) const { return p < kNumPatterns - 1 && chains[p].load (std::memory_order_relaxed); }
    bool isEmpty (int p) const
    {
        for (int c = 0; c < kNumChannels; ++c) if (trigBits[p][c].load() & mask (p)) return false;
        return true;
    }

    void setTrig (int p, int ch, int s, bool on) { setBit (trigBits[p][ch], s, on); }
    void setAcc (int p, int ch, int s, bool on)  { setBit (accBits[p][ch], s, on); }
    void setFill (int p, int ch, int s, bool on) { setBit (fillBits[p][ch], s, on); }
    void setExpr (int p, int ch, int lane, int s, int v)
    {
        const auto& i = exprInfo (lane);
        exprVals[p][ch][lane][s].store ((int8_t) (v < i.min ? i.min : (v > i.max ? i.max : v)));
    }
    void setLength (int p, int len) { lengths[p].store (len < 1 ? 1 : (len > kMaxSteps ? kMaxSteps : len)); }
    void setChainNext (int p, bool on) { if (p < kNumPatterns - 1) chains[p].store (on); }

    PatternData get (int p) const
    {
        PatternData d;
        for (int c = 0; c < kNumChannels; ++c)
        {
            d.trig[(size_t) c] = trigBits[p][c].load();
            d.acc[(size_t) c] = accBits[p][c].load();
            d.fill[(size_t) c] = fillBits[p][c].load();
            for (int l = 0; l < kNumExpr; ++l)
                for (int s = 0; s < kMaxSteps; ++s) d.expr[(size_t) c][(size_t) l][(size_t) s] = exprVals[p][c][l][s].load();
        }
        d.length = lengths[p].load();
        d.chainNext = chains[p].load();
        return d;
    }
    void set (int p, const PatternData& d, bool includeChain = false)
    {
        for (int c = 0; c < kNumChannels; ++c)
        {
            trigBits[p][c].store (d.trig[(size_t) c]);
            accBits[p][c].store (d.acc[(size_t) c]);
            fillBits[p][c].store (d.fill[(size_t) c]);
            for (int l = 0; l < kNumExpr; ++l)
                for (int s = 0; s < kMaxSteps; ++s) setExpr (p, c, l, s, d.expr[(size_t) c][(size_t) l][(size_t) s]);
        }
        setLength (p, d.length);
        if (includeChain) setChainNext (p, d.chainNext);
    }
    void clearAll()
    {
        for (int p = 0; p < kNumPatterns; ++p)
        {
            for (int c = 0; c < kNumChannels; ++c)
            {
                trigBits[p][c] = 0; accBits[p][c] = 0; fillBits[p][c] = 0;
                for (auto& lane : exprVals[p][c]) for (auto& v : lane) v = 0;
            }
            lengths[p] = 16; chains[p] = false;
        }
    }

    // first and last pattern of the chain containing p
    void chainRange (int p, int& first, int& last) const
    {
        first = p; last = p;
        while (first > 0 && chainNext (first - 1)) --first;
        while (last < kNumPatterns - 1 && chainNext (last)) ++last;
    }

private:
    static void setBit (std::atomic<uint16_t>& a, int s, bool on)
    {
        uint16_t v = a.load();
        v = on ? (uint16_t) (v | (1u << s)) : (uint16_t) (v & ~(1u << s));
        a.store (v);
    }
    uint16_t mask (int p) const { const int l = length (p); return (uint16_t) (l >= 16 ? 0xFFFF : ((1u << l) - 1)); }

    std::atomic<uint16_t> trigBits[kNumPatterns][kNumChannels], accBits[kNumPatterns][kNumChannels], fillBits[kNumPatterns][kNumChannels];
    std::atomic<int8_t> exprVals[kNumPatterns][kNumChannels][kNumExpr][kMaxSteps];
    std::atomic<int> lengths[kNumPatterns];
    std::atomic<bool> chains[kNumPatterns];
};
} // namespace ferrum
