// FxRackPanel.cpp - see FxRackPanel.h
// SPDX-License-Identifier: GPL-3.0-only
#include "FxRackPanel.h"

namespace ui
{
using namespace juce;
using namespace ferrum;
namespace fxr = ferrum::fx;

static constexpr int kSlotW = 176, kSlotGap = 24, kSlotH = 84, kLaneH = 90, kLaneGap = 8, kGridTop = 106;
static constexpr int kInX = 166, kSlotX0 = 182;

// lane level: knob 0..1 <-> -inf .. +6 dB
static float laneNormToGain (float n) { return n <= 0.001f ? 0.f : Decibels::decibelsToGain (-40.f + 46.f * n); }
static float laneGainToNorm (float g) { return g <= 0.00001f ? 0.f : jlimit (0.f, 1.f, (Decibels::gainToDecibels (g) + 40.f) / 46.f); }
static String laneText (float n) { return n <= 0.001f ? String ("-inf dB") : String (-40.f + 46.f * n, 1) + " dB"; }

Colour FxRackPanel::categoryColour (const char* c)
{
    const String s (c);
    if (s == "Drive") return Colour (0xffff7a1a);
    if (s == "Tone") return Colour (0xff4ea3ff);
    if (s == "Dynamics") return Colour (0xff44d39a);
    if (s == "Space") return Colour (0xffb07cff);
    if (s == "Modulation") return Colour (0xffff5fa2);
    if (s == "LXR-02") return Colour (0xffffd23f);
    return Colour (0xff9aa3ad);
}

FxRackPanel::FxRackPanel (Ferrum8Processor& p) : proc (p), rack (p.getRack())
{
    for (auto* c : std::initializer_list<Component*> { &dryKnob, &outKnob, &outputSel, &chainsBtn, &diceBtn, &bypassBtn, &leftBtn, &rightBtn,
                                                       &resetBtn, &removeBtn, &swapBtn })
        addAndMakeVisible (c);
    for (auto& k : laneKnobs) addAndMakeVisible (k);
    for (auto& k : pKnobs) addChildComponent (k);
    for (auto& c : pChoices) addChildComponent (c);

    auto undo = [this] { proc.pushUndo(); };
    dryKnob.setLabel ("Dry");
    dryKnob.setAccent (col::text);
    dryKnob.onGestureStart = undo;
    dryKnob.setTooltip ("Dry: how much of the unprocessed bus input is added to the lanes (0% = the bus is a pure insert)");
    dryKnob.bindFree ({ "Dry", 0.f, [this] { return rack.dry (bus); }, [this] (float v) { rack.setDry (bus, v); edited (false); },
                        [] (float v) { return String (roundToInt (v * 100.f)) + "%"; },
                        [] (const String& t, float& n) { n = jlimit (0.f, 1.f, t.getFloatValue() / 100.f); return true; } });
    outKnob.setLabel ("Output");
    outKnob.setAccent (col::text);
    outKnob.setBipolar (true);
    outKnob.onGestureStart = undo;
    outKnob.setTooltip ("Output gain of the bus");
    outKnob.bindFree ({ "Output", 24.f / 36.f, [this] { return (rack.outDb (bus) + 24.f) / 36.f; },
                        [this] (float v) { rack.setOutDb (bus, -24.f + 36.f * v); edited (false); },
                        [] (float v) { return String (-24.f + 36.f * v, 1) + " dB"; },
                        [] (const String& t, float& n) { n = jlimit (0.f, 1.f, (t.getFloatValue() + 24.f) / 36.f); return true; } });
    outputSel.setLabel ("TO OUTPUT");
    outputSel.bindFree ({ "A", "B" }, 0);
    outputSel.onGestureStart = undo;
    outputSel.onUserChoice = [this] (int i) { rack.setOutput (bus, i); edited (true); };
    outputSel.setTooltip ("Where the bus goes: plugin output A (main) or B (second stereo output)");
    for (int l = 0; l < fxr::kLanes; ++l)
    {
        auto& k = laneKnobs[(size_t) l];
        k.setLabel ("Level");
        k.setAccent (col::text);
        k.onGestureStart = undo;
        k.setTooltip ("Level of lane " + String (l + 1) + " in the bus mix");
        k.bindFree ({ "Lane " + String (l + 1) + " level", laneGainToNorm (1.f), [this, l] { return laneGainToNorm (rack.laneLevel (bus, l)); },
                      [this, l] (float v) { rack.setLaneLevel (bus, l, laneNormToGain (v)); edited (false); }, laneText,
                      [] (const String& t, float& n)
                      {
                          if (t.trim().startsWithIgnoreCase ("-inf")) { n = 0.f; return true; }
                          n = jlimit (0.f, 1.f, (t.getFloatValue() + 40.f) / 46.f);
                          return true;
                      } });
    }
    chainsBtn.setTooltip ("Ready-made chains for this bus (Brut Crunch, Amp + Cab, Dub Echo, Drum Bus ...), clear or copy the bus");
    diceBtn.setTooltip ("Roll a random chain for this bus: series drive and tone, or parallel dirt and space. Undo brings the last one back.");
    chainsBtn.onClick = [this] { showChainMenu(); };
    diceBtn.onClick = [this] { rollChain(); };
    diceBtn.setAccent (col::accent);

    bypassBtn.setClickingTogglesState (false);
    bypassBtn.onClick = [this]
    {
        if (selSlot < 0) return;
        proc.pushUndo();
        rack.setBypass (bus, selLane, selSlot, ! rack.bypassed (bus, selLane, selSlot));
        edited (false);
        syncEditor();
        repaint();
    };
    leftBtn.setTooltip ("Move the module one slot to the left (earlier in the chain)");
    rightBtn.setTooltip ("Move the module one slot to the right (later in the chain)");
    leftBtn.onClick = [this] { if (selSlot > 0) moveSlot (selLane, selSlot, selLane, selSlot - 1, false); };
    rightBtn.onClick = [this] { if (selSlot >= 0 && selSlot < fxr::kSlots - 1) moveSlot (selLane, selSlot, selLane, selSlot + 1, false); };
    resetBtn.setTooltip ("Set every parameter of this module back to its default");
    resetBtn.onClick = [this]
    {
        if (selSlot < 0) return;
        proc.pushUndo();
        const auto& in = fxr::info (rack.moduleType (bus, selLane, selSlot));
        for (size_t i = 0; i < in.params.size(); ++i) rack.setParam (bus, selLane, selSlot, (int) i, in.params[i].def);
        edited (false);
        syncEditor();
        repaint();
    };
    removeBtn.onClick = [this] { if (selSlot >= 0) setModule (selLane, selSlot, fxr::Type::None); };
    swapBtn.setTooltip ("Replace this module with another one");
    swapBtn.onClick = [this] { if (selSlot >= 0) pickModule (selLane, selSlot); };

    for (int i = 0; i < fxr::kMaxParams; ++i)
    {
        pKnobs[(size_t) i].onGestureStart = undo;
        pChoices[(size_t) i].onGestureStart = undo;
        pChoices[(size_t) i].onUserChoice = [this, i] (int idx)
        {
            if (selSlot < 0) return;
            const auto& in = fxr::info (rack.moduleType (bus, selLane, selSlot));
            if (i >= (int) in.params.size()) return;
            rack.setParam (bus, selLane, selSlot, i, fxr::choiceNorm (in.params[(size_t) i], idx));
            edited (false);
            repaint (grid);
        };
    }
    refresh();
}

//==============================================================================================
Rectangle<int> FxRackPanel::busTab (int b) const { return { 12 + b * 176, 10, 170, 66 }; }
Rectangle<int> FxRackPanel::sendChip (int ch) const { return { 560 + ch * 34, 42, 30, 28 }; }
Rectangle<int> FxRackPanel::laneArea (int l) const { return { 12, kGridTop + l * (kLaneH + kLaneGap), getWidth() - 24, kLaneH }; }
Rectangle<int> FxRackPanel::slotArea (int l, int s) const { return { kSlotX0 + s * (kSlotW + kSlotGap), laneArea (l).getY() + 4, kSlotW, kSlotH }; }
Rectangle<int> FxRackPanel::powerArea (int l, int s) const { auto r = slotArea (l, s); return { r.getRight() - 28, r.getY() + 6, 22, 22 }; }
Rectangle<int> FxRackPanel::muteArea (int l) const { auto r = laneArea (l); return { r.getX() + 8, r.getY() + 34, 34, 26 }; }
Point<float> FxRackPanel::sumNode() const
{
    const float top = (float) laneArea (0).getCentreY(), bottom = (float) laneArea (fxr::kLanes - 1).getCentreY();
    return { (float) getWidth() - 46.f, (top + bottom) * 0.5f };
}

FxRackPanel::Hit FxRackPanel::findHit (Point<int> p) const
{
    for (int b = 0; b < fxr::kBuses; ++b) if (busTab (b).contains (p)) return { Hit::BusTab, b };
    if (bus != fxr::kMasterBus)
        for (int ch = 0; ch < Ferrum8Processor::NC; ++ch) if (sendChip (ch).contains (p)) return { Hit::Send, ch };
    for (int l = 0; l < fxr::kLanes; ++l)
    {
        if (muteArea (l).contains (p)) return { Hit::LaneMute, l };
        for (int s = 0; s < fxr::kSlots; ++s)
        {
            if (rack.moduleType (bus, l, s) != fxr::Type::None && powerArea (l, s).contains (p)) return { Hit::Power, l, s };
            if (slotArea (l, s).contains (p)) return { Hit::Slot, l, s };
        }
    }
    return {};
}

//==============================================================================================
void FxRackPanel::showBus (int b)
{
    bus = jlimit (0, fxr::kBuses - 1, b);
    selLane = 0;
    selSlot = -1;
    for (int l = 0; l < fxr::kLanes && selSlot < 0; ++l)
        for (int s = 0; s < fxr::kSlots; ++s)
            if (rack.moduleType (bus, l, s) != fxr::Type::None) { selLane = l; selSlot = s; break; }
    refresh();
}

void FxRackPanel::select (int lane, int slot)
{
    selLane = lane;
    selSlot = slot;
    syncEditor();
    repaint();
}

void FxRackPanel::refresh()
{
    if (selSlot >= 0 && rack.moduleType (bus, selLane, selSlot) == fxr::Type::None) selSlot = -1;
    syncHeader();
    syncEditor();
    repaint();
}

void FxRackPanel::syncHeader()
{
    dryKnob.refresh();
    outKnob.refresh();
    for (auto& k : laneKnobs) k.refresh();
    outputSel.setIndex (rack.output (bus));
    outputSel.setVisible (bus != fxr::kMasterBus);
}

void FxRackPanel::syncEditor()
{
    const auto t = selSlot >= 0 ? rack.moduleType (bus, selLane, selSlot) : fxr::Type::None;
    const bool has = t != fxr::Type::None;
    for (auto* c : std::initializer_list<Component*> { &bypassBtn, &leftBtn, &rightBtn, &resetBtn, &removeBtn, &swapBtn }) c->setVisible (has);
    for (auto& k : pKnobs) k.setVisible (false);
    for (auto& c : pChoices) c.setVisible (false);
    if (! has) return;
    const auto& in = fxr::info (t);
    const auto cc = categoryColour (in.category);
    bypassBtn.setAccent (col::mute);
    bypassBtn.setToggleState (rack.bypassed (bus, selLane, selSlot), dontSendNotification);
    leftBtn.setEnabled (selSlot > 0);
    rightBtn.setEnabled (selSlot < fxr::kSlots - 1);
    for (int i = 0; i < (int) in.params.size() && i < fxr::kMaxParams; ++i)
    {
        const auto& ps = in.params[(size_t) i];
        if (ps.scale == fxr::Scale::Choice)
        {
            StringArray names;
            for (auto* n : ps.choices) names.add (n);
            auto& c = pChoices[(size_t) i];
            c.bindFree (ps.name, names, fxr::choiceIndex (ps, rack.param (bus, selLane, selSlot, i)));
            c.setLabel (ps.name);
            c.setAccent (cc);
            c.setVisible (true);
        }
        else
        {
            auto& k = pKnobs[(size_t) i];
            const int l = selLane, s = selSlot, b = bus;
            k.bindFree ({ ps.name, ps.def, [this, b, l, s, i] { return rack.param (b, l, s, i); },
                          [this, b, l, s, i] (float v) { rack.setParam (b, l, s, i, v); edited (false); repaint (grid); },
                          [ps] (float v) { return String (fxr::formatParam (ps, v)); },
                          [ps] (const String& text, float& n) { return fxr::parseParam (ps, text.toStdString(), n); } });
            k.setLabel (ps.name);
            k.setBipolar (ps.bipolar);
            k.setAccent (cc);
            k.setVisible (true);
        }
    }
    layoutEditor();
}

void FxRackPanel::edited (bool structural)
{
    proc.presetDirty = true;
    if (structural) proc.rackChanged();
}

//==============================================================================================
void FxRackPanel::setModule (int lane, int slot, fxr::Type t)
{
    proc.pushUndo();
    rack.setModule (bus, lane, slot, t);
    if (t != fxr::Type::None) select (lane, slot);
    else if (lane == selLane && slot == selSlot) selSlot = -1;
    if (showStatus)
        showStatus (t == fxr::Type::None ? String ("Removed the module from ") + fxr::busName (bus)
                                         : String (fxr::info (t).name) + " added to " + fxr::busName (bus) + ", lane " + String (lane + 1));
    edited (true);
    refresh();
}

void FxRackPanel::pickModule (int lane, int slot)
{
    PopupMenu m;
    const auto current = rack.moduleType (bus, lane, slot);
    static const char* cats[] = { "Drive", "Tone", "Dynamics", "Space", "Modulation", "Utility", "LXR-02" };
    for (auto* cat : cats)
    {
        m.addSectionHeader (cat);
        for (int t = 1; t < (int) fxr::Type::Count; ++t)
        {
            const auto& in = fxr::info ((fxr::Type) t);
            if (String (in.category) != cat) continue;
            PopupMenu::Item it (in.name);
            it.itemID = t;
            it.isTicked = (int) current == t;
            it.colour = categoryColour (in.category).interpolatedWith (col::text, 0.55f);
            m.addItem (it);
        }
    }
    if (current != fxr::Type::None) { m.addSeparator(); m.addItem (1000, "Remove"); }
    SafePointer<FxRackPanel> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (slotArea (lane, slot))).withMinimumWidth (180),
                     [safe, lane, slot, current] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        const auto t = r == 1000 ? fxr::Type::None : (fxr::Type) r;
        if (t != current) safe->setModule (lane, slot, t);
    });
}

