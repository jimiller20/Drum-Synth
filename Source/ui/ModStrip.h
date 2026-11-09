// ModStrip.h - the small modulation panel at the right of the sound section: the six modulators as
// tiles to drag onto any sound knob (a route at 30 %), with a live value, and below them the
// selected channel's routes with their depth. Click a tile to open it on the MOD tab.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ModPanel.h"

namespace ui
{
class ModStrip : public juce::Component, public juce::TooltipClient
{
public:
    explicit ModStrip (Ferrum8Processor& p);
    void setChannel (int ch);
    void refresh();                                   // modulators or routes changed
    void tick();                                      // 30 Hz: live values
    std::function<void (int)> onOpenMod;              // open a modulator on the MOD tab
    std::function<void()> onDepthChanged;             // a depth bar moved (the knobs redraw their rings)
    std::function<void (const juce::String&)> showStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static juce::String modName (const ferrum::mod::ModState& s, int m);

private:
    juce::Rectangle<int> tile (int m) const;
    int tileAt (juce::Point<int> p) const;
    void tileMenu (int m);
    struct Row { int mod = 0, route = 0; };

    Ferrum8Processor& proc;
    int channel = 0, downTile = -1;
    bool dragStarted = false;
    std::vector<Row> rows;
    std::array<DepthBar, 8> bars;
    std::array<IconButton, 8> removeBtns;
    std::array<float, ferrum::mod::kMods> shown {};
    juce::Rectangle<int> tilesArea, routesArea;
};
} // namespace ui
