// PatternPanel.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PatternPanel.h"
#include "Clipboard.h"

namespace ui
{
using namespace juce;
using namespace ferrum;

static const char* const kLetters = "abcdefghijkl";
static constexpr int kLabelW = 40, kGroupGap = 10;

PatternPanel::PatternPanel (Ferrum8Processor& p) : proc (p)
{
    for (auto* b : { &menuBtn, &copyBtn, &pasteBtn, &matrixBtn, &diceBtn, &chainLeft, &chainRight }) addAndMakeVisible (b);
    menuBtn.setTooltip ("Pattern menu (hold Ctrl for the selected channel only, Shift-click repeats the last item)");
    copyBtn.setTooltip ("Copy pattern (Ctrl: selected channel only)");
    pasteBtn.setTooltip ("Paste pattern");
    matrixBtn.setTooltip ("Matrix editor: all eight channels at once");
    diceBtn.setTooltip ("Roll new steps for the selected channel in this pattern (uses the Dice style). Shift: the whole beat");
    diceBtn.onClick = [this]
    {
        const auto msg = ModifierKeys::currentModifiers.isShiftDown() ? proc.rollBeat() : proc.rollChannelSteps (channel);
        if (showStatus) showStatus (msg);
        repaint();
    };
    chainLeft.setTooltip ("Chain from the previous pattern");
    chainRight.setTooltip ("Chain to the next pattern");
    matrixBtn.setClickingTogglesState (true);
    matrixBtn.onClick = [this] { setMatrix (matrixBtn.getToggleState()); };
    menuBtn.onClick = [this]
    {
        const auto m = ModifierKeys::currentModifiers;
        showMenu (m.isShiftDown(), m.isCtrlDown() || m.isCommandDown() || m.isAltDown());
    };
    copyBtn.onClick = [this] { const auto m = ModifierKeys::currentModifiers; runMenuItem (2, m.isCtrlDown() || m.isCommandDown() || m.isAltDown()); };
    pasteBtn.onClick = [this] { runMenuItem (3, Clipboard::pattern && Clipboard::pattern->channelOnly); };
    chainLeft.onClick = [this]
    {
        const int sel = proc.getSelectedPattern();
        if (sel == 0) return;
        proc.pushUndo();
        proc.getEngine().patterns.setChainNext (sel - 1, ! proc.getEngine().patterns.chainNext (sel - 1));
        proc.presetDirty = true;
        repaint();
    };
    chainRight.onClick = [this]
    {
        const int sel = proc.getSelectedPattern();
        if (sel >= kNumPatterns - 1) return;
        proc.pushUndo();
        proc.getEngine().patterns.setChainNext (sel, ! proc.getEngine().patterns.chainNext (sel));
        proc.presetDirty = true;
        repaint();
    };
    matrix = proc.matrixView;
    matrixBtn.setToggleState (matrix, dontSendNotification);
}

void PatternPanel::setMatrix (bool m)
{
    matrix = m;
    proc.matrixView = m;
    matrixBtn.setToggleState (m, dontSendNotification);
    repaint();
}

void PatternPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    toolArea = r.removeFromLeft (34);
    menuBtn.setBounds (toolArea.getX(), toolArea.getY() + 2, 32, 30);
    copyBtn.setBounds (toolArea.getX(), toolArea.getY() + 38, 32, 30);
    pasteBtn.setBounds (toolArea.getX(), toolArea.getY() + 74, 32, 30);
    matrixBtn.setBounds (toolArea.getX(), toolArea.getY() + 110, 32, 30);
    diceBtn.setBounds (toolArea.getX(), toolArea.getY() + 146, 32, 30);
    r.removeFromLeft (12);
    patArea = r.removeFromLeft (4 * 34);
    chainLeft.setBounds (patArea.getX(), patArea.getY() + 3 * 34 + 6, 4 * 34 / 2 - 2, 28);
    chainRight.setBounds (patArea.getX() + 4 * 34 / 2 + 2, patArea.getY() + 3 * 34 + 6, 4 * 34 / 2 - 4, 28);
    r.removeFromLeft (18);
    laneArea = r;
}

Rectangle<int> PatternPanel::patternButton (int p) const
{
    return { patArea.getX() + (p % 4) * 34, patArea.getY() + (p / 4) * 34, 31, 31 };
}

int PatternPanel::stepAt (int x) const
{
    for (int s = 0; s < kMaxSteps; ++s)
    {
        auto c = matrix ? matrixCell (0, s) : laneCell (Trig, s);
        if (x >= c.getX() - 3 && x < c.getRight() + 3) return s;
    }
    return -1;
}

Rectangle<int> PatternPanel::laneCell (int lane, int step) const
{
    const int x0 = laneArea.getX() + kLabelW;
    const float stepW = (float) (laneArea.getWidth() - kLabelW - 3 * kGroupGap) / (float) kMaxSteps;
    const int x = x0 + (int) (step * stepW) + (step / 4) * kGroupGap;
    const int w = (int) stepW - 6;
    static const int ys[NumLanes] = { 4, 52, 72, 120, 180 }, hs[NumLanes] = { 42, 14, 14, 50, 12 };
    return { x, laneArea.getY() + ys[lane], w, hs[lane] };
}

