// FxRack.cpp - see FxRack.h
// SPDX-License-Identifier: GPL-3.0-only
#include "FxRack.h"
#include "../dsp/FxBus.h"
#include <cstring>

namespace ferrum::fx
{
const char* busName (int bus)
{
    static const char* n[kBuses] = { "FX 1", "FX 2", "Master" };
    return n[std::clamp (bus, 0, kBuses - 1)];
}

namespace
{
struct EmptyModule : Module
{
    void prepare (double) override {}
    void reset() override {}
    void process (float*, float*, int, const float*) override {}
};
}

Module* FxRack::emptySentinel()
{
    static EmptyModule e;
    return &e;
}

FxRack::FxRack()
{
    for (auto& b : buses)
        for (auto& l : b.lanes)
            for (auto& s : l.slots)
                for (auto& p : s.params) p.store (0.f);
}

FxRack::~FxRack()
{
    for (auto& b : buses)
        for (auto& l : b.lanes)
            for (auto& s : l.slots)
            {
                Module* p = s.pending.exchange (nullptr);
                if (p != nullptr && p != emptySentinel()) delete p;
                delete s.active;
                s.active = nullptr;
            }
    collectGarbage();
}

void FxRack::hand (Slot& s, Module* m)
{
    Module* prev = s.pending.exchange (m != nullptr ? m : emptySentinel());
    if (prev != nullptr && prev != emptySentinel()) delete prev; // the audio thread never saw it
}

void FxRack::adoptPending (Slot& s)
{
    Module* p = s.pending.exchange (nullptr);
    if (p == nullptr) return;
    Module* old = s.active;
    s.active = p == emptySentinel() ? nullptr : p;
    if (old != nullptr) trash (old);
}

void FxRack::trash (Module* m)
{
    const int w = trashW.load();
    const int next = (w + 1) % kTrash;
    if (next == trashR.load()) return; // full: leak rather than free on the audio thread (never happens in practice)
    trashRing[(size_t) w].store (m);
    trashW.store (next);
}

void FxRack::collectGarbage()
{
    int r = trashR.load();
    while (r != trashW.load())
    {
        delete trashRing[(size_t) r].exchange (nullptr);
        r = (r + 1) % kTrash;
        trashR.store (r);
    }
}

void FxRack::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = std::max (block, 64);
    for (auto* v : { &inL, &inR, &laneL, &laneR, &accL, &accR }) v->assign ((size_t) maxBlock, 0.f);
    for (auto& b : buses)
    {
        for (auto& d : b.dc) { d.set (sr, 6.0); d.reset(); }
        for (auto& l : b.lanes)
            for (auto& s : l.slots)
            {
                adoptPending (s); // the audio thread is not running while we are being prepared
                if (s.active != nullptr) { s.active->prepare (sr); s.active->setTempo (bpm); }
            }
    }
    collectGarbage();
}

void FxRack::reset()
{
    for (auto& b : buses)
    {
        for (auto& d : b.dc) d.reset();
        for (auto& l : b.lanes) for (auto& s : l.slots) if (s.active != nullptr) s.active->reset();
    }
}

void FxRack::setModule (int bus, int lane, int slot, Type t)
{
    Slot& s = at (bus, lane, slot);
    s.uiType = t;
    const auto& in = info (t);
    for (int i = 0; i < kMaxParams; ++i) s.params[i].store (i < (int) in.params.size() ? in.params[(size_t) i].def : 0.f);
    s.bypass.store (false);
    Module* m = nullptr;
    if (t != Type::None)
        if (auto u = create (t)) { u->prepare (sr); u->setTempo (bpm); m = u.release(); }
    hand (s, m);
    collectGarbage();
}

