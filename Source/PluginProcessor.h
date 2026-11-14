#pragma once
#include "Core/Params.h"
#include "Synth/SynthEngine.h"
#include "Engine/OutputChain.h"

/** Processeur principal d'IDGems Synth. */
class IDGemsSynthProcessor : public juce::AudioProcessor
{
public:
    IDGemsSynthProcessor();
    ~IDGemsSynthProcessor() override = default;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "IDGems Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Coupe toutes les voix et vide les buffers (sans clic). Appelable depuis l'interface. */
    void panic() { panicFlag.store (true); }

    float getValue (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // clavier à l'écran
    juce::ApplicationProperties appProps;    // réglages de l'interface (taille...)

    // télémétrie pour l'interface
    std::atomic<float> meterOut[2] { 0.0f, 0.0f };
    std::atomic<float> meterGR { 1.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> cpuLoad { 0.0f };     // part du temps réel utilisée (0..1)

    static constexpr int ScopeSize = 2048;
    juce::AbstractFifo scopeFifo { ScopeSize };
    std::vector<float> scopeData = std::vector<float> (ScopeSize, 0.0f);

private:
    void readParams (idgs::VoiceParams& p) const;

    idgs::SynthEngine engine;
    idg::OutputChain output;
    idgs::VoiceParams vp;

    double sr = 44100.0;
    juce::SmoothedValue<float> masterGain;
    juce::AudioBuffer<float> work;

    std::atomic<bool> panicFlag { false };
    int panicMute = 0;           // échantillons restants avant de vider les buffers
    float panicGain = 1.0f;

    struct Raw
    {
        std::atomic<float>* oscAOn, *oscATable, *oscAPos, *oscAOct, *oscASemi, *oscAFine, *oscALevel, *oscAPan,
            *oscAPhase, *oscAPhRand, *delay, *attack, *hold, *decay, *sustain, *release, *aCurve, *dCurve, *rCurve,
            *voices, *velSens, *bendRange, *masterVol, *ceiling;
    } raw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IDGemsSynthProcessor)
};
