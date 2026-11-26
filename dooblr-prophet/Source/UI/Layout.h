#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "ControlPanel.h"

namespace dooblrprophet::layout
{
    /**
        pages() is the Max for Live / RemoteLink layout (Sound / Modulation / Voices / FX) and must
        stay unchanged so the 0.3.3 panel keeps working without a device update.

        editorPages() is the VST editor layout (Main / Advanced, 0.3.5). The two can diverge.
    */
    struct Control
    {
        const char* id;
        const char* caption;
        ui::ControlPanel::Kind kind;
        int width = 0; // 0: the kind's standard width
        int stack = -1; // >= 0: share a grid cell with other controls of this stack
    };

    struct ModeToggle
    {
        const char* caption;
        const char* id;
        juce::StringArray labels;
    };

    struct Panel
    {
        const char* title;
        std::vector<Control> controls;
        /** The Voices panel's 5 / 6 / 10 buttons (after the Voices knob). */
        bool voicePresets = false;
        /** 0: the Echo preset picker (before Mix); 1: the Reverb one; -1: none. */
        int fxPicker = -1;
    };

    struct Page
    {
        const char* name;
        std::vector<ModeToggle> toggles;
        std::vector<Panel> panels;
        std::vector<std::vector<int>> rows; // panel indices per row, top to bottom
    };

    const std::vector<Page>& pages();
    const std::vector<Page>& editorPages();

    inline constexpr int voicePresetCounts[] { 5, 6, 10 };
    inline constexpr int numVoicePresets = 3;

    /** Component IDs of the controls that are not parameters (dimmed like them; see ControlRules.h). */
    inline constexpr const char* voicePresetsId = "voicesPreset";
    inline constexpr const char* echoPresetPickerId = "echoPresetPicker";
    inline constexpr const char* reverbPresetPickerId = "reverbPresetPicker";
}
