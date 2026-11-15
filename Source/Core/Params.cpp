#include "Params.h"
#include "../Synth/Wavetable.h"

namespace idgs
{
const juce::StringArray& oscModeNames() { static const juce::StringArray n { "Wavetable", "Sample", "Multisample" }; return n; }
const juce::StringArray& warpNames()
{
    static const juce::StringArray n { "Off", "Sync", "Bend +", "Bend -", "PWM", "Asym +", "Asym -", "Flip", "Mirror",
                                       "Quantize", "Remap", "FM", "AM", "RM", "Hard sync" };
    return n;
}
const juce::StringArray& modSourceNames() { static const juce::StringArray n { "Osc A", "Osc B", "Sub", "Noise" }; return n; }
const juce::StringArray& routeNames() { static const juce::StringArray n { "-> F1", "-> F2", "-> F1+F2", "Direct" }; return n; }
const juce::StringArray& filterTypeNames()
{
    static const juce::StringArray n { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24", "Notch 12", "Notch 24",
                                       "Ladder", "Comb +", "Comb -", "Formant", IDG_T ("Destructeur") };
    return n;
}
const juce::StringArray& noiseTypeNames()
{
    static const juce::StringArray n { "Blanc", "Rose", "Vinyle", "Digital", "Vent", "Pluie", "Radio" };
    return n;
}
const juce::StringArray& tableChoiceNames()
{
    static const juce::StringArray n = []
    {
        auto a = WavetableBank::names();
        a.add (IDG_T ("Personnalisée"));
        return a;
    }();
    return n;
}

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
juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String bipolarPct (float v, int) { const int p = juce::roundToInt (v * 100.0f); return (p > 0 ? "+" : "") + juce::String (p) + " %"; }
juce::String curveText (float v, int) { return std::abs (v) < 0.005f ? IDG_T ("linéaire") : juce::String (v, 2); }
juce::String panText (float v, int)
{
    const int p = juce::roundToInt (v * 100.0f);
    return p == 0 ? juce::String ("C") : (p < 0 ? juce::String (-p) + " G" : juce::String (p) + " D");
}
juce::String hzText (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
juce::String noteText (int n)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[n % 12]) + juce::String (n / 12 - 2); // C3 = 60 (convention Ableton)
}

struct Builder
{
    juce::AudioProcessorValueTreeState::ParameterLayout& l;

    void f (const juce::String& id, const juce::String& name, float mn, float mx, float def,
            std::function<juce::String (float, int)> txt = nullptr, float centre = -1.0f, const juce::String& label = {})
    {
        juce::NormalisableRange<float> r (mn, mx);
        if (centre > mn && centre < mx) r.setSkewForCentre (centre);
        auto attr = juce::AudioParameterFloatAttributes().withLabel (label);
        if (txt) attr = attr.withStringFromValueFunction (txt);
        l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def, attr));
    }
    void t (const juce::String& id, const juce::String& name, float maxMs, float centreMs, float def) { f (id, name, 0.0f, maxMs, def, msText, centreMs, "ms"); }
    void b (const juce::String& id, const juce::String& name, bool def) { l.add (std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def)); }
    void c (const juce::String& id, const juce::String& name, const juce::StringArray& ch, int def) { l.add (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, ch, def)); }
    void i (const juce::String& id, const juce::String& name, int mn, int mx, int def, std::function<juce::String (int, int)> txt = nullptr)
    {
        auto attr = juce::AudioParameterIntAttributes();
        if (txt) attr = attr.withStringFromValueFunction (txt);
        l.add (std::make_unique<API> (juce::ParameterID { id, 1 }, name, mn, mx, def, attr));
    }
};
} // namespace

