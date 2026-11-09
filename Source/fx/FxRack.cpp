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
    for (int sc = 0; sc < kScenes; ++sc)
    {
        sceneUsed[sc].store (false);
        for (int b = 0; b < kBuses; ++b)
            for (int l = 0; l < kLanes; ++l)
                for (int s = 0; s < kSlots; ++s)
                {
                    sceneType[sc][b][l][s].store ((int) Type::None);
                    sceneBypass[sc][b][l][s].store (false);
                    for (int i = 0; i < kMaxParams; ++i) sceneP[sc][b][l][s][i].store (0.f);
                }
    }
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
    if (Module* pm = pendingMorph.exchange (nullptr)) delete pm;
    delete morph;
    morph = nullptr;
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
    for (auto* v : { &inL, &inR, &laneL, &laneR, &accL, &accR, &dryL, &dryR, &tmpL, &tmpR, &mixL, &mixR }) v->assign ((size_t) maxBlock, 0.f);
    adoptMorph(); // the audio thread is not running while we are being prepared
    if (morph != nullptr)
        for (auto& bb : morph->slots) for (auto& ll : bb) for (auto& ss : ll)
            for (auto* m : ss.inst) if (m != nullptr) { m->prepare (sr); m->setTempo (bpm); }
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
    if (morph != nullptr)
        for (auto& bb : morph->slots) for (auto& ll : bb) for (auto& ss : ll) for (auto* m : ss.inst) if (m != nullptr) m->reset();
}

//==============================================================================================
// FX Morph
std::array<float, FxRack::kScenes> FxRack::morphWeights (float x, float y, const std::array<bool, kScenes>& used)
{
    x = std::clamp (x, 0.f, 1.f); y = std::clamp (y, 0.f, 1.f);
    std::array<float, kScenes> w { (1.f - x) * (1.f - y), x * (1.f - y), (1.f - x) * y, x * y };
    float sum = 0.f;
    for (int i = 0; i < kScenes; ++i) { if (! used[(size_t) i]) w[(size_t) i] = 0.f; sum += w[(size_t) i]; }
    if (sum <= 1e-9f)
    {
        // the puck sits on empty corners only: the nearest used scene plays
        int best = -1; float bd = 1e9f;
        const float cx[kScenes] = { 0.f, 1.f, 0.f, 1.f }, cy[kScenes] = { 0.f, 0.f, 1.f, 1.f };
        for (int i = 0; i < kScenes; ++i)
            if (used[(size_t) i]) { const float d = (x - cx[i]) * (x - cx[i]) + (y - cy[i]) * (y - cy[i]); if (d < bd) { bd = d; best = i; } }
        w = {};
        if (best >= 0) w[(size_t) best] = 1.f;
        return w;
    }
    for (auto& v : w) v /= sum;
    return w;
}