void FxRackPanel::slotMenu (int lane, int slot)
{
    const auto t = rack.moduleType (bus, lane, slot);
    if (t == fxr::Type::None) { pickModule (lane, slot); return; }
    select (lane, slot);
    PopupMenu m, toLane, toBus;
    m.addSectionHeader (fxr::info (t).name);
    m.addItem (1, "Change Module...");
    m.addItem (2, rack.bypassed (bus, lane, slot) ? "Turn On" : "Bypass");
    m.addItem (3, "Move Left", slot > 0);
    m.addItem (4, "Move Right", slot < fxr::kSlots - 1);
    for (int l = 0; l < fxr::kLanes; ++l)
        if (l != lane) toLane.addItem (10 + l, "Lane " + String (l + 1) + " (same position, swaps)");
    m.addSubMenu ("Move To Lane", toLane);
    for (int b = 0; b < fxr::kBuses; ++b)
        if (b != bus) toBus.addItem (20 + b, String ("Copy to ") + fxr::busName (b) + ", first free slot");
    m.addSubMenu ("Copy To Bus", toBus);
    m.addItem (5, "Duplicate Into Next Slot", slot < fxr::kSlots - 1);
    m.addItem (6, "Reset Parameters");
    m.addSeparator();
    m.addItem (7, "Remove");
    SafePointer<FxRackPanel> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (slotArea (lane, slot))), [safe, lane, slot] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& self = *safe;
        switch (r)
        {
            case 1: self.pickModule (lane, slot); break;
            case 2: self.selLane = lane; self.selSlot = slot; self.bypassBtn.onClick(); break;
            case 3: self.moveSlot (lane, slot, lane, slot - 1, false); break;
            case 4: self.moveSlot (lane, slot, lane, slot + 1, false); break;
            case 5: self.moveSlot (lane, slot, lane, slot + 1, true); break;
            case 6: self.selLane = lane; self.selSlot = slot; self.resetBtn.onClick(); break;
            case 7: self.setModule (lane, slot, fxr::Type::None); break;
            default:
                if (r >= 10 && r < 10 + fxr::kLanes) self.moveSlot (lane, slot, r - 10, slot, false);
                else if (r >= 20 && r < 20 + fxr::kBuses)
                {
                    const int dest = r - 20;
                    auto st = self.rack.getState();
                    const auto moving = st.buses[(size_t) self.bus].lanes[(size_t) lane].slots[(size_t) slot];
                    bool placed = false;
                    for (auto& ln : st.buses[(size_t) dest].lanes)
                    {
                        for (auto& sl : ln.slots) if (sl.type == fxr::Type::None) { sl = moving; placed = true; break; }
                        if (placed) break;
                    }
                    if (! placed) { if (self.showStatus) self.showStatus (String (fxr::busName (dest)) + " is full"); return; }
                    self.proc.pushUndo();
                    self.rack.setState (st);
                    self.edited (true);
                    if (self.showStatus) self.showStatus (String (fxr::info (moving.type).name) + " copied to " + fxr::busName (dest));
                }
                break;
        }
    });
}

