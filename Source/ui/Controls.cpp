// Controls.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Controls.h"

namespace ui
{
using namespace juce;

static constexpr float kLabelH = 15.f;
static constexpr float kArcStart = -MathConstants<float>::pi * 0.75f;
static constexpr float kArcEnd = MathConstants<float>::pi * 0.75f;

static StringArray choicesOf (RangedAudioParameter* p)
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (p)) return c->choices;
    if (dynamic_cast<AudioParameterBool*> (p) != nullptr) return { "Off", "On" };
    return {};
}

//==============================================================================================
Knob::Knob() { setRepaintsOnMouseActivity (false); }
Knob::~Knob() { att.reset(); }

void Knob::bind (RangedAudioParameter* p)
{
    att.reset();
    param = p;
    if (param != nullptr)
    {
        att = std::make_unique<ParameterAttachment> (*param, [this] (float v) { norm = param->convertTo0to1 (v); repaint(); });
        att->sendInitialUpdate();
    }
    repaint();
}

String Knob::valueText() const
{
    if (param == nullptr) return {};
    if (formatter) return formatter (norm);
    return param->getText (norm, 64);
}

void Knob::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto knobArea = b.withTrimmedBottom (kLabelH + 2.f);
    const float d = jmin (knobArea.getWidth(), knobArea.getHeight(), 46.f) - 4.f;
    const auto c = knobArea.getCentre();
    const float r = d * 0.5f;
    const auto ac = inactive ? col::faint : accent;

    Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.f, kArcStart, kArcEnd, true);
    g.setColour (col::line.brighter (0.05f));
    g.strokePath (track, PathStrokeType (3.2f, PathStrokeType::curved, PathStrokeType::rounded));

    const float ang = kArcStart + norm * (kArcEnd - kArcStart);
    const float from = bipolar ? 0.f : kArcStart;
    if (std::abs (ang - from) > 0.01f)
    {
        Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.f, jmin (from, ang), jmax (from, ang), true);
        g.setColour (ac);
        g.strokePath (val, PathStrokeType (3.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    const float br = r - 5.5f;
    g.setGradientFill (ColourGradient (col::raised.brighter (0.12f), c.x, c.y - br, col::panel, c.x, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (col::bg.withAlpha (0.8f));
    g.drawEllipse (c.x - br, c.y - br, br * 2, br * 2, 1.f);
    const Point<float> tip (c.x + std::sin (ang) * (br - 2.5f), c.y - std::cos (ang) * (br - 2.5f));
    const Point<float> mid (c.x + std::sin (ang) * (br * 0.25f), c.y - std::cos (ang) * (br * 0.25f));
    g.setColour (inactive ? col::faint : col::text);
    g.drawLine (Line<float> (mid, tip), 2.f);

    const bool showValue = (dragging || hover) && param != nullptr;
    g.setFont (font (showValue ? 14.f : 14.5f, showValue));
    g.setColour (showValue ? ac.brighter (0.2f) : (inactive ? col::faint : col::dim));
    g.drawFittedText (showValue ? valueText() : label, b.removeFromBottom (kLabelH + 1.f).toNearestInt(), Justification::centred, 1, 0.8f);
}

void Knob::setNorm (float n, bool gesture)
{
    if (param == nullptr || att == nullptr) return;
    norm = jlimit (0.f, 1.f, n);
    const float v = param->convertFrom0to1 (norm);
    if (gesture) att->setValueAsPartOfGesture (v); else att->setValueAsCompleteGesture (v);
    repaint();
}

void Knob::resetToDefault()
{
    if (param == nullptr) return;
    if (onGestureStart) onGestureStart();
    const float before = norm;
    setNorm (param->getDefaultValue(), false);
    if (onUserDelta) onUserDelta (norm - before);
}

void Knob::mouseDown (const MouseEvent& e)
{
    if (param == nullptr) return;
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addSectionHeader (param->getName (64) + ":  " + valueText());
        m.addItem (1, "Reset to default");
        m.addItem (2, "Enter value...");
        SafePointer<Knob> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->resetToDefault();
            if (r == 2)
            {
                auto* w = new AlertWindow ("Enter value", safe->param->getName (64), MessageBoxIconType::NoIcon, safe.getComponent());
                w->addTextEditor ("v", safe->valueText());
                w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
                w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int res)
                {
                    if (res == 1 && safe != nullptr && safe->param != nullptr)
                    {
                        if (safe->onGestureStart) safe->onGestureStart();
                        const float before = safe->norm;
                        safe->setNorm (safe->param->getValueForText (w->getTextEditorContents ("v")), false);
                        if (safe->onUserDelta) safe->onUserDelta (safe->norm - before);
                    }
                }), true);
            }
        });
        return;
    }
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { resetToDefault(); return; }
    dragging = true;
    lastY = e.getPosition().y;
    accumDrag = 0.f;
    dragStartNorm = norm;
    if (onGestureStart) onGestureStart();
    att->beginGesture();
    repaint();
}

