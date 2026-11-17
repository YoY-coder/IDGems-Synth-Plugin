#pragma once
#include "../Core/JuceIncludes.h"

namespace idgs
{
/** Analyse spectrale d'un son (magnitudes par trame) pour la resynthèse spectrale. */
struct SpectralData
{
    static constexpr int Order = 11, N = 1 << Order, Bins = N / 2 + 1, Hop = N / 4;
    static constexpr double MaxSeconds = 20.0;
    int numFrames = 0;
    std::vector<float> mags; // numFrames * Bins
    const float* frame (int f) const { return mags.data() + (size_t) juce::jlimit (0, juce::jmax (0, numFrames - 1), f) * Bins; }
    static std::unique_ptr<SpectralData> analyse (const float* mono, int len);
};

/** Empreinte spectrale : énergie de 32 bandes au fil du temps (100 trames / s). */
struct FingerprintData
{
    static constexpr int NumBands = 32;
    static constexpr double FrameRate = 100.0;
    int numFrames = 0;
    std::vector<float> env; // numFrames * NumBands (0..1)
    static float bandFreq (int b) { return 80.0f * std::pow (150.0f, (float) b / (float) (NumBands - 1)); } // 80 Hz .. 12 kHz
    static std::unique_ptr<FingerprintData> analyse (const float* mono, int len, double sampleRate);
};

/** Un sample chargé en mémoire (stéréo), avec sa note d'origine et sa zone de clavier. */
struct SampleZone
{
    juce::AudioBuffer<float> data;   // 2 canaux
    double sampleRate = 44100.0;
    int root = 60;                   // note jouée à la hauteur d'origine (C3 = 60)
    int lo = 0, hi = 127;            // zone du clavier (multisample)
    juce::File file;
    std::unique_ptr<SpectralData> spectral;       // calculé à la demande (modes spectraux)
    std::unique_ptr<FingerprintData> fingerprint; // calculé pour l'empreinte spectrale

    int length() const { return data.getNumSamples(); }
};

/** Ensemble de samples d'un oscillateur : un seul (mode Sample) ou plusieurs (mode Multisample).
    Construit sur le thread de l'interface puis jamais modifié : lecture sûre depuis le thread audio. */
struct SampleSet
{
    std::vector<std::unique_ptr<SampleZone>> zones;
    juce::String name;

    /** Zone utilisée pour une note. */
    int zoneFor (int note) const
    {
        for (int i = 0; i < (int) zones.size(); ++i)
            if (note >= zones[(size_t) i]->lo && note <= zones[(size_t) i]->hi) return i;
        return zones.empty() ? -1 : 0;
    }

    /** Durée max chargée par fichier (protège la mémoire). */
    static constexpr double maxSeconds = 60.0;

    /** Charge un ou plusieurs fichiers audio. Multisample : les zones sont réparties selon la note
        trouvée dans le nom de chaque fichier (ex. « Piano_C3.wav », « Pad A#2 »), sinon par ordre. */
    static std::unique_ptr<SampleSet> load (const juce::Array<juce::File>& files, bool multi, juce::String& error);

    /** Note trouvée dans un nom de fichier (C3 = 60), ou -1. */
    static int noteFromName (const juce::String& name);

    /** Formats reconnus (wav, aiff, flac, ogg, mp3 selon le système). */
    static bool isAudioFile (const juce::File& f);

    /** Data bending : lit les octets bruts de n'importe quel fichier comme de l'audio
        (canal 0 = lecture 8 bits, canal 1 = lecture 16 bits). 4 Mo max. */
    static std::unique_ptr<SampleSet> loadRaw (const juce::File& f, juce::String& error);
    /** Calcule l'analyse spectrale / l'empreinte de chaque zone (thread de l'interface). */
    void computeSpectral();
    void computeFingerprint();

    /** Lit n'importe quel fichier audio en mono (pour la conversion sample -> wavetable). */
    static std::vector<float> loadMono (const juce::File& f, juce::String& error);
};
} // namespace idgs
