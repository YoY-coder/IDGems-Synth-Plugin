#include "Params.h"
#include "../Synth/Wavetable.h"
#include "../Fx/ParamDefs.h"

namespace idgs
{
const juce::StringArray& divisionNames()
{
    static const juce::StringArray d { "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
                                       "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1", "2/1", "4/1" };
    return d;
}

double divisionBeats (int i)
{
    static const double b[] { 0.0625, 0.125 * 2.0 / 3.0, 0.125, 0.25 * 2.0 / 3.0, 0.25, 0.375, 0.5 * 2.0 / 3.0, 0.5,
                              0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0 };
    return b[juce::jlimit (0, 15, i)];
}

const juce::StringArray& oscModeNames() { static const juce::StringArray n { "Wavetable", "Sample", "Multisample", "Granulaire", "Spectral" }; return n; }
const juce::StringArray& scaleTypeNames()
{
    static const juce::StringArray n { "Majeure", "Mineure", "Dorien", "Phrygien", "Lydien", "Mixolydien", IDG_T ("Mineure harm."),
                                       "Penta maj.", "Penta min.", "Blues", "Par tons", "Chromatique" };
    return n;
}
const juce::StringArray& personalityNames()
{
    static const juce::StringArray n { "Neutre", IDG_T ("Métal"), "Organique", IDG_T ("Digital cassé"), "Aquatique", IDG_T ("Poussière"), "Cosmique" };
    return n;
}

const juce::StringArray& keyNames()
{
    static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n;
}

const juce::StringArray& texEngineNames()
{
    static const juce::StringArray n { "Granulaire", "Spectral", "Chaos", IDG_T ("Résonateur"), IDG_T ("Bruit sculpté"), "Data bending" };
    return n;
}
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
    if (id.startsWith ("lfo"))
    {
        const auto k = id.fromFirstOccurrenceOf ("_", false, false);
        static const std::map<juce::String, juce::String> lh {
            { "rate", IDG_T ("Vitesse du LFO en Hz (mode libre, Sync éteint).") },
            { "sync", IDG_T ("Synchro au tempo de l'hôte (la vitesse devient une division rythmique).") },
            { "div", IDG_T ("Division rythmique quand Sync est allumé.") },
            { "mode", IDG_T ("Libre : tourne en continu. Note (voix) : repart à chaque note, indépendant par voix. Note (global) : toutes les voix repartent à chaque note. One-shot : un seul cycle par note. Calé hôte : suit la position du morceau.") },
            { "phase", IDG_T ("Décalage de phase.") },
            { "fade", IDG_T ("Fondu d'entrée du LFO après chaque note.") },
            { "smooth", IDG_T ("Lissage de la sortie (adoucit les marches et les sauts).") },
            { "stereo", IDG_T ("Décalage de phase entre gauche et droite (appliqué aux cibles stéréo : effets des bus, phase 4).") } };
        auto it = lh.find (k);
        return it == lh.end() ? juce::String() : it->second;
    }
    if (id.startsWith ("rnd"))
    {
        const auto k = id.fromFirstOccurrenceOf ("_", false, false);
        static const std::map<juce::String, juce::String> rh {
            { "rate", IDG_T ("Vitesse des tirages.") }, { "sync", IDG_T ("Synchro au tempo.") }, { "div", IDG_T ("Division rythmique.") },
            { "style", IDG_T ("Perlin : dérive douce. S&H : marches (sample & hold). Lisse : tirages reliés en douceur. Lorenz : chaos continu.") },
            { "retrig", IDG_T ("Nouveau tirage à chaque note jouée.") } };
        auto it = rh.find (k);
        return it == rh.end() ? juce::String() : it->second;
    }
    if (id.startsWith ("macro"))
        return id == "macro8" ? IDG_T ("Macro Chaos : assignable comme les autres ; pilotera aussi l'intensité générale du désordre (random, phase 7).")
                              : IDG_T ("Macro : attrape la pastille et glisse-la sur un potard ou un graphique pour l'assigner.");
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
        { P::masterMix,   IDG_T ("Dry/wet du rack Master : dose l'ensemble des effets du Master.") },
        { P::inGain,      IDG_T ("Gain d'entrée de la chaîne de sortie.") },
        { P::outGain,     IDG_T ("Gain de sortie, avant le soft clip et le limiteur.") },
        { P::hpFreq,      IDG_T ("Passe-haut de sortie (anti-sub, anti-DC).") },
        { P::monoOn,      IDG_T ("Grave en mono sous la fréquence choisie.") },
        { P::monoFreq,    IDG_T ("Fréquence sous laquelle le grave passe en mono.") },
        { P::ottDepth,    IDG_T ("OTT : compression multibande haute et basse (3 bandes).") },
        { P::ottTime,     IDG_T ("OTT : vitesse (attaque / release).") },
        { P::ottUp,       IDG_T ("OTT : compression vers le haut (remonte les détails).") },
        { P::ottDown,     IDG_T ("OTT : compression vers le bas.") },
        { P::ottGain,     IDG_T ("OTT : gain de sortie.") },
        { P::clipDrive,   IDG_T ("Soft clip : saturation douce avant le limiteur.") },
        { P::limRelease,  IDG_T ("Release du limiteur.") },
        { P::zeroLat,     IDG_T ("Sans latence : coupe les effets qui ajoutent de la latence (pitch shift, formant, pixel sort...). Conseillé pour jouer en direct.") },
        { P::destOS,      IDG_T ("Suréchantillonnage de la famille Destruction (moins d'aliasing, plus de CPU).") },
        { P::ceiling,     IDG_T ("Plafond du limiteur de sortie (aucun échantillon ne dépasse cette valeur).") },
    };
    auto it = h.find (id);
    if (it != h.end()) return it->second;
    if (id.startsWith ("send_")) return IDG_T ("Niveau d'envoi de cette source vers ce bus d'effets.");
    if (id.startsWith ("arp_") || id.startsWith ("seq_") || id.startsWith ("gen_") || id.startsWith ("scale_") || id.startsWith ("spray_")
        || id.startsWith ("mr_") || id == "play_mode")
    {
        static const std::map<juce::String, juce::String> ph {
            { "play_mode", IDG_T ("Direct : les notes jouées. Arpège : arpégiateur. Séquenceur : motif de 32 pas. Générateur : séquenceur rempli par le générateur de patterns.") },
            { "arp_mode", IDG_T ("Ordre de l'arpège : montant, descendant, aller-retour, ordre joué, aléatoire.") },
            { "arp_oct", IDG_T ("Nombre d'octaves parcourues.") },
            { "arp_rate", IDG_T ("Vitesse (division rythmique, ternaires T et pointées D).") },
            { "arp_gate", IDG_T ("Durée des notes (en fraction du pas).") },
            { "arp_hold", IDG_T ("Maintient l'arpège quand on relâche les touches.") },
            { "seq_follow", IDG_T ("Note tenue : le motif joue tant qu'une touche est enfoncée, transposé par la note. Toujours : joue en continu à partir de C3.") },
            { "seq_mutate", IDG_T ("Mutation progressive : le motif change un peu à chaque cycle (les pas verrouillés ne bougent jamais).") },
            { "seq_rnd_notes", IDG_T ("Notes aléatoires pondérées par la gamme, tirées à chaque pas.") },
            { "seq_rnd_oct", IDG_T ("Probabilité de sauts d'octave aléatoires.") },
            { "seq_rnd_ratchet", IDG_T ("Probabilité de ratchets aléatoires.") },
            { "gen_style", IDG_T ("Style du générateur de patterns : euclidien, marche aléatoire, arpège brisé, polyrythme 3 contre 4.") },
            { "gen_density", IDG_T ("Densité de notes.") },
            { "gen_range", IDG_T ("Étendue en octaves.") },
            { "gen_jump", IDG_T ("Probabilité de grands intervalles.") },
            { "gen_accent", IDG_T ("Contraste de vélocité (accents).") },
            { "gen_auto", IDG_T ("Régénère automatiquement le motif tous les N cycles.") },
            { "scale_on", IDG_T ("Quantification sur gamme de tout ce qui touche au pitch (notes, random, Note Spray, effets). Éteint = atonal.") },
            { "spray_on", IDG_T ("Note Spray : chaque note jouée déclenche un nuage de micro-notes autour d'elle.") },
            { "spray_density", IDG_T ("Nombre de micro-notes par note jouée.") },
            { "spray_range", IDG_T ("Étendue de hauteur autour de la note (quantifiée sur la gamme si active).") },
            { "spray_time", IDG_T ("Dispersion des micro-notes dans le temps.") },
            { "spray_dur", IDG_T ("Durée de chaque micro-note.") },
            { "spray_vel", IDG_T ("Vélocité aléatoire des micro-notes.") },
            { "spray_max", IDG_T ("Nombre maximum de voix utilisées par le spray.") },
            { "mr_on", IDG_T ("Micro-random par voix : chaque note tire de légères variations (pitch, grain, coupure, pan, phase).") },
            { "mr_amount", IDG_T ("Quantité globale des variations.") } };
        auto it3 = ph.find (id);
        return it3 == ph.end() ? juce::String() : it3->second;
    }
    if (id.startsWith ("rand_") || id.startsWith ("morph_"))
    {
        static const std::map<juce::String, juce::String> rh2 {
            { "rand_amount", IDG_T ("Dosage du random : de la légère variation (0 %) au tirage total (100 %).") },
            { "rand_personality", IDG_T ("Personnalité : oriente le random (types d'osc, filtres, effets, vitesses) pour lui donner un caractère.") },
            { "rand_intensity", IDG_T ("Intensité de la personnalité : mélange entre la personnalité et un random neutre.") },
            { "rand_sync", IDG_T ("Random calé sur le temps : le tirage attend le prochain temps / la prochaine mesure.") },
            { "rand_glide", IDG_T ("Glide du random : transition douce vers les nouvelles valeurs.") },
            { "rand_drift", IDG_T ("Drift : dérive lente et continue des paramètres (dans leurs plages, cadenas respectés).") },
            { "rand_drift_speed", IDG_T ("Vitesse du drift.") },
            { "rand_mutate", IDG_T ("Mutate auto : petites mutations du patch à intervalle régulier.") },
            { "rand_guard", IDG_T ("Garde-fous : rejette les tirages muets ou saturés et égalise le volume entre deux tirages. Éteint = vrai chaos.") },
            { "morph_x", IDG_T ("Morph pad : position X (mélange des snapshots).") },
            { "morph_y", IDG_T ("Morph pad : position Y (mélange des snapshots).") } };
        auto it4 = rh2.find (id);
        return it4 == rh2.end() ? juce::String() : it4->second;
    }
    if (id.startsWith ("tex_") || id.startsWith ("fp_"))
    {
        static const std::map<juce::String, juce::String> th {
            { "tex_on", IDG_T ("Active l'Osc Texture (matière sonore : granulaire, spectral, chaos, résonateur, bruit, data bending).") },
            { "tex_engine1", IDG_T ("Premier moteur de l'Osc Texture.") },
            { "tex_engine2", IDG_T ("Second moteur : le Morph fond progressivement le moteur 1 dans le moteur 2.") },
            { "tex_morph", IDG_T ("Fondu entre le moteur 1 (0 %) et le moteur 2 (100 %). Modulable.") },
            { "tex_g_pos", IDG_T ("Granulaire : position de lecture dans le son (ou retard dans le flux live).") },
            { "tex_g_size", IDG_T ("Granulaire : durée de chaque grain.") },
            { "tex_g_density", IDG_T ("Granulaire : nombre de grains par seconde.") },
            { "tex_g_spray", IDG_T ("Granulaire : dispersion aléatoire de la position des grains.") },
            { "tex_g_pitchrand", IDG_T ("Granulaire : hauteur aléatoire par grain (demi-tons).") },
            { "tex_g_panrand", IDG_T ("Granulaire : panoramique aléatoire par grain.") },
            { "tex_g_reverse", IDG_T ("Granulaire : probabilité qu'un grain soit lu à l'envers.") },
            { "tex_g_shape", IDG_T ("Granulaire : enveloppe des grains (doux -> carré).") },
            { "tex_g_freeze", IDG_T ("Granulaire : fige la source (flux live) / la position.") },
            { "tex_g_source", IDG_T ("Granulaire : grains pris dans le son chargé, ou dans la sortie live des Osc A et B.") },
            { "tex_s_freeze", IDG_T ("Spectral : fige le spectre courant.") },
            { "tex_s_speed", IDG_T ("Spectral : vitesse de lecture du son (0 = immobile).") },
            { "tex_s_smear", IDG_T ("Spectral : étale le spectre dans le temps (traînées).") },
            { "tex_s_shift", IDG_T ("Spectral : décale toutes les fréquences (inharmonique).") },
            { "tex_s_stretch", IDG_T ("Spectral : étire / compresse le spectre.") },
            { "tex_s_harm", IDG_T ("Spectral : filtre d'harmoniques (ne garde que les harmoniques de la note).") },
            { "tex_s_morph", IDG_T ("Spectral : morph entre le son 1 et le son 2.") },
            { "tex_s_scramble", IDG_T ("Spectral : bin scramble, mélange des fréquences.") },
            { "tex_c_type", IDG_T ("Chaos : équation (Lorenz, logistique, Hénon), instable mais continue.") },
            { "tex_c_amount", IDG_T ("Chaos : intensité du désordre.") },
            { "tex_r_type", IDG_T ("Résonateur : corde (Karplus-Strong), métal ou verre.") },
            { "tex_r_decay", IDG_T ("Résonateur : durée de résonance.") },
            { "tex_r_exciter", IDG_T ("Résonateur : excitation par une impulsion de bruit, un bruit continu ou l'Osc A.") },
            { "tex_n_color", IDG_T ("Bruit sculpté : couleur (sombre <-> brillant).") },
            { "tex_n_density", IDG_T ("Bruit sculpté : densité (grésillements épars <-> bruit plein).") },
            { "tex_n_crackle", IDG_T ("Bruit sculpté : craquements.") },
            { "tex_n_focus", IDG_T ("Bruit sculpté : concentre le bruit sur la hauteur de la note.") },
            { "tex_d_offset", IDG_T ("Data bending : position de départ dans le fichier.") },
            { "tex_d_format", IDG_T ("Data bending : lecture des octets en 8 ou 16 bits.") },
            { "fp_on", IDG_T ("Empreinte spectrale : l'enveloppe spectrale d'un son (voix, objet) est plaquée sur le synthé, façon vocoder.") },
            { "fp_amount", IDG_T ("Empreinte : quantité.") },
            { "fp_res", IDG_T ("Empreinte : résolution (nombre de bandes).") },
            { "fp_shift", IDG_T ("Empreinte : décalage des formants.") },
            { "fp_speed", IDG_T ("Empreinte : vitesse de lecture de l'empreinte.") } };
        auto it2 = th.find (id);
        return it2 == th.end() ? IDG_T ("Réglage de l'Osc Texture.") : it2->second;
    }
    if (id.contains ("_to_")) return IDG_T ("Envoi de ce bus vers un autre bus (les boucles sont automatiquement évitées).");
    if (id.startsWith ("bus_")) return IDG_T ("Réglage du bus d'effets.");
    return {};
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

    // ---- LFO
    for (int i = 0; i < NumLfos; ++i)
    {
        const juce::String N = "LFO " + juce::String (i + 1) + " ";
        B.f (lfoId (i, "rate"), N + "Rate", 0.01f, 40.0f, 1.0f, [] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; }, 2.0f, "Hz");
        B.b (lfoId (i, "sync"), N + "Sync", true);
        B.c (lfoId (i, "div"), N + "Division", divisionNames(), divisionNames().indexOf ("1/4"));
        B.c (lfoId (i, "mode"), N + "Mode", { "Libre", "Note (voix)", "Note (global)", "One-shot", IDG_T ("Calé hôte") }, LfoNoteVoice);
        B.f (lfoId (i, "phase"), N + "Phase", 0.0f, 1.0f, 0.0f, pctText);
        B.t (lfoId (i, "fade"), N + "Fade In", 5000.0f, 500.0f, 0.0f);
        B.t (lfoId (i, "smooth"), N + "Smooth", 500.0f, 50.0f, 0.0f);
        B.f (lfoId (i, "stereo"), N + IDG_T ("Stéréo"), 0.0f, 0.5f, 0.0f, pctText);
    }
    // ---- Random
    for (int i = 0; i < NumRandMods; ++i)
    {
        const juce::String N = "Random " + juce::String (i + 1) + " ";
        B.f (rndId (i, "rate"), N + "Rate", 0.01f, 40.0f, 2.0f, [] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; }, 2.0f, "Hz");
        B.b (rndId (i, "sync"), N + "Sync", false);
        B.c (rndId (i, "div"), N + "Division", divisionNames(), divisionNames().indexOf ("1/8"));
        B.c (rndId (i, "style"), N + "Style", { "Perlin", "S&H", "Lisse", "Lorenz" }, RndPerlin);
        B.b (rndId (i, "retrig"), N + "Retrig Note", false);
    }

    // ---- Macros (assignation par glisser-déposer)
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

    // ---- Osc Texture (6 moteurs, fondu de l'un à l'autre)
    {
        auto T = [] (const char* k) { return texId (k); };
        B.b (T ("on"), "Texture On", false);
        B.c (T ("engine1"), "Texture Engine 1", texEngineNames(), TexGranular);
        B.c (T ("engine2"), "Texture Engine 2", texEngineNames(), TexChaos);
        B.f (T ("morph"), "Texture Morph", 0.0f, 1.0f, 0.0f, pctText);
        B.f (T ("level"), "Texture Level", 0.0f, 1.0f, 0.6f, pctText);
        B.f (T ("pan"), "Texture Pan", -1.0f, 1.0f, 0.0f, panText);
        B.i (T ("oct"), "Texture Octave", -4, 4, 0);
        B.i (T ("semi"), "Texture Semi", -12, 12, 0);
        B.c (T ("route"), "Texture Route", routeNames(), RouteF1);
        B.i (T ("root"), "Texture Root", 0, 127, 60, [] (int v, int) { return noteText (v); });
        // granulaire
        B.f (T ("g_pos"), "Grain Position", 0.0f, 1.0f, 0.3f, pctText);
        B.f (T ("g_size"), "Grain Size", 5.0f, 1000.0f, 90.0f, msText, 100.0f, "ms");
        B.f (T ("g_density"), "Grain Density", 1.0f, 200.0f, 25.0f, [] (float v, int) { return juce::String (v, 1) + " /s"; }, 25.0f);
        B.f (T ("g_spray"), "Grain Spray", 0.0f, 1.0f, 0.15f, pctText);
        B.f (T ("g_pitchrand"), "Grain Pitch Rand", 0.0f, 24.0f, 0.0f, [] (float v, int) { return juce::String (v, 1) + " st"; }, -1.0f, "st");
        B.f (T ("g_panrand"), "Grain Pan Rand", 0.0f, 1.0f, 0.4f, pctText);
        B.f (T ("g_reverse"), "Grain Reverse Prob", 0.0f, 1.0f, 0.0f, pctText);
        B.f (T ("g_shape"), "Grain Shape", 0.0f, 1.0f, 0.0f, pctText);
        B.b (T ("g_freeze"), "Grain Freeze", false);
        B.c (T ("g_source"), "Grain Source", { "Sample", "Osc A + B (live)" }, 0);
        // spectral
        B.b (T ("s_freeze"), "Spectral Freeze", false);
        B.f (T ("s_speed"), "Spectral Speed", 0.0f, 4.0f, 1.0f, [] (float v, int) { return "x" + juce::String (v, 2); }, 1.0f);
        B.f (T ("s_smear"), "Spectral Smear", 0.0f, 0.99f, 0.0f, pctText);
        B.f (T ("s_shift"), "Spectral Shift", -2000.0f, 2000.0f, 0.0f, hzText, -1.0f, "Hz");
        B.f (T ("s_stretch"), "Spectral Stretch", 0.25f, 4.0f, 1.0f, [] (float v, int) { return "x" + juce::String (v, 2); }, 1.0f);
        B.f (T ("s_harm"), "Spectral Harmonic Filter", 0.0f, 1.0f, 0.0f, pctText);
        B.f (T ("s_morph"), "Spectral Morph", 0.0f, 1.0f, 0.0f, pctText);
        B.f (T ("s_scramble"), "Spectral Scramble", 0.0f, 1.0f, 0.0f, pctText);
        // chaos
        B.c (T ("c_type"), "Chaos Type", { "Lorenz", "Logistique", IDG_T ("Hénon") }, 0);
        B.f (T ("c_amount"), "Chaos Amount", 0.0f, 1.0f, 0.5f, pctText);
        B.f (T ("c_rate"), "Chaos Rate", 0.1f, 4.0f, 1.0f, [] (float v, int) { return "x" + juce::String (v, 2); }, 1.0f);
        // résonateur
        B.c (T ("r_type"), "Resonator Type", { "Corde", IDG_T ("Métal"), "Verre" }, 0);
        B.f (T ("r_decay"), "Resonator Decay", 0.0f, 1.0f, 0.7f, pctText);
        B.f (T ("r_bright"), "Resonator Brightness", 0.0f, 1.0f, 0.5f, pctText);
        B.c (T ("r_exciter"), "Resonator Exciter", { "Bruit (impulsion)", "Bruit continu", "Osc A" }, 0);
        B.f (T ("r_excite"), "Resonator Excite Level", 0.0f, 1.0f, 0.5f, pctText);
        // bruit sculpté
        B.f (T ("n_color"), "Noise Color", -1.0f, 1.0f, 0.0f, bipolarPct);
        B.f (T ("n_density"), "Noise Density", 0.0f, 1.0f, 1.0f, pctText);
        B.f (T ("n_crackle"), "Noise Crackle", 0.0f, 1.0f, 0.0f, pctText);
        B.f (T ("n_focus"), "Noise Pitch Focus", 0.0f, 1.0f, 0.0f, pctText);
        // data bending
        B.f (T ("d_offset"), "Data Offset", 0.0f, 1.0f, 0.0f, pctText);
        B.c (T ("d_format"), "Data Format", { "8 bits", "16 bits" }, 0);
        B.f (T ("d_rate"), "Data Rate", 0.05f, 4.0f, 1.0f, [] (float v, int) { return "x" + juce::String (v, 2); }, 1.0f);
        // empreinte spectrale
        B.b ("fp_on", "Fingerprint On", false);
        B.f ("fp_amount", "Fingerprint Amount", 0.0f, 1.0f, 0.8f, pctText);
        B.c ("fp_res", "Fingerprint Resolution", { "8 bandes", "16 bandes", "32 bandes" }, 1);
        B.f ("fp_shift", "Fingerprint Formant Shift", -12.0f, 12.0f, 0.0f, [] (float v, int) { return juce::String (v, 1) + " bandes"; });
        B.f ("fp_speed", "Fingerprint Speed", 0.0f, 4.0f, 1.0f, [] (float v, int) { return "x" + juce::String (v, 2); }, 1.0f);
        B.b ("fp_loop", "Fingerprint Loop", true);
    }

    // ---- Jeu : arpégiateur, séquenceur, générateur de patterns, gamme, Note Spray, micro-random
    B.c ("play_mode", "Play Mode", { "Direct", IDG_T ("Arpège"), IDG_T ("Séquenceur"), IDG_T ("Générateur") }, PlayDirect);
    B.c ("arp_mode", "Arp Mode", { "Up", "Down", "Up / Down", IDG_T ("Ordre joué"), IDG_T ("Aléatoire") }, ArpUp);
    B.i ("arp_oct", "Arp Octaves", 1, 4, 1);
    B.c ("arp_rate", "Arp Rate", divisionNames(), divisionNames().indexOf ("1/16"));
    B.f ("arp_gate", "Arp Gate", 0.05f, 1.0f, 0.6f, pctText);
    B.f ("arp_swing", "Arp Swing", 0.0f, 1.0f, 0.0f, pctText);
    B.b ("arp_hold", "Arp Hold", false);
    B.c ("seq_rate", "Seq Rate", divisionNames(), divisionNames().indexOf ("1/16"));
    B.i ("seq_len", "Seq Length", 1, 32, 16);
    B.f ("seq_swing", "Seq Swing", 0.0f, 1.0f, 0.0f, pctText);
    B.c ("seq_follow", "Seq Follow", { IDG_T ("Note tenue"), IDG_T ("Toujours (C3)") }, 0);
    B.f ("seq_mutate", "Seq Mutation", 0.0f, 1.0f, 0.0f, pctText);
    B.f ("seq_rnd_notes", "Seq Random Notes", 0.0f, 1.0f, 0.0f, pctText);
    B.f ("seq_rnd_oct", "Seq Random Octave", 0.0f, 1.0f, 0.0f, pctText);
    B.f ("seq_rnd_ratchet", "Seq Random Ratchet", 0.0f, 1.0f, 0.0f, pctText);
    B.c ("gen_style", "Generator Style", { "Euclidien", IDG_T ("Marche aléatoire"), IDG_T ("Arpège brisé"), "Polyrythme" }, GenEuclid);
    B.f ("gen_density", "Generator Density", 0.05f, 1.0f, 0.5f, pctText);
    B.i ("gen_range", "Generator Range", 1, 4, 2, [] (int v, int) { return juce::String (v) + " oct"; });
    B.f ("gen_jump", "Generator Leaps", 0.0f, 1.0f, 0.3f, pctText);
    B.f ("gen_accent", "Generator Accents", 0.0f, 1.0f, 0.4f, pctText);
    B.c ("gen_auto", "Generator Auto", { "Off", "1 cycle", "2 cycles", "4 cycles", "8 cycles" }, 0);
    B.b ("scale_on", "Scale On", false);
    B.c ("scale_key", "Scale Key", keyNames(), 0);
    B.c ("scale_type", "Scale Type", scaleTypeNames(), 1);
    B.b ("spray_on", "Note Spray On", false);
    B.f ("spray_density", "Spray Density", 1.0f, 16.0f, 4.0f, [] (float v, int) { return juce::String ((int) v) + " notes"; }, 4.0f);
    B.i ("spray_range", "Spray Range", 0, 24, 7, [] (int v, int) { return "+/- " + juce::String (v) + " st"; });
    B.f ("spray_time", "Spray Time", 0.0f, 2000.0f, 300.0f, msText, 300.0f, "ms");
    B.f ("spray_dur", "Spray Duration", 10.0f, 2000.0f, 150.0f, msText, 150.0f, "ms");
    B.f ("spray_vel", "Spray Velocity Rand", 0.0f, 1.0f, 0.5f, pctText);
    B.i ("spray_max", "Spray Max Voices", 1, 16, 8);
    B.b ("mr_on", "Micro Random On", false);
    B.f ("mr_amount", "Micro Random Amount", 0.0f, 1.0f, 0.3f, pctText);
    B.b ("mr_pitch", "Micro Random Pitch", true);
    B.b ("mr_grain", "Micro Random Grain", true);
    B.b ("mr_cutoff", "Micro Random Cutoff", true);
    B.b ("mr_pan", "Micro Random Pan", true);
    B.b ("mr_phase", "Micro Random Phase", true);

    // ---- Système de random
    B.f ("rand_amount", "Random Amount", 0.0f, 1.0f, 0.5f, pctText);
    B.c ("rand_personality", "Random Personality", personalityNames(), 0);
    B.f ("rand_intensity", "Personality Intensity", 0.0f, 1.0f, 0.7f, pctText);
    B.c ("rand_sync", "Random Sync", { IDG_T ("Immédiat"), "Au temps", IDG_T ("À la mesure"), "Toutes les 2 mesures", "Toutes les 4 mesures" }, 0);
    B.t ("rand_glide", "Random Glide", 5000.0f, 500.0f, 0.0f);
    B.f ("rand_drift", "Drift", 0.0f, 1.0f, 0.0f, pctText);
    B.f ("rand_drift_speed", "Drift Speed", 0.01f, 2.0f, 0.2f, [] (float v, int) { return juce::String (v, 2) + " Hz"; }, 0.2f);
    B.b ("rand_mutate", "Auto Mutate", false);
    B.c ("rand_mutate_every", "Mutate Every", { "1 mesure", "2 mesures", "4 mesures", "8 mesures", "16 mesures" }, 1);
    B.f ("rand_mutate_amount", "Mutate Amount", 0.0f, 1.0f, 0.15f, pctText);
    B.b ("rand_guard", "Random Guards", true);
    B.f ("morph_x", "Morph X", 0.0f, 1.0f, 0.5f, pctText);
    B.f ("morph_y", "Morph Y", 0.0f, 1.0f, 0.5f, pctText);

    // ---- Envois des sources vers les bus (par défaut : tout dans le bus A)
    static const char* srcNames[] { "Osc A", "Osc B", "Sub", "Noise", "Texture" };
    for (int src = 0; src < NumSendSources; ++src)
        for (int b = 0; b < NumBuses; ++b)
            B.f (sendId (src, b), juce::String (srcNames[src]) + " -> Bus " + juce::String::charToString ((juce::juce_wchar) ('A' + b)),
                 0.0f, 1.0f, b == 0 ? 1.0f : 0.0f, pctText);
    // ---- Bus A / B / C
    for (int b = 0; b < NumBuses; ++b)
    {
        const juce::String N = "Bus " + juce::String::charToString ((juce::juce_wchar) ('A' + b)) + " ";
        B.f (busId (b, "vol"), N + "Volume", -60.0f, 6.0f, 0.0f, [] (float v, int) { return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }, -1.0f, "dB");
        B.f (busId (b, "pan"), N + "Pan", -1.0f, 1.0f, 0.0f, panText);
        B.b (busId (b, "mute"), N + "Mute", false);
        B.b (busId (b, "solo"), N + "Solo", false);
        for (int t = 0; t < NumBuses; ++t)
            if (t != b) B.f (busSendId (b, t), N + "-> Bus " + juce::String::charToString ((juce::juce_wchar) ('A' + t)), 0.0f, 1.0f, 0.0f, pctText);
    }
    // ---- Racks d'effets (Bus A, B, C, Master) : repris d'IDGems
    idg::addRackParameters (l);

    // ---- Master et chaîne de sortie
    B.f (P::masterMix, "Master Dry/Wet", 0.0f, 1.0f, 1.0f, pctText);
    B.f (P::inGain, "Input Gain", -24.0f, 24.0f, 0.0f, [] (float v, int) { return juce::String (v, 1) + " dB"; }, -1.0f, "dB");
    B.f (P::outGain, "Output Gain", -24.0f, 24.0f, 0.0f, [] (float v, int) { return juce::String (v, 1) + " dB"; }, -1.0f, "dB");
    B.f (P::hpFreq, "High-pass", 5.0f, 300.0f, 5.0f, hzText, 40.0f, "Hz");
    B.b (P::monoOn, "Bass Mono", false);
    B.f (P::monoFreq, "Bass Mono Freq", 40.0f, 400.0f, 120.0f, hzText, 120.0f, "Hz");
    B.f (P::ottDepth, "OTT Depth", 0.0f, 1.0f, 0.0f, pctText);
    B.f (P::ottTime, "OTT Time", 0.1f, 10.0f, 1.0f, [] (float v, int) { return "x" + juce::String (v, 2); }, 1.0f);
    B.f (P::ottUp, "OTT Upward", 0.0f, 1.0f, 1.0f, pctText);
    B.f (P::ottDown, "OTT Downward", 0.0f, 1.0f, 1.0f, pctText);
    B.f (P::ottGain, "OTT Gain", -12.0f, 12.0f, 0.0f, [] (float v, int) { return juce::String (v, 1) + " dB"; }, -1.0f, "dB");
    B.f (P::clipDrive, "Soft Clip Drive", 0.0f, 24.0f, 0.0f, [] (float v, int) { return juce::String (v, 1) + " dB"; }, -1.0f, "dB");
    B.f (P::limRelease, "Limiter Release", 10.0f, 500.0f, 80.0f, msText, 80.0f, "ms");
    B.b (P::zeroLat, "Zero Latency", true);
    B.c (P::destOS, "Destruction Oversampling", { "1x", "2x", "4x" }, 1);

    // ---- Sortie
    B.f (P::masterVol, "Master Volume", -60.0f, 6.0f, -6.0f,
         [] (float v, int) { return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }, -1.0f, "dB");
    B.f (P::ceiling, "Limiter Ceiling", -12.0f, 0.0f, -0.3f, [] (float v, int) { return juce::String (v, 1) + " dB"; }, -1.0f, "dB");
    return l;
}
} // namespace idgs
