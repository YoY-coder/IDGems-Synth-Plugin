#pragma once
#include "Core/Params.h"
#include "Synth/SynthEngine.h"
#include "Engine/OutputChain.h"
#include "State/PresetManager.h"
#include "Modulation/ModEngine.h"

/** Processeur principal d'IDGems Synth. */
class IDGemsSynthProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    IDGemsSynthProcessor();
    ~IDGemsSynthProcessor() override;

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

    // ---- État complet (paramètres + tables personnalisées + samples)
    juce::ValueTree captureState();
    void restoreState (const juce::ValueTree& state);
    void resetToInit();

    // ---- Paramètres (thread de l'interface)
    float getValue (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setValue (const juce::String& id, float realValue);

    // ---- Tables et samples des oscillateurs (thread de l'interface uniquement)
    /** Fichiers glissés sur un oscillateur : en mode Wavetable, le sample est découpé en wavetable ;
        en mode Sample / Multisample, il est chargé tel quel. Renvoie un message (vide si tout va bien). */
    juce::String dropFiles (int osc, const juce::Array<juce::File>& files);
    /** Bouton dé : nouvelle wavetable aléatoire. */
    void randomTable (int osc);
    /** Crayon : remplace la table de l'oscillateur par ces cycles (Size échantillons par frame). */
    void setTableFromCycles (int osc, const std::vector<float>& cycles, const juce::String& name);
    const idgs::Wavetable& displayTable (int osc) const;
    const idgs::SampleSet* currentSamples (int osc) const { return assets[osc].samples.load(); }
    juce::String assetName (int osc) const;
    std::atomic<int> assetsVersion { 0 }; // incrémenté à chaque changement (l'interface se redessine)

    /** Coupe toutes les voix et vide les buffers (sans clic). Appelable depuis l'interface. */
    void panic() { panicFlag.store (true); }

    juce::AudioProcessorValueTreeState apvts;
    idgs::ModShared modShared;               // connexions de modulation + formes de LFO (partagé avec l'interface)
    idgs::ModTargets modTargets;
    // état de l'interface de modulation (survol / glisser-déposer), lu par tous les potards
    std::atomic<int> uiHoverSource { -1 }, uiDragSource { -1 }, uiHoverParam { -1 };
    int paramIndex (const juce::String& id) const { return modTargets.paramIndexForId (id); }
    double getBpm() const { return uiBpm.load(); }
    juce::MidiKeyboardState keyboardState;   // clavier à l'écran
    juce::ApplicationProperties appProps;    // réglages de l'interface (taille, auteur...)
    std::unique_ptr<idgs::PresetManager> presets;

    // télémétrie pour l'interface
    std::atomic<float> meterOut[2] { 0.0f, 0.0f };
    std::atomic<float> meterGR { 1.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> cpuLoad { 0.0f };     // part du temps réel utilisée (0..1)

    static constexpr int ScopeSize = 2048;
    juce::AbstractFifo scopeFifo { ScopeSize };
    std::vector<float> scopeData = std::vector<float> (ScopeSize, 0.0f);

private:
    void timerCallback() override;
    void readParams (idgs::VoiceParams& p) const;
    void setTable (int osc, std::unique_ptr<idgs::Wavetable> t);
    void setSamples (int osc, std::unique_ptr<idgs::SampleSet> s, const juce::Array<juce::File>& files);
    void retire (std::shared_ptr<void> p);

    idgs::SynthEngine engine;
    idgs::ModGlobal modGlobal;
    std::unique_ptr<idgs::ModBlock> modBlock = std::make_unique<idgs::ModBlock>();
    std::atomic<double> uiBpm { 120.0 };
    double ppqNow = 0.0;
    idg::OutputChain output;
    idgs::VoiceParams vp;

    double sr = 44100.0;
    juce::SmoothedValue<float> masterGain;
    juce::AudioBuffer<float> work;

    std::atomic<bool> panicFlag { false };
    int panicMute = 0;
    float panicGain = 1.0f;

    /** Tables / samples : l'audio lit des pointeurs atomiques ; les anciens objets sont détruits
        par l'interface 2 s après leur remplacement (jamais dans le thread audio). */
    struct OscAssets
    {
        std::atomic<const idgs::Wavetable*> table { nullptr };
        std::atomic<const idgs::SampleSet*> samples { nullptr };
        std::shared_ptr<idgs::Wavetable> tableOwner;
        std::shared_ptr<idgs::SampleSet> samplesOwner;
        juce::Array<juce::File> files;
    } assets[idgs::NumOscs];
    std::vector<std::pair<double, std::shared_ptr<void>>> retired;

    // pointeurs vers les valeurs brutes des paramètres (lecture sans verrou)
    struct OscRaw
    {
        std::atomic<float>* on, *mode, *table, *pos, *oct, *semi, *fine, *level, *pan, *phase, *phrand, *unison, *detune, *blend,
            *spread, *stretch, *shift, *warp[2], *warpAmt[2], *warpSrc[2], *loop, *root, *route;
    } oscRaw[idgs::NumOscs] {};
    struct FiltRaw { std::atomic<float>* on, *type, *cutoff, *reso, *drive, *key, *mix; } filtRaw[2] {};
    struct Raw
    {
        std::atomic<float>* subOn, *subShape, *subOct, *subLevel, *subPan, *subRoute, *noiseOn, *noiseType, *noiseLevel,
            *noisePan, *noiseTone, *noiseRoute, *serial, *delay, *attack, *hold, *decay, *sustain, *release, *aCurve, *dCurve,
            *rCurve, *voices, *voiceMode, *glide, *glideCurve, *glideLegato, *velSens, *bendRange, *masterVol, *ceiling;
    } raw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IDGemsSynthProcessor)
};
