// Groove.h - turns one sequencer step (trigger + accent + fill + expression lanes) into the hits it
// plays. Shared by the real-time sequencer, the MIDI export and the audio export.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Patterns.h"
#include "../dsp/PatchParams.h" // Articulation
#include <string>

namespace ferrum
{
struct StepHit
{
    double offset;      // quarter notes from the step's grid time (before swing); negative = early
    float vel;          // 0..1
    Articulation art;
};

constexpr int kMaxStepHits = 12;

// Fill steps roll at the fill rate (Microtonic); otherwise the ROLL lane adds a flam, a drag or
// ratchets. Returns the number of hits written to 'out'.
int buildStepHits (bool accent, bool fill, float fillRate, const int8_t* expr, double stepPpq,
                   double ppqPerSecond, StepHit* out);

// CHANCE lane: 0 always, 1..7 = 88%..13%, 8..13 = cycle conditions 1:2 2:2 1:4 2:4 3:4 4:4.
// 'cycle' counts how often the pattern has started since play (1 = first time through).
bool chancePasses (int chanceValue, int cycle, float random01);

inline double nudgePpq (int v, double stepPpq) { return v * stepPpq / 36.0; }
// how far ahead the sequencer must schedule so early hits (nudge, flams, humanize) can happen
double lookaheadPpq (double stepPpq, double ppqPerSecond);
// MIDI velocity scale for ghost levels (what the hit "means" to other instruments)
float ghostVelocityScale (int ghost);
// text for a lane value ("G2", "+3 st", "50%", "Flam" ...)
std::string exprValueText (int lane, int v, bool shortForm = false);
} // namespace ferrum
