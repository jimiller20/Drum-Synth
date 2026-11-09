// ModPanel.cpp - see ModPanel.h
// SPDX-License-Identifier: GPL-3.0-only
#include "ModPanel.h"

namespace ui
{
using namespace juce;
using namespace ferrum;

//==============================================================================================
void DepthBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f, 3.f);
    g.setColour (col::raised);
    g.fillRoundedRectangle (r, 4.f);
    const float cx = r.getCentreX();
    const float x1 = cx + value * r.getWidth() * 0.5f;
    g.setColour (col::mod.withAlpha (0.75f));
    g.fillRoundedRectangle (Rectangle<float> (jmin (cx, x1), r.getY() + 2.f, std::abs (x1 - cx), r.getHeight() - 4.f), 2.f);
    g.setColour (col::line.brighter (0.3f));
    g.drawVerticalLine ((int) cx, r.getY(), r.getBottom());
    g.setColour (col::text);
    g.setFont (font (13.5f, true));
    g.drawText ((value > 0 ? "+" : "") + String (roundToInt (value * 100.f)) + "%", r.toNearestInt(), Justification::centred);
}

void DepthBar::mouseDown (const MouseEvent&) { startValue = value; if (onGestureStart) onGestureStart(); }

void DepthBar::mouseDrag (const MouseEvent& e)
{
    const float scale = e.mods.isShiftDown() ? 0.2f : 1.f;
    setValue (startValue + (float) e.getDistanceFromDragStartX() / (float) getWidth() * 2.f * scale);
    if (onChange) onChange (value);
}

void DepthBar::mouseDoubleClick (const MouseEvent&)
{
    if (onGestureStart) onGestureStart();
    setValue (0.f);
    if (onChange) onChange (0.f);
}

//==============================================================================================
static bool modulatable (int p) { return paramDef (p).numChoices == 0 && p != P_MODEL; }

String ModPanel::targetName (const Ferrum8Processor& p, const mod::Route& r)
{
    if (r.kind == mod::Route::Channel)
        return "Ch " + String (r.ch + 1) + " " + p.getChannelName (r.ch) + ":  " + paramDef (r.param).name;
    if (r.kind == mod::Route::Fx)
    {
        auto& rack = const_cast<Ferrum8Processor&> (p).getRack();
        const auto t = rack.moduleType (r.bus, r.lane, r.slot);
        String s = String (fx::busName (r.bus)) + " L" + String (r.lane + 1) + " S" + String (r.slot + 1) + " ";
        if (t == fx::Type::None) return s + "(empty slot)";
        const auto& in = fx::info (t);
        return s + in.name + ":  " + (r.idx < (int) in.params.size() ? in.params[(size_t) r.idx].name : "?");
    }
    return "(no target)";
}

void ModPanel::addTargetMenu (const Ferrum8Processor& p, PopupMenu& m, int base)
{
    for (int ch = 0; ch < Ferrum8Processor::NC; ++ch)
    {
        PopupMenu sub;
        for (int q = 0; q < NUM_PATCH_PARAMS; ++q) if (modulatable (q)) sub.addItem (base + ch * 64 + q, paramDef (q).name);
        m.addSubMenu ("Ch " + String (ch + 1) + "  " + p.getChannelName (ch), sub);
    }
    m.addSeparator();
    auto& rack = const_cast<Ferrum8Processor&> (p).getRack();
    for (int b = 0; b < fx::kBuses; ++b)
    {
        PopupMenu busMenu;
        for (int l = 0; l < fx::kLanes; ++l)
            for (int s = 0; s < fx::kSlots; ++s)
            {
                const auto t = rack.moduleType (b, l, s);
                if (t == fx::Type::None) continue;
                PopupMenu mm;
                const auto& in = fx::info (t);
                for (int i = 0; i < (int) in.params.size(); ++i)
                    if (in.params[(size_t) i].scale != fx::Scale::Choice) mm.addItem (base + 1000 + b * 200 + l * 60 + s * 12 + i, in.params[(size_t) i].name);
                busMenu.addSubMenu ("Lane " + String (l + 1) + " slot " + String (s + 1) + ":  " + in.name, mm);
            }
        m.addSubMenu (String (fx::busName (b)) + (rack.hasModules (b) ? "" : "  (empty)"), busMenu, rack.hasModules (b));
    }
}