Rectangle<int> PatternPanel::exprTab (int lane) const
{
    constexpr int tabW = 78;
    return { laneArea.getX() + kLabelW + lane * (tabW + 4), laneArea.getY() + 95, tabW, 19 };
}

int PatternPanel::exprValueFromY (const Rectangle<int>& cell, int y) const
{
    const auto& info = exprInfo (proc.exprLane);
    const float frac = jlimit (0.f, 1.f, 1.f - (float) (y - cell.getY()) / (float) jmax (1, cell.getHeight()));
    if (proc.exprLane == X_CHANCE) return jlimit (0, 7, roundToInt ((1.f - frac) * 8.f)); // top = always
    return jlimit (info.min, info.max, roundToInt ((float) info.min + frac * (float) (info.max - info.min)));
}

Rectangle<int> PatternPanel::matrixCell (int ch, int step) const
{
    const int x0 = laneArea.getX() + kLabelW;
    const float stepW = (float) (laneArea.getWidth() - kLabelW - 3 * kGroupGap) / (float) kMaxSteps;
    const int x = x0 + (int) (step * stepW) + (step / 4) * kGroupGap;
    const float rowH = (float) (laneArea.getHeight() - 12) / (float) kNumChannels;
    const int row = kNumChannels - 1 - ch; // channel 8 at the top
    return { x, laneArea.getY() + 4 + (int) (row * rowH), (int) stepW - 6, (int) rowH - 4 };
}

Rectangle<int> PatternPanel::matrixLabel (int ch) const
{
    auto c = matrixCell (ch, 0);
    return { laneArea.getX(), c.getY(), kLabelW - 8, c.getHeight() };
}

void PatternPanel::tick()
{
    auto& e = proc.getEngine();
    const int step = e.uiStep.load(), pat = e.uiPlayingPattern.load();
    ++blinkPhase;
    if (step != shownStep || pat != shownPattern || (blinkPhase % 8) == 0)
    {
        shownStep = step; shownPattern = pat;
        repaint();
    }
}

