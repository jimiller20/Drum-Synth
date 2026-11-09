// ModStrip.cpp - see ModStrip.h
// SPDX-License-Identifier: GPL-3.0-only
#include "ModStrip.h"

namespace ui
{
using namespace juce;
using namespace ferrum;

static const char* shortType (mod::Type t)
{
    switch (t)
    {
        case mod::Type::LFO: return "LFO";
        case mod::Type::Envelope: return "ENV";
        case mod::Type::SampleHold: return "S&H";
        case mod::Type::Peak: return "PEAK";
        case mod::Type::Random: return "DRIFT";
        case mod::Type::Steps: return "STEPS";
        case mod::Type::Macro: return "MACRO";
        default: return "OFF";
    }
}

String ModStrip::modName (const mod::ModState& s, int m) { return String (m + 1) + " " + shortType (s.type); }

ModStrip::ModStrip (Ferrum8Processor& p) : proc (p)
{
    for (size_t i = 0; i < bars.size(); ++i)
    {
        addChildComponent (bars[i]);
        addChildComponent (removeBtns[i]);
        removeBtns[i].setButtonText ("x");
        removeBtns[i].setTooltip ("Remove this route");
        bars[i].onGestureStart = [this] { proc.pushUndo(); };
        bars[i].onChange = [this, i] (float v)
        {
            if (i >= rows.size()) return;
            auto ms = proc.getMods();
            ms[(size_t) rows[i].mod].routes[(size_t) rows[i].route].depth = v;
            proc.setMods (ms);
            if (onDepthChanged) onDepthChanged();
        };
        removeBtns[i].onClick = [this, i]
        {
            if (i >= rows.size()) return;
            proc.pushUndo();
            auto ms = proc.getMods();
            ms[(size_t) rows[i].mod].routes[(size_t) rows[i].route] = {};
            proc.setMods (ms);
            proc.sendUiChange();
        };
    }
}

void ModStrip::setChannel (int ch) { channel = ch; refresh(); }

void ModStrip::refresh()
{
    rows.clear();
    const auto& ms = proc.getMods();
    for (int m = 0; m < mod::kMods; ++m)
    {
        if (ms[(size_t) m].type == mod::Type::Off) continue;
        for (int r = 0; r < mod::kRoutes; ++r)
        {
            const auto& rt = ms[(size_t) m].routes[(size_t) r];
            if (rt.kind == mod::Route::Channel && rt.ch == channel) rows.push_back ({ m, r });
        }
    }
    resized();
    repaint();
}

void ModStrip::tick()
{
    bool changed = false;
    for (int m = 0; m < mod::kMods; ++m)
    {
        const float v = proc.getMods()[(size_t) m].type == mod::Type::Off ? 0.f : proc.modValue (m);
        if (std::abs (v - shown[(size_t) m]) > 0.004f) { shown[(size_t) m] = v; changed = true; }
    }
    if (changed) repaint (tilesArea);
}

Rectangle<int> ModStrip::tile (int m) const
{
    const int w = (tilesArea.getWidth() - 6) / 2, h = 40;
    return { tilesArea.getX() + (m % 2) * (w + 6), tilesArea.getY() + (m / 2) * (h + 5), w, h };
}

int ModStrip::tileAt (Point<int> p) const
{
    for (int m = 0; m < mod::kMods; ++m) if (tile (m).contains (p)) return m;
    return -1;
}

void ModStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    r.removeFromTop (18);
    tilesArea = r.removeFromTop (3 * 45);
    r.removeFromTop (6);
    r.removeFromTop (18);
    routesArea = r;
    const int rowH = 22;
    const int maxRows = jmax (0, routesArea.getHeight() / rowH);
    for (size_t i = 0; i < bars.size(); ++i)
    {
        const bool vis = (int) i < (int) rows.size() && (int) i < maxRows;
        bars[i].setVisible (vis);
        removeBtns[i].setVisible (vis);
        if (! vis) continue;
        auto row = Rectangle<int> (routesArea.getX(), routesArea.getY() + (int) i * rowH, routesArea.getWidth(), rowH);
        removeBtns[i].setBounds (row.removeFromRight (16).reduced (0, 3));
        row.removeFromRight (2);
        bars[i].setBounds (row.removeFromRight (58));
        const auto& rt = proc.getMods()[(size_t) rows[i].mod].routes[(size_t) rows[i].route];
        bars[i].setValue (rt.depth);
    }
}