void Knob::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    const int y = e.getPosition().y;
    const float sens = e.mods.isShiftDown() ? 1.f / 900.f : 1.f / 170.f;
    const float before = norm;
    setNorm (norm + (float) (lastY - y) * sens, true);
    lastY = y;
    if (onUserDelta && norm != before) onUserDelta (norm - before);
}

void Knob::mouseUp (const MouseEvent&)
{
    if (dragging && att != nullptr) att->endGesture();
    dragging = false;
    repaint();
}

void Knob::mouseDoubleClick (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) resetToDefault();
}

void Knob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float before = norm;
    setNorm (norm + w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.05f), false);
    if (onUserDelta && norm != before) onUserDelta (norm - before);
}

//==============================================================================================
ChoiceBox::ChoiceBox() {}
ChoiceBox::~ChoiceBox() { att.reset(); }

void ChoiceBox::bind (RangedAudioParameter* p)
{
    att.reset();
    param = p;
    if (param != nullptr)
    {
        att = std::make_unique<ParameterAttachment> (*param, [this] (float v) { index = roundToInt (v); repaint(); });
        att->sendInitialUpdate();
    }
    repaint();
}

void ChoiceBox::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto lab = b.removeFromBottom (kLabelH + 1.f);
    auto box = b.reduced (2.f, 4.f).withSizeKeepingCentre (jmin (b.getWidth() - 4.f, 64.f), jmin (b.getHeight() - 8.f, 30.f));
    g.setColour (col::raised);
    g.fillRoundedRectangle (box, 4.f);
    g.setColour (isMouseOverOrDragging() ? accent.withAlpha (0.7f) : col::line.brighter (0.15f));
    g.drawRoundedRectangle (box, 4.f, 1.f);
    const auto names = choicesOf (param);
    const auto c = inactive ? col::faint : col::text;
    auto inner = box.reduced (6.f, 5.f);
    if (style == Style::Waves) drawWaveIcon (g, inner.withTrimmedRight (6.f), index, inactive ? col::faint : accent);
    else if (style == Style::LfoWaves) drawLfoIcon (g, inner.withTrimmedRight (6.f), index, inactive ? col::faint : accent);
    else
    {
        g.setColour (c);
        g.setFont (font (15.f, true));
        g.drawFittedText (names[index], inner.withTrimmedRight (5.f).toNearestInt(), Justification::centred, 1, 0.7f);
    }
    Path tri;
    const float tx = box.getRight() - 7.f, ty = box.getCentreY();
    tri.addTriangle (tx - 3.f, ty - 1.5f, tx + 3.f, ty - 1.5f, tx, ty + 2.5f);
    g.setColour (col::dim);
    g.fillPath (tri);
    g.setFont (font (14.5f));
    g.setColour (inactive ? col::faint : col::dim);
    const String text = (style != Style::Text && isMouseOver()) ? names[index] : label;
    g.drawFittedText (text, lab.toNearestInt(), Justification::centred, 1, 0.8f);
}

void ChoiceBox::choose (int idx)
{
    if (param == nullptr || att == nullptr) return;
    const int n = choicesOf (param).size();
    idx = jlimit (0, jmax (0, n - 1), idx);
    if (idx == index) return;
    if (onGestureStart) onGestureStart();
    att->setValueAsCompleteGesture ((float) idx);
    index = idx;
    if (onUserChoice) onUserChoice (idx);
    repaint();
}

void ChoiceBox::mouseDown (const MouseEvent&) { dragged = false; dragStartIndex = index; }

void ChoiceBox::mouseDrag (const MouseEvent& e)
{
    const int steps = -e.getDistanceFromDragStartY() / 14;
    if (steps != 0) dragged = true;
    if (dragged) choose (dragStartIndex + steps);
}

