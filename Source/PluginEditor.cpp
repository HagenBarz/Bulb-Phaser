/*
  ==============================================================================

    Bulb Phaser | PluginEditor.cpp

  ==============================================================================
*/

#include "PluginEditor.h"

//==============================================================================
namespace BulbPhaserColours
{
    const juce::Colour chassis      { 0xffb8b8b8 };
    const juce::Colour chassisDark  { 0xff9a9a9a };
    const juce::Colour knobBody     { 0xff3a3a3a };
    const juce::Colour knobEdge     { 0xff1a1a1a };
    const juce::Colour knobPointer  { 0xffe0e0e0 };
    const juce::Colour labelText    { 0xff1a1a1a };
    const juce::Colour accent       { 0xffb71c1c };
    const juce::Colour toggleBg     { 0xff2a2a2a };
    const juce::Colour toggleTextOff{ 0xff555555 };
    const juce::Colour toggleTextOn { 0xffe0e0e0 };
    const juce::Colour ledLit       { 0xffff3020 };
    const juce::Colour ledDim       { 0xff501010 };
}

BulbPhaserLookAndFeel::BulbPhaserLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, BulbPhaserColours::chassis);
    setColour (juce::Label::textColourId,                  BulbPhaserColours::labelText);
}

void BulbPhaserLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float startAngle, float endAngle,
                                           juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = startAngle + sliderPos * (endAngle - startAngle);

    juce::ColourGradient grad (BulbPhaserColours::knobEdge,  centre.x - radius * 0.4f, centre.y - radius * 0.4f,
                               BulbPhaserColours::knobBody,  centre.x + radius * 0.4f, centre.y + radius * 0.4f,
                               false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    const float borderWidth = juce::jmax (1.0f, radius * 0.05f);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, borderWidth);

    juce::Path pointer;
    const float innerR = radius * 0.35f;
    const float outerR = radius * 0.92f;
    pointer.startNewSubPath (centre.x + innerR * std::sin (angle),
                             centre.y - innerR * std::cos (angle));
    pointer.lineTo (centre.x + outerR * std::sin (angle),
                    centre.y - outerR * std::cos (angle));

    g.setColour (BulbPhaserColours::knobPointer);
    const float strokeWidth = juce::jmax (1.0f, radius * 0.12f);
    g.strokePath (pointer, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void BulbPhaserLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);

    auto bg = BulbPhaserColours::toggleBg;
    if (shouldDrawButtonAsDown)             bg = bg.darker (0.15f);
    else if (shouldDrawButtonAsHighlighted) bg = bg.brighter (0.10f);

    const float corner = 4.0f;
    g.setColour (bg);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, corner, 1.2f);

    const float fontSize = bounds.getHeight() * 0.42f;

    const auto text = button.getButtonText();
    const bool alwaysWhite = (text == "CHORUS"  || text == "VIBRATO"
                           || text == "VINTAGE" || text == "MODERN");

    g.setColour (alwaysWhite ? BulbPhaserColours::toggleTextOn
                             : (button.getToggleState() ? BulbPhaserColours::toggleTextOn
                                                        : BulbPhaserColours::toggleTextOff));
    g.setFont (juce::Font (juce::FontOptions (fontSize).withStyle ("Bold")));
    g.drawFittedText (text, bounds.toNearestInt(), juce::Justification::centred, 1);
}

//==============================================================================
void BulbPhaserLEDButton::paintButton (juce::Graphics& g, bool, bool)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    auto d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto circle = juce::Rectangle<float> (d, d).withCentre (bounds.getCentre());

    const bool ledOn = ! getToggleState();
    const juce::Colour lit = BulbPhaserColours::ledLit;
    const juce::Colour dim = BulbPhaserColours::ledDim;

    if (ledOn)
    {
        g.setColour (lit.withAlpha (0.35f));
        g.fillEllipse (circle.expanded (3.0f));
    }

    g.setColour (ledOn ? lit : dim);
    g.fillEllipse (circle);

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (circle, 1.5f);

    g.setColour (juce::Colours::white.withAlpha (ledOn ? 0.5f : 0.15f));
    g.fillEllipse (circle.getX() + circle.getWidth()  * 0.25f,
                   circle.getY() + circle.getHeight() * 0.20f,
                   circle.getWidth()  * 0.30f,
                   circle.getHeight() * 0.30f);
}

