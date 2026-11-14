#pragma once
#include "../Core/JuceIncludes.h"

namespace idgs
{
/** Un sample chargé en mémoire (stéréo), avec sa note d'origine et sa zone de clavier. */
struct SampleZone
{
    juce::AudioBuffer<float> data;   // 2 canaux
    double sampleRate = 44100.0;
    int root = 60;                   // note jouée à la hauteur d'origine (C3 = 60)
    int lo = 0, hi = 127;            // zone du clavier (multisample)
    juce::File file;

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

    /** Lit n'importe quel fichier audio en mono (pour la conversion sample -> wavetable). */
    static std::vector<float> loadMono (const juce::File& f, juce::String& error);
};
} // namespace idgs
