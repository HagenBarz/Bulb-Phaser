/*
  ==============================================================================

    Bulb Phaser | PluginEditor.h

  ==============================================================================
*/

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class BulbPhaserLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BulbPhaserLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

class BulbPhaserLEDButton : public juce::Button
{
public:
    BulbPhaserLEDButton() : juce::Button ("LED") {}

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;
};

class BulbPhaserAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BulbPhaserAudioProcessorEditor (BulbPhaserAudioProcessor&);
    ~BulbPhaserAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr float designW  = 420.0f;
    static constexpr float designH  = 250.0f;
    static constexpr float minScale = 0.75f;
    static constexpr float maxScale = 5.00f;

    BulbPhaserAudioProcessor& audioProcessor;
    BulbPhaserLookAndFeel lnf;

    juce::Label  titleLabel;
    juce::Slider levelSlider, rateSlider, intensitySlider;
    juce::Label  levelLabel, rateLabel, intensityLabel;
    juce::ToggleButton modeButton, characterButton;
    BulbPhaserLEDButton   ledButton;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> levelAttachment;
    std::unique_ptr<SliderAttachment> rateAttachment;
    std::unique_ptr<SliderAttachment> intensityAttachment;
    std::unique_ptr<ButtonAttachment> modeAttachment;
    std::unique_ptr<ButtonAttachment> characterAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    void updateModeButtonText();
    void updateCharacterButtonText();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BulbPhaserAudioProcessorEditor)
};