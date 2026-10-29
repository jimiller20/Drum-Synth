// FxChains.h - ready-made bus chains (one click: "Brut Crunch", "Amp + Cab", "Dub Echo" ...) and
// the chain dice, which builds a random but musical bus: drive -> tone -> dynamics in series, or a
// parallel blend of a dirty lane and a space lane.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "FxRack.h"
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace ferrum::fx
{
struct Setting
{
    const char* param;            // parameter name as shown in the rack
    float value = 0.f;            // display units (dB, Hz, %, ms ...)
    const char* choice = nullptr; // for choice parameters: the option's name
};

// put a module in a slot with its defaults, then apply settings given in display units
void setSlot (RackState::Slot& s, Type t, std::initializer_list<Setting> settings = {});

struct Chain
{
    const char* name;
    const char* description;
    bool master;                          // meant for the Master bus
    void (*build) (RackState::Bus& bus);  // fills a cleared bus (output A, out 0 dB)
};
const std::vector<Chain>& chains();

// A random bus. kind: -1 any, 0 series insert, 1 parallel dirt + space, 2 space send.
// The master bus only gets gentle series chains (dynamics, tone, tape).
RackState::Bus randomChain (uint32_t seed, bool master, int kind = -1);
} // namespace ferrum::fx
