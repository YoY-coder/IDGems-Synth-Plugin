#include "PresetManager.h"
#include "../PluginProcessor.h"

namespace idgs
{
namespace
{
struct Factory { juce::String name, category, tags; };
const Factory factoryList[] {
    { "Init", "Factory", "init" },
    { "Basse Pulse", "Basse", "pwm, legato, glide, ladder" },
    { IDG_T ("Supersaw Néon"), "Lead", "unison, scie, large" },
    { "Cloche Tordue", IDG_T ("Cloche / Métal"), IDG_T ("inharmonique, stretch, fm") },
    { "Pad Formant", "Pad", "formant, voyelles, vent" },
    { "Sync Hurlant", "Lead", "hard sync, mono, drive" },
    { IDG_T ("Métal FM"), IDG_T ("Cloche / Métal"), "fm, comb, keytrack" },
    { IDG_T ("Poussière Digitale"), "Texture", "bruit digital, destructeur, quantize" },
    { "Sub Profond", "Basse", "sub, mono" },
    { IDG_T ("Vinyle Rêveur"), "Texture", "vinyle, lo-fi, doux" },
};
} // namespace

PresetManager::PresetManager (IDGemsSynthProcessor& p) : proc (p)
{
    loadFavourites();
    rescan();
}

juce::File PresetManager::folder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("IDGems Synth").getChildFile ("Presets");
    f.createDirectory();
    return f;
}

const juce::StringArray& PresetManager::categories()
{
    static const juce::StringArray c { "Basse", "Lead", "Pad", "Pluck", "Texture", IDG_T ("Cloche / Métal"), IDG_T ("Expérimental"), "Utilisateur" };
    return c;
}

void PresetManager::loadFavourites()
{
    auto f = folder().getChildFile ("favourites.txt");
    favouriteKeys = juce::StringArray::fromLines (f.loadFileAsString());
    favouriteKeys.removeEmptyStrings();
}

void PresetManager::saveFavourites()
{
    folder().getChildFile ("favourites.txt").replaceWithText (favouriteKeys.joinIntoString ("\n"));
}

void PresetManager::rescan()
{
    const auto currentKey = currentIdx >= 0 && currentIdx < (int) presets.size() ? presets[(size_t) currentIdx].key() : juce::String();
    presets.clear();
    int idx = 0;
    for (const auto& f : factoryList)
    {
        Info i;
        i.name = f.name; i.author = "IDGems"; i.category = f.category; i.tags = f.tags; i.factoryIndex = idx++;
        presets.push_back (i);
    }
    for (const auto& file : folder().findChildFiles (juce::File::findFiles, true, juce::String ("*") + extension))
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr) continue;
        Info i;
        i.file = file;
        i.name = xml->getStringAttribute ("presetName", file.getFileNameWithoutExtension());
        i.author = xml->getStringAttribute ("author");
        i.category = xml->getStringAttribute ("category", "Utilisateur");
        i.tags = xml->getStringAttribute ("tags");
        presets.push_back (i);
    }
    for (auto& p : presets) p.favourite = favouriteKeys.contains (p.key());
    std::stable_sort (presets.begin() + (int) std::size (factoryList), presets.end(),
                      [] (const Info& a, const Info& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    if (currentKey.isNotEmpty())
        for (int i = 0; i < (int) presets.size(); ++i) if (presets[(size_t) i].key() == currentKey) currentIdx = i;
    if (onChange) onChange();
}

std::vector<int> PresetManager::filter (const juce::String& search, const juce::String& category, bool favouritesOnly) const
{
    std::vector<int> out;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const auto& p = presets[(size_t) i];
        if (favouritesOnly && ! p.favourite) continue;
        if (category.isNotEmpty() && p.category != category) continue;
        if (search.isNotEmpty())
        {
            auto hay = (p.name + " " + p.author + " " + p.tags + " " + p.category).toLowerCase();
            bool all = true;
            for (auto& w : juce::StringArray::fromTokens (search.toLowerCase(), " ", ""))
                if (w.isNotEmpty() && ! hay.contains (w)) { all = false; break; }
            if (! all) continue;
        }
        out.push_back (i);
    }
    return out;
}