void FxRack::setMorphScenes (const std::array<bool, kScenes>& used, const std::array<RackState, kScenes>& scenes)
{
    for (int sc = 0; sc < kScenes; ++sc) sceneUsed[sc].store (used[(size_t) sc]);
    // values first (the audio thread reads them every block)
    for (int sc = 0; sc < kScenes; ++sc)
        for (int b = 0; b < kBuses; ++b)
            for (int l = 0; l < kLanes; ++l)
                for (int s = 0; s < kSlots; ++s)
                {
                    const auto& ss = scenes[(size_t) sc].buses[(size_t) b].lanes[(size_t) l].slots[(size_t) s];
                    const Type t = used[(size_t) sc] ? ss.type : Type::None;
                    sceneType[sc][b][l][s].store ((int) t);
                    sceneBypass[sc][b][l][s].store (ss.bypass);
                    for (int i = 0; i < kMaxParams; ++i) sceneP[sc][b][l][s][i].store (ss.p[i]);
                }
    // the module types each slot needs; build a new instance set only when they changed
    bool changed = false;
    std::array<std::array<std::array<std::array<Type, kScenes>, kSlots>, kLanes>, kBuses> want {};
    std::array<std::array<std::array<int, kSlots>, kLanes>, kBuses> wantCount {};
    for (int b = 0; b < kBuses; ++b)
        for (int l = 0; l < kLanes; ++l)
            for (int s = 0; s < kSlots; ++s)
            {
                auto& types = want[(size_t) b][(size_t) l][(size_t) s];
                int& n = wantCount[(size_t) b][(size_t) l][(size_t) s];
                // keep the published order so instances can be reused in place
                const auto& pub = pubTypes[(size_t) b][(size_t) l][(size_t) s];
                const int pubN = pubCount[(size_t) b][(size_t) l][(size_t) s];
                auto needed = [&] (Type t)
                {
                    for (int sc = 0; sc < kScenes; ++sc)
                        if (used[(size_t) sc] && scenes[(size_t) sc].buses[(size_t) b].lanes[(size_t) l].slots[(size_t) s].type == t) return true;
                    return false;
                };
                for (int j = 0; j < pubN; ++j) if (needed (pub[(size_t) j])) types[(size_t) n++] = pub[(size_t) j];
                for (int sc = 0; sc < kScenes; ++sc)
                {
                    if (! used[(size_t) sc]) continue;
                    const Type t = scenes[(size_t) sc].buses[(size_t) b].lanes[(size_t) l].slots[(size_t) s].type;
                    if (t == Type::None) continue;
                    bool have = false;
                    for (int j = 0; j < n; ++j) have |= types[(size_t) j] == t;
                    if (! have) types[(size_t) n++] = t;
                }
                if (n != pubN) changed = true;
                else for (int j = 0; j < n; ++j) changed |= types[(size_t) j] != pub[(size_t) j];
            }
    if (! changed) return;
    auto* set = new MorphSet();
    for (int b = 0; b < kBuses; ++b)
        for (int l = 0; l < kLanes; ++l)
            for (int s = 0; s < kSlots; ++s)
            {
                auto& ss = set->slots[(size_t) b][(size_t) l][(size_t) s];
                const auto& types = want[(size_t) b][(size_t) l][(size_t) s];
                const auto& pub = pubTypes[(size_t) b][(size_t) l][(size_t) s];
                const int pubN = pubCount[(size_t) b][(size_t) l][(size_t) s];
                ss.count = wantCount[(size_t) b][(size_t) l][(size_t) s];
                for (int j = 0; j < ss.count; ++j)
                {
                    ss.type[(size_t) j] = types[(size_t) j];
                    for (int k = 0; k < pubN; ++k) if (pub[(size_t) k] == types[(size_t) j]) ss.reuse[(size_t) j] = k;
                    if (ss.reuse[(size_t) j] < 0)
                        if (auto u = create (types[(size_t) j])) { u->prepare (sr); u->setTempo (bpm); ss.inst[(size_t) j] = u.release(); }
                }
            }
    pubTypes = want;
    pubCount = wantCount;
    // take back a set the audio thread has not adopted yet, so the new set refers to the one it holds
    if (Module* prev = pendingMorph.exchange (nullptr))
    {
        // the audio thread never took the previous set: its reused instances still live in the
        // current one, its new ones go with it
        auto* p = static_cast<MorphSet*> (prev);
        for (int b = 0; b < kBuses; ++b) for (int l = 0; l < kLanes; ++l) for (int s = 0; s < kSlots; ++s)
        {
            auto& ps = p->slots[(size_t) b][(size_t) l][(size_t) s];
            for (int j = 0; j < kScenes; ++j) if (ps.reuse[(size_t) j] >= 0) ps.inst[(size_t) j] = nullptr;
            // the new set's reuse indices refer to 'p's order: re-point them at instances of the set the audio thread holds
            auto& ns = set->slots[(size_t) b][(size_t) l][(size_t) s];
            for (int j = 0; j < ns.count; ++j)
            {
                const int r = ns.reuse[(size_t) j];
                if (r < 0) continue;
                if (ps.inst[(size_t) r] != nullptr) { ns.inst[(size_t) j] = ps.inst[(size_t) r]; ps.inst[(size_t) r] = nullptr; ns.reuse[(size_t) j] = -1; }
                else ns.reuse[(size_t) j] = ps.reuse[(size_t) r] >= 0 ? ps.reuse[(size_t) r] : -1;
            }
        }
        delete p;
    }
    pendingMorph.store (set);
    collectGarbage();
}

void FxRack::adoptMorph()
{
    Module* p = pendingMorph.exchange (nullptr);
    if (p == nullptr) return;
    auto* set = static_cast<MorphSet*> (p);
    if (morph != nullptr)
        for (int b = 0; b < kBuses; ++b) for (int l = 0; l < kLanes; ++l) for (int s = 0; s < kSlots; ++s)
        {
            auto& ns = set->slots[(size_t) b][(size_t) l][(size_t) s];
            auto& os = morph->slots[(size_t) b][(size_t) l][(size_t) s];
            for (int j = 0; j < ns.count; ++j)
            {
                const int r = ns.reuse[(size_t) j];
                if (r < 0 || r >= os.count) continue;
                ns.inst[(size_t) j] = os.inst[(size_t) r];
                ns.smooth[(size_t) j] = os.smooth[(size_t) r];
                os.inst[(size_t) r] = nullptr;
            }
        }
    MorphSet* old = morph;
    morph = set;
    if (old != nullptr) trash (old);
}

