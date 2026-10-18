/*
  ==============================================================================

    Bulb Phaser | PluginProcessor.h

  ==============================================================================
*/

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
/*  Bulb Phaser-DSP (mono)

    Signalweg:  Preamp (Saettigung) -> DC-Blocker -> 4 Allpass-Stufen -> Mix -> Level

    Modulationskette (der eigentliche Charakter):
      LFO -> Lampen-/LED-Treiber -> Lichtleistung (Potenzgesetz) -> thermische Traegheit
          -> LDR-Leitwert (Potenzgesetz) -> asymmetrische LDR-Traegheit
          -> R_ldr -> Grenzfrequenz jeder Stufe  f = 1 / (2*pi*C*(R_fix + R_ldr))

    Vibrato-Pitch: Im Vibrato-Modus liegt im Wet-Pfad zusaetzlich eine kurze, modulierte
      Verzoegerung (Doppler). Ihr Hub folgt log(R_ldr) und damit exakt dem Opto-Zustand
      (gleiche Traegheit/Asymmetrie wie der Sweep). Pitch-Hub ~ Rate * Hub.

    Zwei Voicings:
      Vintage : Gluehlampe + CdS-LDR (traege, asymmetrisch, stark nichtlinear),
                asymmetrische Vorstufe, leicht gestauchter Sinus-LFO
      Modern  : generisches LED/LDR-Opto (schnell, sauber), reiner Sinus-LFO
*/
class BulbPhaserDSP
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* samples, int numSamples,
                  float rateHz, float intensity01,
                  int mode, int character,
                  float levelDb);

private:
    struct StageDef { float capNF; float rFixOhm; };

    struct Voicing
    {
        // Lampe / LED
        float lampBias;        // Ruhespannung (0..1)
        float lampSwing;       // max. LFO-Hub bei Intensity = 100 %
        float lampExp;         // Treiberspannung -> Lichtleistung
        float lampAttackMs;    // thermische Traegheit (Aufheizen)
        float lampReleaseMs;   // thermische Traegheit (Abkuehlen)
        // LDR
        float ldrAttackMs;     // Widerstand faellt (heller): schnell
        float ldrReleaseMs;    // Widerstand steigt (dunkler): langsam
        float ldrGamma;        // Leitwert ~ Licht^gamma
        float ldrBrightMin;    // Mindestwiderstand bei maximaler Helligkeit (Ohm)
        float ldrDarkMax;      // Maximaler Dunkelwiderstand (Ohm)
        // LFO
        float lfoSoftness;     // 0 = reiner Sinus, >0 = gestauchter Sinus
        float lfoSkew;         // 0 = symmetrisch, >0 = asymmetrischer Shin-ei Throb
        // Vorstufe & Saettigung
        float preDrive;        // Saettigungsgrad (Warmth / Harmonische)
        float preBias;         // >0 = asymmetrisch (gerade Harmonische)
        // Doppler-Pitch (Leslie-Effekt)
        float vibDepthMs;      // Delay-Hub bei Intensity = 100 % (Vibrato-Modus)
        float chorusDoppler;   // Anteil des Doppler-Delays im Chorus-Modus (echter Leslie-Swirl)
        // Chaos & Fluktuation
        float chaosAmount;     // 0 = synchron (Modern), 1 = volle Bauteiltoleranz (Vintage)
        float thermalFluct;    // Thermisches Gluehfaden-Rauschen (0 = aus)
        // Regler-Kennlinie
        float intensityCurve;  // Exponent fuer musikalisch gleichmaessigen Reglerweg
        // Vintage Tone-Suck (Eingangsimpedanz-Bedaempfung der Hoehen)
        float toneFilterCutoff; // z.B. 3800 Hz fuer Vintage, 0 = aus (Modern)
        float toneShelfDirect;  // z.B. 0.30f fuer Vintage, 1.0f = aus (Modern)
        // Eigene Stufen-Abstimmung pro Voicing
        StageDef stages[4];
    };

    static constexpr int   kChunk     = 16;        // Controlrate: fs / 16

    static constexpr Voicing kVintage {
        0.55f, 0.48f, 2.2f,  8.0f, 14.0f,
        5.0f, 22.0f, 0.80f, 1800.0f, 26000.0f,
        1.0f, 0.30f, 0.60f, 0.05f,
        0.95f, 0.50f, 1.0f, 0.030f, 1.15f,
        3800.0f, 0.30f,
        { { 15.0f, 2200.0f }, { 220.0f, 1200.0f }, { 0.47f, 1500.0f }, { 4.7f, 2000.0f } }
    };

    static constexpr Voicing kModern {
        0.50f, 0.48f, 1.5f,  1.0f,  1.0f,
        4.0f, 15.0f, 0.72f, 2000.0f, 30000.0f,
        0.0f, 0.00f, 0.00f, 0.00f,
        0.60f, 0.35f, 0.0f, 0.000f, 1.00f,
        0.0f, 1.0f,
        { { 15.0f, 2200.0f }, {  47.0f, 2000.0f }, { 4.7f, 2200.0f }, { 1.5f, 2000.0f } }
    };

    // Wahrgenommene Intensitaet: Ausgewogene Kompensation der thermischen/LDR-Traegheit bei schnellen Sweeps
    static constexpr float kRateComp       = 0.75f; // Harmonische Kompensation ohne Uebersteuern/Flatlining
    static constexpr float kMaxRateBoost   = 2.8f;  // Ausgewogene Obergrenze fuer vollen Leslie-Sweep

    static float onePoleCoef (float timeMs, float dtSec);
    static float ldrResistance (const Voicing& v, float conductance);
    static float resistanceAtDrive (const Voicing& v, float drive);

    double fs = 44100.0;
    bool   primed = false;

    double lfoPhase = 0.0;
    float  rateSm = 0.5f, intensitySm = 0.5f;

    float lampState = 0.0f;
    float ldrState[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    float flucState = 0.0f, flucTarget = 0.0f;
    uint32_t rngSeed = 123456789;

    float aCur[4]    { 0.0f, 0.0f, 0.0f, 0.0f };
    float apState[4] { 0.0f, 0.0f, 0.0f, 0.0f };

    float toneY = 0.0f;
    float dcX = 0.0f, dcY = 0.0f, dcCoef = 0.999f;

    float levelSm = 1.0f, dryGainSm = 0.7071f, wetGainSm = 0.7071f;
    float smoothCoef = 0.998f;

    // Vibrato-Pitch (modulierte Verzoegerung im Wet-Pfad)
    std::vector<float> delayBuf;
    int   delayMask  = 0;
    int   delayWrite = 0;
    float vibAmountSm = 0.0f;
    float vibCoef     = 0.998f;
    float uCur        = 0.0f;   // normierte Opto-Auslenkung (0 = Ruhelage)
    float dryPrev     = 0.0f;   // Dry um 1 Sample verzoegert (Laufzeit-Abgleich zum Delay)
};

//==============================================================================
class BulbPhaserAudioProcessor : public juce::AudioProcessor
{
public:
    BulbPhaserAudioProcessor();
    ~BulbPhaserAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
/*
   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif
*/
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    BulbPhaserDSP dsp[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BulbPhaserAudioProcessor)
};