//==============================================================================
BulbPhaserAudioProcessorEditor::BulbPhaserAudioProcessorEditor (BulbPhaserAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lnf);

    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& text)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (s);

        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, BulbPhaserColours::labelText);
        addAndMakeVisible (l);
    };

    setup (levelSlider,     levelLabel,     "LEVEL");
    setup (rateSlider,      rateLabel,      "RATE");
    setup (intensitySlider, intensityLabel, "INTENSITY");

    levelAttachment     = std::make_unique<SliderAttachment> (audioProcessor.apvts, "level",     levelSlider);
    rateAttachment      = std::make_unique<SliderAttachment> (audioProcessor.apvts, "rate",      rateSlider);
    intensityAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, "intensity", intensitySlider);

    modeButton.setButtonText ("CHORUS");
    addAndMakeVisible (modeButton);
    modeAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "mode", modeButton);
    modeButton.onClick = [this] { updateModeButtonText(); };
    updateModeButtonText();

    characterButton.setButtonText ("MODERN");
    addAndMakeVisible (characterButton);
    characterAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "character", characterButton);
    characterButton.onClick = [this] { updateCharacterButtonText(); };
    updateCharacterButtonText();

    titleLabel.setText ("Bulb Phaser", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, BulbPhaserColours::accent);
    addAndMakeVisible (titleLabel);

    ledButton.setClickingTogglesState (true);
    addAndMakeVisible (ledButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "bypassed", ledButton);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (designW * minScale),
                     juce::roundToInt (designH * minScale),
                     juce::roundToInt (designW * maxScale),
                     juce::roundToInt (designH * maxScale));
    getConstrainer()->setFixedAspectRatio (designW / designH);
    setSize (juce::roundToInt (designW), juce::roundToInt (designH));
}

BulbPhaserAudioProcessorEditor::~BulbPhaserAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void BulbPhaserAudioProcessorEditor::updateModeButtonText()
{
    modeButton.setButtonText (modeButton.getToggleState() ? "VIBRATO" : "CHORUS");
}

void BulbPhaserAudioProcessorEditor::updateCharacterButtonText()
{
    characterButton.setButtonText (characterButton.getToggleState() ? "VINTAGE" : "MODERN");
}

void BulbPhaserAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient grad (BulbPhaserColours::chassis.brighter (0.10f), 0.0f, 0.0f,
                               BulbPhaserColours::chassisDark,            0.0f, (float) getHeight(),
                               false);
    g.setGradientFill (grad);
    g.fillAll();
}

void BulbPhaserAudioProcessorEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth()  / designW,
                                    (float) getHeight() / designH);
    auto area = getLocalBounds().reduced (juce::roundToInt (14.0f * scale));

    titleLabel.setBounds (area.removeFromTop (juce::roundToInt (26.0f * scale)));
    titleLabel.setFont (juce::Font (juce::FontOptions (20.0f * scale).withStyle ("Bold")));

    auto ledRow = area.removeFromTop (juce::roundToInt (26.0f * scale));
    const int ledSize = juce::roundToInt (20.0f * scale);
    ledButton.setBounds (ledRow.withSizeKeepingCentre (ledSize, ledSize));

    area.removeFromTop (juce::roundToInt (8.0f * scale));

    auto topRow = area.removeFromTop (juce::roundToInt (96.0f * scale));
    const int thirdW = topRow.getWidth() / 3;

    auto placeLabelBelow = [scale] (juce::Rectangle<int> r, juce::Slider& s, juce::Label& l)
    {
        const int labelH = juce::roundToInt (18.0f * scale);
        l.setBounds (r.removeFromBottom (labelH));
        l.setFont (juce::Font (juce::FontOptions (12.0f * scale).withStyle ("Bold")));

        const int padding = juce::roundToInt (6.0f * scale);
        auto knobArea = r.reduced (padding);
        const int size = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
        s.setBounds (knobArea.withSizeKeepingCentre (size, size));
    };

    placeLabelBelow (topRow.removeFromLeft (thirdW), levelSlider,     levelLabel);
    placeLabelBelow (topRow.removeFromLeft (thirdW), rateSlider,      rateLabel);
    placeLabelBelow (topRow,                          intensitySlider, intensityLabel);

    area.removeFromTop (juce::roundToInt (12.0f * scale));
    auto toggleRow = area.removeFromTop (juce::roundToInt (46.0f * scale));
    const int halfW = toggleRow.getWidth() / 2;
    modeButton.setBounds      (toggleRow.removeFromLeft (halfW).reduced (juce::roundToInt (6.0f * scale)));
    characterButton.setBounds (toggleRow.reduced (juce::roundToInt (6.0f * scale)));
}