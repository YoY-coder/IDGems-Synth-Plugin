#include "Params.h"
#include "../Synth/Wavetable.h"

namespace idgs
{
namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;

juce::String msText (float v, int)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " s";
    return juce::String (v, v < 10.0f ? 1 : 0) + " ms";
}

std::unique_ptr<APF> timeParam (const char* id, const juce::String& name, float maxMs, float centreMs, float def)
{
    juce::NormalisableRange<float> r (0.0f, maxMs);
    r.setSkewForCentre (centreMs);
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def,
                                  juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText));
}

std::unique_ptr<APF> floatParam (const char* id, const juce::String& name, float mn, float mx, float def,
                                 std::function<juce::String (float, int)> txt = nullptr, const juce::String& label = {})
{
    auto attr = juce::AudioParameterFloatAttributes().withLabel (label);
    if (txt) attr = attr.withStringFromValueFunction (txt);
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (mn, mx), def, attr);
}

juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String curveText (float v, int) { return std::abs (v) < 0.005f ? IDG_T ("linéaire") : juce::String (v, 2); }
juce::String panText (float v, int)
{
    const int p = juce::roundToInt (v * 100.0f);
    return p == 0 ? juce::String ("C") : (p < 0 ? juce::String (-p) + " G" : juce::String (p) + " D");
}
} // namespace

juce::String helpFor (const juce::String& id)
{
    static const std::map<juce::String, juce::String> h {
        { P::oscAOn,     IDG_T ("Active / coupe l'oscillateur A.") },
        { P::oscATable,  IDG_T ("Wavetable utilisée par l'Osc A (tables d'usine, d'autres arrivent en phase 2 avec l'import de samples et le générateur aléatoire).") },
        { P::oscAPos,    IDG_T ("Position dans la wavetable : fondu continu d'une frame à la suivante.") },
        { P::oscAOct,    IDG_T ("Transposition par octave.") },
        { P::oscASemi,   IDG_T ("Transposition en demi-tons.") },
        { P::oscAFine,   IDG_T ("Accordage fin en cents.") },
        { P::oscALevel,  IDG_T ("Niveau de l'oscillateur.") },
        { P::oscAPan,    IDG_T ("Panoramique de l'oscillateur (loi à puissance constante).") },
        { P::oscAPhase,  IDG_T ("Phase de départ de l'oscillateur à chaque note.") },
        { P::oscAPhRand, IDG_T ("Part de phase aléatoire à chaque note (0 % = attaque identique à chaque fois, 100 % = phase totalement libre).") },
        { P::env1Delay,  IDG_T ("Délai avant le départ de l'attaque.") },
        { P::env1Attack, IDG_T ("Temps de montée.") },
        { P::env1Hold,   IDG_T ("Temps de maintien au maximum avant le decay.") },
        { P::env1Decay,  IDG_T ("Temps de descente vers le sustain.") },
        { P::env1Sustain, IDG_T ("Niveau tenu tant que la note est enfoncée.") },
        { P::env1Release, IDG_T ("Temps de chute après le relâchement de la note.") },
        { P::env1ACurve, IDG_T ("Courbe de l'attaque : négatif = démarrage rapide (log), positif = démarrage lent (exp).") },
        { P::env1DCurve, IDG_T ("Courbe du decay : négatif = chute rapide puis lente, positif = l'inverse.") },
        { P::env1RCurve, IDG_T ("Courbe du release : négatif = chute rapide puis lente, positif = l'inverse.") },
        { P::voices,     IDG_T ("Nombre maximum de voix simultanées. Au-delà, la voix la plus ancienne est volée avec un fondu court (sans clic).") },
        { P::velSens,    IDG_T ("Influence de la vélocité sur le volume (0 % = toutes les notes au même niveau).") },
        { P::bendRange,  IDG_T ("Amplitude du pitch bend en demi-tons.") },
        { P::masterVol,  IDG_T ("Volume général, avant le limiteur.") },
        { P::ceiling,    IDG_T ("Plafond du limiteur de sortie (aucun échantillon ne dépasse cette valeur).") },
    };
    auto it = h.find (id);
    return it == h.end() ? juce::String() : it->second;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // ---- Osc A
    l.add (std::make_unique<APB> (juce::ParameterID { P::oscAOn, 1 }, "Osc A On", true));
    l.add (std::make_unique<APC> (juce::ParameterID { P::oscATable, 1 }, "Osc A Table", WavetableBank::names(), 0));
    l.add (floatParam (P::oscAPos, "Osc A WT Pos", 0.0f, 1.0f, 0.0f, pctText));
    l.add (std::make_unique<API> (juce::ParameterID { P::oscAOct, 1 }, "Osc A Octave", -4, 4, 0));
    l.add (std::make_unique<API> (juce::ParameterID { P::oscASemi, 1 }, "Osc A Semi", -12, 12, 0));
    l.add (floatParam (P::oscAFine, "Osc A Fine", -100.0f, 100.0f, 0.0f,
                       [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ct"; }, "ct"));
    l.add (floatParam (P::oscALevel, "Osc A Level", 0.0f, 1.0f, 0.75f, pctText));
    l.add (floatParam (P::oscAPan, "Osc A Pan", -1.0f, 1.0f, 0.0f, panText));
    l.add (floatParam (P::oscAPhase, "Osc A Phase", 0.0f, 1.0f, 0.0f,
                       [] (float v, int) { return juce::String (juce::roundToInt (v * 360.0f)) + juce::String (juce::CharPointer_UTF8 ("°")); }));
    l.add (floatParam (P::oscAPhRand, "Osc A Phase Rand", 0.0f, 1.0f, 0.0f, pctText));

    // ---- Enveloppe 1
    l.add (timeParam (P::env1Delay, "Env 1 Delay", 5000.0f, 300.0f, 0.0f));
    l.add (timeParam (P::env1Attack, "Env 1 Attack", 10000.0f, 400.0f, 2.0f));
    l.add (timeParam (P::env1Hold, "Env 1 Hold", 5000.0f, 300.0f, 0.0f));
    l.add (timeParam (P::env1Decay, "Env 1 Decay", 20000.0f, 600.0f, 400.0f));
    l.add (floatParam (P::env1Sustain, "Env 1 Sustain", 0.0f, 1.0f, 0.8f, pctText));
    l.add (timeParam (P::env1Release, "Env 1 Release", 20000.0f, 600.0f, 250.0f));
    l.add (floatParam (P::env1ACurve, "Env 1 Attack Curve", -1.0f, 1.0f, 0.0f, curveText));
    l.add (floatParam (P::env1DCurve, "Env 1 Decay Curve", -1.0f, 1.0f, -0.5f, curveText));
    l.add (floatParam (P::env1RCurve, "Env 1 Release Curve", -1.0f, 1.0f, -0.5f, curveText));

    // ---- Voix / jeu
    l.add (std::make_unique<API> (juce::ParameterID { P::voices, 1 }, "Voices", 1, MaxVoices, 16));
    l.add (floatParam (P::velSens, "Velocity Sens", 0.0f, 1.0f, 0.7f, pctText));
    l.add (std::make_unique<API> (juce::ParameterID { P::bendRange, 1 }, "Bend Range", 0, 24, 2));

    // ---- Sortie
    l.add (floatParam (P::masterVol, "Master Volume", -60.0f, 6.0f, -6.0f,
                       [] (float v, int) { return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }, "dB"));
    l.add (floatParam (P::ceiling, "Limiter Ceiling", -12.0f, 0.0f, -0.3f,
                       [] (float v, int) { return juce::String (v, 1) + " dB"; }, "dB"));
    return l;
}
} // namespace idgs
