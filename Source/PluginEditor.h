// PluginEditor.h - the Ferrum 8 window: toolbar, channel strip, drum patch section,
// pattern section and global section (the Microtonic workflow), scalable 60-200%.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/PatchPanel.h"
#include "ui/PatternPanel.h"
#include "ui/TopPanels.h"
#include "ui/DicePanel.h"

class Ferrum8Editor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    static constexpr int kDesignW = 1240, kDesignH = 848;
    explicit Ferrum8Editor (Ferrum8Processor&);
    ~Ferrum8Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Content : public juce::Component
    {
    public:
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override { g.fillAll (ui::col::bg); if (onPaint) onPaint (g); }
    };
    void drawStatus (juce::Graphics& g);

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void selectChannel (int ch);
    void preview (int ch, bool accent);
    void showMainMenu (bool repeatLast);
    void runMainMenuItem (int id);
    void showPresetList();
    void stepPreset (int dir);
    void showMorphMenu();
    void showChannelMenu (int ch);
    void showMidiConfig();
    void showAbout();
    void setZoom (float z);
    void status (const juce::String& s);
    void pressPlay();
    void pressStop();
    void randomizeAll();
    void showDicePanel();

    Ferrum8Processor& proc;
    ui::LookAndFeel lnf;
    Content content;
    ui::Toolbar toolbar;
    ui::ChannelStrip channels;
    ui::PatchPanel patch;
    ui::PatternPanel pattern;
    ui::GlobalPanel global;
    juce::TooltipWindow tooltips { this, 700 };
    juce::String statusText;
    int statusTimer = 0;
    int lastMainMenuItem = 0;
    float zoom = 1.0f;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ferrum8Editor)
};
