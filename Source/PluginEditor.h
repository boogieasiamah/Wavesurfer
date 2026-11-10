#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ui/WaveSurferUI.h"

// ==============================================================================
// WAVESURFER — editor. Everything is laid out on a fixed 1060 x 664 canvas that
// is scaled as a whole, so the window resizes with a locked aspect ratio.
// ==============================================================================
class WaveSurferEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static constexpr int baseW = 1060, baseH = 664;

    explicit WaveSurferEditor(WaveSurferProcessor&);
    ~WaveSurferEditor() override;

    void paint(juce::Graphics&) override {}
    void resized() override;

    void refresh();   // pulls engine state; public so the snapshot tool can drive it

private:
    void timerCallback() override { refresh(); }

    struct Canvas : juce::Component {
        explicit Canvas(WaveSurferEditor& e) : ed(e) {}
        void paint(juce::Graphics&) override;
        WaveSurferEditor& ed;
    };

    WaveSurferProcessor& proc;
    wsui::Theme theme;
    wsui::LnF lnf { theme };
    Canvas canvas { *this };

    wsui::Display display { theme, proc };
    std::unique_ptr<wsui::Knob> recover, tame, amount, bias, brighten, boost, lowLimit, highLimit;
    juce::TextButton gainMatchBtn { "Gain Match" }, deltaBtn { "Delta" }, clipBtn { "Clip" }, bypassBtn { "Bypass" };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    juce::TooltipWindow tooltips { this, 600 };

    float gmShown = 0.0f;
    bool gmOnShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveSurferEditor)
};
