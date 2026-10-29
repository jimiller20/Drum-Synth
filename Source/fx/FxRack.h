// FxRack.h - the modular FX rack: three buses (FX 1, FX 2, Master). Each bus runs up to three
// lanes in parallel; each lane is a series chain of up to five modules. A bus mixes its dry input
// with every lane (each with its own level and mute) and sends the result to output A or B.
// Channels choose their destination with their Output setting (A, B, FX 1, FX 2); the Master bus
// processes the summed output A.
//
// Threading: edits come from the message thread, processing happens on the audio thread. Levels and
// parameters are atomics. A module that changes type is created and prepared on the message thread,
// handed over through an atomic pointer, and the replaced module travels back through a small
// lock-free queue to be deleted on the message thread (collectGarbage) - the audio thread never
// allocates or frees.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "FxModule.h"
#include "FxDsp.h"
#include <array>
#include <atomic>
#include <vector>

namespace ferrum
{
struct FxSettings;
}

namespace ferrum::fx
{
constexpr int kBuses = 3, kLanes = 3, kSlots = 5;
constexpr int kMasterBus = 2;
const char* busName (int bus);

struct RackState // plain copy for files, undo and the offline renderer
{
    struct Slot { Type type = Type::None; float p[kMaxParams] {}; bool bypass = false; };
    struct Lane { std::array<Slot, kSlots> slots {}; float level = 1.f; bool mute = false; };
    struct Bus { std::array<Lane, kLanes> lanes {}; float dry = 0.f; float outDb = 0.f; int output = 0; };
    std::array<Bus, kBuses> buses {};
};

class FxRack
{
public:
    FxRack();
    ~FxRack();

    // message thread (or with the audio stopped)
    void prepare (double sampleRate, int maxBlock);
    void setModule (int bus, int lane, int slot, Type t);   // keeps parameters at the module's defaults
    void setParam (int bus, int lane, int slot, int idx, float v) { at (bus, lane, slot).params[idx].store (v); }
    void setBypass (int bus, int lane, int slot, bool b) { at (bus, lane, slot).bypass.store (b); }
    void setLaneLevel (int bus, int lane, float l) { buses[(size_t) bus].lanes[(size_t) lane].level.store (l); }
    void setLaneMute (int bus, int lane, bool m) { buses[(size_t) bus].lanes[(size_t) lane].mute.store (m); }
    void setDry (int bus, float d) { buses[(size_t) bus].dry.store (d); }
    void setOutDb (int bus, float db) { buses[(size_t) bus].outDb.store (db); }
    void setOutput (int bus, int o) { buses[(size_t) bus].output.store (o); }
    void moveModule (int bus, int lane, int fromSlot, int toSlot);   // reorders a lane, keeping parameters
    void clearBus (int bus);
    void collectGarbage();

    Type moduleType (int bus, int lane, int slot) const { return at (bus, lane, slot).uiType; }
    float param (int bus, int lane, int slot, int idx) const { return at (bus, lane, slot).params[idx].load(); }
    bool bypassed (int bus, int lane, int slot) const { return at (bus, lane, slot).bypass.load(); }
    float laneLevel (int bus, int lane) const { return buses[(size_t) bus].lanes[(size_t) lane].level.load(); }
    bool laneMuted (int bus, int lane) const { return buses[(size_t) bus].lanes[(size_t) lane].mute.load(); }
    float dry (int bus) const { return buses[(size_t) bus].dry.load(); }
    float outDb (int bus) const { return buses[(size_t) bus].outDb.load(); }
    int output (int bus) const { return buses[(size_t) bus].output.load(); }
    bool hasModules (int bus) const;
    int moduleCount (int bus) const;

    RackState getState() const;
    void setState (const RackState& s);
    static RackState fromLegacy (const FxSettings& s, int output); // the old single LXR FX bus
    static float laneLevelToDb (float l) { return l <= 0.0001f ? -100.f : 20.f * std::log10 (l); }

    // audio thread
    void setTempo (double bpm);
    void process (int bus, float* L, float* R, int n);       // in place
    float meter (int bus) const { return buses[(size_t) bus].meter.load(); }

    void reset(); // clears tails (audio stopped)

private:
    struct Slot
    {
        std::atomic<float> params[kMaxParams];
        std::atomic<bool> bypass { false };
        std::atomic<Module*> pending { nullptr };   // message -> audio
        Module* active = nullptr;                  // audio thread only
        Type uiType = Type::None;                  // message thread mirror
    };
    struct Lane
    {
        std::array<Slot, kSlots> slots;
        std::atomic<float> level { 1.f };
        std::atomic<bool> mute { false };
        float smoothLevel = 1.f;
    };
    struct Bus
    {
        std::array<Lane, kLanes> lanes;
        std::atomic<float> dry { 0.f }, outDb { 0.f }, meter { 0.f };
        std::atomic<int> output { 0 };
        float smoothDry = 0.f, smoothOut = 1.f;
        dsp::DcBlocker dc[2];
    };
    Slot& at (int b, int l, int s) { return buses[(size_t) b].lanes[(size_t) l].slots[(size_t) s]; }
    const Slot& at (int b, int l, int s) const { return buses[(size_t) b].lanes[(size_t) l].slots[(size_t) s]; }
    void hand (Slot& s, Module* m);           // message thread: queue m (nullptr = empty)
    void adoptPending (Slot& s);              // audio thread
    void trash (Module* m);                   // audio thread

    std::array<Bus, kBuses> buses;
    double sr = 44100.0, bpm = 120.0;
    int maxBlock = 512;
    std::vector<float> inL, inR, laneL, laneR, accL, accR;

    // audio -> message garbage queue (single producer, single consumer)
    static constexpr int kTrash = 512;
    std::array<std::atomic<Module*>, kTrash> trashRing {};
    std::atomic<int> trashW { 0 }, trashR { 0 };
    static Module* emptySentinel();
};
} // namespace ferrum::fx
