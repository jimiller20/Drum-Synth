// PatchPanel.h - the drum patch section: header (patch menu, name, model, randomise, Edit All)
// and every sound parameter of the selected channel, grouped like the LXR's pages.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Controls.h"
#include "ModStrip.h"
#include "../PluginProcessor.h"

namespace ui
{
class PatchPanel : public juce::Component
{
public:
    explicit PatchPanel (Ferrum8Processor& p);
    ~PatchPanel() override;
    void setChannel (int ch);
    int getChannel() const { return channel; }
    void refresh();                 // model / names changed
    void tick();                    // 30 Hz: flash the Mutate button when the sound moves
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::String&)> showStatus;
    std::function<void (int)> onOpenMod;          // a MOD strip tile was clicked
    ModStrip& getModStrip() { return modStrip; }

private:
    void assignMod (int param, int modulator, bool dropped);
    void updateModRings();
    struct Group { juce::String title; std::vector<int> params; juce::Rectangle<int> bounds; };
    void buildControls();
    void layoutGroups();
    void showPatchMenu (bool repeatLast);
    void runPatchMenuItem (int id);
    void showRandomMenu();
    void showMutatePanel();
    void updateMutateButton();
    void showPatchList();
    void stepPatchFile (int dir);
    void applyEditAll (int param, float delta);
    void applyEditAllChoice (int param, int index);
    juce::String groupTitle (const Group& g) const;

    Ferrum8Processor& proc;
    int channel = 0;
    std::array<std::unique_ptr<juce::Component>, ferrum::NUM_PATCH_PARAMS> controls;
    std::vector<Group> groups;
    Segmented modelSel;
    IconButton menuBtn { IconButton::Icon::Menu }, prevBtn { IconButton::Icon::Prev }, nextBtn { IconButton::Icon::Next },
               openBtn { IconButton::Icon::Folder }, saveBtn { IconButton::Icon::Disk },
               diceBtn { IconButton::Icon::Dice, "Random" }, flavorBtn { IconButton::Icon::Down }, alterBtn { IconButton::Icon::Alter, "Alter" },
               editAllBtn { IconButton::Icon::None, "EDIT ALL" }, mutateBtn { IconButton::Icon::None, "Mutate: Off" };
    juce::TextButton nameBox;
    ModStrip modStrip;
    juce::Rectangle<int> headerArea, nameArea;
    int lastPatchMenuItem = 0;
    float diceAmount = -1.f;           // set by a positional dice click for the item it runs (-1: from a menu)
    uint32_t lastMutCount = 0;
    int blinkFrames = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    bool editAll = false;
};
} // namespace ui