void PatternPanel::paint (Graphics& g)
{
    g.fillAll (col::panel);
    auto& eng = proc.getEngine();
    const auto& pb = eng.patterns;
    const int sel = proc.getSelectedPattern();
    const int state = eng.uiState.load();
    const bool running = state == 2 || state == 3;
    const int playPat = eng.uiPlayingPattern.load();
    const int playStep = eng.uiStep.load();
    int chainFirst, chainLast;
    pb.chainRange (sel, chainFirst, chainLast);

    // pattern buttons
    for (int p = 0; p < kNumPatterns; ++p)
    {
        auto b = patternButton (p).toFloat();
        const bool isSel = p == sel, isPlaying = running && p == playPat;
        g.setColour (isSel ? col::select : col::raised);
        g.fillRoundedRectangle (b, 4.f);
        if (isPlaying)
        {
            g.setColour (col::play.withAlpha ((blinkPhase / 4) % 2 == 0 ? 1.f : 0.55f));
            g.drawRoundedRectangle (b.reduced (1.f), 4.f, 2.5f);
        }
        else if (p == playPat && state == 1)
        {
            g.setColour (col::play.withAlpha (0.4f));
            g.drawRoundedRectangle (b.reduced (1.f), 4.f, 1.5f);
        }
        const bool inChain = chainFirst != chainLast && p >= chainFirst && p <= chainLast;
        g.setColour (isSel ? Colours::black : (pb.isEmpty (p) ? col::faint : (inChain ? col::select.brighter (0.3f) : col::text)));
        g.setFont (font (19.f, true));
        g.drawText (String::charToString ((juce_wchar) kLetters[p]), b.toNearestInt(), Justification::centred);
        if (pb.chainNext (p) && p % 4 != 3)
        {
            g.setColour (col::select.brighter (0.2f));
            g.fillRect (b.getRight(), b.getCentreY() - 1.5f, 3.f, 3.f);
        }
    }
    chainLeft.setToggleState (sel > 0 && pb.chainNext (sel - 1), dontSendNotification);
    chainRight.setToggleState (pb.chainNext (sel), dontSendNotification);
    chainLeft.setAccent (col::select); chainRight.setAccent (col::select);

    const int len = pb.length (sel);
    const bool showPlay = running && playPat == sel && playStep >= 0;

    if (! matrix)
    {
        const auto cc = col::channel (channel);
        g.setFont (font (14.5f, true));
        g.setColour (col::dim);
        static const char* names[NumLanes] = { "TRIG", "ACC", "FILL", "", "LEN" };
        for (int l = 0; l < NumLanes; ++l)
        {
            auto c = laneCell (l, 0);
            g.drawText (names[l], laneArea.getX(), c.getY(), kLabelW - 4, c.getHeight(), Justification::centredLeft);
        }
        for (int s = 0; s < kMaxSteps; ++s)
        {
            const bool inLen = s < len;
            const float a = inLen ? 1.f : 0.35f;
            const bool t = pb.trig (sel, channel, s), ac = pb.acc (sel, channel, s), fi = pb.fill (sel, channel, s);
            auto tr = laneCell (Trig, s).toFloat();
            const int ghost = pb.expr (sel, channel, X_GHOST, s);
            static const float ghostAlpha[4] = { 1.f, 0.78f, 0.6f, 0.44f };
            g.setColour ((t ? cc.withMultipliedBrightness (ac ? 1.1f : 0.8f).withMultipliedAlpha (ghostAlpha[jlimit (0, 3, ghost)]) : col::raised).withMultipliedAlpha (a));
            g.fillRoundedRectangle (tr, 6.f);
            if (t) // small tags: ghost level, chance, roll
            {
                String tag;
                for (int l : { (int) X_GHOST, (int) X_CHANCE, (int) X_ROLL })
                {
                    const auto txt = exprValueText (l, pb.expr (sel, channel, l, s), true);
                    if (txt.empty()) continue;
                    if (tag.isNotEmpty()) tag << " ";
                    tag << txt;
                }
                if (tag.isNotEmpty())
                {
                    g.setColour (Colours::black.withAlpha (0.7f * a));
                    g.setFont (font (11.5f, true));
                    g.drawText (tag, tr.reduced (4.f, 2.f).toNearestInt(), Justification::topLeft, false);
                }
            }
            if (t && ac) { g.setColour (Colours::white.withAlpha (0.85f * a)); g.fillEllipse (tr.getCentreX() - 3.f, tr.getY() + 6.f, 6.f, 6.f); }
            if (t && fi)
            {
                g.setColour (Colours::black.withAlpha (0.6f));
                for (int k = 0; k < 3; ++k) g.fillEllipse (tr.getX() + tr.getWidth() * (0.3f + 0.2f * k) - 2.f, tr.getBottom() - 10.f, 4.f, 4.f);
            }
            if (showPlay && s == playStep)
            {
                g.setColour (col::play);
                g.drawRoundedRectangle (tr.reduced (0.5f), 6.f, 2.5f);
            }
            auto acR = laneCell (Acc, s).toFloat();
            g.setColour ((ac ? col::accent : col::raised).withMultipliedAlpha (a * (t || ! ac ? 1.f : 0.45f)));
            g.fillRoundedRectangle (acR, 3.f);
            auto fR = laneCell (Fill, s).toFloat();
            g.setColour ((fi ? col::accent.withRotatedHue (0.08f) : col::raised).withMultipliedAlpha (a * (t || ! fi ? 1.f : 0.45f)));
            g.fillRoundedRectangle (fR, 3.f);
            auto lR = laneCell (Len, s).toFloat();
            g.setColour (showPlay && s == playStep ? col::play : (inLen ? col::select.withAlpha (0.85f) : col::raised));
            g.fillRoundedRectangle (lR, 3.f);
            g.setColour (inLen ? col::dim : col::faint);
            g.setFont (font (13.f));
            g.drawText (String (s + 1), lR.translated (0, 15).toNearestInt(), Justification::centred);
        }
        paintExpr (g, sel, len, showPlay, playStep);
    }
    else
    {
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            const auto cc = col::channel (ch);
            auto lab = matrixLabel (ch);
            const bool muted = proc.apvts.getRawParameterValue ("mute" + String (ch + 1))->load() > 0.5f || eng.uiMidiMute[ch].load();
            if (ch == channel) { g.setColour (cc.withAlpha (0.18f)); g.fillRoundedRectangle (lab.toFloat().withRight ((float) laneArea.getRight()), 3.f); }
            g.setColour (muted ? col::mute : cc);
            g.setFont (font (16.f, true));
            g.drawText (String (ch + 1), lab.withTrimmedRight (12), Justification::centredLeft);
            if (proc.getPatchValue (ch, P_CHOKE) > 0.5f)
            {
                g.setColour (col::dim);
                auto cr = lab.removeFromRight (12).toFloat().reduced (1.f, 4.f);
                g.drawLine (cr.getX(), cr.getY(), cr.getRight(), cr.getBottom(), 1.3f);
                g.drawLine (cr.getX(), cr.getY(), cr.getX(), cr.getBottom(), 1.3f);
            }
            for (int s = 0; s < kMaxSteps; ++s)
            {
                auto c = matrixCell (ch, s).toFloat();
                const bool inLen = s < len;
                const bool t = pb.trig (sel, ch, s), ac = pb.acc (sel, ch, s), fi = pb.fill (sel, ch, s);
                const float a = inLen ? 1.f : 0.3f;
                if (t)
                {
                    static const float ghostAlpha[4] = { 1.f, 0.75f, 0.55f, 0.4f };
                    const int ghost = jlimit (0, 3, pb.expr (sel, ch, X_GHOST, s));
                    g.setColour (cc.withMultipliedBrightness (ac ? 1.15f : 0.75f).withMultipliedAlpha (a * ghostAlpha[ghost]));
                    g.fillRoundedRectangle (ac ? c : c.reduced (2.f, 2.f + (float) ghost), 4.f);
                    if (pb.expr (sel, ch, X_ROLL, s) > 0 || pb.expr (sel, ch, X_CHANCE, s) > 0)
                    {
                        g.setColour (Colours::black.withAlpha (0.55f));
                        g.fillEllipse (c.getRight() - 7.f, c.getY() + 3.f, 4.f, 4.f);
                    }
                    if (fi)
                    {
                        g.setColour (Colours::black.withAlpha (0.65f));
                        for (int k = 0; k < 3; ++k) g.fillEllipse (c.getX() + c.getWidth() * (0.3f + 0.2f * k) - 2.f, c.getCentreY() - 2.f, 4.f, 4.f);
                    }
                }
                else
                {
                    g.setColour (col::raised.withMultipliedAlpha (a));
                    g.fillRoundedRectangle (c.reduced (2.f, 2.f), 4.f);
                }
                if (showPlay && s == playStep)
                {
                    g.setColour (col::play.withAlpha (0.8f));
                    g.drawRoundedRectangle (c, 4.f, 1.5f);
                }
            }
        }
        if (reorderFrom >= 0 && reorderTo >= 0 && reorderTo != reorderFrom)
        {
            auto r = matrixLabel (reorderTo);
            g.setColour (col::accent);
            g.drawRect (r.withRight (laneArea.getRight()), 2);
        }
    }
}