void FxRackPanel::moveSlot (int fromLane, int fromSlot, int toLane, int toSlot, bool copy)
{
    if (toSlot < 0 || toSlot >= fxr::kSlots || toLane < 0 || toLane >= fxr::kLanes) return;
    if (fromLane == toLane && fromSlot == toSlot) return;
    proc.pushUndo();
    if (! copy && fromLane == toLane) rack.moveModule (bus, fromLane, fromSlot, toSlot);
    else
    {
        auto st = rack.getState();
        auto& lanes = st.buses[(size_t) bus].lanes;
        auto& from = lanes[(size_t) fromLane].slots[(size_t) fromSlot];
        auto& to = lanes[(size_t) toLane].slots[(size_t) toSlot];
        if (copy) to = from; else std::swap (from, to);
        rack.setState (st);
    }
    selLane = toLane;
    selSlot = toSlot;
    edited (true);
    refresh();
}

//==============================================================================================
void FxRackPanel::applyBus (const fxr::RackState::Bus& b, const String& what)
{
    proc.pushUndo();
    auto st = rack.getState();
    auto nb = b;
    nb.output = bus == fxr::kMasterBus ? 0 : st.buses[(size_t) bus].output;
    st.buses[(size_t) bus] = nb;
    rack.setState (st);
    edited (true);
    showBus (bus);
    if (showStatus) showStatus (what);
}