void ChoiceBox::mouseUp (const MouseEvent& e)
{
    if (dragged || param == nullptr || ! getLocalBounds().contains (e.getPosition())) return;
    PopupMenu m;
    const auto names = choicesOf (param);
    m.addSectionHeader (param->getName (64));
    for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, i == index);
    SafePointer<ChoiceBox> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (120), [safe] (int r)
    {
        if (safe != nullptr && r > 0) safe->choose (r - 1);
    });
}

void ChoiceBox::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    wheelAccum += w.deltaY;
    if (std::abs (wheelAccum) >= 0.12f) { choose (index + (wheelAccum > 0 ? -1 : 1)); wheelAccum = 0; }
}

//==============================================================================================
Segmented::Segmented() {}
Segmented::~Segmented() { att.reset(); }

void Segmented::bind (RangedAudioParameter* p, StringArray displayNames)
{
    att.reset();
    param = p;
    names = displayNames.isEmpty() ? choicesOf (p) : displayNames;
    if (param != nullptr)
    {
        att = std::make_unique<ParameterAttachment> (*param, [this] (float v) { index = roundToInt (v); repaint(); });
        att->sendInitialUpdate();
    }
    repaint();
}

Rectangle<float> Segmented::segmentArea() const
{
    auto b = getLocalBounds().toFloat();
    if (label.isNotEmpty()) b.removeFromBottom (kLabelH + 1.f);
    return b.reduced (1.f);
}

void Segmented::paint (Graphics& g)
{
    auto area = segmentArea();
    const int n = jmax (1, names.size());
    g.setColour (col::raised);
    g.fillRoundedRectangle (area, 4.f);
    for (int i = 0; i < n; ++i)
    {
        Rectangle<float> s = vertical ? Rectangle<float> (area.getX(), area.getY() + area.getHeight() * i / n, area.getWidth(), area.getHeight() / n)
                                      : Rectangle<float> (area.getX() + area.getWidth() * i / n, area.getY(), area.getWidth() / n, area.getHeight());
        if (i == index)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (s.reduced (1.5f), 3.f);
        }
        else if (i > 0)
        {
            g.setColour (col::line);
            if (vertical) g.drawHorizontalLine ((int) s.getY(), s.getX() + 3, s.getRight() - 3);
            else g.drawVerticalLine ((int) s.getX(), s.getY() + 4, s.getBottom() - 4);
        }
        g.setColour (i == index ? Colours::black : col::text);
        g.setFont (font (jmin (15.f, s.getHeight() * 0.7f), i == index));
        g.drawFittedText (names[i], s.toNearestInt(), Justification::centred, 1, 0.7f);
    }
    g.setColour (col::line.brighter (0.15f));
    g.drawRoundedRectangle (area, 4.f, 1.f);
    if (label.isNotEmpty())
    {
        g.setColour (col::dim);
        g.setFont (font (14.5f));
        g.drawFittedText (label, getLocalBounds().removeFromBottom ((int) kLabelH + 1), Justification::centred, 1);
    }
}

void Segmented::mouseDown (const MouseEvent& e)
{
    if (param == nullptr || att == nullptr) return;
    auto area = segmentArea();
    const int n = jmax (1, names.size());
    const float rel = vertical ? (e.position.y - area.getY()) / area.getHeight() : (e.position.x - area.getX()) / area.getWidth();
    const int idx = jlimit (0, n - 1, (int) (rel * n));
    if (idx == index) return;
    if (onGestureStart) onGestureStart();
    att->setValueAsCompleteGesture ((float) idx);
    index = idx;
    if (onUserChoice) onUserChoice (idx);
    repaint();
}

//==============================================================================================
IconButton::IconButton (Icon i, const String& buttonText) : Button (buttonText), icon (i) {}