void PresetManager::applyFactory (int index)
{
    proc.resetToInit();
    auto set = [this] (const juce::String& id, float v) { proc.setValue (id, v); };
    auto table = [&] (int osc, const juce::String& name) { set (oscId (osc, "table"), (float) WavetableBank::names().indexOf (name)); };
    switch (index)
    {
        case 1: // Basse Pulse
            table (0, "Pulse (PWM)"); set (P::oscAPos, 0.3f); set (P::oscAOct, -1);
            set (P::voiceMode, VoiceLegato); set (P::glide, 80.0f);
            set (P::subOn, 1); set (P::subLevel, 0.5f);
            set (filtId (0, "on"), 1); set (filtId (0, "type"), FiltLadder); set (filtId (0, "cutoff"), 650.0f);
            set (filtId (0, "reso"), 0.4f); set (filtId (0, "drive"), 0.3f); set (filtId (0, "key"), 0.5f);
            set (P::env1Decay, 300.0f); set (P::env1Sustain, 0.6f); set (P::env1Release, 120.0f);
            break;
        case 2: // Supersaw Néon
            set (P::oscAPos, 0.6667f); set (oscId (0, "unison"), 7); set (oscId (0, "detune"), 0.35f); set (oscId (0, "spread"), 1.0f);
            set (oscId (1, "on"), 1); table (1, "Harmoniques"); set (oscId (1, "pos"), 1.0f); set (oscId (1, "oct"), -1);
            set (oscId (1, "level"), 0.4f); set (oscId (1, "unison"), 5); set (oscId (1, "detune"), 0.25f);
            set (filtId (0, "on"), 1); set (filtId (0, "cutoff"), 6000.0f); set (filtId (0, "reso"), 0.2f);
            set (P::env1Release, 300.0f);
            break;
        case 3: // Cloche Tordue
            table (0, "Harmoniques"); set (P::oscAPos, 0.6f); set (oscId (0, "stretch"), 0.35f);
            set (oscId (1, "on"), 1); set (oscId (1, "table"), 0); set (oscId (1, "oct"), 1); set (oscId (1, "level"), 0.3f);
            set (oscId (1, "warp1"), WarpFM); set (oscId (1, "warp1src"), SrcOscA); set (oscId (1, "warp1amt"), 0.3f);
            set (P::env1Attack, 1.0f); set (P::env1Decay, 2500.0f); set (P::env1Sustain, 0.0f); set (P::env1Release, 2500.0f);
            break;
        case 4: // Pad Formant
            table (0, "Formant"); set (P::oscAPos, 0.4f); set (oscId (0, "unison"), 5); set (oscId (0, "detune"), 0.2f);
            set (filtId (0, "on"), 1); set (filtId (0, "type"), FiltFormant); set (filtId (0, "cutoff"), 800.0f);
            set (filtId (0, "reso"), 0.4f); set (filtId (0, "mix"), 0.6f);
            set (P::noiseOn, 1); set (P::noiseType, NoiseWind); set (P::noiseLevel, 0.15f);
            set (P::env1Attack, 900.0f); set (P::env1Sustain, 1.0f); set (P::env1Release, 2500.0f);
            break;
        case 5: // Sync Hurlant
            set (P::oscAPos, 0.6667f); set (oscId (0, "warp1"), WarpHardSync); set (oscId (0, "warp1src"), SrcOscB); set (oscId (0, "warp1amt"), 0.4f);
            set (oscId (1, "table"), 0); set (oscId (1, "pos"), 0.0f);
            set (filtId (0, "on"), 1); set (filtId (0, "cutoff"), 5000.0f); set (filtId (0, "drive"), 0.4f);
            set (P::voiceMode, VoiceLegato); set (P::glide, 60.0f);
            break;
        case 6: // Métal FM
            set (P::oscAPos, 0.0f); set (oscId (0, "warp1"), WarpFM); set (oscId (0, "warp1src"), SrcOscB); set (oscId (0, "warp1amt"), 0.6f);
            set (oscId (1, "table"), 0); set (oscId (1, "pos"), 0.0f); set (oscId (1, "oct"), 1); set (oscId (1, "semi"), 7);
            set (filtId (0, "on"), 1); set (filtId (0, "type"), FiltCombPlus); set (filtId (0, "cutoff"), 220.0f);
            set (filtId (0, "reso"), 0.8f); set (filtId (0, "key"), 1.0f); set (filtId (0, "mix"), 0.6f);
            set (P::env1Decay, 900.0f); set (P::env1Sustain, 0.2f); set (P::env1Release, 900.0f);
            break;
        case 7: // Poussière Digitale
            table (0, "Pulse (PWM)"); set (P::oscALevel, 0.25f); set (oscId (0, "warp1"), WarpQuantize); set (oscId (0, "warp1amt"), 0.7f);
            set (P::noiseOn, 1); set (P::noiseType, NoiseDigital); set (P::noiseLevel, 0.6f);
            set (filtId (0, "on"), 1); set (filtId (0, "type"), FiltDestroy); set (filtId (0, "cutoff"), 3000.0f); set (filtId (0, "reso"), 0.6f);
            set (P::env1Release, 1200.0f);
            break;
        case 8: // Sub Profond
            set (P::oscALevel, 0.3f);
            set (P::subOn, 1); set (P::subLevel, 0.9f);
            set (P::voiceMode, VoiceMono); set (P::env1Release, 150.0f);
            break;
        case 9: // Vinyle Rêveur
            table (0, "Harmoniques"); set (P::oscAPos, 0.2f); set (oscId (0, "unison"), 3); set (oscId (0, "detune"), 0.15f);
            set (P::noiseOn, 1); set (P::noiseType, NoiseVinyl); set (P::noiseLevel, 0.35f); set (P::noiseRoute, RouteDirect);
            set (filtId (0, "on"), 1); set (filtId (0, "type"), FiltLP12); set (filtId (0, "cutoff"), 1800.0f);
            set (P::env1Attack, 400.0f); set (P::env1Release, 1800.0f);
            break;
        default: break;
    }
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= (int) presets.size()) return false;
    const auto& info = presets[(size_t) index];
    if (info.factoryIndex >= 0) applyFactory (info.factoryIndex);
    else
    {
        auto xml = juce::XmlDocument::parse (info.file);
        if (xml == nullptr) return false;
        proc.restoreState (juce::ValueTree::fromXml (*xml));
    }
    current = info.name;
    currentIdx = index;
    if (onChange) onChange();
    return true;
}