juce::String helpFor (const juce::String& fullId)
{
    // les paramètres d'Osc A et d'Osc B partagent les mêmes textes
    auto id = fullId.startsWith ("oscb_") ? "osca_" + fullId.substring (5) : fullId;
    id = id.startsWith ("f2_") ? "f1_" + id.substring (3) : id;
    if (id.startsWith ("env") && id.length() > 4 && id[3] != '1' && id[4] == '_') id = "env1_" + id.substring (5);
    if (id.startsWith ("macro"))
        return id == "macro8" ? IDG_T ("Macro Chaos : pilotera d'un seul geste l'intensité générale du désordre (phase 3 / 7).")
                              : IDG_T ("Macro : glisser vers n'importe quel potard pour l'assigner (phase 3).");
    static const std::map<juce::String, juce::String> h {
        { "osca_on",      IDG_T ("Active / coupe l'oscillateur.") },
        { "osca_mode",    IDG_T ("Moteur de l'oscillateur : Wavetable, Sample (un fichier joué à la hauteur des notes) ou Multisample (plusieurs fichiers répartis sur le clavier). Granulaire et Spectral arrivent en phase 5.") },
        { "osca_table",   IDG_T ("Wavetable utilisée. « Personnalisée » = table générée au dé ou issue d'un sample glissé sur l'oscillateur.") },
        { "osca_pos",     IDG_T ("Wavetable : position dans la table (fondu continu entre frames). Sample : point de départ de la lecture.") },
        { "osca_oct",     IDG_T ("Transposition par octave.") },
        { "osca_semi",    IDG_T ("Transposition en demi-tons.") },
        { "osca_fine",    IDG_T ("Accordage fin en cents.") },
        { "osca_level",   IDG_T ("Niveau de l'oscillateur.") },
        { "osca_pan",     IDG_T ("Panoramique (loi à puissance constante).") },
        { "osca_phase",   IDG_T ("Phase de départ à chaque note.") },
        { "osca_phrand",  IDG_T ("Part de phase aléatoire à chaque note (0 % = attaque identique à chaque fois).") },
        { "osca_unison",  IDG_T ("Nombre de voix d'unison par note.") },
        { "osca_detune",  IDG_T ("Désaccord entre les voix d'unison (jusqu'à ±1 demi-ton).") },
        { "osca_blend",   IDG_T ("Niveau des voix d'unison latérales par rapport à la voix centrale.") },
        { "osca_spread",  IDG_T ("Largeur stéréo de l'unison.") },
        { "osca_stretch", IDG_T ("Inharmonicité : étire (+) ou comprime (-) l'écart entre les harmoniques. Sons de cloche, de métal tordu. Passe l'oscillateur en synthèse additive (64 partiels max).") },
        { "osca_shift",   IDG_T ("Inharmonicité : décale toutes les harmoniques d'une même fréquence (en fraction de la fondamentale). Rend le son inharmonique, façon frequency shifter.") },
        { "osca_warp1",   IDG_T ("Warp 1 : déformation de la lecture (sync, bend, PWM, asym, flip, mirror, quantize, remap) ou modulation par une autre source (FM, AM, RM, hard sync).") },
        { "osca_warp2",   IDG_T ("Warp 2 : appliqué après le warp 1.") },
        { "osca_warp1amt", IDG_T ("Quantité du warp 1.") },
        { "osca_warp2amt", IDG_T ("Quantité du warp 2.") },
        { "osca_warp1src", IDG_T ("Source de modulation du warp 1 (FM, AM, RM, hard sync).") },
        { "osca_warp2src", IDG_T ("Source de modulation du warp 2 (FM, AM, RM, hard sync).") },
        { "osca_loop",    IDG_T ("Mode Sample : lecture en boucle (avec fondu enchaîné au point de bouclage).") },
        { "osca_root",    IDG_T ("Mode Sample : note à laquelle le sample est joué à sa hauteur d'origine (détectée dans le nom du fichier si possible, ex. « Piano_C3.wav »).") },
        { "osca_route",   IDG_T ("Routage vers les filtres : F1, F2, les deux, ou direct (sans filtre).") },
        { P::subOn,       IDG_T ("Active / coupe le sub.") },
        { P::subShape,    IDG_T ("Forme d'onde du sub.") },
        { P::subOct,      IDG_T ("Octave du sub sous la note jouée.") },
        { P::subLevel,    IDG_T ("Niveau du sub.") },
        { P::subPan,      IDG_T ("Panoramique du sub.") },
        { P::subRoute,    IDG_T ("Routage du sub vers les filtres.") },
        { P::noiseOn,     IDG_T ("Active / coupe le bruit.") },
        { P::noiseType,   IDG_T ("Banque de bruits : blanc, rose, vinyle, digital (suit la hauteur des notes), vent, pluie, radio.") },
        { P::noiseLevel,  IDG_T ("Niveau du bruit.") },
        { P::noisePan,    IDG_T ("Panoramique du bruit.") },
        { P::noiseTone,   IDG_T ("Filtre simple du bruit : vers la gauche passe-bas, vers la droite passe-haut.") },
        { P::noiseRoute,  IDG_T ("Routage du bruit vers les filtres.") },
        { P::filtSerial,  IDG_T ("Série : F1 puis F2. Parallèle : F1 et F2 côte à côte, sorties additionnées.") },
        { "f1_on",        IDG_T ("Active le filtre (désactivé, le signal le traverse sans être modifié).") },
        { "f1_type",      IDG_T ("Type de filtre : LP / HP / BP / notch 12 et 24 dB, ladder, comb, formant (morph de voyelles), destructeur (downsample + bitcrush).") },
        { "f1_cutoff",    IDG_T ("Fréquence de coupure. Formant : voyelle (A-E-I-O-U). Destructeur : fréquence d'échantillonnage simulée.") },
        { "f1_reso",      IDG_T ("Résonance. Comb : réinjection. Destructeur : réduction du nombre de bits.") },
        { "f1_drive",     IDG_T ("Saturation avant le filtre.") },
        { "f1_key",       IDG_T ("Keytracking : la coupure suit la note jouée (100 % = suit exactement, référence C3).") },
        { "f1_mix",       IDG_T ("Dosage entre le signal non filtré et filtré.") },
        { P::env1Delay,   IDG_T ("Délai avant le départ de l'attaque.") },
        { P::env1Attack,  IDG_T ("Temps de montée.") },
        { P::env1Hold,    IDG_T ("Temps de maintien au maximum avant le decay.") },
        { P::env1Decay,   IDG_T ("Temps de descente vers le sustain.") },
        { P::env1Sustain, IDG_T ("Niveau tenu tant que la note est enfoncée.") },
        { P::env1Release, IDG_T ("Temps de chute après le relâchement de la note.") },
        { P::env1ACurve,  IDG_T ("Courbe de l'attaque : négatif = démarrage rapide, positif = démarrage lent.") },
        { P::env1DCurve,  IDG_T ("Courbe du decay : négatif = chute rapide puis lente, positif = l'inverse.") },
        { P::env1RCurve,  IDG_T ("Courbe du release : négatif = chute rapide puis lente, positif = l'inverse.") },
        { P::voices,      IDG_T ("Nombre maximum de voix. Au-delà, la plus ancienne est volée avec un fondu court (sans clic).") },
        { P::voiceMode,   IDG_T ("Poly : plusieurs notes. Mono : une note, l'enveloppe repart à chaque note. Legato : une note, l'enveloppe ne repart pas quand les notes se chevauchent.") },
        { P::glide,       IDG_T ("Temps de glide (portamento) d'une note à l'autre.") },
        { P::glideCurve,  IDG_T ("Courbe du glide : négatif = arrive vite puis ralentit, positif = l'inverse.") },
        { P::glideLegato, IDG_T ("Glide seulement quand les notes se chevauchent (jeu legato).") },
        { P::velSens,     IDG_T ("Influence de la vélocité sur le volume (0 % = toutes les notes au même niveau).") },
        { P::bendRange,   IDG_T ("Amplitude du pitch bend en demi-tons.") },
        { P::masterVol,   IDG_T ("Volume général, avant le limiteur.") },
        { P::ceiling,     IDG_T ("Plafond du limiteur de sortie (aucun échantillon ne dépasse cette valeur).") },
    };
    auto it = h.find (id);
    return it == h.end() ? juce::String() : it->second;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    Builder B { l };

    // ---- Osc A / Osc B
    for (int o = 0; o < NumOscs; ++o)
    {
        const juce::String N = o == 0 ? "Osc A " : "Osc B ";
        B.b (oscId (o, "on"), N + "On", o == 0);
        B.c (oscId (o, "mode"), N + "Mode", oscModeNames(), 0);
        B.c (oscId (o, "table"), N + "Table", tableChoiceNames(), o == 0 ? 0 : 1);
        B.f (oscId (o, "pos"), N + "WT Pos", 0.0f, 1.0f, 0.0f, pctText);
        B.i (oscId (o, "oct"), N + "Octave", -4, 4, 0);
        B.i (oscId (o, "semi"), N + "Semi", -12, 12, 0);
        B.f (oscId (o, "fine"), N + "Fine", -100.0f, 100.0f, 0.0f, [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ct"; }, -1.0f, "ct");
        B.f (oscId (o, "level"), N + "Level", 0.0f, 1.0f, 0.75f, pctText);
        B.f (oscId (o, "pan"), N + "Pan", -1.0f, 1.0f, 0.0f, panText);
        B.f (oscId (o, "phase"), N + "Phase", 0.0f, 1.0f, 0.0f,
             [] (float v, int) { return juce::String (juce::roundToInt (v * 360.0f)) + juce::String (juce::CharPointer_UTF8 ("°")); });
        B.f (oscId (o, "phrand"), N + "Phase Rand", 0.0f, 1.0f, 0.0f, pctText);
        B.i (oscId (o, "unison"), N + "Unison", 1, 16, 1);
        B.f (oscId (o, "detune"), N + "Detune", 0.0f, 1.0f, 0.2f, pctText);
        B.f (oscId (o, "blend"), N + "Blend", 0.0f, 1.0f, 0.75f, pctText);
        B.f (oscId (o, "spread"), N + "Stereo Spread", 0.0f, 1.0f, 0.8f, pctText);
        B.f (oscId (o, "stretch"), N + "Inharm Stretch", -1.0f, 1.0f, 0.0f, bipolarPct);
        B.f (oscId (o, "shift"), N + "Inharm Shift", -1.0f, 1.0f, 0.0f, bipolarPct);
        for (int w = 1; w <= 2; ++w)
        {
            const juce::String k = "warp" + juce::String (w);
            B.c (oscId (o, k.toRawUTF8()), N + "Warp " + juce::String (w), warpNames(), 0);
            B.f (oscId (o, (k + "amt").toRawUTF8()), N + "Warp " + juce::String (w) + " Amount", 0.0f, 1.0f, 0.0f, pctText);
            B.c (oscId (o, (k + "src").toRawUTF8()), N + "Warp " + juce::String (w) + " Source", modSourceNames(), o == 0 ? SrcOscB : SrcOscA);
        }
        B.b (oscId (o, "loop"), N + "Sample Loop", true);
        B.i (oscId (o, "root"), N + "Sample Root", 0, 127, 60, [] (int v, int) { return noteText (v); });
        B.c (oscId (o, "route"), N + "Route", routeNames(), RouteF1);
    }

    // ---- Sub
    B.b (P::subOn, "Sub On", false);
    B.c (P::subShape, "Sub Shape", { "Sinus", "Triangle", IDG_T ("Carré") }, 0);
    B.c (P::subOct, "Sub Octave", { "-1 oct", "-2 oct" }, 0);
    B.f (P::subLevel, "Sub Level", 0.0f, 1.0f, 0.6f, pctText);
    B.f (P::subPan, "Sub Pan", -1.0f, 1.0f, 0.0f, panText);
    B.c (P::subRoute, "Sub Route", routeNames(), RouteDirect);

    // ---- Noise
    B.b (P::noiseOn, "Noise On", false);
    B.c (P::noiseType, "Noise Type", noiseTypeNames(), 0);
    B.f (P::noiseLevel, "Noise Level", 0.0f, 1.0f, 0.3f, pctText);
    B.f (P::noisePan, "Noise Pan", -1.0f, 1.0f, 0.0f, panText);
    B.f (P::noiseTone, "Noise Tone", -1.0f, 1.0f, 0.0f, bipolarPct);
    B.c (P::noiseRoute, "Noise Route", routeNames(), RouteF1);

    // ---- Filtres
    for (int f = 0; f < 2; ++f)
    {
        const juce::String N = "Filter " + juce::String (f + 1) + " ";
        B.b (filtId (f, "on"), N + "On", false);
        B.c (filtId (f, "type"), N + "Type", filterTypeNames(), f == 0 ? FiltLP24 : FiltHP12);
        B.f (filtId (f, "cutoff"), N + "Cutoff", 20.0f, 20000.0f, f == 0 ? 2000.0f : 80.0f, hzText, 1000.0f, "Hz");
        B.f (filtId (f, "reso"), N + "Resonance", 0.0f, 1.0f, 0.2f, pctText);
        B.f (filtId (f, "drive"), N + "Drive", 0.0f, 1.0f, 0.0f, pctText);
        B.f (filtId (f, "key"), N + "Keytrack", 0.0f, 1.0f, 0.0f, pctText);
        B.f (filtId (f, "mix"), N + "Mix", 0.0f, 1.0f, 1.0f, pctText);
    }
    B.c (P::filtSerial, "Filter Routing", { IDG_T ("Série"), IDG_T ("Parallèle") }, 0);

    // ---- Enveloppes (ENV 1 = volume des voix ; les autres deviennent des sources de modulation en phase 3)
    for (int e = 0; e < NumEnvelopes; ++e)
    {
        const juce::String N = "Env " + juce::String (e + 1) + " ";
        B.t (envId (e, "delay"), N + "Delay", 5000.0f, 300.0f, 0.0f);
        B.t (envId (e, "attack"), N + "Attack", 10000.0f, 400.0f, 2.0f);
        B.t (envId (e, "hold"), N + "Hold", 5000.0f, 300.0f, 0.0f);
        B.t (envId (e, "decay"), N + "Decay", 20000.0f, 600.0f, e == 0 ? 400.0f : 800.0f);
        B.f (envId (e, "sustain"), N + "Sustain", 0.0f, 1.0f, e == 0 ? 0.8f : 0.5f, pctText);
        B.t (envId (e, "release"), N + "Release", 20000.0f, 600.0f, 250.0f);
        B.f (envId (e, "acurve"), N + "Attack Curve", -1.0f, 1.0f, 0.0f, curveText);
        B.f (envId (e, "dcurve"), N + "Decay Curve", -1.0f, 1.0f, -0.5f, curveText);
        B.f (envId (e, "rcurve"), N + "Release Curve", -1.0f, 1.0f, -0.5f, curveText);
    }

    // ---- Macros (assignation par glisser-déposer en phase 3)
    for (int m = 0; m < NumMacros; ++m)
        B.f (macroId (m), m == NumMacros - 1 ? juce::String ("Macro Chaos") : "Macro " + juce::String (m + 1), 0.0f, 1.0f, 0.0f, pctText);

    // ---- Voix / jeu
    B.i (P::voices, "Voices", 1, MaxVoices, 16);
    B.c (P::voiceMode, "Voice Mode", { "Poly", "Mono", "Legato" }, 0);
    B.t (P::glide, "Glide Time", 5000.0f, 300.0f, 0.0f);
    B.f (P::glideCurve, "Glide Curve", -1.0f, 1.0f, -0.4f, curveText);
    B.b (P::glideLegato, "Glide Legato Only", false);
    B.f (P::velSens, "Velocity Sens", 0.0f, 1.0f, 0.7f, pctText);
    B.i (P::bendRange, "Bend Range", 0, 24, 2);

    // ---- Sortie
    B.f (P::masterVol, "Master Volume", -60.0f, 6.0f, -6.0f,
         [] (float v, int) { return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }, -1.0f, "dB");
    B.f (P::ceiling, "Limiter Ceiling", -12.0f, 0.0f, -0.3f, [] (float v, int) { return juce::String (v, 1) + " dB"; }, -1.0f, "dB");
    return l;
}
} // namespace idgs
