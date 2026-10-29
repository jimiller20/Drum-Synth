// Groove.cpp - see Groove.h
// SPDX-License-Identifier: GPL-3.0-only
#include "Groove.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ferrum
{
int buildStepHits (bool accent, bool fill, float fillRate, const int8_t* expr, double stepPpq,
                   double ppqPerSecond, StepHit* out)
{
    const Articulation a { expr[X_GHOST], expr[X_DECAY], expr[X_TONE], expr[X_PITCH] };
    const float v0 = accent ? 1.0f : 64.f / 127.f;
    int n = 0;

    if (fill) // Microtonic fill: a roll at the fill rate fading 127 -> 64 (accent) or 64 -> 0
    {
        const float rate = std::clamp (fillRate, 1.0f, 8.0f);
        const int count = std::min (kMaxStepHits, std::max (1, (int) std::ceil (rate - 1e-4f)));
        const float vStart = accent ? 127.f : 64.f, vEnd = accent ? 64.f : 0.f;
        for (int j = 0; j < count; ++j)
        {
            const float v = (vStart - (vStart - vEnd) * (float) j / (float) count) / 127.f;
            out[n++] = { j * stepPpq / rate, std::max (v, 1.f / 127.f), a };
        }
        return n;
    }

    const int roll = expr[X_ROLL];
    // grace notes: whisper-level, a little darker and shorter, ~28 ms apart (less on fast steps)
    const double grace = std::min (0.028 * ppqPerSecond, stepPpq * 0.3);
    Articulation g = a;
    g.ghost = 3;
    g.tone = (int8_t) std::max (-8, a.tone - 2);
    g.decay = (int8_t) std::max (-8, a.decay - 1);

    if (roll == 1) out[n++] = { -grace, v0 * 0.8f, g };                 // flam
    else if (roll == 2)                                                // drag (ruff): two quick graces
    {
        out[n++] = { -1.6 * grace, v0 * 0.7f, g };
        out[n++] = { -0.8 * grace, v0 * 0.8f, g };
    }
    else if (roll >= 3)                                                // ratchets inside the step
    {
        const int count = roll - 1;
        for (int j = 0; j < count; ++j)
            out[n++] = { j * stepPpq / count, v0 * std::pow (0.88f, (float) j), a };
        return n;
    }
    out[n++] = { 0.0, v0, a };
    return n;
}

bool chancePasses (int v, int cycle, float r)
{
    if (v <= 0) return true;
    if (v <= 7) return r < (float) (8 - v) / 8.f;
    static const int cond[6][2] = { { 1, 2 }, { 2, 2 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 } };
    const auto& c = cond[std::min (v - 8, 5)];
    const int cyc = std::max (1, cycle);
    return ((cyc - 1) % c[1]) == c[0] - 1;
}

double lookaheadPpq (double stepPpq, double ppqPerSecond)
{
    return nudgePpq (12, stepPpq) + 0.07 * ppqPerSecond;
}

float ghostVelocityScale (int ghost)
{
    static const float s[4] = { 1.f, 0.7f, 0.5f, 0.32f };
    return s[std::clamp (ghost, 0, 3)];
}

std::string exprValueText (int lane, int v, bool shortForm)
{
    char b[32];
    switch (lane)
    {
        case X_GHOST:
        {
            static const char* longN[4] = { "Full", "Soft", "Ghost", "Whisper" };
            static const char* shortN[4] = { "", "G1", "G2", "G3" };
            return (shortForm ? shortN : longN)[std::clamp (v, 0, 3)];
        }
        case X_PITCH: std::snprintf (b, sizeof b, shortForm ? "%+d" : "%+d st", v); return v == 0 && shortForm ? "" : b;
        case X_NUDGE:
            if (v == 0) return shortForm ? "" : "on grid";
            std::snprintf (b, sizeof b, "%+d%%", (int) std::lround (v * 100.0 / 36.0)); return b;
        case X_CHANCE:
        {
            static const char* n[14] = { "100%", "88%", "75%", "63%", "50%", "38%", "25%", "13%",
                                         "1:2", "2:2", "1:4", "2:4", "3:4", "4:4" };
            if (v == 0 && shortForm) return "";
            return n[std::clamp (v, 0, 13)];
        }
        case X_ROLL:
        {
            static const char* n[6] = { "Off", "Flam", "Drag", "x2", "x3", "x4" };
            if (v == 0 && shortForm) return "";
            return n[std::clamp (v, 0, 5)];
        }
        default:
            if (v == 0) return shortForm ? "" : "0";
            std::snprintf (b, sizeof b, "%+d", v); return b;
    }
}
} // namespace ferrum
