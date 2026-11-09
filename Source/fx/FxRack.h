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
// where an FX bus goes (RackState::Bus::output): main A, output B, the bus's own plugin output,
// or (FX 1 only) into FX 2, so the two buses run in series
constexpr int kToA = 0, kToB = 1, kToOwn = 2, kToFx2 = 3;
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
    // modulation offsets (audio thread): added to the parameters for the next process() calls
    void clearModulation() { if (modUsed) { for (auto& b : modOff) for (auto& l : b) for (auto& s : l) s.fill (0.f); modUsed = false; } }
    void addModulation (int bus, int lane, int slot, int idx, float v)
    {
        if (bus < 0 || bus >= kBuses || lane < 0 || lane >= kLanes || slot < 0 || slot >= kSlots || idx < 0 || idx >= kMaxParams) return;
        modOff[(size_t) bus][(size_t) lane][(size_t) slot][(size_t) idx] += v;
        modUsed = true;
    }

    void reset(); // clears tails (audio stopped)

    // ---- FX Morph ------------------------------------------------------------------------------
    // Four scenes of module settings in the corners of an XY pad: 0 top left, 1 top right, 2 bottom
    // left, 3 bottom right. While Morph is on, the rack plays its morph instances - one per module
    // type a slot holds in any scene - instead of its own modules: settings blend by the XY weights
    // (bilinear), and each module fades in and out with the share of the weight of the scenes that
    // hold it (unbypassed), so effects come and go between scenes. Dry, Out, lane levels and each
    // bus's output stay the rack's own. Everything is blended on the audio thread, so automation and
    // offline renders follow it exactly.
    static constexpr int kScenes = 4;
    // message thread: new scenes (instances are created here; unchanged module types keep theirs)
    void setMorphScenes (const std::array<bool, kScenes>& used, const std::array<RackState, kScenes>& scenes);
    void setMorphOn (bool on) { morphOn.store (on); }
    void setMorphXY (float x, float y) { morphX.store (x); morphY.store (y); }
    bool isMorphOn() const { return morphOn.load(); }
    static std::array<float, kScenes> morphWeights (float x, float y, const std::array<bool, kScenes>& used);

private:
    // the morph instances of every slot, handed to the audio thread as one object (a Module only so
    // the garbage queue can delete it on the message thread)
    struct MorphSet : Module
    {
        struct SlotSet
        {
            std::array<Module*, kScenes> inst {};
            std::array<Type, kScenes> type {};
            std::array<int, kScenes> reuse { -1, -1, -1, -1 };   // take instance j of the previous set
            std::array<float, kScenes> smooth {};                // presence, audio thread
            int count = 0;
        };
        std::array<std::array<std::array<SlotSet, kSlots>, kLanes>, kBuses> slots {};
        ~MorphSet() override { for (auto& b : slots) for (auto& l : b) for (auto& s : l) for (auto* m : s.inst) delete m; }
        void prepare (double) override {}
        void reset() override {}
        void process (float*, float*, int, const float*) override {}
    };
    void adoptMorph();                                           // audio thread
    void processMorphSlot (MorphSet::SlotSet& ms, int bus, int lane, int slot, const std::array<float, kScenes>& w,
                           float* L, float* R, int n);           // audio thread
    std::atomic<Module*> pendingMorph { nullptr };
    MorphSet* morph = nullptr;                                   // audio thread
    std::array<std::array<std::array<std::array<Type, kScenes>, kSlots>, kLanes>, kBuses> pubTypes {}; // message thread mirror
    std::array<std::array<std::array<int, kSlots>, kLanes>, kBuses> pubCount {};
    std::atomic<float> sceneP[kScenes][kBuses][kLanes][kSlots][kMaxParams];
    std::atomic<int> sceneType[kScenes][kBuses][kLanes][kSlots];
    std::atomic<bool> sceneBypass[kScenes][kBuses][kLanes][kSlots];
    std::atomic<bool> sceneUsed[kScenes];
    std::atomic<bool> morphOn { false };
    std::atomic<float> morphX { 0.f }, morphY { 0.f };
    std::vector<float> dryL, dryR, tmpL, tmpR, mixL, mixR;

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
    std::array<std::array<std::array<std::array<float, kMaxParams>, kSlots>, kLanes>, kBuses> modOff {};
    bool modUsed = false;
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
