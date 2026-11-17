#pragma once
#include "LfoShape.h"
#include "../Core/Params.h"

namespace idgs
{
/** Sources de modulation. Sauvées par nom (sourceKey), jamais par numéro : on peut en ajouter sans casser les presets. */
enum ModSrcId
{
    MSrcMacro0 = 0,
    MSrcLfo0 = MSrcMacro0 + NumMacros,
    MSrcRnd0 = MSrcLfo0 + NumLfos,
    MSrcEnv0 = MSrcRnd0 + NumRandMods,
    MSrcVelocity = MSrcEnv0 + NumEnvelopes,
    MSrcNote,
    MSrcNoteRandom,
    MSrcModWheel,
    MSrcAftertouch,
    MSrcPitchBend,
    MSrcFollow0,                         // suiveurs d'enveloppe des bus A, B, C
    NumModSrc = MSrcFollow0 + NumBuses
};

juce::String sourceKey (int src);            // "macro1", "lfo3", "env2", "velocity"...
int sourceFromKey (const juce::String& key);
juce::String sourceName (int src);           // nom affiché
juce::Colour sourceColour (int src);
bool sourceIsPerVoice (int src);
bool sourceIsBipolar (int src);              // pitch bend : centré sur 0

/** Champs des réglages de voix qui peuvent être modulés (cibles par voix). */
enum ModField
{
    FOscPos = 0, FOscLevel, FOscPan, FOscSemi, FOscFine, FOscDetune, FOscBlend, FOscSpread, FOscStretch, FOscShift,
    FOscWarp1, FOscWarp2, NumOscFields,
    FSubLevel = NumOscFields * NumOscs, FSubPan, FNoiseLevel, FNoisePan, FNoiseTone,
    FFilt0, // + f * NumFiltFields + k
    NumFiltFields = 5,
    FEnvAttack = FFilt0 + 2 * NumFiltFields, FEnvDecay, FEnvSustain, FEnvRelease, FGlide,
    NumModFields
};
enum FiltField { FFCutoff, FFReso, FFDrive, FFKey, FFMix };

/** Une connexion source -> paramètre. Atomique : modifiée par l'interface, lue par l'audio. */
struct ModSlot
{
    std::atomic<int> src { -1 }, param { -1 };   // param = index dans getParameters()
    std::atomic<float> depth { 0.0f };           // -1..1 (en fraction de la course du paramètre)
    std::atomic<bool> bipolar { false };
};
constexpr int MaxModSlots = 64;

/** Copie compacte d'une connexion pour le thread audio (lue une fois par bloc). */
struct ActiveSlot { int src, field, param; float depth; bool bipolar; };
/** Connexion vers un paramètre global (effets des bus) : rack + index dans la banque du rack. */
struct GlobalSlot { int src, rack, index; float depth; bool bipolar; };

/** Partie partagée interface <-> audio. */
struct ModShared
{
    ModSlot slots[MaxModSlots];
    float tables[NumLfos][2][idg::LfoShape::tableSize] {};
    std::atomic<int> tableIndex[NumLfos];
    idg::LfoShape shapes[NumLfos];                     // côté interface uniquement

    // affichage
    std::atomic<float> sourceValue[NumModSrc];     // valeur courante (voix la plus récente pour les sources par voix)
    std::atomic<float> lfoPhase[NumLfos];
    std::atomic<float> fieldValue01[NumModFields];     // valeur modulée (0..1) de chaque cible, voix la plus récente
    std::atomic<int> version { 0 };                    // incrémenté à chaque changement de connexion

    ModShared();
    void setShape (int lfo, const idg::LfoShape& s);

    int addConnection (int src, int param, float depth, bool bipolar = false);
    void removeConnection (int slot) { slots[slot].src = -1; slots[slot].param = -1; ++version; }
    int findConnection (int src, int param) const;
    int countForSource (int src) const;
    bool sourceUsed (int src) const { return countForSource (src) > 0; }
    void clearAll();
};

/** Correspondance paramètre <-> champ de voix, et conversion normalisé -> valeur réelle. */
class ModTargets
{
public:
    void init (juce::AudioProcessor& p);
    int fieldForParam (int paramIndex) const { return paramIndex >= 0 && paramIndex < (int) toField.size() ? toField[(size_t) paramIndex] : -1; }
    int paramForField (int f) const { return fields[(size_t) f].param; }
    bool isModulatable (int paramIndex) const { return fieldForParam (paramIndex) >= 0 || globalTarget (paramIndex).first >= 0; }
    /** Paramètres globaux (effets) : (rack, index dans la banque), ou (-1, -1). */
    void setGlobalTarget (int paramIndex, int rack, int index)
    {
        if (paramIndex >= 0 && paramIndex < (int) toGlobal.size()) toGlobal[(size_t) paramIndex] = { rack, index };
    }
    std::pair<int, int> globalTarget (int paramIndex) const
    {
        return paramIndex >= 0 && paramIndex < (int) toGlobal.size() ? toGlobal[(size_t) paramIndex] : std::pair<int, int> { -1, -1 };
    }
    float toReal (int f, float norm01) const { return fields[(size_t) f].range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm01)); }
    juce::RangedAudioParameter* parameter (int f) const { return fields[(size_t) f].prm; }
    juce::String paramId (int paramIndex) const;
    int paramIndexForId (const juce::String& id) const;

private:
    struct Field { int param = -1; juce::RangedAudioParameter* prm = nullptr; juce::NormalisableRange<float> range; };
    std::array<Field, NumModFields> fields;
    std::vector<int> toField;
    std::vector<std::pair<int, int>> toGlobal;
    juce::StringArray ids;
};
} // namespace idgs