void ModStrip::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (b.reduced (0.5f), 5.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 5.f, 1.f);
    auto top = getLocalBounds().reduced (8, 4).removeFromTop (18);
    g.setFont (font (14.f, true));
    g.setColour (col::mod);
    g.drawText ("MOD", top, Justification::centredLeft);
    g.setFont (font (12.5f));
    g.setColour (col::dim);
    g.drawText ("drag to a knob", top, Justification::centredRight);

    const auto& ms = proc.getMods();
    for (int m = 0; m < mod::kMods; ++m)
    {
        const auto t = tile (m).toFloat();
        const auto& s = ms[(size_t) m];
        const bool off = s.type == mod::Type::Off;
        g.setColour (downTile == m ? col::raised.brighter (0.15f) : col::raised);
        g.fillRoundedRectangle (t, 4.f);
        g.setColour (off ? col::line.brighter (0.1f) : col::mod.withAlpha (0.55f));
        g.drawRoundedRectangle (t.reduced (0.5f), 4.f, 1.f);
        auto txt = t.reduced (6.f, 3.f);
        g.setFont (font (14.f, true));
        g.setColour (off ? col::faint : col::mod);
        g.drawText (String (m + 1), txt.removeFromLeft (12.f), Justification::topLeft);
        g.setColour (off ? col::faint : col::text);
        g.drawText (shortType (s.type), txt.withTrimmedLeft (2.f), Justification::topLeft);
        int n = 0;
        for (const auto& r : s.routes) n += r.kind != mod::Route::None;
        if (n > 0)
        {
            g.setFont (font (12.f));
            g.setColour (col::dim);
            g.drawText (String (n), t.reduced (6.f, 3.f), Justification::topRight);
        }
        // the live value as a bar from the middle (or "= LFO" for an empty slot)
        auto bar = t.reduced (6.f, 0.f).withTrimmedTop (t.getHeight() - 12.f).withHeight (5.f);
        if (off)
        {
            g.setFont (font (11.5f));
            g.setColour (col::faint);
            g.drawText ("drop = LFO", t.reduced (6.f, 2.f), Justification::bottomLeft);
            continue;
        }
        g.setColour (col::bg);
        g.fillRoundedRectangle (bar, 2.f);
        const float v = jlimit (-1.f, 1.f, shown[(size_t) m]);
        const float cx = bar.getCentreX(), x1 = cx + v * bar.getWidth() * 0.5f;
        g.setColour (col::mod);
        g.fillRect (Rectangle<float> (jmin (cx, x1), bar.getY(), jmax (1.f, std::abs (x1 - cx)), bar.getHeight()));
    }

    auto rh = Rectangle<int> (tilesArea.getX(), tilesArea.getBottom() + 6, tilesArea.getWidth(), 18);
    g.setFont (font (13.f, true));
    g.setColour (col::dim);
    g.drawText ("ON CH " + String (channel + 1), rh, Justification::centredLeft);
    if (rows.empty())
    {
        g.setFont (font (12.5f));
        g.setColour (col::faint);
        g.drawFittedText ("No modulation on this channel yet. Drag a tile onto a knob.", routesArea.withHeight (40), Justification::topLeft, 3);
        return;
    }
    const int rowH = 22;
    const int maxRows = jmax (0, routesArea.getHeight() / rowH);
    for (int i = 0; i < (int) rows.size() && i < maxRows; ++i)
    {
        const auto& rt = ms[(size_t) rows[(size_t) i].mod].routes[(size_t) rows[(size_t) i].route];
        auto row = Rectangle<int> (routesArea.getX(), routesArea.getY() + i * rowH, routesArea.getWidth() - 78, rowH);
        g.setFont (font (13.f, true));
        g.setColour (col::mod);
        g.drawText (String (rows[(size_t) i].mod + 1), row.removeFromLeft (11), Justification::centredLeft);
        g.setFont (font (12.5f));
        g.setColour (col::text);
        g.drawFittedText (paramDef (rt.param).label, row, Justification::centredLeft, 1, 0.75f);
    }
    if ((int) rows.size() > maxRows)
    {
        g.setFont (font (12.f));
        g.setColour (col::dim);
        g.drawText ("+" + String ((int) rows.size() - maxRows) + " more on the MOD tab", routesArea.withTrimmedTop (maxRows * rowH).withHeight (16), Justification::centredLeft);
    }
}