void FxRack::processMorphSlot (MorphSet::SlotSet& ms, int bus, int lane, int slot, const std::array<float, kScenes>& w,
                               float* L, float* R, int n)
{
    // each instance: its presence (weight of the scenes that hold its type, unbypassed) and its
    // settings (weighted mean over those scenes; a choice comes from the heaviest one)
    float target[kScenes] {};
    bool anyOn = false;
    for (int j = 0; j < ms.count; ++j)
    {
        const Type t = ms.type[(size_t) j];
        float wt = 0.f;
        for (int sc = 0; sc < kScenes; ++sc)
            if (w[(size_t) sc] > 0.f && sceneType[sc][bus][lane][slot].load (std::memory_order_relaxed) == (int) t
                && ! sceneBypass[sc][bus][lane][slot].load (std::memory_order_relaxed))
                wt += w[(size_t) sc];
        target[j] = std::clamp (wt, 0.f, 1.f);
        anyOn |= target[j] > 0.f || ms.smooth[(size_t) j] > 0.f;
    }
    if (! anyOn) return;
    std::memcpy (dryL.data(), L, sizeof (float) * (size_t) n);
    std::memcpy (dryR.data(), R, sizeof (float) * (size_t) n);
    std::memcpy (mixL.data(), L, sizeof (float) * (size_t) n);
    std::memcpy (mixR.data(), R, sizeof (float) * (size_t) n);
    for (int j = 0; j < ms.count; ++j)
    {
        Module* m = ms.inst[(size_t) j];
        const float g0 = ms.smooth[(size_t) j], g1 = target[j];
        if (m == nullptr) { ms.smooth[(size_t) j] = g1; continue; }
        if (g0 <= 0.f && g1 <= 0.f) continue;
        if (g0 <= 0.f) m->reset();          // coming back in: no stale tail
        const Type t = ms.type[(size_t) j];
        const auto& in = info (t);
        float p[kMaxParams] {};
        float wt = 0.f, best = -1.f;
        int heavy = -1;
        for (int sc = 0; sc < kScenes; ++sc)
        {
            if (w[(size_t) sc] <= 0.f || sceneType[sc][bus][lane][slot].load (std::memory_order_relaxed) != (int) t) continue;
            for (int i = 0; i < kMaxParams; ++i) p[i] += w[(size_t) sc] * sceneP[sc][bus][lane][slot][i].load (std::memory_order_relaxed);
            wt += w[(size_t) sc];
            if (w[(size_t) sc] > best) { best = w[(size_t) sc]; heavy = sc; }
        }
        if (wt <= 0.f) { ms.smooth[(size_t) j] = g1; continue; }
        for (int i = 0; i < kMaxParams; ++i)
        {
            p[i] /= wt;
            if (i < (int) in.params.size() && in.params[(size_t) i].scale == Scale::Choice)
                p[i] = sceneP[heavy][bus][lane][slot][i].load (std::memory_order_relaxed);
        }
        if (modUsed)
        {
            const auto& mo = modOff[(size_t) bus][(size_t) lane][(size_t) slot];
            for (int i = 0; i < kMaxParams; ++i) if (mo[(size_t) i] != 0.f) p[i] = std::clamp (p[i] + mo[(size_t) i], 0.f, 1.f);
        }
        std::memcpy (tmpL.data(), dryL.data(), sizeof (float) * (size_t) n);
        std::memcpy (tmpR.data(), dryR.data(), sizeof (float) * (size_t) n);
        m->setTempo (bpm);
        m->process (tmpL.data(), tmpR.data(), n, p);
        for (int i = 0; i < n; ++i)
        {
            const float g = g0 + (g1 - g0) * (float) (i + 1) / (float) n;
            mixL[(size_t) i] += g * (tmpL[(size_t) i] - dryL[(size_t) i]);
            mixR[(size_t) i] += g * (tmpR[(size_t) i] - dryR[(size_t) i]);
        }
        ms.smooth[(size_t) j] = g1;
    }
    std::memcpy (L, mixL.data(), sizeof (float) * (size_t) n);
    std::memcpy (R, mixR.data(), sizeof (float) * (size_t) n);
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
    adoptMorph();
    const bool morphing = morph != nullptr && morphOn.load (std::memory_order_relaxed);
    std::array<float, kScenes> w {};
    if (morphing)
    {
        std::array<bool, kScenes> used {};
        for (int i = 0; i < kScenes; ++i) used[(size_t) i] = sceneUsed[i].load (std::memory_order_relaxed);
        w = morphWeights (morphX.load (std::memory_order_relaxed), morphY.load (std::memory_order_relaxed), used);
    }
    bool laneOn[kLanes] {};
    bool any = false;
    for (int l = 0; l < kLanes; ++l)
        for (int si = 0; si < kSlots; ++si)
        {
            auto& s = b.lanes[(size_t) l].slots[(size_t) si];
            adoptPending (s);
            if (morphing ? morph->slots[(size_t) bus][(size_t) l][(size_t) si].count > 0 : s.active != nullptr) { laneOn[l] = true; any = true; }
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
            if (morphing)
            {
                for (int si = 0; si < kSlots; ++si)
                    processMorphSlot (morph->slots[(size_t) bus][(size_t) l][(size_t) si], bus, l, si, w, laneL.data(), laneR.data(), m);
            }
            else for (auto& s : lane.slots)
            {
                if (s.active == nullptr || s.bypass.load()) continue;
                float p[kMaxParams];
                for (int i = 0; i < kMaxParams; ++i) p[i] = s.params[i].load (std::memory_order_relaxed);
                if (modUsed)
                {
                    const auto& mo = modOff[(size_t) bus][(size_t) l][(size_t) (&s - lane.slots.data())];
                    for (int i = 0; i < kMaxParams; ++i) if (mo[(size_t) i] != 0.f) p[i] = std::clamp (p[i] + mo[(size_t) i], 0.f, 1.f);
                }
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
