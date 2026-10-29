// FxRackPanel.h - the FX tab: three buses (FX 1, FX 2, Master). Each bus shows its three
// parallel lanes of five series slots, the dry level, output gain and destination, which
// channels are sent to it, ready-made chains and the chain dice. Below the grid sits the
// editor for the selected module.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"
#include "../fx/FxChains.h"

namespace ui
{
class FxRackPanel : public juce::Component, public juce::TooltipClient
{
public:
    explicit FxRackPanel (Ferrum8Processor& p);
    void refresh();                  // processor state changed (preset, undo, kit)
    void tick();                     // 30 Hz: meters
    void showBus (int bus);
    int getBus() const { return bus; }
    std::function<void (const juce::String&)> showStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static juce::Colour categoryColour (const char* category);

private:
    struct Hit { enum Kind { None, BusTab, Send, Slot, Power, LaneMute } kind = None; int a = -1, b = -1; };
    Hit findHit (juce::Point<int> p) const;
    juce::Rectangle<int> busTab (int b) const;
    juce::Rectangle<int> sendChip (int ch) const;
    juce::Rectangle<int> laneArea (int lane) const;
    juce::Rectangle<int> slotArea (int lane, int slot) const;
    juce::Rectangle<int> powerArea (int lane, int slot) const;
    juce::Rectangle<int> muteArea (int lane) const;
    juce::Point<float> sumNode() const;

    void select (int lane, int slot);
    void syncHeader();
    void syncEditor();
    void layoutEditor();
    void edited (bool structural);   // marks the preset dirty; structural changes also refresh the UI
    void pickModule (int lane, int slot);
    void slotMenu (int lane, int slot);
    void setModule (int lane, int slot, ferrum::fx::Type t);
    void moveSlot (int fromLane, int fromSlot, int toLane, int toSlot, bool copy);
    void showChainMenu();
    void rollChain();
    void applyBus (const ferrum::fx::RackState::Bus& b, const juce::String& what);
    void toggleSend (int ch);
    juce::String slotSummary (int lane, int slot, int line) const;
    juce::String routedChannels (int b) const;

    Ferrum8Processor& proc;
    ferrum::fx::FxRack& rack;
    int bus = 0, selLane = 0, selSlot = -1;
    Hit hover;
    // dragging a card
    int dragLane = -1, dragSlot = -1, dropLane = -1, dropSlot = -1;
    bool dragging = false;
    juce::Point<int> dragPos;

    Knob dryKnob, outKnob;
    Segmented outputSel;
    IconButton chainsBtn { IconButton::Icon::Down, "Chains" }, diceBtn { IconButton::Icon::Dice, "Roll Chain" };
    std::array<Knob, ferrum::fx::kLanes> laneKnobs;
    // module editor
    std::array<Knob, ferrum::fx::kMaxParams> pKnobs;
    std::array<ChoiceBox, ferrum::fx::kMaxParams> pChoices;
    IconButton bypassBtn { IconButton::Icon::None, "Bypass" }, leftBtn { IconButton::Icon::Prev }, rightBtn { IconButton::Icon::Next },
               resetBtn { IconButton::Icon::None, "Reset" }, removeBtn { IconButton::Icon::None, "Remove" }, swapBtn { IconButton::Icon::Down, "Change" };
    std::array<float, ferrum::fx::kBuses> meters {};
    uint32_t diceSeed = 0x5eed1234u;
    juce::Rectangle<int> header, grid, editor, editorInfo;
};
} // namespace ui
