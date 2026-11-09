// PatternPanel.h - pattern selection (A-L, chains) and the step editor: per-channel lanes
// (trigger / accent / fill / length) or the 8-channel matrix editor.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ui
{
class PatternPanel : public juce::Component, public juce::TooltipClient
{
public:
    explicit PatternPanel (Ferrum8Processor& p);
    void setChannel (int ch) { channel = ch; updateDiceButton(); repaint(); }
    void updateDiceButton();                         // shows the channel's step-variation setting
    void setMatrix (bool m);
    bool isMatrix() const { return matrix; }
    void tick();                                     // called by the editor timer
    void runMenuItem (int id, bool channelOnly);     // also used by keyboard shortcuts

    std::function<void (int)> onSelectChannel;
    std::function<void (int, bool)> onPreview;       // channel, accented
    std::function<void (const juce::String&)> showStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    enum Lane { Trig = 0, Acc, Fill, Expr, Len, NumLanes };
    juce::Rectangle<int> patternButton (int p) const;
    juce::Rectangle<int> laneCell (int lane, int step) const;
    juce::Rectangle<int> exprTab (int lane) const;
    // the expression-lane dice is positional too (wide enough to pick an amount)
    juce::Rectangle<int> exprDice() const { auto t = exprTab (ferrum::kNumExpr - 1); return t.withX (t.getRight() + 6).withWidth (58); }
    void showRerollMenu();                           // per-channel step variations (right-click on the step dice)
    void rollExprLane (bool allChannels, float amount = 1.f);
    int exprValueFromY (const juce::Rectangle<int>& cell, int y) const;
    void editExpr (int step, int value, const juce::ModifierKeys& mods);
    void showExprMenu (int step, const juce::ModifierKeys& mods);
    void paintExpr (juce::Graphics& g, int sel, int len, bool showPlay, int playStep);
    juce::Rectangle<int> matrixCell (int ch, int step) const;
    juce::Rectangle<int> matrixLabel (int ch) const;
    int stepAt (int x) const;
    void showMenu (bool repeatLast, bool channelOnly);
    void editStep (int lane, int step, bool value, const juce::ModifierKeys& mods);
    void exportAudioDialog();
    std::vector<int> targetChannels (bool allUnmuted) const;

    Ferrum8Processor& proc;
    int channel = 0;
    bool matrix = false;
    IconButton menuBtn { IconButton::Icon::Menu }, copyBtn { IconButton::Icon::Copy }, pasteBtn { IconButton::Icon::Paste },
               matrixBtn { IconButton::Icon::Matrix }, diceBtn { IconButton::Icon::Dice, "Roll Steps" },
               rerollBtn { IconButton::Icon::Down }, chainLeft { IconButton::Icon::ChainLeft }, chainRight { IconButton::Icon::ChainRight };
    juce::Rectangle<int> toolArea, patArea, laneArea;

    // drag state
    int dragLane = -1, dragChannel = -1, lastDragStep = -1, dragMatrixRow = -1, reorderFrom = -1, reorderTo = -1;
    bool dragValue = false;
    juce::ModifierKeys dragMods;
    int lastMenuItem = 0;
    bool lastMenuChannelOnly = false;
    int shownStep = -2, shownPattern = -1, blinkPhase = 0;
    int exprDiceHoverX = -1, diceBlink = 0;
    uint32_t lastRerollCount = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace ui
