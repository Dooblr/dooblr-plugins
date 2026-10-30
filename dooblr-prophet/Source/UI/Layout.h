#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "ControlPanel.h"

namespace dooblrprophet::layout
{
    /**
        The editor's pages as data: every page's mode switches, panels (in order, with their
        controls in order) and panel rows. The editor builds itself from this, and the remote link
        (Link/RemoteLink.h) sends the same description to the Max for Live device, so the two
        surfaces cannot drift apart (0.3.0).
    */
    struct Control
    {
        const char* id;
        const char* caption;
        ui::ControlPanel::Kind kind;
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

    inline constexpr int voicePresetCounts[] { 5, 6, 10 };
    inline constexpr int numVoicePresets = 3;

    /** Component IDs of the controls that are not parameters (dimmed like them; see ControlRules.h). */
    inline constexpr const char* voicePresetsId = "voicesPreset";
    inline constexpr const char* echoPresetPickerId = "echoPresetPicker";
    inline constexpr const char* reverbPresetPickerId = "reverbPresetPicker";
}