void PatternPanel::paintExpr (Graphics& g, int sel, int len, bool showPlay, int playStep)
{
    const auto& pb = proc.getEngine().patterns;
    const int lane = proc.exprLane;
    const auto& info = exprInfo (lane);
    const auto cc = col::channel (channel);

    // lane tabs; a dot marks lanes this channel uses in the pattern
    for (int l = 0; l < kNumExpr; ++l)
    {
        auto r = exprTab (l).toFloat();
        const bool on = l == lane;
        g.setColour (on ? cc.withAlpha (0.9f) : col::raised);
        g.fillRoundedRectangle (r, 4.f);
        g.setColour (on ? Colours::black : col::dim);
        g.setFont (font (12.5f, true));
        g.drawText (exprInfo (l).name, r.toNearestInt(), Justification::centred);
        bool used = false;
        for (int s = 0; s < kMaxSteps; ++s) used |= pb.expr (sel, channel, l, s) != 0;
        if (used && ! on) { g.setColour (cc); g.fillEllipse (r.getRight() - 9.f, r.getY() + 4.f, 5.f, 5.f); }
    }
    {
        auto dr = exprDice().toFloat();
        g.setColour (col::raised);
        g.fillRoundedRectangle (dr, 4.f);
        IconButton::drawIcon (g, IconButton::Icon::Dice, dr.withSizeKeepingCentre (14.f, 14.f), cc);
    }
    auto helpR = Rectangle<int> (exprDice().getRight() + 12, exprTab (0).getY(), 0, exprTab (0).getHeight());
    helpR.setRight (laneArea.getRight());
    g.setColour (col::faint);
    g.setFont (font (12.5f));
    g.drawText (info.help, helpR, Justification::centredLeft, true);

    auto c0 = laneCell (Expr, 0);
    g.setColour (col::dim);
    g.setFont (font (11.f, true));
    if (info.bipolar)
    {
        g.drawText ("+", laneArea.getX(), c0.getY() - 2, kLabelW - 10, 14, Justification::centredRight);
        g.drawText ("-", laneArea.getX(), c0.getBottom() - 12, kLabelW - 10, 14, Justification::centredRight);
    }

    for (int s = 0; s < kMaxSteps; ++s)
    {
        auto r = laneCell (Expr, s).toFloat();
        const bool inLen = s < len;
        const bool t = pb.trig (sel, channel, s);
        const int v = pb.expr (sel, channel, lane, s);
        const float a = (inLen ? 1.f : 0.35f) * (t ? 1.f : 0.45f);
        g.setColour (col::raised.withMultipliedAlpha (inLen ? 1.f : 0.5f));
        g.fillRoundedRectangle (r, 3.f);
        const Colour bar = (t ? cc : col::dim).withMultipliedAlpha (a);
        g.setColour (bar);
        if (info.bipolar)
        {
            const float mid = r.getCentreY();
            g.setColour (col::faint.withMultipliedAlpha (0.6f));
            g.fillRect (r.getX() + 2.f, mid - 0.5f, r.getWidth() - 4.f, 1.f);
            g.setColour (bar);
            const float hh = (r.getHeight() * 0.5f - 2.f) * (float) v / (float) info.max;
            if (v > 0) g.fillRoundedRectangle (r.getX() + 3.f, mid - hh, r.getWidth() - 6.f, hh, 2.f);
            else if (v < 0) g.fillRoundedRectangle (r.getX() + 3.f, mid, r.getWidth() - 6.f, -hh, 2.f);
        }
        else if (lane == X_CHANCE)
        {
            if (v >= 1 && v <= 7)
            {
                const float h = (r.getHeight() - 4.f) * (float) (8 - v) / 8.f;
                g.fillRoundedRectangle (r.getX() + 3.f, r.getBottom() - 2.f - h, r.getWidth() - 6.f, h, 2.f);
            }
            else if (v >= 8) g.drawRoundedRectangle (r.reduced (2.f), 3.f, 1.5f);
        }
        else if (v > 0)
        {
            const float h = (r.getHeight() - 4.f) * (float) v / (float) info.max;
            g.fillRoundedRectangle (r.getX() + 3.f, r.getBottom() - 2.f - h, r.getWidth() - 6.f, h, 2.f);
        }
        if (v != 0)
        {
            g.setColour ((t ? col::text : col::dim).withMultipliedAlpha (inLen ? 1.f : 0.5f));
            g.setFont (font (12.f, true));
            g.drawText (exprValueText (lane, v, true), r.toNearestInt().removeFromTop (16), Justification::centred);
        }
        if (showPlay && s == playStep)
        {
            g.setColour (col::play.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.f, 1.5f);
        }
    }
}