void IconButton::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    Colour bgc = on ? accent.withAlpha (0.9f) : col::raised;
    if (blink) bgc = accent.withAlpha (0.55f);
    if (down) bgc = bgc.brighter (0.15f); else if (over) bgc = bgc.brighter (0.08f);
    g.setColour (bgc);
    g.fillRoundedRectangle (r, 4.f);
    g.setColour (col::line.brighter (0.15f));
    g.drawRoundedRectangle (r, 4.f, 1.f);
    const auto fg = on ? Colours::black : (isEnabled() ? col::text : col::faint);
    if (icon != Icon::None)
    {
        auto ir = r.reduced (r.getHeight() * 0.24f);
        if (getButtonText().isNotEmpty()) ir = ir.withWidth (ir.getHeight());
        else ir = ir.withSizeKeepingCentre (jmin (ir.getWidth(), ir.getHeight()), jmin (ir.getWidth(), ir.getHeight()));
        drawIcon (g, icon, ir, fg);
        if (getButtonText().isNotEmpty())
        {
            g.setColour (fg);
            g.setFont (font (15.f, true));
            g.drawFittedText (getButtonText(), r.withTrimmedLeft (ir.getRight() - r.getX() + 2).toNearestInt(), Justification::centredLeft, 1);
        }
    }
    else
    {
        g.setColour (fg);
        g.setFont (font (jmin (16.f, r.getHeight() * 0.68f), true));
        g.drawFittedText (getButtonText(), r.toNearestInt(), Justification::centred, 1, 0.7f);
    }
}