bool ModPanel::routeFromMenu (const Ferrum8Processor&, int id, int base, mod::Route& r)
{
    id -= base;
    if (id < 0) return false;
    if (id < 1000)
    {
        r.kind = mod::Route::Channel;
        r.ch = id / 64;
        r.param = id % 64;
        return r.ch < Ferrum8Processor::NC && r.param < NUM_PATCH_PARAMS;
    }
    id -= 1000;
    r.kind = mod::Route::Fx;
    r.bus = id / 200; id %= 200;
    r.lane = id / 60; id %= 60;
    r.slot = id / 12;
    r.idx = id % 12;
    return r.bus < fx::kBuses;
}

//==============================================================================================
ModPanel::ModPanel (Ferrum8Processor& p) : proc (p)
{
    StringArray types;
    for (int t = 0; t < (int) mod::Type::Count; ++t) types.add (t == (int) mod::Type::SampleHold ? "S&H" : t == (int) mod::Type::Peak ? "Peak" :
                                                            t == (int) mod::Type::Random ? "Random" : t == (int) mod::Type::Steps ? "Steps" : mod::kTypeNames[t]);
    typeSel.bindFree (types, 0);
    typeSel.setAccent (col::mod);
    typeSel.onGestureStart = [this] { proc.pushUndo(); };
    typeSel.onUserChoice = [this] (int t)
    {
        edit ([t] (mod::ModState& s) { const auto keep = s.routes; s = mod::defaultState ((mod::Type) t); s.routes = keep; });
        syncControls();
        if (showStatus) showStatus ("MOD " + String (sel + 1) + " is now " + mod::kTypeNames[t]);
    };
    addAndMakeVisible (typeSel);

    for (int i = 0; i < mod::kKnobs; ++i)
    {
        addChildComponent (knobs[(size_t) i]);
        addChildComponent (choices[(size_t) i]);
        knobs[(size_t) i].setAccent (col::mod);
        knobs[(size_t) i].onGestureStart = [this] { proc.pushUndo(); };
        choices[(size_t) i].setAccent (col::mod);
        choices[(size_t) i].onGestureStart = [this] { proc.pushUndo(); };
        choices[(size_t) i].onUserChoice = [this, i] (int idx)
        {
            std::vector<std::string> names;
            const auto t = proc.getMods()[(size_t) sel].type;
            mod::knobIsChoice (t, i, &names);
            const float v = mod::choiceToNorm (idx, (int) names.size());
            edit ([i, v] (mod::ModState& s) { s.p[i] = v; });
            syncControls(); // a synced LFO swaps its Rate between divisions and Hz
        };
    }
    StringArray trig;
    for (int t = 0; t < mod::TrigCount; ++t) trig.add (mod::triggerName (t));
    addChildComponent (trigger);
    trigger.setAccent (col::mod);
    trigger.onGestureStart = [this] { proc.pushUndo(); };
    trigger.onUserChoice = [this] (int i) { edit ([i] (mod::ModState& s) { s.trigger = i; }); };
    trigger.bindFree ("Trigger", trig, 0);
    trigger.setLabel ("Trigger");
    StringArray src;
    for (int s = 0; s < mod::PeakCount; ++s) src.add (mod::peakSourceName (s));
    addChildComponent (source);
    source.setAccent (col::mod);
    source.onGestureStart = [this] { proc.pushUndo(); };
    source.onUserChoice = [this] (int i) { edit ([i] (mod::ModState& s) { s.source = i; }); };
    source.bindFree ("Follows", src, 0);
    source.setLabel ("Follows");
    addChildComponent (macro);
    macro.setAccent (col::mod);
    macro.setLabel ("Macro");

    for (int r = 0; r < mod::kRoutes; ++r)
    {
        auto& b = routeBtns[(size_t) r];
        addChildComponent (b);
        b.onClick = [this, r] { pickTarget (r); };
        b.setTooltip ("What this route moves (click to change)");
        auto& d = depthBars[(size_t) r];
        addChildComponent (d);
        d.onGestureStart = [this] { proc.pushUndo(); };
        d.onChange = [this, r] (float v) { edit ([r, v] (mod::ModState& s) { s.routes[(size_t) r].depth = v; }); };
        auto& x = removeBtns[(size_t) r];
        x.setButtonText ("x");
        addChildComponent (x);
        x.setTooltip ("Remove this route");
        x.onClick = [this, r]
        {
            edit ([r] (mod::ModState& s)
            {
                for (int i = r; i < mod::kRoutes - 1; ++i) s.routes[(size_t) i] = s.routes[(size_t) i + 1];
                s.routes[mod::kRoutes - 1] = {};
            }, true);
            syncControls();
        };
    }
    addAndMakeVisible (addBtn);
    addBtn.setAccent (col::mod);
    addBtn.setTooltip ("Route this modulator to a channel sound parameter or an FX parameter (or right-click any knob: Modulate with)");
    addBtn.onClick = [this]
    {
        const auto& s = proc.getMods()[(size_t) sel];
        int free = -1;
        for (int i = 0; i < mod::kRoutes; ++i) if (s.routes[(size_t) i].kind == mod::Route::None) { free = i; break; }
        if (free < 0) { if (showStatus) showStatus ("MOD " + String (sel + 1) + " already has 8 routes"); return; }
        pickTarget (free);
    };
    syncControls();
}