void PatternPanel::editExpr (int step, int value, const ModifierKeys& mods)
{
    auto& pb = proc.getEngine().patterns;
    const int sel = proc.getSelectedPattern();
    for (int ch : targetChannels (mods.isShiftDown())) pb.setExpr (sel, ch, proc.exprLane, step, value);
    proc.presetDirty = true;
    if (showStatus)
        showStatus (String ("Step ") + String (step + 1) + "  " + exprInfo (proc.exprLane).name + ": "
                    + exprValueText (proc.exprLane, pb.expr (sel, channel, proc.exprLane, step)));
    repaint();
}

void PatternPanel::showExprMenu (int step, const ModifierKeys& mods)
{
    const int lane = proc.exprLane;
    const auto& info = exprInfo (lane);
    const int cur = proc.getEngine().patterns.expr (proc.getSelectedPattern(), channel, lane, step);
    PopupMenu m;
    m.addSectionHeader (String (info.name) + "  -  step " + String (step + 1));
    for (int v = info.max; v >= info.min; --v)
        m.addItem (1000 + v - info.min, exprValueText (lane, v), true, v == cur);
    m.addSeparator();
    m.addItem (1, "Clear This Lane (channel " + String (channel + 1) + ")");
    m.addItem (2, "Copy Step To Every Step With A Trigger");
    SafePointer<PatternPanel> safe (this);
    m.showMenuAsync (PopupMenu::Options(), [safe, step, mods, lane, info] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& self = *safe;
        auto& pb = self.proc.getEngine().patterns;
        const int sel = self.proc.getSelectedPattern();
        self.proc.pushUndo();
        if (r >= 1000) { self.editExpr (step, r - 1000 + info.min, mods); return; }
        const int v = pb.expr (sel, self.channel, lane, step);
        for (int ch : self.targetChannels (mods.isShiftDown()))
            for (int s = 0; s < kMaxSteps; ++s)
            {
                if (r == 1) pb.setExpr (sel, ch, lane, s, 0);
                else if (pb.trig (sel, ch, s)) pb.setExpr (sel, ch, lane, s, v);
            }
        self.proc.presetDirty = true;
        self.repaint();
    });
}

void PatternPanel::rollExprLane (bool allChannels)
{
    proc.pushUndo();
    const int sel = proc.getSelectedPattern();
    PatternData d = proc.getPattern (sel);
    const auto roles = proc.channelRoles();
    for (int ch : targetChannels (allChannels))
        if (! proc.locked[(size_t) ch]) proc.randomizer.randomizeExpressionLane (d, ch, roles[(size_t) ch], proc.exprLane, 0.5f + proc.dice.expression);
    proc.setPattern (sel, d);
    if (showStatus) showStatus (String ("Rolled the ") + exprInfo (proc.exprLane).name + " lane");
    repaint();
}

String PatternPanel::getTooltip()
{
    if (matrix) return {};
    const auto pos = getMouseXYRelative();
    for (int l = 0; l < kNumExpr; ++l)
        if (exprTab (l).contains (pos)) return exprInfo (l).help;
    if (exprDice().contains (pos)) return String ("Roll this ") + exprInfo (proc.exprLane).name + " lane for the selected channel (Shift: every unmuted channel)";
    for (int s = 0; s < kMaxSteps; ++s)
        if (laneCell (Expr, s).contains (pos))
            return String (exprInfo (proc.exprLane).name) + ": drag up / down to draw values across steps, double-click resets, "
                   "right-click for a list. Shift edits every unmuted channel.";
    if (laneCell (Trig, 0).withRight (laneArea.getRight()).contains (pos))
        return "Click / drag to set triggers (Ctrl: with accent, Shift: every unmuted channel)";
    return {};
}

std::vector<int> PatternPanel::targetChannels (bool allUnmuted) const
{
    std::vector<int> v;
    if (! allUnmuted) { v.push_back (channel); return v; }
    for (int c = 0; c < kNumChannels; ++c)
        if (proc.apvts.getRawParameterValue ("mute" + String (c + 1))->load() < 0.5f) v.push_back (c);
    return v;
}

void PatternPanel::editStep (int lane, int step, bool value, const ModifierKeys& mods)
{
    auto& pb = proc.getEngine().patterns;
    const int sel = proc.getSelectedPattern();
    if (lane == Len) { pb.setLength (sel, step + 1); proc.presetDirty = true; repaint(); return; }
    const bool accentToo = mods.isCtrlDown() || mods.isCommandDown() || mods.isAltDown();
    for (int ch : targetChannels (mods.isShiftDown()))
    {
        if (lane == Trig) { pb.setTrig (sel, ch, step, value); if (accentToo) pb.setAcc (sel, ch, step, value); }
        else if (lane == Acc) pb.setAcc (sel, ch, step, value);
        else if (lane == Fill) pb.setFill (sel, ch, step, value);
    }
    proc.presetDirty = true;
    repaint();
}

