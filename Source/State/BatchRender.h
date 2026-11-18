#pragma once
#include "../Core/JuceIncludes.h"

class IDGemsSynthProcessor;

namespace idgs
{
/** Réglages d'un rendu hors ligne. */
struct RenderSettings
{
    enum Mode { OneShot, Loop, Texture };
    int mode = OneShot;
    int count = 8;               // nombre de variations
    bool hq = false;             // HQ : 96 kHz + suréchantillonnage du destructeur ; Draft : 48 kHz
    bool randomize = true;       // variations par le système de random (sinon : N rendus du même patch)
    float amount = 0.5f;         // dosage du random entre deux variations
    float noteSeconds = 1.5f;    // One-shot : durée de la note
    int bars = 2;                // Loop : longueur en mesures
    float textureSeconds = 20.0f;
    int note = 48;
    bool chord = false;          // accord (note + quinte + octave) au lieu d'une note
    bool normalize = true;       // crête à -1 dB
    double bpm = 120.0;
    juce::String name = "IDGemsSynth";
};

/** Rendu hors ligne de N variations (thread de l'interface, par tranches : l'interface reste fluide).
    Utilise un processeur séparé qui partage les tables / samples du synthé : le son en cours n'est pas touché. */
class BatchRenderer : private juce::Timer
{
public:
    explicit BatchRenderer (IDGemsSynthProcessor& p);
    ~BatchRenderer() override;

    bool start (const RenderSettings& s);
    void cancel();
    bool isRunning() const { return running; }
    float progress() const;
    juce::String status() const { return statusText; }
    const juce::Array<juce::File>& files() const { return rendered; }
    juce::File folder() const { return outFolder; }
    std::function<void()> onFinished;

    /** Rend une seule variation tout de suite (sans random) : renvoie le fichier (vide si échec). Utilisé par
        l'export glisser-déposer et les tests. */
    juce::File renderNow (const RenderSettings& s, const juce::File& target);

    static juce::File rendersFolder();
    /** Tests : avance le rendu sans boucle de messages. */
    void step() { timerCallback(); }

private:
    struct Job;
    void timerCallback() override;
    bool renderChunk (Job& j, double budgetMs);
    std::unique_ptr<Job> makeJob (const RenderSettings& s, const juce::File& file, uint32_t seed, bool randomize);
    void finishJob (Job& j);

    IDGemsSynthProcessor& proc;
    RenderSettings settings;
    std::unique_ptr<Job> job;
    bool running = false;
    int index = 0;
    juce::Array<juce::File> rendered;
    juce::File outFolder;
    juce::String statusText;
};
} // namespace idgs