void ModPanel::edit (const std::function<void (mod::ModState&)>& f, bool undo)
{
    if (undo) proc.pushUndo();
    auto ms = proc.getMods();
    f (ms[(size_t) sel]);
    proc.setMods (ms);
    repaint();
}

void ModPanel::select (int m)
{
    sel = jlimit (0, mod::kMods - 1, m);
    syncControls();
    repaint();
}

void ModPanel::refresh() { syncControls(); repaint(); }

void ModPanel::pickTarget (int route)
{
    PopupMenu m;
    m.addSectionHeader ("MOD " + String (sel + 1) + " moves...");
    addTargetMenu (proc, m, 10000);
    SafePointer<ModPanel> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (route < mod::kRoutes && routeBtns[(size_t) route].isVisible() ? (Component*) &routeBtns[(size_t) route] : &addBtn),
                     [safe, route] (int id)
    {
        if (safe == nullptr || id <= 0) return;
        mod::Route r;
        if (! routeFromMenu (safe->proc, id, 10000, r)) return;
        const float keepDepth = safe->proc.getMods()[(size_t) safe->sel].routes[(size_t) route].depth;
        r.depth = keepDepth != 0.f ? keepDepth : 0.3f;
        safe->edit ([route, r] (mod::ModState& s)
        {
            if (s.type == mod::Type::Off) { const auto keep = s.routes; s = mod::defaultState (mod::Type::LFO); s.routes = keep; }
            s.routes[(size_t) route] = r;
        }, true);
        safe->syncControls();
        if (safe->showStatus) safe->showStatus ("MOD " + String (safe->sel + 1) + " now moves " + targetName (safe->proc, r));
    });
}