void PatternPanel::mouseDown (const MouseEvent& e)
{
    const auto pos = e.getPosition();
    auto& pb = proc.getEngine().patterns;
    const int sel = proc.getSelectedPattern();
    dragLane = -1; dragMatrixRow = -1; reorderFrom = reorderTo = -1;

    for (int p = 0; p < kNumPatterns; ++p)
        if (patternButton (p).contains (pos))
        {
            proc.selectPattern (p);
            if (e.mods.isPopupMenu()) showMenu (false, false);
            repaint();
            return;
        }

    if (! matrix)
    {
        for (int l = 0; l < kNumExpr; ++l)
            if (exprTab (l).contains (pos)) { proc.exprLane = l; repaint(); return; }
        if (exprDice().contains (pos)) { rollExprLane (e.mods.isShiftDown()); return; }
        for (int s = 0; s < kMaxSteps; ++s)
            if (laneCell (Expr, s).expanded (3, 3).contains (pos))
            {
                if (e.mods.isPopupMenu()) { showExprMenu (s, e.mods); return; }
                proc.pushUndo();
                dragLane = Expr; lastDragStep = s; dragMods = e.mods;
                editExpr (s, e.getNumberOfClicks() >= 2 ? 0 : exprValueFromY (laneCell (Expr, s), pos.y), e.mods);
                return;
            }
        for (int l = 0; l < NumLanes; ++l)
            for (int s = 0; s < kMaxSteps; ++s)
                if (l != Expr && laneCell (l, s).expanded (3, 3).contains (pos))
                {
                    proc.pushUndo();
                    dragLane = l; lastDragStep = s; dragMods = e.mods;
                    if (l == Trig) dragValue = ! pb.trig (sel, channel, s);
                    else if (l == Acc) dragValue = ! pb.acc (sel, channel, s);
                    else if (l == Fill) dragValue = ! pb.fill (sel, channel, s);
                    editStep (l, s, dragValue, e.mods);
                    return;
                }
        return;
    }

    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        if (matrixLabel (ch).contains (pos))
        {
            if (e.mods.isShiftDown())
            {
                auto* mp = proc.apvts.getParameter ("mute" + String (ch + 1));
                mp->setValueNotifyingHost (mp->getValue() > 0.5f ? 0.f : 1.f);
            }
            else if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            {
                bool soloed = true;
                for (int c = 0; c < kNumChannels; ++c)
                    if ((proc.apvts.getRawParameterValue ("mute" + String (c + 1))->load() > 0.5f) != (c != ch)) soloed = false;
                for (int c = 0; c < kNumChannels; ++c)
                    proc.apvts.getParameter ("mute" + String (c + 1))->setValueNotifyingHost (soloed ? 0.f : (c == ch ? 0.f : 1.f));
            }
            else
            {
                if (onSelectChannel) onSelectChannel (ch);
                if (onPreview) onPreview (ch, e.mods.isAltDown());
                reorderFrom = ch;
            }
            repaint();
            return;
        }
        for (int s = 0; s < kMaxSteps; ++s)
            if (matrixCell (ch, s).expanded (2, 2).contains (pos))
            {
                proc.pushUndo();
                dragMatrixRow = ch; lastDragStep = s; dragMods = e.mods;
                const bool accentMode = e.mods.isCtrlDown() || e.mods.isCommandDown();
                const bool fillMode = e.mods.isAltDown();
                if (accentMode)
                {
                    dragValue = ! (pb.trig (sel, ch, s) && pb.acc (sel, ch, s));
                    if (dragValue) pb.setTrig (sel, ch, s, true);
                    pb.setAcc (sel, ch, s, dragValue);
                }
                else if (fillMode)
                {
                    dragValue = ! (pb.trig (sel, ch, s) && pb.fill (sel, ch, s));
                    if (dragValue) pb.setTrig (sel, ch, s, true);
                    pb.setFill (sel, ch, s, dragValue);
                }
                else
                {
                    dragValue = ! pb.trig (sel, ch, s);
                    pb.setTrig (sel, ch, s, dragValue);
                }
                if (onSelectChannel && ch != channel) onSelectChannel (ch);
                proc.presetDirty = true;
                repaint();
                return;
            }
    }
}