static String chainText (const fxr::RackState::Bus& b)
{
    StringArray lanes;
    for (const auto& l : b.lanes)
    {
        StringArray names;
        for (const auto& s : l.slots) if (s.type != fxr::Type::None) names.add (fxr::info (s.type).name);
        if (! names.isEmpty()) lanes.add (names.joinIntoString (" > "));
    }
    return lanes.isEmpty() ? String ("empty") : lanes.joinIntoString ("  |  ");
}

void FxRackPanel::rollChain()
{
    diceSeed = (uint32_t) Random::getSystemRandom().nextInt();
    const auto b = fxr::randomChain (diceSeed, bus == fxr::kMasterBus);
    applyBus (b, String ("Rolled ") + fxr::busName (bus) + ": " + chainText (b) + (b.dry > 0.5f ? "  (parallel with dry)" : ""));
}

void FxRackPanel::showChainMenu()
{
    PopupMenu m, other, copyTo;
    const auto& list = fxr::chains();
    const bool master = bus == fxr::kMasterBus;
    m.addSectionHeader (master ? "MASTER CHAINS" : "BUS CHAINS");
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& c = list[(size_t) i];
        if (c.master == master) m.addItem (100 + i, c.name);
        else other.addItem (100 + i, c.name);
    }
    m.addSubMenu (master ? "Bus Chains" : "Master Chains", other);
    m.addSeparator();
    m.addItem (1, "Roll Series Chain", ! master);
    m.addItem (2, "Roll Parallel Dirt + Space", ! master);
    m.addItem (3, "Roll Space Send", ! master);
    m.addSeparator();
    for (int b = 0; b < fxr::kBuses; ++b) if (b != bus) copyTo.addItem (10 + b, String ("Replace ") + fxr::busName (b) + " with this bus");
    m.addSubMenu ("Copy Bus", copyTo);
    m.addItem (4, String ("Clear ") + fxr::busName (bus), rack.hasModules (bus) || rack.dry (bus) > 0.f);
    SafePointer<FxRackPanel> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&chainsBtn).withMinimumWidth (220), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& self = *safe;
        const auto& list = fxr::chains();
        if (r >= 100 && r < 100 + (int) list.size())
        {
            fxr::RackState::Bus b;
            list[(size_t) r - 100].build (b);
            self.applyBus (b, String (list[(size_t) r - 100].name) + ": " + list[(size_t) r - 100].description);
        }
        else if (r >= 1 && r <= 3)
        {
            self.diceSeed = (uint32_t) Random::getSystemRandom().nextInt();
            const auto b = fxr::randomChain (self.diceSeed, false, r - 1);
            self.applyBus (b, String ("Rolled ") + fxr::busName (self.bus) + ": " + chainText (b));
        }
        else if (r == 4) self.applyBus ({}, String ("Cleared ") + fxr::busName (self.bus));
        else if (r >= 10 && r < 10 + fxr::kBuses)
        {
            self.proc.pushUndo();
            auto st = self.rack.getState();
            const int dest = r - 10;
            const int keepOut = st.buses[(size_t) dest].output;
            st.buses[(size_t) dest] = st.buses[(size_t) self.bus];
            st.buses[(size_t) dest].output = dest == fxr::kMasterBus ? 0 : keepOut;
            self.rack.setState (st);
            self.edited (true);
            if (self.showStatus) self.showStatus (String (fxr::busName (self.bus)) + " copied to " + fxr::busName (dest));
        }
    });
}

void FxRackPanel::toggleSend (int ch)
{
    if (bus == fxr::kMasterBus) return;
    const int n = paramDef (P_OUTPUT).numChoices;
    const int cur = choiceIndex (proc.getPatchValue (ch, P_OUTPUT), n);
    const int target = bus + 2;
    const int next = cur == target ? 0 : target;
    proc.pushUndo();
    proc.setPatchValue (ch, P_OUTPUT, choiceValue (next, n));
    proc.sendUiChange();
    if (showStatus)
        showStatus ("Channel " + String (ch + 1) + (next == target ? String (" now plays through ") + fxr::busName (bus) : String (" back on output A")));
    repaint();
}