void FxRack::moveModule (int bus, int lane, int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= kSlots || to >= kSlots) return;
    auto st = getState();
    auto& slots = st.buses[(size_t) bus].lanes[(size_t) lane].slots;
    const auto moving = slots[(size_t) from];
    if (from < to) for (int i = from; i < to; ++i) slots[(size_t) i] = slots[(size_t) i + 1];
    else for (int i = from; i > to; --i) slots[(size_t) i] = slots[(size_t) i - 1];
    slots[(size_t) to] = moving;
    setState (st);
}

void FxRack::clearBus (int bus)
{
    for (int l = 0; l < kLanes; ++l)
    {
        for (int s = 0; s < kSlots; ++s) if (at (bus, l, s).uiType != Type::None) setModule (bus, l, s, Type::None);
        setLaneLevel (bus, l, 1.f);
        setLaneMute (bus, l, false);
    }
    setDry (bus, 0.f);
    setOutDb (bus, 0.f);
}

bool FxRack::hasModules (int bus) const { return moduleCount (bus) > 0; }

int FxRack::moduleCount (int bus) const
{
    int n = 0;
    for (const auto& l : buses[(size_t) bus].lanes) for (const auto& s : l.slots) if (s.uiType != Type::None) ++n;
    return n;
}

RackState FxRack::getState() const
{
    RackState st;
    for (int b = 0; b < kBuses; ++b)
    {
        auto& sb = st.buses[(size_t) b];
        const auto& rb = buses[(size_t) b];
        sb.dry = rb.dry.load(); sb.outDb = rb.outDb.load(); sb.output = rb.output.load();
        for (int l = 0; l < kLanes; ++l)
        {
            auto& sl = sb.lanes[(size_t) l];
            sl.level = rb.lanes[(size_t) l].level.load(); sl.mute = rb.lanes[(size_t) l].mute.load();
            for (int s = 0; s < kSlots; ++s)
            {
                const auto& rs = rb.lanes[(size_t) l].slots[(size_t) s];
                auto& ss = sl.slots[(size_t) s];
                ss.type = rs.uiType; ss.bypass = rs.bypass.load();
                for (int i = 0; i < kMaxParams; ++i) ss.p[i] = rs.params[i].load();
            }
        }
    }
    return st;
}

void FxRack::setState (const RackState& st)
{
    for (int b = 0; b < kBuses; ++b)
    {
        const auto& sb = st.buses[(size_t) b];
        setDry (b, sb.dry); setOutDb (b, sb.outDb); setOutput (b, sb.output);
        for (int l = 0; l < kLanes; ++l)
        {
            const auto& sl = sb.lanes[(size_t) l];
            setLaneLevel (b, l, sl.level); setLaneMute (b, l, sl.mute);
            for (int s = 0; s < kSlots; ++s)
            {
                const auto& ss = sl.slots[(size_t) s];
                if (at (b, l, s).uiType != ss.type) setModule (b, l, s, ss.type); // same type: keep the instance (and its tail)
                for (int i = 0; i < kMaxParams; ++i) setParam (b, l, s, i, ss.p[i]);
                setBypass (b, l, s, ss.bypass);
            }
        }
    }
}

RackState FxRack::fromLegacy (const FxSettings& fs, int output)
{
    RackState st;
    auto& bus = st.buses[0];
    bus.output = output;
    if (fs.type == FxType::Off) return st;
    auto& slot = bus.lanes[0].slots[0];
    auto cn = [] (Type t, int param, int index) { return choiceNorm (info (t).params[(size_t) param], index); };
    switch (fs.type)
    {
        case FxType::Distortion:
            slot.type = Type::LxrDist;
            slot.p[0] = cn (Type::LxrDist, 0, (int) fs.distType);
            for (int i = 0; i < 4; ++i) slot.p[i + 1] = fs.p[i];
            slot.p[5] = fs.dryWet;
            break;
        case FxType::RingMod:
            slot.type = Type::LxrRing;
            slot.p[0] = fs.p[0]; slot.p[1] = cn (Type::LxrRing, 1, fs.ringWave); slot.p[2] = fs.dryWet;
            break;
        case FxType::Compressor:
            slot.type = Type::LxrComp;
            slot.p[0] = fs.p[0]; slot.p[1] = fs.p[1]; slot.p[2] = fs.p[2]; slot.p[3] = cn (Type::LxrComp, 3, fs.ratio); slot.p[4] = fs.p[3]; slot.p[5] = fs.dryWet;
            break;
        case FxType::Delay:
        default:
            slot.type = Type::LxrDelay;
            for (int i = 0; i < 4; ++i) slot.p[i] = fs.p[i];
            slot.p[4] = cn (Type::LxrDelay, 4, fs.pingPong ? 1 : 0); slot.p[5] = fs.dryWet;
            break;
    }
    return st;
}

