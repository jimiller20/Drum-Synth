// Tutorial.h - the first-run tour: a few pages over a dimmed editor (Main menu > Show Tutorial
// opens it again). Whether it has been seen is kept in Ferrum 8's settings file, next to the presets.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Controls.h"

namespace ui
{
class Tutorial : public juce::Component
{
public:
    Tutorial();
    void showPage (int page);
    int pageCount() const { return (int) pages.size(); }
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // a click on the dimmed area closes it
    bool keyPressed (const juce::KeyPress&) override;

    // the first-run flag
    static bool hasBeenSeen();
    static void markSeen();

private:
    struct Page
    {
        juce::String title, body;
        std::function<void (juce::Graphics&, juce::Rectangle<float>)> picture;   // optional illustration
    };
    juce::Rectangle<int> card() const;
    std::vector<Page> pages;
    int page = 0;
    IconButton backBtn { IconButton::Icon::Prev, "Back" }, nextBtn { IconButton::Icon::Next, "Next" },
               closeBtn { IconButton::Icon::None, "Close" };
};
} // namespace ui