void IconButton::drawIcon (Graphics& g, Icon icon, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    const auto stroke = PathStrokeType (jmax (1.3f, h * 0.1f), PathStrokeType::curved, PathStrokeType::rounded);
    Path p;
    switch (icon)
    {
        case Icon::Menu:
            for (int i = 0; i < 3; ++i) g.fillRoundedRectangle (x, y + h * (0.12f + 0.32f * i), w, h * 0.12f, 1.f);
            return;
        case Icon::Folder:
            p.startNewSubPath (x, y + h * 0.2f); p.lineTo (x + w * 0.38f, y + h * 0.2f); p.lineTo (x + w * 0.48f, y + h * 0.32f);
            p.lineTo (x + w, y + h * 0.32f); p.lineTo (x + w, y + h * 0.9f); p.lineTo (x, y + h * 0.9f); p.closeSubPath();
            g.strokePath (p, stroke); return;
        case Icon::Disk:
            p.addRoundedRectangle (r, 1.5f); g.strokePath (p, stroke);
            g.fillRect (x + w * 0.25f, y, w * 0.5f, h * 0.32f);
            g.drawRect (Rectangle<float> (x + w * 0.22f, y + h * 0.55f, w * 0.56f, h * 0.45f), 1.2f);
            return;
        case Icon::Lock:
        case Icon::Unlock:
        {
            const float bw = w * 0.62f, bh = h * 0.46f;
            const float bx = r.getCentreX() - bw / 2, by = r.getBottom() - bh - h * 0.06f;
            g.fillRoundedRectangle (bx, by, bw, bh, w * 0.08f);
            Path sh;
            const float sw = bw * 0.62f, top = y + h * 0.1f;
            const float shift = icon == Icon::Unlock ? sw * 0.55f : 0.f;
            sh.startNewSubPath (r.getCentreX() - sw / 2 + shift, by);
            sh.lineTo (r.getCentreX() - sw / 2 + shift, top + sw / 2);
            sh.addCentredArc (r.getCentreX() + shift, top + sw / 2, sw / 2, sw / 2, 0.f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi);
            sh.lineTo (r.getCentreX() + sw / 2 + shift, icon == Icon::Unlock ? top + sw : by);
            g.strokePath (sh, stroke);
            return;
        }
        case Icon::Dice:
        {
            p.addRoundedRectangle (r, w * 0.2f); g.strokePath (p, stroke);
            const float d = w * 0.16f;
            for (auto pt : { Point<float> (0.28f, 0.28f), Point<float> (0.72f, 0.72f), Point<float> (0.5f, 0.5f), Point<float> (0.72f, 0.28f), Point<float> (0.28f, 0.72f) })
                g.fillEllipse (x + w * pt.x - d / 2, y + h * pt.y - d / 2, d, d);
            return;
        }
        case Icon::Undo: case Icon::Redo:
        {
            const bool redo = icon == Icon::Undo; // arc direction: drawn mirrored for undo
            p.addCentredArc (r.getCentreX(), r.getCentreY() + h * 0.08f, w * 0.38f, h * 0.36f, 0.f,
                             redo ? -MathConstants<float>::pi * 0.75f : MathConstants<float>::pi * 0.75f, redo ? MathConstants<float>::pi * 0.6f : -MathConstants<float>::pi * 0.6f, true);
            g.strokePath (p, stroke);
            Path a;
            const float ax = redo ? x + w * 0.2f : x + w * 0.8f;
            const float ay = y + h * 0.12f;
            a.addTriangle (ax - w * 0.18f, ay, ax + w * 0.18f, ay, ax, ay + h * 0.32f);
            a.applyTransform (AffineTransform::rotation (redo ? -0.9f : 0.9f, ax, ay + h * 0.12f));
            g.fillPath (a);
            return;
        }
        case Icon::Prev: p.addTriangle (x + w * 0.75f, y + h * 0.1f, x + w * 0.75f, y + h * 0.9f, x + w * 0.2f, y + h * 0.5f); g.fillPath (p); return;
        case Icon::Down: p.addTriangle (x + w * 0.15f, y + h * 0.3f, x + w * 0.85f, y + h * 0.3f, x + w * 0.5f, y + h * 0.75f); g.fillPath (p); return;
        case Icon::Next: p.addTriangle (x + w * 0.25f, y + h * 0.1f, x + w * 0.25f, y + h * 0.9f, x + w * 0.8f, y + h * 0.5f); g.fillPath (p); return;
        case Icon::Play: p.addTriangle (x + w * 0.2f, y, x + w * 0.2f, y + h, x + w * 0.95f, y + h * 0.5f); g.fillPath (p); return;
        case Icon::Stop: g.fillRoundedRectangle (r.reduced (w * 0.08f), 2.f); return;
        case Icon::Copy:
            g.drawRoundedRectangle (Rectangle<float> (x, y + h * 0.25f, w * 0.68f, h * 0.75f), 1.5f, 1.3f);
            g.drawRoundedRectangle (Rectangle<float> (x + w * 0.32f, y, w * 0.68f, h * 0.75f), 1.5f, 1.3f);
            return;
        case Icon::Paste:
            g.drawRoundedRectangle (Rectangle<float> (x + w * 0.1f, y + h * 0.12f, w * 0.8f, h * 0.88f), 1.5f, 1.3f);
            g.fillRoundedRectangle (Rectangle<float> (x + w * 0.3f, y, w * 0.4f, h * 0.24f), 1.f);
            return;
        case Icon::Matrix:
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    g.fillRoundedRectangle (x + w * (0.02f + 0.36f * i), y + h * (0.02f + 0.36f * j), w * 0.26f, h * 0.26f, 1.f);
            return;
        case Icon::Midi:
        {
            p.addEllipse (r); g.strokePath (p, stroke);
            const float d = w * 0.13f;
            for (int i = 0; i < 5; ++i)
            {
                const float a = MathConstants<float>::pi * (0.75f + 0.375f * i);
                g.fillEllipse (r.getCentreX() + std::cos (a) * w * 0.28f - d / 2, r.getCentreY() + std::sin (a) * h * 0.28f - d / 2, d, d);
            }
            return;
        }
        case Icon::Alter:
            p.startNewSubPath (x, y + h * 0.3f); p.cubicTo (x + w * 0.5f, y + h * 0.3f, x + w * 0.5f, y + h * 0.7f, x + w, y + h * 0.7f);
            p.startNewSubPath (x, y + h * 0.7f); p.cubicTo (x + w * 0.5f, y + h * 0.7f, x + w * 0.5f, y + h * 0.3f, x + w, y + h * 0.3f);
            g.strokePath (p, stroke); return;
        case Icon::ChainLeft: case Icon::ChainRight:
        {
            const bool right = icon == Icon::ChainRight;
            for (int i = 0; i < 2; ++i)
            {
                const float ox = x + w * (right ? 0.15f + 0.35f * i : 0.5f - 0.35f * i);
                Path a;
                if (right) { a.startNewSubPath (ox, y + h * 0.15f); a.lineTo (ox + w * 0.3f, y + h * 0.5f); a.lineTo (ox, y + h * 0.85f); }
                else { a.startNewSubPath (ox + w * 0.3f, y + h * 0.15f); a.lineTo (ox, y + h * 0.5f); a.lineTo (ox + w * 0.3f, y + h * 0.85f); }
                g.strokePath (a, stroke);
            }
            return;
        }
        case Icon::Edit:
            p.startNewSubPath (x + w * 0.1f, y + h * 0.9f); p.lineTo (x + w * 0.2f, y + h * 0.6f); p.lineTo (x + w * 0.75f, y + h * 0.05f);
            p.lineTo (x + w * 0.95f, y + h * 0.25f); p.lineTo (x + w * 0.4f, y + h * 0.8f); p.closeSubPath();
            g.strokePath (p, stroke); return;
        case Icon::None: default: return;
    }
}
} // namespace ui
