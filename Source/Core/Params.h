#pragma once
#include "JuceIncludes.h"

/** Identifiants et définition des paramètres exposés à l'hôte (automation Ableton). */
namespace idgs
{
namespace P
{
    // Osc A
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
    static const char* const voices     = "voices";
    static const char* const velSens    = "vel_sens";
    static const char* const bendRange  = "bend_range";

    // Sortie
    static const char* const masterVol  = "master_vol";
    static const char* const ceiling    = "lim_ceiling";
}

constexpr int MaxVoices = 32;

/** Textes d'aide (bulles au survol). */
juce::String helpFor (const juce::String& paramId);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace idgs
