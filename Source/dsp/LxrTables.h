// LxrTables.h - lookup tables from the Erica Synths LXR-02 firmware (see LxrTables.cpp)
// Copyright (C) 2012-2026 Erica Synths (tables), Ferrum 8 port.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace lxr
{
    extern const int16_t sineTable[4097];        // 4096-entry sine + guard
    extern const int16_t sawTable[11][1025];     // octave band-limited, 1024 + guard each
    extern const int16_t recTable[11][1025];
    extern const int16_t triTable[11][1025];
    extern const float   transientVolumeTable[69];
    extern const int8_t  transientData[12][2205]; // 12 one-shot attack samples, signed 8 bit
    extern const float   diodeTable[257];         // 1N914 diode curve (master distortion "Rat")
    extern const double  ecc83Table[129];         // ECC83 tube curve (master distortion "Tube")
    extern const float   midiNoteFrequencies[128]; // MidiNoteNumbers.h

    constexpr int kTransientLength = 2205;
    constexpr int kNumTransientSamples = 12;
}