void ModStrip::mouseDown (const MouseEvent& e)
{
    downTile = tileAt (e.getPosition());
    dragStarted = false;
    if (downTile >= 0 && e.mods.isPopupMenu()) { const int m = downTile; downTile = -1; tileMenu (m); return; }
    repaint (tilesArea);
}

void ModStrip::mouseDrag (const MouseEvent& e)
{
    if (downTile < 0 || dragStarted || e.getDistanceFromDragStart() < 5) return;
    if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        const auto t = tile (downTile);
        Image img (Image::ARGB, t.getWidth(), t.getHeight(), true);
        {
            Graphics g (img);
            g.setColour (col::mod.withAlpha (0.85f));
            g.fillRoundedRectangle (img.getBounds().toFloat(), 5.f);
            g.setColour (Colours::black);
            g.setFont (font (15.f, true));
            g.drawText (modName (proc.getMods()[(size_t) downTile], downTile), img.getBounds(), Justification::centred);
        }
        c->startDragging ("ferrum-mod:" + String (downTile), this, ScaledImage (img), true);
        if (showStatus) showStatus ("Drop MOD " + String (downTile + 1) + " on a sound knob to modulate it");
    }
}

void ModStrip::mouseUp (const MouseEvent& e)
{
    const int m = downTile;
    downTile = -1;
    repaint (tilesArea);
    if (m >= 0 && ! dragStarted && tile (m).contains (e.getPosition()) && ! e.mods.isPopupMenu() && onOpenMod) onOpenMod (m);
}

void ModStrip::tileMenu (int m)
{
    PopupMenu menu, type;
    const auto& s = proc.getMods()[(size_t) m];
    menu.addSectionHeader ("MOD " + String (m + 1));
    menu.addItem (1, "Open On The MOD Tab");
    for (int t = 0; t < (int) mod::Type::Count; ++t) type.addItem (100 + t, mod::kTypeNames[t], true, (int) s.type == t);
    menu.addSubMenu ("Type", type);
    menu.addItem (2, "Remove Its Routes On Channel " + String (channel + 1));
    SafePointer<ModStrip> safe (this);
    menu.showMenuAsync (PopupMenu::Options(), [safe, m] (int id)
    {
        if (safe == nullptr || id <= 0) return;
        auto& self = *safe;
        if (id == 1) { if (self.onOpenMod) self.onOpenMod (m); return; }
        self.proc.pushUndo();
        auto ms = self.proc.getMods();
        if (id == 2)
        {
            for (auto& r : ms[(size_t) m].routes) if (r.kind == mod::Route::Channel && r.ch == self.channel) r = {};
        }
        else if (id >= 100)
        {
            const auto keep = ms[(size_t) m].routes;
            ms[(size_t) m] = mod::defaultState ((mod::Type) (id - 100));
            ms[(size_t) m].routes = keep;
        }
        self.proc.setMods (ms);
        self.proc.sendUiChange();
    });
}

String ModStrip::getTooltip()
{
    const auto pos = getMouseXYRelative();
    const int m = tileAt (pos);
    if (m >= 0)
        return "MOD " + String (m + 1) + " (" + mod::kTypeNames[(int) proc.getMods()[(size_t) m].type] + "): drag onto a sound knob to "
               "modulate it (starts at 30 %), click to open it on the MOD tab, right-click for its type";
    if (routesArea.contains (pos)) return "Drag a depth bar to set how far the modulator moves the knob (Shift: fine, double-click: 0)";
    return {};
}
} // namespace ui