void PatternPanel::mouseDrag (const MouseEvent& e)
{
    const auto pos = e.getPosition();
    auto& pb = proc.getEngine().patterns;
    const int sel = proc.getSelectedPattern();
    if (dragLane == Expr) // draw values: every step crossed takes the value under the mouse
    {
        const int s = stepAt (pos.x);
        if (s < 0) return;
        const int v = exprValueFromY (laneCell (Expr, s), pos.y);
        const int a = lastDragStep < 0 ? s : jmin (s, lastDragStep), b = lastDragStep < 0 ? s : jmax (s, lastDragStep);
        for (int k = a; k <= b; ++k) editExpr (k, v, dragMods);
        lastDragStep = s;
        return;
    }
    if (dragLane >= 0)
    {
        const int s = stepAt (pos.x);
        if (s < 0 || s == lastDragStep) return;
        const int a = jmin (s, lastDragStep), b = jmax (s, lastDragStep);
        for (int k = a; k <= b; ++k) editStep (dragLane, k, dragValue, dragMods);
        if (dragLane == Len) editStep (Len, s, true, dragMods);
        lastDragStep = s;
        return;
    }
    if (dragMatrixRow >= 0)
    {
        const int s = stepAt (pos.x);
        if (s < 0 || s == lastDragStep) return;
        const bool accentMode = dragMods.isCtrlDown() || dragMods.isCommandDown();
        const bool fillMode = dragMods.isAltDown();
        const int a = jmin (s, lastDragStep), b = jmax (s, lastDragStep);
        for (int k = a; k <= b; ++k)
        {
            if (accentMode) { if (dragValue) pb.setTrig (sel, dragMatrixRow, k, true); pb.setAcc (sel, dragMatrixRow, k, dragValue); }
            else if (fillMode) { if (dragValue) pb.setTrig (sel, dragMatrixRow, k, true); pb.setFill (sel, dragMatrixRow, k, dragValue); }
            else pb.setTrig (sel, dragMatrixRow, k, dragValue);
        }
        lastDragStep = s;
        proc.presetDirty = true;
        repaint();
        return;
    }
    if (reorderFrom >= 0)
    {
        int target = -1;
        for (int ch = 0; ch < kNumChannels; ++ch)
            if (pos.y >= matrixLabel (ch).getY() - 2 && pos.y < matrixLabel (ch).getBottom() + 2) target = ch;
        if (target >= 0 && target != reorderTo) { reorderTo = target; repaint(); }
    }
}

void PatternPanel::mouseUp (const MouseEvent&)
{
    if (reorderFrom >= 0 && reorderTo >= 0 && reorderTo != reorderFrom)
    {
        const int to = reorderTo;
        proc.moveChannel (reorderFrom, to);
        if (onSelectChannel) onSelectChannel (to);
        if (showStatus) showStatus ("Moved channel " + String (reorderFrom + 1) + " to " + String (to + 1));
    }
    reorderFrom = reorderTo = -1;
    dragLane = -1; dragMatrixRow = -1;
    repaint();
}

//==============================================================================================
void PatternPanel::showMenu (bool repeatLast, bool channelOnly)
{
    if (repeatLast && lastMenuItem != 0) { runMenuItem (lastMenuItem, lastMenuChannelOnly); return; }
    const String what = channelOnly ? "Pattern Channel" : "Pattern";
    const int sel = proc.getSelectedPattern();
    PopupMenu m;
    m.addSectionHeader ("Pattern " + String::charToString ((juce_wchar) (kLetters[sel] - 32)) + (channelOnly ? "  -  channel " + String (channel + 1) + " only" : ""));
    m.addItem (1, "Cut " + what);
    m.addItem (2, "Copy " + what);
    m.addItem (3, "Paste " + what, Clipboard::pattern.has_value());
    m.addItem (4, "Exchange " + what + " / Clipboard", Clipboard::pattern.has_value());
    m.addSeparator();
    m.addItem (5, "Export Pattern To MIDI File...");
    m.addItem (6, "Export Pattern To Audio File...");
    m.addSeparator();
    m.addItem (7, "Shift " + what + " Left");
    m.addItem (8, "Shift " + what + " Right");
    m.addItem (9, "Reverse " + what);
    m.addItem (10, "Alter " + what);
    m.addItem (11, channelOnly ? "Randomize Channel Accents and Fills" : "Randomize Accents and Fills");
    m.addItem (12, "Randomize " + what);
    m.addItem (13, "Clear " + what);
    m.addSeparator();
    m.addSectionHeader ("Groove / expression");
    m.addItem (14, channelOnly ? "Add Ghost Notes To This Channel (DnB)" : "Add Ghost Notes (DnB, snare channels)");
    m.addItem (15, "Randomize Expression" + String (channelOnly ? " (channel)" : ""));
    m.addItem (16, "Groove Shake (happy accidents)" + String (channelOnly ? " (channel)" : ""));
    m.addItem (17, "Clear Expression" + String (channelOnly ? " (channel)" : ""));
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&menuBtn), [this, channelOnly] (int r)
    {
        if (r > 0) runMenuItem (r, channelOnly);
    });
}

