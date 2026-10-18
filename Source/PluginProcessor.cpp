/*
  ==============================================================================

    Bulb Phaser | PluginProcessor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// DSP-BEGIN
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr float  kPiF   = 3.14159265358979f;
}

float BulbPhaserDSP::onePoleCoef (float timeMs, float dtSec)
{
    return std::exp (-dtSec / (0.001f * timeMs));
}

float BulbPhaserDSP::ldrResistance (const Voicing& v, float g)
{
    return std::clamp (v.ldrBrightMin / std::max (g, 1.0e-4f), v.ldrBrightMin, v.ldrDarkMax);
}

float BulbPhaserDSP::resistanceAtDrive (const Voicing& v, float drive)
{
    const float light = std::pow (std::max (0.02f, drive), v.lampExp);
    return ldrResistance (v, std::pow (light, v.ldrGamma));
}

void BulbPhaserDSP::prepare (double sampleRate)
{
    fs = sampleRate;

    int size = 256;
    while (size < (int) (0.006 * fs) + 8) size <<= 1;      // >= 6 ms Platz
    delayBuf.assign ((size_t) size, 0.0f);
    delayMask = size - 1;
    vibCoef   = std::exp (-1.0f / (0.100f * (float) fs));  // 100 ms: weiches Ein-/Ausblenden
    dcCoef     = std::exp (-(float) kTwoPi * 5.0f / (float) fs);          // ~5 Hz Hochpass
    smoothCoef = std::exp (-1.0f / (0.020f * (float) fs));                // 20 ms Glaettung
    reset();
}

void BulbPhaserDSP::reset()
{
    primed   = false;
    lfoPhase = 0.0;
    lampState = 0.0f;
    for (auto& s : ldrState) s = 0.0f;
    flucState = 0.0f;
    flucTarget = 0.0f;
    toneY = 0.0f;
    dcX = dcY = 0.0f;
    for (auto& s : apState) s = 0.0f;
    std::fill (delayBuf.begin(), delayBuf.end(), 0.0f);
    delayWrite = 0;
    dryPrev = 0.0f;
    uCur = 0.0f;
}

void BulbPhaserDSP::process (float* samples, int numSamples,
                          float rateHz, float intensity01,
                          int mode, int character,
                          float levelDb)
{
    const Voicing& v = (character == 1) ? kVintage : kModern;

    const float dtChunk = (float) kChunk / (float) fs;

    const float lampAtt = onePoleCoef (v.lampAttackMs,  dtChunk);
    const float lampRel = onePoleCoef (v.lampReleaseMs, dtChunk);
    const float rateCoef = onePoleCoef (60.0f, dtChunk);
    const float intCoef  = onePoleCoef (30.0f, dtChunk);

    const float levelTarget = std::pow (10.0f, levelDb / 20.0f);
    const float dryTarget   = (mode == 1) ? 0.0f : 0.7071f;   // Chorus: pegelkompensiert (~ +3 dB), Vibrato: nur Wet
    const float wetTarget   = (mode == 1) ? 1.0f : 0.7071f;

    // Vorstufe: statischer Offset und Kleinsignal-Normierung (Verstaerkung 1 bei kleinen Pegeln)
    const bool  hasPreamp = (v.preDrive > 0.001f);
    const float preOffset = hasPreamp ? std::tanh (v.preDrive * v.preBias) : 0.0f;
    const float preNorm   = hasPreamp ? 1.0f / (v.preDrive * (1.0f - preOffset * preOffset)) : 1.0f;

    // Tone-Shaping Filter (Vintage 1968 Eingangsimpedanz-Bedaempfung der Hoehen)
    const bool  useToneFilter   = (v.toneFilterCutoff > 0.0f);
    const float toneCoef        = useToneFilter ? std::exp (-(float) kTwoPi * v.toneFilterCutoff / (float) fs) : 0.0f;
    const float toneShelfDirect = v.toneShelfDirect;
    const float toneShelfFilt   = 1.0f - toneShelfDirect;

    const float lfoNorm = (v.lfoSoftness > 0.0f) ? 1.0f / std::tanh (v.lfoSoftness) : 1.0f;

    // Mittlere Zeitkonstanten von Lampe und LDR (Sekunden) fuer die Rate-Kompensation
    const float tauLamp = 0.0005f * (v.lampAttackMs + v.lampReleaseMs);
    const float tauLdr  = 0.0005f * (v.ldrAttackMs  + v.ldrReleaseMs);

    // Vibrato-Pitch / Leslie-Doppler: Delay-Hub D in Samples, Opto-Auslenkung u = ln(R/R_ruhe) / halber Log-Hub
    const float vibTarget = (mode == 1) ? 1.0f : v.chorusDoppler;
    const float dSamp     = 0.001f * v.vibDepthMs * (float) fs;
    const float rRest     = resistanceAtDrive (v, v.lampBias);
    const float rBright   = resistanceAtDrive (v, v.lampBias + v.lampSwing);
    const float rDark     = resistanceAtDrive (v, v.lampBias - v.lampSwing);
    const float uScale    = 0.5f * std::log (rDark / rBright);
    const float dMax      = (float) delayMask - 4.0f;

    // Individuelle Toleranzen der 4 LDRs (Streuung bei Vintage, synchron bei Modern)
    static constexpr float kTolSens[4]   { 1.06f, 0.90f, 1.14f, 0.94f };
    static constexpr float kTolGamma[4]  { 1.04f, 0.95f, 1.07f, 0.97f };
    static constexpr float kTolAtt[4]    { 0.90f, 1.15f, 0.85f, 1.10f };
    static constexpr float kTolRel[4]    { 0.92f, 1.20f, 0.88f, 1.12f };

    if (! primed)
    {
        rateSm      = rateHz;
        intensitySm = intensity01;
        levelSm     = levelTarget;
        dryGainSm   = dryTarget;
        wetGainSm   = wetTarget;

        lampState = std::pow (v.lampBias, v.lampExp);   // LFO-Phase 0 -> Ruhespannung
        for (int k = 0; k < 4; ++k)
        {
            const float sensMult = 1.0f + v.chaosAmount * (kTolSens[k] - 1.0f);
            const float gammaEff = v.ldrGamma * (1.0f + v.chaosAmount * (kTolGamma[k] - 1.0f));
            ldrState[k] = std::pow (lampState, gammaEff) * sensMult;

            const float rLdrK = std::clamp (v.ldrBrightMin / std::max (ldrState[k], 1.0e-4f),
                                            v.ldrBrightMin, v.ldrDarkMax);
            const float rTot  = v.stages[k].rFixOhm + rLdrK;
            const float c     = v.stages[k].capNF * 1.0e-9f;
            const float f     = 1.0f / ((float) kTwoPi * c * rTot);
            const float fc    = std::clamp (f, 10.0f, 0.45f * (float) fs);
            const float t     = std::tan (kPiF * fc / (float) fs);
            aCur[k]           = (t - 1.0f) / (t + 1.0f);
        }

        vibAmountSm = vibTarget;
        uCur = 0.0f;
        flucState = 0.0f;
        flucTarget = 0.0f;
        primed = true;
    }

    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);

        // ---------------- Controlrate ----------------
        rateSm      = rateHz      + rateCoef * (rateSm      - rateHz);
        intensitySm = intensity01 + intCoef  * (intensitySm - intensity01);

        lfoPhase += (double) rateSm * (double) n / fs;
        lfoPhase -= std::floor (lfoPhase);

        // LFO: Warping/Skew fuer originalgetreue Shin-ei Phasenverzerrung (laengeres Tal, zuegiger Anstieg)
        float warpedPhase = (float) lfoPhase;
        if (v.lfoSkew > 0.0f)
        {
            warpedPhase -= v.lfoSkew * 0.15915494f * (float) std::sin (kTwoPi * lfoPhase);
            warpedPhase -= std::floor (warpedPhase);
        }

        float lfo = (float) std::sin (kTwoPi * warpedPhase);
        if (v.lfoSoftness > 0.0f)
            lfo = std::tanh (v.lfoSoftness * lfo) * lfoNorm;

        // Thermische Mikro-Fluktuation der Gluehlampe (Chaos / organischer Drift)
        if (v.thermalFluct > 0.0f)
        {
            rngSeed = rngSeed * 1664525u + 1013904223u;
            const float rnd = ((float) (rngSeed >> 16) / 32768.0f) - 1.0f;
            flucTarget = rnd * v.thermalFluct;
            flucState += 0.04f * (flucTarget - flucState);
        }
        else
        {
            flucState = 0.0f;
        }

        // Wahrgenommene Tiefe: Regler-Kennlinie + teilweiser Ausgleich des Hubverlusts bei
        // schnellen Raten (Lampe und LDR wirken wie zwei Tiefpaesse auf die Modulation).
        const float wRate = (float) kTwoPi * rateSm;
        const float x1 = wRate * tauLamp, x2 = wRate * tauLdr;
        const float rateBoost = std::min (kMaxRateBoost,
                                          std::pow (std::sqrt ((1.0f + x1 * x1) * (1.0f + x2 * x2)), kRateComp));
        const float depth = std::pow (std::max (intensitySm, 0.0f), v.intensityCurve) * rateBoost;

        // Lampe/LED: Ruhespannung + Drift + LFO-Hub. Die Lampe geht nie ganz aus -> kein Kollaps am Minimum.
        const float drive       = std::clamp (v.lampBias + flucState + v.lampSwing * depth * lfo, 0.015f, 1.0f);
        const float lightTarget = std::pow (drive, v.lampExp);
        lampState = lightTarget + (lightTarget > lampState ? lampAtt : lampRel) * (lampState - lightTarget);

        // 4 individuelle LDRs: Leitwert ~ Licht^gamma, individuelle Streuung & Traegheiten
        float aTarget[4], da[4];
        float avgConductance = 0.0f;

        for (int k = 0; k < 4; ++k)
        {
            const float sensMult = 1.0f + v.chaosAmount * (kTolSens[k] - 1.0f);
            const float gammaEff = v.ldrGamma * (1.0f + v.chaosAmount * (kTolGamma[k] - 1.0f));

            const float gTarget = std::pow (lampState, gammaEff) * sensMult;

            const float attMs   = v.ldrAttackMs  * (1.0f + v.chaosAmount * (kTolAtt[k] - 1.0f));
            const float relMs   = v.ldrReleaseMs * (1.0f + v.chaosAmount * (kTolRel[k] - 1.0f));
            const float ldrAttK = onePoleCoef (attMs, dtChunk);
            const float ldrRelK = onePoleCoef (relMs, dtChunk);

            ldrState[k] = gTarget + (gTarget > ldrState[k] ? ldrAttK : ldrRelK) * (ldrState[k] - gTarget);
            avgConductance += ldrState[k];

            const float rLdrK = std::clamp (v.ldrBrightMin / std::max (ldrState[k], 1.0e-4f),
                                            v.ldrBrightMin, v.ldrDarkMax);
            const float rTot  = v.stages[k].rFixOhm + rLdrK;
            const float c     = v.stages[k].capNF * 1.0e-9f;
            const float f     = 1.0f / ((float) kTwoPi * c * rTot);
            const float fc    = std::clamp (f, 10.0f, 0.45f * (float) fs);
            const float t     = std::tan (kPiF * fc / (float) fs);
            aTarget[k]        = (t - 1.0f) / (t + 1.0f);
            da[k]             = (aTarget[k] - aCur[k]) / (float) n;
        }
        avgConductance *= 0.25f;

        const float rAvg    = std::clamp (v.ldrBrightMin / std::max (avgConductance, 1.0e-4f),
                                          v.ldrBrightMin, v.ldrDarkMax);
        const float uTarget = std::log (rAvg / rRest) / uScale;
        const float du      = (uTarget - uCur) / (float) n;

        // ---------------- Audiorate ----------------
        float* p = samples + pos;
        for (int i = 0; i < n; ++i)
        {
            const float x = p[i];

            // Vintage Tone-Shaping (Hoehenbedaempfung der 1968 Eingangsimpedanz)
            float inSig = x;
            if (useToneFilter)
            {
                toneY = inSig + toneCoef * (toneY - inSig);
                inSig = toneShelfDirect * inSig + toneShelfFilt * toneY;
            }

            // Vorstufe
            const float sat = hasPreamp ? (std::tanh (v.preDrive * (inSig + v.preBias)) - preOffset) * preNorm : inSig;

            // DC-Blocker (Kopplungskondensator)
            const float dry = sat - dcX + dcCoef * dcY;
            dcX = sat;
            dcY = dry;

            // 4 Allpass-Stufen, Koeffizient pro Sample linear interpoliert
            float w = dry;
            for (int k = 0; k < 4; ++k)
            {
                aCur[k] += da[k];
                const float y = aCur[k] * w + apState[k];
                apState[k] = w - aCur[k] * y;
                w = y;
            }

            // Vibrato-Pitch: modulierte Verzoegerung (Hermite). Dunkler LDR -> laengere Verzoegerung
            // -> Pitch faellt. Mindestverzoegerung 1 Sample; Dry bekommt dieselbe 1 Sample.
            uCur += du;
            vibAmountSm = vibTarget + vibCoef * (vibAmountSm - vibTarget);

            delayBuf[(size_t) delayWrite] = w;
            const float d = std::min (dMax, 1.0f + vibAmountSm * dSamp * std::max (0.0f, 1.0f + uCur));
            const int   di = (int) d;
            const float fr = d - (float) di;
            const int   m  = delayWrite - di + delayMask + 1;
            const float ym1 = delayBuf[(size_t) ((m + 1) & delayMask)];
            const float y0  = delayBuf[(size_t) ( m      & delayMask)];
            const float y1  = delayBuf[(size_t) ((m - 1) & delayMask)];
            const float y2  = delayBuf[(size_t) ((m - 2) & delayMask)];
            const float c1  = 0.5f * (y1 - ym1);
            const float c2  = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3  = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            w = ((c3 * fr + c2) * fr + c1) * fr + y0;
            delayWrite = (delayWrite + 1) & delayMask;

            const float dryD = dryPrev;
            dryPrev = dry;

            // Glatte Mix-/Level-Umschaltung (kein Knacken beim Moduswechsel)
            levelSm   = levelTarget + smoothCoef * (levelSm   - levelTarget);
            dryGainSm = dryTarget   + smoothCoef * (dryGainSm - dryTarget);
            wetGainSm = wetTarget   + smoothCoef * (wetGainSm - wetTarget);

            p[i] = (dryGainSm * dryD + wetGainSm * w) * levelSm;
        }

        for (int k = 0; k < 4; ++k)
            aCur[k] = aTarget[k];
    }
}
// DSP-END

//==============================================================================
BulbPhaserAudioProcessor::BulbPhaserAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                      .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
}

BulbPhaserAudioProcessor::~BulbPhaserAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
BulbPhaserAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "level", 1 }, "Level",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

    juce::NormalisableRange<float> rateRange (0.05f, 10.0f, 0.001f);
    rateRange.setSkewForCentre (5.0f);
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "rate", 1 }, "Rate", rateRange, 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "intensity", 1 }, "Intensity",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "mode", 1 }, "Mode",
        juce::StringArray { "Chorus", "Vibrato" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "character", 1 }, "Character",
        juce::StringArray { "Modern", "Vintage" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "bypassed", 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

void BulbPhaserAudioProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& d : dsp) d.prepare (sampleRate);
}

void BulbPhaserAudioProcessor::releaseResources() {}
/*
#ifndef JucePlugin_PreferredChannelConfigurations
bool BulbPhaserAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
}
#endif
*/
void BulbPhaserAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int inputChannels  = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();
    const int numSamples     = buffer.getNumSamples();

    const bool bypassed   = apvts.getRawParameterValue ("bypassed")->load() > 0.5f;
    if (bypassed)
        return;

    const float rate      = apvts.getRawParameterValue ("rate")->load();
    const float intensity = apvts.getRawParameterValue ("intensity")->load() * 0.01f;
    const int   mode      = (int) apvts.getRawParameterValue ("mode")->load();
    const int   character = (int) apvts.getRawParameterValue ("character")->load();
    const float levelDb   = apvts.getRawParameterValue ("level")->load();

    // Hinweis: Der DSP-Zustand ist mono. Bei einem spaeteren Stereo-Layout
    // eine Instanz pro Kanal verwenden (LFO/Lampe dann geteilt).
    for (int ch = 0; ch < inputChannels; ++ch)
        dsp[ch].process (buffer.getWritePointer (ch), numSamples,
                     rate, intensity, mode, character, levelDb);

    for (int channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);
}

juce::AudioProcessorEditor* BulbPhaserAudioProcessor::createEditor()
{
    return new BulbPhaserAudioProcessorEditor (*this);
}

bool BulbPhaserAudioProcessor::hasEditor() const { return true; }
const juce::String BulbPhaserAudioProcessor::getName() const { return "Bulb Phaser"; }
bool BulbPhaserAudioProcessor::acceptsMidi() const { return false; }
bool BulbPhaserAudioProcessor::producesMidi() const { return false; }
bool BulbPhaserAudioProcessor::isMidiEffect() const { return false; }
double BulbPhaserAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int BulbPhaserAudioProcessor::getNumPrograms() { return 1; }
int BulbPhaserAudioProcessor::getCurrentProgram() { return 0; }
void BulbPhaserAudioProcessor::setCurrentProgram (int) {}
const juce::String BulbPhaserAudioProcessor::getProgramName (int) { return {}; }
void BulbPhaserAudioProcessor::changeProgramName (int, const juce::String&) {}

juce::AudioProcessorParameter*
BulbPhaserAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter ("bypassed");
}

void BulbPhaserAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BulbPhaserAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BulbPhaserAudioProcessor();
}
