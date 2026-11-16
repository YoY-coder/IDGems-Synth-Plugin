#pragma once
#include "JuceIncludes.h"

/** Identifiants et définition des paramètres exposés à l'hôte (automation Ableton). */
namespace idgs
{
constexpr int MaxVoices = 32;
constexpr int NumOscs = 2;

enum OscMode { ModeWavetable, ModeSample, ModeMulti, NumOscModes };

enum WarpMode
{
    WarpOff, WarpSync, WarpBendPlus, WarpBendMinus, WarpPWM, WarpAsymPlus, WarpAsymMinus,
    WarpFlip, WarpMirror, WarpQuantize, WarpRemap, WarpFM, WarpAM, WarpRM, WarpHardSync, NumWarpModes
};

enum ModSource { SrcOscA, SrcOscB, SrcSub, SrcNoise, NumModSources };
enum Route { RouteF1, RouteF2, RouteBoth, RouteDirect };
enum VoiceMode { VoicePoly, VoiceMono, VoiceLegato };

enum FilterType
{
    FiltLP12, FiltLP24, FiltHP12, FiltHP24, FiltBP12, FiltBP24, FiltNotch12, FiltNotch24,
    FiltLadder, FiltCombPlus, FiltCombMinus, FiltFormant, FiltDestroy, NumFilterTypes
};

enum NoiseType { NoiseWhite, NoisePink, NoiseVinyl, NoiseDigital, NoiseWind, NoiseRain, NoiseRadio, NumNoiseTypes };

const juce::StringArray& oscModeNames();
const juce::StringArray& warpNames();
const juce::StringArray& modSourceNames();
const juce::StringArray& routeNames();
const juce::StringArray& filterTypeNames();
const juce::StringArray& noiseTypeNames();
const juce::StringArray& tableChoiceNames(); // tables d'usine + « Personnalisée »

/** Identifiant d'un paramètre d'oscillateur : oscId (0, "level") -> "osca_level". */
inline juce::String oscId (int osc, const char* key) { return juce::String (osc == 0 ? "osca_" : "oscb_") + key; }
/** Identifiant d'un paramètre de filtre : filtId (1, "cutoff") -> "f2_cutoff". */
inline juce::String filtId (int f, const char* key) { return "f" + juce::String (f + 1) + "_" + key; }
/** Identifiant d'un paramètre d'enveloppe : envId (0, "attack") -> "env1_attack". */
inline juce::String envId (int e, const char* key) { return "env" + juce::String (e + 1) + "_" + key; }
/** Identifiant d'une macro : macroId (0) -> "macro1". */
inline juce::String macroId (int m) { return "macro" + juce::String (m + 1); }

inline juce::String lfoId (int i, const char* key) { return "lfo" + juce::String (i + 1) + "_" + key; }
inline juce::String rndId (int i, const char* key) { return "rnd" + juce::String (i + 1) + "_" + key; }

constexpr int NumLfos = 10;
constexpr int NumRandMods = 5;
enum LfoMode { LfoFree, LfoNoteVoice, LfoNoteGlobal, LfoOneShot, LfoHost, NumLfoModes };
enum RndStyle { RndPerlin, RndSampleHold, RndSmooth, RndLorenz };

/** Divisions rythmiques (reprises d'IDGems). */
const juce::StringArray& divisionNames();
double divisionBeats (int index);

constexpr int NumEnvelopes = 8;   // ENV 1 = volume ; ENV 2-4 visibles ; 5-8 se débloquent (phase 3)
constexpr int VisibleEnvelopes = 4;
constexpr int NumMacros = 8;

namespace P
{
    // Osc A (identifiants de la phase 1 conservés)
    static const char* const oscAOn     = "osca_on";
    static const char* const oscATable  = "osca_table";
    static const char* const oscAPos    = "osca_pos";
    static const char* const oscAOct    = "osca_oct";
    static const char* const oscASemi   = "osca_semi";
    static const char* const oscAFine   = "osca_fine";
    static const char* const oscALevel  = "osca_level";
    static const char* const oscAPan    = "osca_pan";
    static const char* const oscAPhase  = "osca_phase";
    static const char* const oscAPhRand = "osca_phrand";

    // Sub
    static const char* const subOn    = "sub_on";
    static const char* const subShape = "sub_shape";
    static const char* const subOct   = "sub_oct";
    static const char* const subLevel = "sub_level";
    static const char* const subPan   = "sub_pan";
    static const char* const subRoute = "sub_route";

    // Noise
    static const char* const noiseOn    = "noise_on";
    static const char* const noiseType  = "noise_type";
    static const char* const noiseLevel = "noise_level";
    static const char* const noisePan   = "noise_pan";
    static const char* const noiseTone  = "noise_tone";
    static const char* const noiseRoute = "noise_route";

    // Filtres
    static const char* const filtSerial = "filt_serial";

    // Enveloppe 1 (volume des voix)
    static const char* const env1Delay   = "env1_delay";
    static const char* const env1Attack  = "env1_attack";
    static const char* const env1Hold    = "env1_hold";
    static const char* const env1Decay   = "env1_decay";
    static const char* const env1Sustain = "env1_sustain";
    static const char* const env1Release = "env1_release";
    static const char* const env1ACurve  = "env1_acurve";
    static const char* const env1DCurve  = "env1_dcurve";
    static const char* const env1RCurve  = "env1_rcurve";

    // Voix / jeu
    static const char* const voices      = "voices";
    static const char* const voiceMode   = "voice_mode";
    static const char* const glide       = "glide";
    static const char* const glideCurve  = "glide_curve";
    static const char* const glideLegato = "glide_legato";
    static const char* const velSens     = "vel_sens";
    static const char* const bendRange   = "bend_range";

    // Sortie
    static const char* const masterVol  = "master_vol";
    static const char* const ceiling    = "lim_ceiling";
}

/** Textes d'aide (bulles au survol). */
juce::String helpFor (const juce::String& paramId);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace idgs
