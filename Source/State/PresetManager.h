#pragma once
#include "../Core/JuceIncludes.h"

class IDGemsSynthProcessor;

namespace idgs
{
/** Gestion des presets façon Vital (reprise d'IDGems) : catégories, tags, auteur, favoris, recherche.
    Fichiers : Documents/IDGems Synth/Presets, extension .idgsynth (XML). */
class PresetManager
{
public:
    struct Info
    {
        juce::String name, author, category, tags;
        juce::File file;          // vide pour les presets d'usine
        int factoryIndex = -1;
        bool favourite = false;
        juce::String key() const { return factoryIndex >= 0 ? "factory:" + name : file.getFullPathName(); }
    };

    explicit PresetManager (IDGemsSynthProcessor& p);

    static juce::File folder();
    static const juce::StringArray& categories();
    static constexpr const char* extension = ".idgsynth";

    void rescan();
    const std::vector<Info>& getAll() const { return presets; }
    std::vector<int> filter (const juce::String& search, const juce::String& category, bool favouritesOnly) const;

    bool load (int index);
    bool loadFile (const juce::File& f);
    bool save (const juce::String& name, const juce::String& author, const juce::String& category, const juce::String& tags);
    void toggleFavourite (int index);
    void next (int delta);

    juce::String currentName() const { return current; }
    int currentIndex() const { return currentIdx; }
    void setCurrentName (const juce::String& n) { current = n; currentIdx = -1; if (onChange) onChange(); }
    std::function<void()> onChange;

private:
    void loadFavourites();
    void saveFavourites();
    void applyFactory (int index);

    IDGemsSynthProcessor& proc;
    std::vector<Info> presets;
    juce::StringArray favouriteKeys;
    juce::String current { "Init" };
    int currentIdx = 0;
};
} // namespace idgs