String FxRackPanel::routedChannels (int b) const
{
    if (b == fxr::kMasterBus) return "everything on output A";
    const int n = paramDef (P_OUTPUT).numChoices;
    StringArray chans;
    for (int ch = 0; ch < Ferrum8Processor::NC; ++ch)
        if (choiceIndex (proc.getPatchValue (ch, P_OUTPUT), n) == b + 2) chans.add (String (ch + 1));
    return chans.isEmpty() ? String ("no channels sent") : "ch " + chans.joinIntoString (" ");
}

String FxRackPanel::slotSummary (int lane, int slot, int line) const
{
    const auto t = rack.moduleType (bus, lane, slot);
    if (t == fxr::Type::None) return {};
    const auto& in = fxr::info (t);
    int shown = 0;
    for (size_t i = 0; i < in.params.size(); ++i)
    {
        const auto& ps = in.params[i];
        if (String (ps.name) == "Mix") continue;
        if (shown++ != line) continue;
        return String (ps.name) + "  " + String (fxr::formatParam (ps, rack.param (bus, lane, slot, (int) i)));
    }
    return {};
}

//==============================================================================================
void FxRackPanel::tick()
{
    bool changed = false;
    for (int b = 0; b < fxr::kBuses; ++b)
    {
        const float target = rack.meter (b);
        float& m = meters[(size_t) b];
        const float next = jmax (target, m * 0.82f);
        if (std::abs (next - m) > 0.002f) { m = next; changed = true; }
    }
    if (changed) for (int b = 0; b < fxr::kBuses; ++b) repaint (busTab (b));
}

void FxRackPanel::resized()
{
    header = { 0, 0, getWidth(), 84 };
    grid = { 0, 84, getWidth(), kGridTop - 84 + fxr::kLanes * (kLaneH + kLaneGap) };
    editor = { 12, grid.getBottom() + 4, getWidth() - 24, getHeight() - grid.getBottom() - 12 };
    editorInfo = editor.withWidth (380);

    const int right = getWidth() - 12;
    outputSel.setBounds (right - 76, 14, 76, 54);
    outKnob.setBounds (right - 76 - 70, 8, 64, 70);
    dryKnob.setBounds (right - 76 - 140, 8, 64, 70);
    chainsBtn.setBounds (dryKnob.getX() - 146, 12, 132, 28);
    diceBtn.setBounds (dryKnob.getX() - 146, 46, 132, 28);
    for (int l = 0; l < fxr::kLanes; ++l)
    {
        auto la = laneArea (l);
        laneKnobs[(size_t) l].setBounds (la.getX() + 74, la.getY() + 10, 60, 74);
    }
    auto row = editorInfo.reduced (12, 10);
    row = row.removeFromBottom (30);
    bypassBtn.setBounds (row.removeFromLeft (72)); row.removeFromLeft (4);
    leftBtn.setBounds (row.removeFromLeft (30)); row.removeFromLeft (4);
    rightBtn.setBounds (row.removeFromLeft (30)); row.removeFromLeft (4);
    swapBtn.setBounds (row.removeFromLeft (88)); row.removeFromLeft (4);
    resetBtn.setBounds (row.removeFromLeft (58)); row.removeFromLeft (4);
    removeBtn.setBounds (row.removeFromLeft (70));
    layoutEditor();
}

void FxRackPanel::layoutEditor()
{
    const auto t = selSlot >= 0 ? rack.moduleType (bus, selLane, selSlot) : fxr::Type::None;
    if (t == fxr::Type::None) return;
    const int n = jmin ((int) fxr::info (t).params.size(), fxr::kMaxParams);
    const int x0 = editorInfo.getRight() + 8, cw = jmin (100, (editor.getRight() - 8 - x0) / jmax (1, n));
    for (int i = 0; i < n; ++i)
    {
        const int x = x0 + i * cw;
        pKnobs[(size_t) i].setBounds (x + (cw - 84) / 2, editor.getY() + 26, 84, 94);
        pChoices[(size_t) i].setBounds (x + (cw - 88) / 2, editor.getY() + 40, 88, 62);
    }
}

