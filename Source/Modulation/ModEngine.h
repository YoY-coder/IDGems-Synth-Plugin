#pragma once
#include "ModSystem.h"
#include "../Synth/Envelope.h"
#include "../Core/Dsp.h"

namespace idgs
{
/** Tout ce dont une voix a besoin pour se moduler pendant un bloc (préparé par le moteur global). */
struct ModBlock
{
    ActiveSlot slots[MaxModSlots];
    int numSlots = 0;
    GlobalSlot gslots[MaxModSlots];      // connexions vers les effets des bus
    int numGlobal = 0;
    float global[NumModSrc] {};      // valeurs des sources globales (0..1)
    float globalR[NumModSrc] {};     // canal droit (LFO stéréo)
    float base01[NumModFields] {};       // valeur de base normalisée de chaque cible
    bool srcUsed[NumModSrc] {};
    bool fieldUsed[NumModFields] {};
    const ModTargets* targets = nullptr;

    struct Lfo
    {
        const float* table = nullptr;
        float inc = 0.0f;                // cycles par échantillon
        int mode = LfoNoteVoice;
        float phase = 0.0f, fadeMs = 0.0f, smoothMs = 0.0f;
    } lfo[NumLfos];
    Envelope::Settings env[NumEnvelopes];
};

/** Sources globales : macros, LFO (modes libre / note global / calé hôte), random, molette, aftertouch, bend. */
class ModGlobal
{
public:
    void init (juce::AudioProcessorValueTreeState& apvts, ModShared& shared, const ModTargets& targets);
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset();

    /** Calcule les sources et prépare le ModBlock. noteOns = nombre de notes jouées depuis le dernier bloc. */
    void process (ModBlock& b, int numSamples, double bpm, double ppq, bool playing, int noteOns,
                  float modWheel, float aftertouch, float bend01, const float* followers);

private:
    ModShared* shared = nullptr;
    const ModTargets* targets = nullptr;
    double sr = 44100.0;

    struct LfoRaw { std::atomic<float>* rate, *sync, *div, *mode, *phase, *fade, *smooth, *stereo; } lr[NumLfos] {};
    struct RndRaw { std::atomic<float>* rate, *sync, *div, *style, *retrig; } rr[NumRandMods] {};
    struct EnvRaw { std::atomic<float>* v[9]; } er[NumEnvelopes] {};
    std::atomic<float>* macros[NumMacros] {};

    double lfoPhase[NumLfos] {};
    float lfoSmooth[NumLfos] {};
    struct Rnd { double t = 0.0; int64_t last = -1; float from = 0, to = 0; double lx = 0.1, ly = 0, lz = 0; } rnd[NumRandMods];
    idg::dsp::Rng rng;
};
} // namespace idgs