void PatternPanel::runMenuItem (int id, bool channelOnly)
{
    if (id != 5 && id != 6) { lastMenuItem = id; lastMenuChannelOnly = channelOnly; }
    const int sel = proc.getSelectedPattern();
    PatternData d = proc.getPattern (sel);
    const int only = channelOnly ? channel : -1;
    auto forChannels = [&] (auto fn) { for (int c = 0; c < kNumChannels; ++c) if (only < 0 || c == only) fn (c); };
    auto clearLanes = [&] { forChannels ([&] (int c) { d.clearChannel (c); }); };

    switch (id)
    {
        case 1: case 2:
            Clipboard::pattern = PatternClip { d, channelOnly, channel };
            if (id == 1) { proc.pushUndo(); clearLanes(); proc.setPattern (sel, d); }
            if (showStatus) showStatus (String (id == 1 ? "Cut " : "Copied ") + (channelOnly ? "channel " + String (channel + 1) + " of " : "") + "pattern " + String::charToString ((juce_wchar) (kLetters[sel] - 32)));
            break;
        case 3: case 4:
        {
            if (! Clipboard::pattern) return;
            proc.pushUndo();
            const PatternClip clip = *Clipboard::pattern;
            if (id == 4) Clipboard::pattern = PatternClip { d, channelOnly, channel };
            if (clip.channelOnly || channelOnly)
            {
                const int src = clip.channelOnly ? clip.channel : channel;
                d.setChannel (channel, clip.data.channel (src));
            }
            else { const bool chain = d.chainNext; d = clip.data; d.chainNext = chain; }
            proc.setPattern (sel, d);
            break;
        }
        case 5:
        {
            auto start = Ferrum8Processor::userFolder().getChildFile (File::createLegalFileName (proc.presetName + " " + String::charToString ((juce_wchar) (kLetters[sel] - 32))) + ".mid");
            chooser = std::make_unique<FileChooser> ("Export pattern to MIDI file", start, "*.mid");
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this, sel] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == File()) return;
                if (! f.hasFileExtension ("mid")) f = f.withFileExtension ("mid");
                const bool ok = proc.exportPatternMidi (f, sel);
                if (showStatus) showStatus (ok ? "Exported " + f.getFileName() : "Could not write " + f.getFullPathName());
            });
            return;
        }
        case 6: exportAudioDialog(); return;
        case 7: case 8:
            proc.pushUndo();
            forChannels ([&] (int c) { d.rotateChannel (c, id == 7 ? -1 : 1); });
            proc.setPattern (sel, d);
            break;
        case 9:
            proc.pushUndo();
            forChannels ([&] (int c) { d.reverseChannel (c); });
            proc.setPattern (sel, d);
            break;
        case 10: proc.pushUndo(); proc.randomizer.alterPattern (d, only); proc.setPattern (sel, d); break;
        case 11: proc.pushUndo(); proc.randomizer.randomizeAccentsFills (d, only); proc.setPattern (sel, d); break;
        case 12: proc.pushUndo(); proc.randomizer.randomizePattern (d, proc.channelRoles(), only); proc.setPattern (sel, d); break;
        case 13: proc.pushUndo(); clearLanes(); proc.setPattern (sel, d); break;
        case 14:
        {
            proc.pushUndo();
            const int n = proc.randomizer.addGhostNotes (d, proc.channelRoles(), only);
            proc.setPattern (sel, d);
            if (showStatus) showStatus (n > 0 ? "Added " + String (n) + " ghost notes" : "No snare or clap channel found (use the channel-only version: Ctrl + menu)");
            break;
        }
        case 15: proc.pushUndo(); proc.randomizer.randomizeExpression (d, proc.channelRoles(), only); proc.setPattern (sel, d); break;
        case 16: proc.pushUndo(); proc.randomizer.grooveShake (d, only); proc.setPattern (sel, d); break;
        case 17:
            proc.pushUndo();
            forChannels ([&] (int c) { for (auto& lane : d.expr[(size_t) c]) lane.fill (0); });
            proc.setPattern (sel, d);
            break;
        default: break;
    }
    repaint();
}

void PatternPanel::exportAudioDialog()
{
    auto* w = new AlertWindow ("Export pattern to audio file",
                               "Renders the selected pattern (or its whole chain) with the current sounds and tempo.",
                               MessageBoxIconType::NoIcon, this);
    w->addComboBox ("bits", { "16 bit", "24 bit", "32 bit float" }, "Resolution");
    w->getComboBoxComponent ("bits")->setSelectedItemIndex (1);
    w->addComboBox ("rate", { "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "192000 Hz" }, "Sample rate");
    w->getComboBoxComponent ("rate")->setSelectedItemIndex (1);
    w->addComboBox ("tail", { "None (cut at pattern end)", "Append (let sounds ring out)", "Loop (fold tail into the start)" }, "Tail");
    w->getComboBoxComponent ("tail")->setSelectedItemIndex (2);
    w->addButton ("Export...", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    SafePointer<PatternPanel> safe (this);
    w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int res)
    {
        if (res != 1 || safe == nullptr) return;
        static const int bitsOpts[3] = { 16, 24, 32 };
        static const double rates[5] = { 44100, 48000, 88200, 96000, 192000 };
        const int bits = bitsOpts[w->getComboBoxComponent ("bits")->getSelectedItemIndex()];
        const double sr = rates[w->getComboBoxComponent ("rate")->getSelectedItemIndex()];
        const int tail = w->getComboBoxComponent ("tail")->getSelectedItemIndex();
        const int sel = safe->proc.getSelectedPattern();
        auto start = Ferrum8Processor::userFolder().getChildFile (File::createLegalFileName (safe->proc.presetName + " " + String::charToString ((juce_wchar) (kLetters[sel] - 32))) + ".wav");
        safe->chooser = std::make_unique<FileChooser> ("Export pattern to audio file", start, "*.wav");
        safe->chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [safe, bits, sr, tail, sel] (const FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f == File()) return;
            if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
            const bool ok = safe->proc.exportPatternAudio (f, sel, bits, sr, tail);
            if (safe->showStatus) safe->showStatus (ok ? "Rendered " + f.getFileName() : "Could not write " + f.getFullPathName());
        });
    }), true);
}
} // namespace ui
