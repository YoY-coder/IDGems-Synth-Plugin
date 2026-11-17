#pragma once
#include "../Core/JuceIncludes.h"

namespace idg
{
enum Family { FamGlitch, FamDestruction, FamPitch, FamFilters, FamSpace, FamMovement, NumFamilies };

enum FxId
{
    FxStutter, FxBeatRepeat, FxReverse, FxTapeStop, FxShuffle, FxFreeze, FxScratch,
    FxBitcrush, FxDistortion, FxRingMod, FxFreqShift, FxCorruption,
    FxPitchShift, FxFormant,
    FxFilter, FxComb, FxVocoder, FxPixelSort,
    FxDelay, FxReverb, FxChorus, FxFlanger, FxPhaser,
    FxGate, FxTremolo, FxAutoPan, FxStereo,
    NumFx
};

enum class PType { Float, Bool, Choice };
enum class PitchKind { None, Semis, Hz, Midi };

struct ParamDef
{
    juce::String id, name, unit, help;
    PType type = PType::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
    bool symSkew = false;
    juce::StringArray choices;
    int effect = -1;          // -1 = global
    int family = -1;
    int localIndex = -1;      // index du paramètre dans son effet (hors communs)
    bool randomizable = false;
    float rmin = 0.0f, rmax = 1.0f; // plage de random par défaut (normalisée)
    PitchKind pitch = PitchKind::None;
    bool common = false;      // on / mix / mode / par

    juce::NormalisableRange<float> range() const;
    juce::String toText (float value) const;
};

struct EffectDef
{
    juce::String key, name, shortName, help;
    Family family;
    std::vector<ParamDef> params; // paramètres spécifiques
    float defMix = 1.0f;
    bool triggerable = false;     // réagit aux déclenchements (séquenceur, MIDI, transitoires)
    bool stft = false;            // utilise une FFT (latence en mode normal)
    bool noZeroLatency = false;   // indisponible en mode sans latence
};

const std::vector<EffectDef>& effectDefs();
const juce::StringArray& familyNames();
juce::Colour familyColour (int family);

// Divisions rythmiques
const juce::StringArray& divisionNames();
double divisionBeats (int index);
int divisionIndex (const juce::String& name);

const juce::StringArray& seqRateNames();
double seqRateBeats (int index);

constexpr int NumRacks = 4; // Bus A, Bus B, Bus C, Master

inline juce::String fxParamId (int fx, const juce::String& key) { return "fx" + juce::String (fx) + "_" + key; }
juce::String rackPrefix (int rack);       // "busa_", "busb_", "busc_", "mast_"
juce::String rackDisplayName (int rack);  // "Bus A"...

/** Registre de tous les paramètres (ordre = ordre d'exposition à l'hôte). */
struct Registry
{
    std::vector<ParamDef> all;
    std::map<juce::String, int> index;
    juce::String prefix;
    int fxOn[NumFx], fxMix[NumFx], fxMode[NumFx], fxPar[NumFx], fxTrans[NumFx];
    int fxFirst[NumFx], fxCount[NumFx];
    int famMix[NumFamilies];

    int find (const juce::String& id) const
    {
        auto it = index.find (id);
        return it == index.end() ? -1 : it->second;
    }
};

/** Registre des paramètres d'un rack (identifiants préfixés : « busa_fx3_mix »...). */
const Registry& rackRegistry (int rack);
/** Ajoute les paramètres des 4 racks au layout du synthé. */
void addRackParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

const juce::StringArray& modeNames();
} // namespace idg