//==============================================================================================
void FxRackPanel::paint (Graphics& g)
{
    g.fillAll (col::panel);
    // ---- header
    g.setColour (col::panel2);
    g.fillRect (header);
    g.setColour (col::line);
    g.drawHorizontalLine (header.getBottom(), 0.f, (float) getWidth());
    for (int b = 0; b < fxr::kBuses; ++b)
    {
        auto r = busTab (b).toFloat();
        const bool sel = b == bus;
        g.setColour (sel ? col::raised.brighter (0.06f) : col::panel);
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (sel ? col::accent : (hover.kind == Hit::BusTab && hover.a == b ? col::dim : col::line));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.f, sel ? 2.f : 1.f);
        auto t = r.reduced (12.f, 7.f);
        g.setColour (sel ? col::accent : col::text);
        g.setFont (font (21.f, true));
        g.drawText (String (fxr::busName (b)).toUpperCase(), t.removeFromTop (24.f), Justification::centredLeft);
        g.setFont (font (13.5f));
        g.setColour (col::dim);
        const int nm = rack.moduleCount (b);
        String sub = nm == 0 ? String ("empty") : String (nm) + (nm == 1 ? " module" : " modules");
        if (b != fxr::kMasterBus) sub << "  |  " << routedChannels (b) << "  >  " << (rack.output (b) == 1 ? "B" : "A");
        g.drawFittedText (sub, t.removeFromTop (18.f).toNearestInt(), Justification::centredLeft, 1, 0.75f);
        auto meter = Rectangle<float> (r.getX() + 12.f, r.getBottom() - 11.f, r.getWidth() - 24.f, 4.f);
        g.setColour (col::bg);
        g.fillRoundedRectangle (meter, 2.f);
        const float lvl = jlimit (0.f, 1.f, (Decibels::gainToDecibels (meters[(size_t) b], -60.f) + 60.f) / 66.f);
        g.setColour (meters[(size_t) b] > 0.98f ? col::mute : col::play);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * lvl), 2.f);
    }
    if (bus != fxr::kMasterBus)
    {
        g.setColour (col::dim);
        g.setFont (font (13.5f, true));
        g.drawText ("CHANNELS SENT HERE", sendChip (0).getX(), 16, 300, 18, Justification::centredLeft);
        const int n = paramDef (P_OUTPUT).numChoices;
        for (int ch = 0; ch < Ferrum8Processor::NC; ++ch)
        {
            auto r = sendChip (ch).toFloat();
            const bool on = choiceIndex (proc.getPatchValue (ch, P_OUTPUT), n) == bus + 2;
            const auto cc = col::channel (ch);
            g.setColour (on ? cc : col::raised);
            g.fillRoundedRectangle (r, 4.f);
            g.setColour (hover.kind == Hit::Send && hover.a == ch ? col::text : (on ? cc.brighter (0.3f) : col::line.brighter (0.2f)));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);
            g.setColour (on ? Colours::black : col::dim);
            g.setFont (font (15.f, true));
            g.drawText (String (ch + 1), r, Justification::centred);
        }
    }
    else
    {
        g.setColour (col::dim);
        g.setFont (font (14.f));
        g.drawFittedText ("The Master bus processes output A: every channel set to A plus the FX buses sent to A. "
                          "Output B stays untouched.", Rectangle<int> (560, 14, 270, 60), Justification::centredLeft, 3);
    }

    // ---- signal flow
    const auto sum = sumNode();
    const float dryY = (float) kGridTop - 8.f;
    const float laneTop = (float) laneArea (0).getCentreY(), laneBottom = (float) laneArea (fxr::kLanes - 1).getCentreY();
    {
        const bool dryOn = rack.dry (bus) > 0.001f;
        Path dry;
        dry.startNewSubPath ((float) kInX, dryY);
        dry.lineTo (sum.x, dryY);
        dry.lineTo (sum.x, sum.y - 16.f);
        g.setColour (dryOn ? col::dim : col::faint.withAlpha (0.6f));
        if (dryOn) g.strokePath (dry, PathStrokeType (1.5f));
        else
        {
            Path dashed;
            const float dashes[] = { 4.f, 4.f };
            PathStrokeType (1.f).createDashedStroke (dashed, dry, dashes, 2);
            g.fillPath (dashed);
        }
        const auto dryLabel = Rectangle<float> (580.f, dryY - 7.f, 80.f, 14.f);
        g.setColour (col::panel);
        g.fillRect (dryLabel);
        g.setColour (dryOn ? col::dim : col::faint);
        g.setFont (font (13.f, true));
        g.drawText ("DRY " + String (roundToInt (rack.dry (bus) * 100.f)) + "%", dryLabel, Justification::centred);
        // input bus
        g.setColour (col::dim);
        g.drawLine ((float) kInX, dryY, (float) kInX, laneBottom, 2.f);
        g.fillEllipse ((float) kInX - 4.f, dryY - 4.f, 8.f, 8.f);
        g.setFont (font (12.5f, true));
        g.drawText ("IN", Rectangle<float> ((float) kInX - 30.f, dryY - 8.f, 24.f, 16.f), Justification::centredRight);
    }
    for (int l = 0; l < fxr::kLanes; ++l)
    {
        auto la = laneArea (l);
        bool any = false;
        for (int s = 0; s < fxr::kSlots; ++s) any |= rack.moduleType (bus, l, s) != fxr::Type::None;
        const bool muted = rack.laneMuted (bus, l);
        // lane header
        g.setColour (col::panel2);
        g.fillRoundedRectangle (Rectangle<float> ((float) la.getX(), (float) la.getY(), 140.f, (float) la.getHeight()), 5.f);
        g.setColour (any ? col::text : col::faint);
        g.setFont (font (14.5f, true));
        g.drawText ("LANE " + String (l + 1), la.getX() + 10, la.getY() + 8, 70, 18, Justification::centredLeft);
        auto mb = muteArea (l).toFloat();
        g.setColour (muted ? col::mute : col::raised);
        g.fillRoundedRectangle (mb, 4.f);
        g.setColour (hover.kind == Hit::LaneMute && hover.a == l ? col::text : col::line.brighter (0.2f));
        g.drawRoundedRectangle (mb.reduced (0.5f), 4.f, 1.f);
        g.setColour (muted ? Colours::black : col::dim);
        g.setFont (font (14.f, true));
        g.drawText ("M", mb, Justification::centred);
        laneKnobs[(size_t) l].setInactive (! any || muted);

        // wires
        const float y = (float) la.getCentreY();
        const auto wire = any && ! muted ? col::dim : col::faint.withAlpha (0.55f);
        g.setColour (wire);
        g.drawLine ((float) kInX, y, (float) kSlotX0, y, 1.5f);
        for (int s = 0; s < fxr::kSlots - 1; ++s)
        {
            const float x0 = (float) slotArea (l, s).getRight(), x1 = (float) slotArea (l, s + 1).getX();
            g.drawLine (x0, y, x1 - 5.f, y, 1.5f);
            Path arrow;
            arrow.addTriangle (x1 - 7.f, y - 4.f, x1 - 7.f, y + 4.f, x1 - 1.f, y);
            g.fillPath (arrow);
        }
        const float xe = (float) slotArea (l, fxr::kSlots - 1).getRight();
        Path toSum;
        toSum.startNewSubPath (xe, y);
        toSum.lineTo (sum.x - 22.f, y);
        toSum.lineTo (sum.x - 12.f, jlimit (sum.y - 10.f, sum.y + 10.f, y));
        g.strokePath (toSum, PathStrokeType (1.5f));

        // slots
        for (int s = 0; s < fxr::kSlots; ++s)
        {
            auto r = slotArea (l, s).toFloat();
            const auto t = rack.moduleType (bus, l, s);
            const bool hov = hover.kind == Hit::Slot && hover.a == l && hover.b == s;
            const bool isDrop = dragging && dropLane == l && dropSlot == s && ! (dragLane == l && dragSlot == s);
            if (t == fxr::Type::None)
            {
                g.setColour (col::panel);
                g.fillRoundedRectangle (r, 6.f);
                Path outline;
                outline.addRoundedRectangle (r.reduced (0.5f), 6.f);
                Path dashed;
                const float dashes[] = { 5.f, 4.f };
                PathStrokeType (1.f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (isDrop ? col::accent : (hov ? col::dim : col::line.brighter (0.25f)));
                g.fillPath (dashed);
                g.setColour (isDrop ? col::accent : (hov ? col::text : col::faint));
                g.setFont (font (hov ? 15.f : 22.f, hov));
                g.drawText (hov ? "+  Add effect" : "+", r, Justification::centred);
                continue;
            }
            const auto& in = fxr::info (t);
            const auto cc = categoryColour (in.category);
            const bool byp = rack.bypassed (bus, l, s);
            const bool sel = l == selLane && s == selSlot;
            const float alpha = (byp || muted) ? 0.45f : 1.f;
            g.setColour ((sel ? col::raised.brighter (0.08f) : col::raised).withMultipliedAlpha (dragging && dragLane == l && dragSlot == s ? 0.4f : 1.f));
            g.fillRoundedRectangle (r, 6.f);
            g.setColour (cc.withAlpha (alpha));
            g.fillRoundedRectangle (r.getX() + 5.f, r.getY() + 8.f, 4.f, r.getHeight() - 16.f, 2.f);
            g.setColour (isDrop ? col::accent : (sel ? cc : (hov ? col::dim : col::line.brighter (0.15f))));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.f, sel || isDrop ? 2.f : 1.f);
            auto tr = r.withTrimmedLeft (16.f).withTrimmedRight (32.f).withTrimmedTop (7.f);
            g.setColour (col::text.withAlpha (alpha));
            g.setFont (font (16.5f, true));
            g.drawFittedText (in.name, tr.removeFromTop (20.f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
            g.setFont (font (13.f));
            g.setColour (col::dim.withAlpha (alpha));
            tr.removeFromTop (3.f);
            for (int line = 0; line < 2; ++line)
                g.drawFittedText (slotSummary (l, s, line), tr.removeFromTop (17.f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
            const int mixIdx = fxr::paramIndex (t, "Mix");
            if (mixIdx >= 0)
            {
                const float mix = rack.param (bus, l, s, mixIdx);
                auto bar = Rectangle<float> (r.getX() + 16.f, r.getBottom() - 10.f, r.getWidth() - 30.f, 3.f);
                g.setColour (col::bg);
                g.fillRoundedRectangle (bar, 1.5f);
                g.setColour (cc.withAlpha (0.8f * alpha));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * mix), 1.5f);
            }
            // power
            auto pr = powerArea (l, s).toFloat();
            const bool phov = hover.kind == Hit::Power && hover.a == l && hover.b == s;
            g.setColour (byp ? col::faint : cc);
            if (phov) g.setColour (col::text);
            Path pw;
            const auto c = pr.getCentre();
            pw.addCentredArc (c.x, c.y, 6.5f, 6.5f, 0.f, 0.6f, MathConstants<float>::twoPi - 0.6f, true);
            g.strokePath (pw, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
            g.drawLine (c.x, c.y - 8.f, c.x, c.y - 1.f, 1.8f);
        }
    }
    // sum node and output
    g.setColour (col::raised);
    g.fillEllipse (sum.x - 14.f, sum.y - 14.f, 28.f, 28.f);
    g.setColour (col::dim);
    g.drawEllipse (sum.x - 14.f, sum.y - 14.f, 28.f, 28.f, 1.5f);
    g.drawLine (sum.x - 6.f, sum.y, sum.x + 6.f, sum.y, 2.f);
    g.drawLine (sum.x, sum.y - 6.f, sum.x, sum.y + 6.f, 2.f);
    g.drawLine (sum.x, sum.y + 14.f, sum.x, laneBottom + 30.f, 1.5f);
    g.setFont (font (12.5f, true));
    const String dest = bus == fxr::kMasterBus ? String ("OUT A") : String ("OUT ") + (rack.output (bus) == 1 ? "B" : "A");
    g.drawText (dest, Rectangle<float> (sum.x - 30.f, laneBottom + 32.f, 60.f, 14.f), Justification::centred);
    ignoreUnused (laneTop);

    // ---- module editor
    g.setColour (col::panel2);
    g.fillRoundedRectangle (editor.toFloat(), 6.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (editor.toFloat().reduced (0.5f), 6.f, 1.f);
    const auto t = selSlot >= 0 ? rack.moduleType (bus, selLane, selSlot) : fxr::Type::None;
    if (t == fxr::Type::None)
    {
        g.setColour (col::dim);
        g.setFont (font (15.5f));
        g.drawFittedText ("Click a slot to add an effect. Slots in a lane run in series, left to right; the three lanes run in parallel "
                          "and are mixed with the dry signal. Drag a card to move it (Alt-drag copies), right-click for more.\n"
                          "Send a channel here with the numbers above, or with its Output setting (A, B, FX 1, FX 2). "
                          "Chains has ready-made setups; Roll Chain makes a random one.",
                          editor.reduced (24, 16), Justification::centredLeft, 5);
        return;
    }
    const auto& in = fxr::info (t);
    const auto cc = categoryColour (in.category);
    auto ir = editorInfo.reduced (14, 10);
    g.setColour (cc);
    g.setFont (font (23.f, true));
    g.drawText (in.name, ir.removeFromTop (26), Justification::centredLeft);
    g.setFont (font (13.5f, true));
    g.setColour (col::dim);
    g.drawText (String (in.category).toUpperCase() + "   |   " + fxr::busName (bus) + "  LANE " + String (selLane + 1) + "  SLOT " + String (selSlot + 1)
                    + (rack.bypassed (bus, selLane, selSlot) ? "   |   BYPASSED" : ""),
                ir.removeFromTop (18), Justification::centredLeft);
    ir.removeFromTop (4);
    g.setFont (font (14.5f));
    g.setColour (col::text.withAlpha (0.8f));
    g.drawFittedText (in.description, ir.withTrimmedBottom (36), Justification::topLeft, 5, 0.9f);
    // values under the knobs
    g.setFont (font (14.f, true));
    for (int i = 0; i < (int) in.params.size() && i < fxr::kMaxParams; ++i)
    {
        const auto& ps = in.params[(size_t) i];
        const auto& k = pKnobs[(size_t) i];
        const auto& c = pChoices[(size_t) i];
        const auto b = ps.scale == fxr::Scale::Choice ? c.getBounds() : k.getBounds();
        if (ps.scale == fxr::Scale::Choice) continue;
        g.setColour (cc.interpolatedWith (col::text, 0.4f));
        g.drawFittedText (fxr::formatParam (ps, rack.param (bus, selLane, selSlot, i)), Rectangle<int> (b.getX() - 6, b.getBottom() + 2, b.getWidth() + 12, 18),
                          Justification::centred, 1, 0.75f);
    }
    // ghost of a dragged card
    if (dragging && dragLane >= 0)
    {
        const auto dt = rack.moduleType (bus, dragLane, dragSlot);
        if (dt != fxr::Type::None)
        {
            auto r = Rectangle<float> ((float) kSlotW * 0.8f, 34.f).withCentre (dragPos.toFloat());
            g.setColour (col::raised.withAlpha (0.9f));
            g.fillRoundedRectangle (r, 6.f);
            g.setColour (categoryColour (fxr::info (dt).category));
            g.drawRoundedRectangle (r, 6.f, 2.f);
            g.setColour (col::text);
            g.setFont (font (15.f, true));
            g.drawText (String (fxr::info (dt).name) + (ModifierKeys::currentModifiers.isAltDown() ? "  +copy" : ""), r, Justification::centred);
        }
    }
}

//==============================================================================================
void FxRackPanel::mouseMove (const MouseEvent& e)
{
    const auto h = findHit (e.getPosition());
    if (h.kind != hover.kind || h.a != hover.a || h.b != hover.b) { hover = h; repaint(); }
}

void FxRackPanel::mouseExit (const MouseEvent&)
{
    if (hover.kind != Hit::None) { hover = {}; repaint(); }
}

void FxRackPanel::mouseDown (const MouseEvent& e)
{
    const auto h = findHit (e.getPosition());
    dragLane = dragSlot = -1;
    dragging = false;
    switch (h.kind)
    {
        case Hit::BusTab: showBus (h.a); break;
        case Hit::Send: toggleSend (h.a); break;
        case Hit::LaneMute:
            proc.pushUndo();
            rack.setLaneMute (bus, h.a, ! rack.laneMuted (bus, h.a));
            edited (false);
            repaint();
            break;
        case Hit::Power:
            proc.pushUndo();
            rack.setBypass (bus, h.a, h.b, ! rack.bypassed (bus, h.a, h.b));
            edited (false);
            if (h.a == selLane && h.b == selSlot) syncEditor();
            repaint();
            break;
        case Hit::Slot:
        {
            const bool filled = rack.moduleType (bus, h.a, h.b) != fxr::Type::None;
            if (e.mods.isPopupMenu()) { slotMenu (h.a, h.b); break; }
            if (! filled) { pickModule (h.a, h.b); break; }
            if (e.getNumberOfClicks() > 1) { pickModule (h.a, h.b); break; }
            select (h.a, h.b);
            dragLane = h.a;
            dragSlot = h.b;
            break;
        }
        case Hit::None:
        default: break;
    }
}

void FxRackPanel::mouseDrag (const MouseEvent& e)
{
    if (dragLane < 0) return;
    if (! dragging && e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    dragPos = e.getPosition();
    const auto h = findHit (e.getPosition());
    dropLane = h.kind == Hit::Slot || h.kind == Hit::Power ? h.a : -1;
    dropSlot = h.kind == Hit::Slot || h.kind == Hit::Power ? h.b : -1;
    repaint();
}

void FxRackPanel::mouseUp (const MouseEvent& e)
{
    if (dragging && dropLane >= 0 && dropSlot >= 0 && ! (dropLane == dragLane && dropSlot == dragSlot))
        moveSlot (dragLane, dragSlot, dropLane, dropSlot, e.mods.isAltDown());
    dragging = false;
    dragLane = dragSlot = dropLane = dropSlot = -1;
    repaint();
}

String FxRackPanel::getTooltip()
{
    switch (hover.kind)
    {
        case Hit::BusTab:
            return hover.a == fxr::kMasterBus ? String ("Master: processes everything on output A")
                                              : String (fxr::busName (hover.a)) + ": " + routedChannels (hover.a);
        case Hit::Send:
            return "Send channel " + String (hover.a + 1) + " through " + fxr::busName (bus) + " (click again: back to output A)";
        case Hit::LaneMute: return "Mute lane " + String (hover.a + 1);
        case Hit::Power: return "Bypass on / off";
        case Hit::Slot:
        {
            const auto t = rack.moduleType (bus, hover.a, hover.b);
            if (t == fxr::Type::None) return "Add an effect to lane " + String (hover.a + 1) + ", slot " + String (hover.b + 1);
            return String (fxr::info (t).name) + ": " + fxr::info (t).description + "\n(drag to move, Alt-drag to copy, double-click to change, right-click for more)";
        }
        case Hit::None:
        default: return {};
    }
}
} // namespace ui
