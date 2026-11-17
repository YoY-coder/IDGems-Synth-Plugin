#pragma once
#include "../Core/JuceIncludes.h"

class IDGemsSynthProcessor;

namespace idgs
{
/** Sections du random (boutons dé + cadenas de section). */
enum RandomSection { SecOsc, SecFilters, SecEnv, SecMod, SecFx, SecPlay, NumRandomSections };
const juce::StringArray& randomSectionNames();

/** Personnalité : règles qui orientent le random (plages, probabilités, poids des choix). */
struct Personality
{
    struct Rule
    {
        juce::String pattern;              // identifiant exact, ou préfixe terminé par '*', ou « fam:N » (famille d'effets)
        float lo = -1.0f, hi = -1.0f;      // plage normalisée (-1 = inchangée)
        float prob = -1.0f;                // probabilité d'allumage (booléens, effets d'une famille)
        std::vector<float> weights;        // poids des choix (listes)
    };
    juce::String name;
    std::vector<Rule> rules;
    const Rule* find (const juce::String& id) const;
    static std::vector<Personality> builtIn();
    /** Personnalités utilisateur : Documents/IDGems Synth/Personnalites/*.json */
    static std::vector<Personality> loadUser();
};

/** Système de random (thread de l'interface uniquement). */
class RandomEngine : private juce::Timer
{
public:
    enum class Scope { All, Section, Module, FxRack, FxFamily, FxModule };

    struct Target { Scope scope = Scope::All; int section = 0; juce::String module; int rack = 0, index = 0; };

    explicit RandomEngine (IDGemsSynthProcessor& p);
    ~RandomEngine() override;

    /** Demande un tirage (attend le prochain temps / la prochaine mesure si la synchro est active). */
    void request (const Target& t, uint32_t seed = 0);
    /** Tirage immédiat (renvoie le seed utilisé). */
    uint32_t perform (const Target& t, uint32_t seed, float amount, bool useGuards, const juce::String& label);

    // ---- cadenas et plages (par paramètre, par module, par section)
    bool isLocked (int paramIndex) const { return paramIndex >= 0 && paramIndex < (int) meta.size() && meta[(size_t) paramIndex].locked; }
    void setLocked (int paramIndex, bool l);
    juce::Range<float> getRange (int paramIndex) const;
    void setRange (int paramIndex, float lo, float hi);
    void resetRange (int paramIndex);
    bool hasCustomRange (int paramIndex) const;
    bool isRandomizable (int paramIndex) const { return paramIndex >= 0 && paramIndex < (int) meta.size() && meta[(size_t) paramIndex].randomizable; }
    bool sectionLocked[NumRandomSections] {};
    juce::StringArray lockedModules;
    void setSectionLocked (int section, bool l);
    void setModuleLocked (const juce::String& module, bool l);
    bool isModuleLocked (const juce::String& module) const { return lockedModules.contains (module); }
    /** Cadenas du paramètre, de son module, de son rack ou de sa section. */
    bool isFrozen (int paramIndex) const;

    // ---- personnalités (intégrées : paramètre rand_personality ; utilisateur : fichiers JSON)
    int userPersonality = -1;
    juce::StringArray userPersonalityNames() const;
    const Personality* currentPersonality() const;
    juce::String labelFor (const Target& t) const;
    bool isPending() const { return pending.active; }
    bool isGliding() const { return glide.active; }

    // ---- historique
    struct Entry { uint32_t seed = 0; juce::String label; bool favourite = false; juce::ValueTree state; };
    const std::vector<Entry>& getHistory() const { return history; }
    int getCursor() const { return cursor; }
    void recall (int index);
    void toggleFavourite (int index);
    uint32_t lastSeed() const { return seedShown; }

    // ---- snapshots / morph pad
    static constexpr int NumSnapshots = 8;
    struct Snapshot { bool valid = false; float x = 0.5f, y = 0.5f; std::vector<float> values; };
    Snapshot snapshots[NumSnapshots];
    void storeSnapshot (int i, float x, float y);
    void clearSnapshot (int i);
    void moveSnapshot (int i, float x, float y) { snapshots[i].x = x; snapshots[i].y = y; rebuildMorphList(); applyMorph (true); }
    void applyMorph (bool force = false);
    int numSnapshots() const;

    std::function<void()> onChange;
    std::atomic<int> version { 0 };   // incrémenté à chaque changement (cadenas, plages, historique...)
    juce::String lastMessage;   // résultat des garde-fous (affiché par l'interface)

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);
    int sectionOf (int paramIndex) const { return meta[(size_t) paramIndex].section; }

private:
    void timerCallback() override;
    void changed() { ++version; if (onChange) onChange(); }
    struct Meta
    {
        int section = -1; juce::String module; bool randomizable = false; bool locked = false;
        float lo = 0, hi = 1, defLo = 0, defHi = 1;
        int rack = -1, bankIndex = -1, effect = -1;
    };
    void classify();
    bool selected (int i, const Target& t) const;
    float drawValue (int i, juce::Random& r, const Personality* pers, float intensity, float cur);
    std::vector<std::pair<int, float>> draw (const Target& t, uint32_t seed, float amount);
    void setNorm (int i, float v);
    int64_t syncCounter() const;
    bool guardMeasure (float& rmsDb, float& grDb);
    void addHistory (uint32_t seed, const juce::String& label);
    void rebuildMorphList();
    void pushRangesToAudio();

    IDGemsSynthProcessor& proc;
    std::vector<Meta> meta;
    std::vector<Personality> personalities, userPersonalities;
    std::vector<Entry> history;
    int cursor = -1;
    uint32_t seedShown = 0;
    struct Pending { bool active = false; Target t; uint32_t seed = 0; int64_t counter = 0; } pending;
    struct Glide
    {
        bool active = false; double start = 0, duration = 0; std::vector<std::pair<int, std::pair<float, float>>> moves;
        juce::String label; uint32_t seed = 0;
    } glide;
    int64_t lastMutateBar = -1;
    std::vector<int> morphParams;
    float lastMorphX = -1.0f, lastMorphY = -1.0f;
    std::unique_ptr<IDGemsSynthProcessor> probe;
};
} // namespace idgs