void ModPanel::syncControls()
{
    const auto& s = proc.getMods()[(size_t) sel];
    typeSel.setIndex ((int) s.type);
    for (int i = 0; i < mod::kKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        auto& c = choices[(size_t) i];
        const auto& spec = mod::knobSpec (s.type, i);
        std::vector<std::string> names;
        const bool isChoice = spec.name != nullptr && mod::knobIsChoice (s.type, i, &names);
        // a synced LFO / random shows its division, a free one its rate in Hz
        bool hidden = spec.name == nullptr;
        if (s.type == mod::Type::LFO && (i == 1 || i == 5)) hidden = (i == 1) != (mod::normToChoice (s.p[2], 2) == 1);
        if (s.type == mod::Type::Random && (i == 0 || i == 4)) hidden = (i == 4) != (mod::normToChoice (s.p[2], 2) == 1);
        k.setVisible (! hidden && ! isChoice);
        c.setVisible (! hidden && isChoice);
        if (hidden) continue;
        if (isChoice)
        {
            StringArray sa;
            for (auto& n : names) sa.add (n);
            c.bindFree (spec.name, sa, mod::normToChoice (s.p[i], (int) names.size()));
            c.setLabel (spec.name);
        }
        else
        {
            const auto type = s.type;
            k.bindFree ({ spec.name, spec.def, [this, i] { return proc.getMods()[(size_t) sel].p[i]; },
                          [this, i] (float v) { edit ([i, v] (mod::ModState& st) { st.p[i] = v; }); },
                          [type, i] (float v) { return String (mod::formatKnob (type, i, v)); }, {} });
            k.setLabel (spec.name);
        }
    }
    const bool usesTrigger = s.type == mod::Type::LFO || s.type == mod::Type::Envelope || s.type == mod::Type::SampleHold || s.type == mod::Type::Random;
    trigger.setVisible (usesTrigger);
    if (usesTrigger)
    {
        StringArray trig;
        for (int t = 0; t < mod::TrigCount; ++t) trig.add (mod::triggerName (t));
        trig.set (0, s.type == mod::Type::Envelope ? "Every beat" : s.type == mod::Type::SampleHold ? "Its rate (clock)" : "Never (free)");
        trigger.bindFree ("Trigger", trig, s.trigger);
    }
    trigger.setIndex (s.trigger);
    trigger.setLabel (s.type == mod::Type::LFO ? "Restart on" : s.type == mod::Type::Envelope ? "Triggered by" : "New value on");
    source.setVisible (s.type == mod::Type::Peak);
    source.setIndex (s.source);
    macro.setVisible (s.type == mod::Type::Macro);
    if (s.type == mod::Type::Macro) { macro.bind (proc.apvts.getParameter ("macro" + String (sel + 1))); macro.setLabel ("Macro " + String (sel + 1)); }
    int shown = 0;
    for (int r = 0; r < mod::kRoutes; ++r)
    {
        const auto& rt = s.routes[(size_t) r];
        const bool on = rt.kind != mod::Route::None;
        routeBtns[(size_t) r].setVisible (on);
        depthBars[(size_t) r].setVisible (on);
        removeBtns[(size_t) r].setVisible (on);
        if (! on) continue;
        ++shown;
        routeBtns[(size_t) r].setButtonText (targetName (proc, rt));
        depthBars[(size_t) r].setValue (rt.depth);
    }
    addBtn.setEnabled (shown < mod::kRoutes);
    layoutControls();
}

//==============================================================================================
Rectangle<int> ModPanel::card (int m) const { return { 12, 48 + m * 89, 280, 83 }; }
Rectangle<int> ModPanel::stepArea() const { return { controlsArea.getX(), controlsArea.getBottom() + 20, controlsArea.getWidth(), 62 }; }

void ModPanel::resized()
{
    header = { 0, 0, getWidth(), 40 };
    typeSel.setBounds (304, 48, getWidth() - 316, 34);
    scope = { 304, 90, getWidth() - 316, 104 };
    controlsArea = { 304, 202, getWidth() - 316, 100 };
    routesArea = { 304, 392, getWidth() - 316, getHeight() - 392 - 8 };
    addBtn.setBounds (routesArea.getRight() - 140, routesArea.getY(), 140, 26);
    layoutControls();
}

void ModPanel::layoutControls()
{
    int x = controlsArea.getX();
    for (int i = 0; i < mod::kKnobs; ++i)
    {
        if (! knobs[(size_t) i].isVisible() && ! choices[(size_t) i].isVisible()) continue;
        knobs[(size_t) i].setBounds (x, controlsArea.getY(), 84, 94);
        choices[(size_t) i].setBounds (x, controlsArea.getY() + 14, 100, 62);
        x += choices[(size_t) i].isVisible() ? 108 : 92;
    }
    if (macro.isVisible()) { macro.setBounds (x, controlsArea.getY(), 84, 94); x += 92; }
    trigger.setBounds (x + 8, controlsArea.getY() + 14, 150, 62);
    source.setBounds (x + 8, controlsArea.getY() + 14, 150, 62);
    const int colW = (routesArea.getWidth() - 12) / 2;
    int shown = 0;
    for (int r = 0; r < mod::kRoutes; ++r)
    {
        if (! routeBtns[(size_t) r].isVisible()) continue;
        const int col = shown / 4, row = shown % 4;
        const int rx = routesArea.getX() + col * (colW + 12), ry = routesArea.getY() + 34 + row * 40;
        routeBtns[(size_t) r].setBounds (rx, ry, colW - 200, 32);
        depthBars[(size_t) r].setBounds (rx + colW - 194, ry, 156, 32);
        removeBtns[(size_t) r].setBounds (rx + colW - 32, ry + 2, 28, 28);
        ++shown;
    }
}

//==============================================================================================
void ModPanel::tick()
{
    histPos = (histPos + 1) % 120;
    for (int m = 0; m < mod::kMods; ++m) history[(size_t) m][(size_t) histPos] = proc.modValue (m);
    repaint (scope);
    for (int m = 0; m < mod::kMods; ++m) repaint (card (m));
}