void FxRack::setTempo (double b) { bpm = b > 0 ? b : 120.0; }

void FxRack::process (int bus, float* L, float* R, int n)
{
    Bus& b = buses[(size_t) bus];
    bool laneOn[kLanes] {};
    bool any = false;
    for (int l = 0; l < kLanes; ++l)
        for (auto& s : b.lanes[(size_t) l].slots)
        {
            adoptPending (s);
            if (s.active != nullptr) { laneOn[l] = true; any = true; }
        }
    if (! any) // an empty bus passes its input through untouched
    {
        float pk = 0;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::fabs (L[i]), std::fabs (R[i])));
        b.meter.store (pk);
        return;
    }
    float peak = 0;
    for (int done = 0; done < n;)
    {
        const int m = std::min (maxBlock, n - done);
        float* xl = L + done;
        float* xr = R + done;
        std::memcpy (inL.data(), xl, sizeof (float) * (size_t) m);
        std::memcpy (inR.data(), xr, sizeof (float) * (size_t) m);
        // dry part
        const float dryT = b.dry.load(), dry0 = b.smoothDry;
        for (int i = 0; i < m; ++i)
        {
            const float g = dry0 + (dryT - dry0) * (float) (i + 1) / (float) m;
            accL[(size_t) i] = inL[(size_t) i] * g;
            accR[(size_t) i] = inR[(size_t) i] * g;
        }
        b.smoothDry = dryT;
        // lanes in parallel, slots in series
        for (int l = 0; l < kLanes; ++l)
        {
            if (! laneOn[l]) continue;
            Lane& lane = b.lanes[(size_t) l];
            std::memcpy (laneL.data(), inL.data(), sizeof (float) * (size_t) m);
            std::memcpy (laneR.data(), inR.data(), sizeof (float) * (size_t) m);
            for (auto& s : lane.slots)
            {
                if (s.active == nullptr || s.bypass.load()) continue;
                float p[kMaxParams];
                for (int i = 0; i < kMaxParams; ++i) p[i] = s.params[i].load (std::memory_order_relaxed);
                s.active->setTempo (bpm);
                s.active->process (laneL.data(), laneR.data(), m, p);
            }
            const float lt = lane.mute.load() ? 0.f : lane.level.load(), l0 = lane.smoothLevel;
            for (int i = 0; i < m; ++i)
            {
                const float g = l0 + (lt - l0) * (float) (i + 1) / (float) m;
                accL[(size_t) i] += laneL[(size_t) i] * g;
                accR[(size_t) i] += laneR[(size_t) i] * g;
            }
            lane.smoothLevel = lt;
        }
        const float outT = dsp::dbToGain (b.outDb.load()), out0 = b.smoothOut;
        for (int i = 0; i < m; ++i)
        {
            const float g = out0 + (outT - out0) * (float) (i + 1) / (float) m;
            const float yl = b.dc[0].process (accL[(size_t) i] * g), yr = b.dc[1].process (accR[(size_t) i] * g);
            xl[i] = yl; xr[i] = yr;
            peak = std::max (peak, std::max (std::fabs (yl), std::fabs (yr)));
        }
        b.smoothOut = outT;
        done += m;
    }
    b.meter.store (peak);
}
} // namespace ferrum::fx