bool PresetManager::loadFile (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return false;
    proc.restoreState (juce::ValueTree::fromXml (*xml));
    current = xml->getStringAttribute ("presetName", f.getFileNameWithoutExtension());
    currentIdx = -1;
    for (int i = 0; i < (int) presets.size(); ++i) if (presets[(size_t) i].file == f) currentIdx = i;
    if (onChange) onChange();
    return true;
}

bool PresetManager::save (const juce::String& name, const juce::String& author, const juce::String& category, const juce::String& tags)
{
    auto state = proc.captureState();
    auto xml = state.createXml();
    if (xml == nullptr) return false;
    xml->setAttribute ("presetName", name);
    xml->setAttribute ("author", author);
    xml->setAttribute ("category", category);
    xml->setAttribute ("tags", tags);
    auto file = folder().getChildFile (juce::File::createLegalFileName (name) + extension);
    if (! xml->writeTo (file)) return false;
    current = name;
    rescan();
    for (int i = 0; i < (int) presets.size(); ++i) if (presets[(size_t) i].file == file) currentIdx = i;
    if (onChange) onChange();
    return true;
}

void PresetManager::toggleFavourite (int index)
{
    if (index < 0 || index >= (int) presets.size()) return;
    auto& p = presets[(size_t) index];
    p.favourite = ! p.favourite;
    if (p.favourite) favouriteKeys.addIfNotAlreadyThere (p.key());
    else favouriteKeys.removeString (p.key());
    saveFavourites();
    if (onChange) onChange();
}

void PresetManager::next (int delta)
{
    if (presets.empty()) return;
    const int n = (int) presets.size();
    const int i = currentIdx < 0 ? 0 : ((currentIdx + delta) % n + n) % n;
    load (i);
}
} // namespace idgs