void ModPanel::paint (Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::text);
    g.setFont (font (17.f, true));
    g.drawText ("MODULATION", 14, 10, 200, 22, Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (14.f));
    g.drawFittedText ("Six modulators move sound and FX parameters over time. Pick one on the left, choose what it is, then add targets "
                      "below - or right-click any knob in DRUMS or FX RACK and choose Modulate with.",
                      Rectangle<int> (160, 6, getWidth() - 174, 30), Justification::centredLeft, 2, 0.85f);

    const auto& ms = proc.getMods();
    for (int m = 0; m < mod::kMods; ++m)
    {
        auto r = card (m).toFloat();
        const auto& s = ms[(size_t) m];
        const bool on = s.type != mod::Type::Off;
        g.setColour (m == sel ? col::raised.brighter (0.08f) : col::raised);
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (m == sel ? col::mod : col::line.brighter (0.15f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.f, m == sel ? 2.f : 1.f);
        g.setColour (on ? col::text : col::faint);
        g.setFont (font (16.f, true));
        g.drawText ("MOD " + String (m + 1), r.reduced (12.f, 6.f).removeFromTop (20.f), Justification::centredLeft);
        g.setColour (on ? col::mod : col::faint);
        g.setFont (font (14.f, true));
        g.drawText (mod::kTypeNames[(int) s.type], r.reduced (12.f, 6.f).removeFromTop (20.f), Justification::centredRight);
        g.setColour (col::dim);
        g.setFont (font (13.f));
        const int n = s.routeCount();
        g.drawText (on ? (n == 0 ? String ("no targets yet") : String (n) + (n == 1 ? " target" : " targets")) : String ("off"),
                    r.reduced (12.f, 6.f).withTrimmedTop (22.f).removeFromTop (16.f), Justification::centredLeft);
        // sparkline
        auto sp = Rectangle<float> (r.getX() + 12.f, r.getBottom() - 34.f, r.getWidth() - 24.f, 26.f);
        g.setColour (col::bg);
        g.fillRoundedRectangle (sp, 3.f);
        if (on)
        {
            Path pth;
            for (int i = 0; i < 120; ++i)
            {
                const float v = history[(size_t) m][(size_t) ((histPos + 1 + i) % 120)];
                const Point<float> pt (sp.getX() + sp.getWidth() * (float) i / 119.f, sp.getCentreY() - v * sp.getHeight() * 0.45f);
                if (i == 0) pth.startNewSubPath (pt); else pth.lineTo (pt);
            }
            g.setColour (col::mod);
            g.strokePath (pth, PathStrokeType (1.5f));
        }
    }

    // scope of the selected modulator
    g.setColour (col::bg);
    g.fillRoundedRectangle (scope.toFloat(), 6.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (scope.toFloat().reduced (0.5f), 6.f, 1.f);
    const auto sc = scope.toFloat().reduced (10.f, 8.f);
    g.setColour (col::line.brighter (0.1f));
    g.drawHorizontalLine ((int) sc.getCentreY(), sc.getX(), sc.getRight());
    const auto& s = ms[(size_t) sel];
    if (s.type != mod::Type::Off)
    {
        Path pth;
        for (int i = 0; i < 120; ++i)
        {
            const float v = history[(size_t) sel][(size_t) ((histPos + 1 + i) % 120)];
            const Point<float> pt (sc.getX() + sc.getWidth() * (float) i / 119.f, sc.getCentreY() - v * sc.getHeight() * 0.5f);
            if (i == 0) pth.startNewSubPath (pt); else pth.lineTo (pt);
        }
        g.setColour (col::mod);
        g.strokePath (pth, PathStrokeType (2.f));
        g.setColour (col::text);
        g.setFont (font (14.f, true));
        g.drawText ("MOD " + String (sel + 1) + "  " + String (proc.modValue (sel), 2), sc.toNearestInt().removeFromTop (18), Justification::topLeft);
        g.setColour (col::faint);
        g.setFont (font (12.5f));
        g.drawText ("last 4 seconds", sc.toNearestInt().removeFromTop (18), Justification::topRight);
    }
    else
    {
        g.setColour (col::dim);
        g.setFont (font (15.f));
        g.drawText ("MOD " + String (sel + 1) + " is off - choose a type above", sc.toNearestInt(), Justification::centred);
    }

    // what the selected type does
    String help;
    switch (s.type)
    {
        case mod::Type::LFO: help = "LFO: a repeating wave. Synced locks it to the bar; Restart on makes each hit start the wave again."; break;
        case mod::Type::Envelope: help = "Envelope (ADSR): starts when the trigger fires. Hold is how long the 'key' stays down after a hit."; break;
        case mod::Type::SampleHold: help = "Sample & Hold: a new random value on every clock step or trigger; Glide slides between them, Chance skips some."; break;
        case mod::Type::Peak: help = "Peak Controller: follows how loud a channel (or output A) is, like FL Studio's Peak Controller. Great for ducking."; break;
        case mod::Type::Random: help = "Random Drift: a smooth (or stepped) random walk - slow movement that never repeats."; break;
        case mod::Type::Steps: help = "Step Lane: draw 16 values below; they play in time with the song."; break;
        case mod::Type::Macro: help = "Macro: a knob your DAW can automate (Macro " + String (sel + 1) + "). Map it to several targets at once."; break;
        default: help = "Off. Choose a type above."; break;
    }
    g.setColour (col::dim);
    g.setFont (font (13.5f));
    g.drawFittedText (help, Rectangle<int> (controlsArea.getX(), controlsArea.getBottom() - 2, controlsArea.getWidth(), 18), Justification::centredLeft, 1, 0.8f);

    if (s.type == mod::Type::Steps)
    {
        const auto sa = stepArea().toFloat();
        const int len = 1 + mod::normToChoice (s.p[1], mod::kSteps);
        const float w = sa.getWidth() / (float) mod::kSteps;
        for (int i = 0; i < mod::kSteps; ++i)
        {
            auto cell = Rectangle<float> (sa.getX() + w * (float) i + 1.f, sa.getY(), w - 2.f, sa.getHeight());
            g.setColour (i < len ? col::raised : col::panel2);
            g.fillRoundedRectangle (cell, 3.f);
            const float v = s.steps[(size_t) i];
            g.setColour (i < len ? col::mod.withAlpha (0.8f) : col::faint);
            g.fillRoundedRectangle (cell.withTop (cell.getBottom() - cell.getHeight() * v), 3.f);
        }
    }

    // routes
    g.setColour (col::text);
    g.setFont (font (15.f, true));
    g.drawText ("TARGETS  -  what MOD " + String (sel + 1) + " moves, and how far (drag the bars; double-click resets)",
                routesArea.withHeight (26), Justification::centredLeft);
    if (s.routeCount() == 0)
    {
        g.setColour (col::dim);
        g.setFont (font (14.f));
        g.drawFittedText ("No targets yet. Click + Add target, or right-click a knob anywhere and choose Modulate with > MOD " + String (sel + 1) + ".",
                          routesArea.withTrimmedTop (34).withHeight (40), Justification::centredLeft, 2);
    }
}

void ModPanel::mouseDown (const MouseEvent& e)
{
    for (int m = 0; m < mod::kMods; ++m) if (card (m).contains (e.getPosition())) { select (m); return; }
    const auto& s = proc.getMods()[(size_t) sel];
    if (s.type == mod::Type::Steps && stepArea().contains (e.getPosition()))
    {
        proc.pushUndo();
        mouseDrag (e);
    }
}

void ModPanel::mouseDrag (const MouseEvent& e)
{
    const auto& s = proc.getMods()[(size_t) sel];
    if (s.type != mod::Type::Steps) return;
    const auto sa = stepArea();
    if (! sa.expanded (0, 30).contains (e.getPosition())) return;
    const int i = jlimit (0, mod::kSteps - 1, (int) ((float) (e.x - sa.getX()) / (float) sa.getWidth() * mod::kSteps));
    const float v = jlimit (0.f, 1.f, 1.f - (float) (e.y - sa.getY()) / (float) sa.getHeight());
    edit ([i, v] (mod::ModState& st) { st.steps[(size_t) i] = v; });
}

String ModPanel::getTooltip()
{
    const auto p = getMouseXYRelative();
    for (int m = 0; m < mod::kMods; ++m) if (card (m).contains (p)) return "MOD " + String (m + 1) + " (click to edit)";
    return {};
}
} // namespace ui
