// Tests de stabilité d'IDGems Synth : NaN, plafond du limiteur, clics (vol de voix, release, automation),
// fin des voix, Panic, sauvegarde de l'état, aliasing, CPU.
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace idgs;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    if (! ok) { ++failures; std::cout << "  [ECHEC] " << what << std::endl; }
}

struct Harness
{
    // sur le tas : la pile de Windows (1 Mo) ne supporte pas plusieurs processeurs dans main()
    std::unique_ptr<IDGemsSynthProcessor> procPtr = std::make_unique<IDGemsSynthProcessor>();
    IDGemsSynthProcessor& proc = *procPtr;
    double sr;
    int block;
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer midi;
    std::vector<float> outL, outR; // tout ce qui a été rendu

    Harness (double sampleRate = 48000.0, int blockSize = 512) : sr (sampleRate), block (blockSize), buf (2, blockSize)
    {
        proc.prepareToPlay (sr, block);
    }

    void set (const char* id, float realValue)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
    }

    /** Repart de zéro (voix, effets) après un changement de patch. */
    void prepareFresh() { proc.prepareToPlay (sr, block); outL.clear(); outR.clear(); }

    void noteOn (int note, float vel = 0.8f, int offset = 0) { midi.addEvent (juce::MidiMessage::noteOn (1, note, vel), offset); }
    void noteOff (int note, int offset = 0) { midi.addEvent (juce::MidiMessage::noteOff (1, note), offset); }

    /** Traite `seconds` ; les événements déjà dans `midi` partent dans le premier bloc. */
    void run (double seconds)
    {
        const int total = (int) (seconds * sr);
        for (int done = 0; done < total; done += block)
        {
            buf.clear();
            proc.processBlock (buf, midi);
            midi.clear();
            for (int i = 0; i < block; ++i)
            {
                outL.push_back (buf.getSample (0, i));
                outR.push_back (buf.getSample (1, i));
            }
        }
    }

    float peak (size_t from = 0) const
    {
        float p = 0.0f;
        for (size_t i = from; i < outL.size(); ++i)
        {
            if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i])) return -1.0f;
            p = juce::jmax (p, std::abs (outL[i]), std::abs (outR[i]));
        }
        return p;
    }

    /** Plus grand saut entre deux échantillons consécutifs (détecteur de clics). */
    float maxStep (size_t from = 1) const
    {
        float m = 0.0f;
        for (size_t i = juce::jmax ((size_t) 1, from); i < outL.size(); ++i)
            m = juce::jmax (m, std::abs (outL[i] - outL[i - 1]), std::abs (outR[i] - outR[i - 1]));
        return m;
    }

    /** Réglage « sinus pur » : un clic se voit immédiatement sur la dérivée. */
    void sineSetup()
    {
        set (P::oscATable, 0);
        set (P::oscAPos, 0.0f);
        set (P::masterVol, 0.0f);
    }
};

float ceilingGain (Harness& h) { return idg::dsp::dbToGain (h.proc.getValue (P::ceiling)) + 1.0e-5f; }
} // namespace

/** Capture de l'interface en PNG (pour vérifier la mise en page sans hôte). */
int snapshotMain (const juce::String& path, int tab)
{
    auto procPtr = std::make_unique<IDGemsSynthProcessor>();
    auto& proc = *procPtr;
    proc.prepareToPlay (48000.0, 512);
    if (tab == 1) { proc.presets->load (2); }
    if (tab == 2) { auto* p = proc.apvts.getParameter ("oscb_mode"); p->setValueNotifyingHost (p->convertTo0to1 (1.0f)); }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (IDGemsSynthEditor::baseW, IDGemsSynthEditor::baseH);
    if (tab >= 10 && tab < 20) if (auto* e = dynamic_cast<IDGemsSynthEditor*> (ed.get())) e->showTab (tab - 10);
    if (tab == 20)
        if (auto* e = dynamic_cast<IDGemsSynthEditor*> (ed.get()))
        {
            auto& re = proc.randomEngine();
            for (int k = 0; k < 4; ++k) { re.perform ({}, (uint32_t) (k + 3), 0.6f, false, k == 2 ? "Filtres" : IDG_T ("Général")); re.storeSnapshot (k, 0.2f + 0.2f * k, 0.3f + 0.1f * (k % 2)); }
            re.toggleFavourite (2);
            e->showRandomPanel();
        }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    return png.writeImageToStream (img, out) ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return snapshotMain (argv[2], argc >= 4 ? juce::String (argv[3]).getIntValue() : 0);
    std::cout << "=== Tests de stabilite IDGems Synth ===" << std::endl;
    // --only N : ne lance que le test N
    const int onlyTest = argc >= 3 && juce::String (argv[1]) == "--only" ? juce::String (argv[2]).getIntValue() : 0;

    // 1. Wavetables : pas de NaN, crête normalisée, pas d'harmonique au-dessus de Nyquist à la note la plus aiguë
    if (onlyTest == 0 || onlyTest == 1)
    {
        std::cout << "1. Wavetables d'usine..." << std::endl;
        const auto& bank = WavetableBank::get();
        check (bank.size() == WavetableBank::names().size(), "nombre de tables incohérent avec la liste des noms");
        for (int t = 0; t < bank.size(); ++t)
            for (int f = 0; f < bank[t].getNumFrames(); ++f)
                for (int m = 0; m < Wavetable::NumMips; ++m)
                {
                    const float* d = bank[t].table (f, m);
                    float pk = 0.0f;
                    bool finite = true;
                    for (int i = 0; i <= Wavetable::Size; ++i) { finite &= std::isfinite (d[i]); pk = juce::jmax (pk, std::abs (d[i])); }
                    check (finite, bank[t].getName() + " : NaN dans la frame " + juce::String (f));
                    check (pk < 1.6f, bank[t].getName() + " : crête trop forte " + juce::String (pk));
                }
        // mip choisi pour 12 kHz à 44.1 kHz : (1024 >> mip) * 12000 doit rester sous Nyquist
        const float inc = 12000.0f / 44100.0f;
        const int mip = Wavetable::mipFor (inc);
        check ((float) (Wavetable::MaxHarmonics >> mip) * 12000.0f <= 22050.0f, "anti-aliasing : mip trop riche pour 12 kHz");
    }

    // 2. Notes simples à plusieurs fréquences d'échantillonnage / tailles de bloc
    if (onlyTest == 0 || onlyTest == 2)
    {
        std::cout << "2. Notes simples (44.1 / 48 / 96 kHz, blocs 1 a 2048)..." << std::endl;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int bs : { 1, 7, 64, 512, 2048 })
            {
                Harness h (sr, bs);
                h.set (P::oscATable, 1);
                h.set (P::oscAPos, 1.0f);
                for (int n = 24; n <= 108; n += 12) h.noteOn (n, 1.0f);
                h.run (0.3);
                for (int n = 24; n <= 108; n += 12) h.noteOff (n);
                h.run (0.5);
                const float pk = h.peak();
                check (pk >= 0.0f, "NaN a " + juce::String (sr) + " Hz / bloc " + juce::String (bs));
                check (pk <= ceilingGain (h), "plafond depasse a " + juce::String (sr) + " Hz / bloc " + juce::String (bs) + " : " + juce::String (pk));
                check (pk > 0.05f, "aucun son a " + juce::String (sr) + " Hz / bloc " + juce::String (bs));
            }
    }

    // 3. Fin des voix : après le release, silence total et plus aucune voix active
    if (onlyTest == 0 || onlyTest == 3)
    {
        std::cout << "3. Fin des voix apres release..." << std::endl;
        Harness h;
        h.set (P::env1Release, 100.0f);
        for (int n = 48; n < 60; ++n) h.noteOn (n);
        h.run (0.2);
        for (int n = 48; n < 60; ++n) h.noteOff (n);
        h.run (0.5);
        const size_t tail = h.outL.size() - 2048;
        check (h.peak (tail) < 1.0e-6f, "il reste du signal apres le release : " + juce::String (h.peak (tail)));
        check (h.proc.activeVoices.load() == 0, "des voix restent actives : " + juce::String (h.proc.activeVoices.load()));
    }

    // 4. Clics : attaque / release à 0 ms, vol de voix, réduction du nombre de voix, automation brutale
    if (onlyTest == 0 || onlyTest == 4)
    {
        std::cout << "4. Absence de clics (sinus 110 Hz)..." << std::endl;
        // pente max d'un sinus 110 Hz à pleine échelle ~ 2*pi*110/48000 = 0.0144 ; un clic dépasse largement
        const float limit = 0.04f;
        {
            Harness h;
            h.sineSetup();
            h.set (P::env1Attack, 0.0f);
            h.set (P::env1Release, 0.0f);
            h.set (P::env1Decay, 0.0f);
            h.set (P::env1Sustain, 1.0f);
            h.set (P::velSens, 0.0f);
            for (int k = 0; k < 20; ++k)
            {
                h.noteOn (45, 1.0f, k * 13 % 500);
                h.run (0.05);
                h.noteOff (45, k * 7 % 500);
                h.run (0.03);
            }
            check (h.maxStep() < limit, "clic sur attaque / release a 0 ms : saut de " + juce::String (h.maxStep()));
        }
        {
            Harness h;
            h.sineSetup();
            h.set (P::voices, 2);
            h.set (P::velSens, 0.0f);
            h.set (P::env1Attack, 0.0f);
            h.set (P::env1Sustain, 1.0f);
            for (int k = 0; k < 40; ++k)
            {
                h.noteOn (40 + (k % 3), 1.0f, (k * 37) % 512);
                h.run (0.04);
            }
            check (h.maxStep() < limit, "clic lors du vol de voix : saut de " + juce::String (h.maxStep()));
            check (h.proc.activeVoices.load() <= 2, "plus de voix que le maximum");
        }
        {
            Harness h;
            h.sineSetup();
            h.set (P::velSens, 0.0f);
            for (int n : { 40, 41, 42, 43, 44, 45 }) h.noteOn (n, 0.5f);
            h.run (0.2);
            h.set (P::voices, 1);
            h.run (0.1);
            check (h.maxStep() < limit, "clic lors de la reduction du nombre de voix : saut de " + juce::String (h.maxStep()));
            h.set (P::voices, 16);
        }
        {
            Harness h;
            h.sineSetup();
            h.set (P::oscATable, 1);
            h.noteOn (40, 1.0f);
            h.run (0.1);
            const size_t from = h.outL.size();
            for (int k = 0; k < 20; ++k)
            {
                h.set (P::oscALevel, k % 2 ? 0.0f : 1.0f);
                h.set (P::oscAPan, k % 2 ? -1.0f : 1.0f);
                h.set (P::masterVol, k % 2 ? -30.0f : 0.0f);
                h.run (0.02);
            }
            // signal plus riche (table Harmoniques, pos 0 = presque sinus) : seuil un peu plus large
            check (h.maxStep (from) < 0.08f, "clic sur automation brutale (niveau / pan / volume) : saut de " + juce::String (h.maxStep (from)));
        }
    }

    // 5. Panic : silence en moins de 15 ms, sans clic, et le synthé rejoue ensuite
    if (onlyTest == 0 || onlyTest == 5)
    {
        std::cout << "5. Panic..." << std::endl;
        Harness h;
        h.sineSetup();
        h.set (P::env1Release, 5000.0f);
        for (int n : { 45, 52, 57 }) h.noteOn (n, 1.0f);
        h.run (0.2);
        h.proc.panic();
        const size_t from = h.outL.size();
        h.run (0.05);
        const size_t after = from + (size_t) (0.015 * h.sr);
        float pk = 0.0f;
        for (size_t i = after; i < h.outL.size(); ++i) pk = juce::jmax (pk, std::abs (h.outL[i]));
        check (pk < 1.0e-6f, "Panic : signal apres 15 ms : " + juce::String (pk));
        check (h.maxStep (from) < 0.04f, "Panic : clic de " + juce::String (h.maxStep (from)));
        h.noteOn (60);
        const size_t again = h.outL.size();
        h.run (0.1);
        check (h.peak (again) > 0.05f, "plus de son apres Panic");
    }

    // 6. Sustain (CC64) et pitch bend
    if (onlyTest == 0 || onlyTest == 6)
    {
        std::cout << "6. Pedale de sustain et pitch bend..." << std::endl;
        Harness h;
        h.set (P::env1Release, 50.0f);
        h.midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
        h.noteOn (60);
        h.run (0.05);
        h.noteOff (60);
        h.run (0.3);
        check (h.proc.activeVoices.load() == 1, "la pedale de sustain ne tient pas la note");
        h.midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
        h.run (0.3);
        check (h.proc.activeVoices.load() == 0, "la note ne s'arrete pas au relachement de la pedale");
        h.noteOn (60);
        h.midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 10);
        h.run (0.1);
        check (h.peak() >= 0.0f, "NaN avec pitch bend");
    }

    // 7. Sauvegarde / rappel de l'état
    if (onlyTest == 0 || onlyTest == 7)
    {
        std::cout << "7. Sauvegarde et rappel de l'etat..." << std::endl;
        Harness a;
        a.set (P::oscAPos, 0.42f);
        a.set (P::oscATable, 3);
        a.set (P::env1Attack, 1234.0f);
        juce::MemoryBlock mb;
        a.proc.getStateInformation (mb);
        Harness b;
        b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b.proc.getValue (P::oscAPos) - 0.42f) < 1.0e-3f, "position de wavetable non restauree");
        check ((int) b.proc.getValue (P::oscATable) == 3, "table non restauree");
        check (std::abs (b.proc.getValue (P::env1Attack) - 1234.0f) < 1.0f, "attaque non restauree");
    }

    // 8. Paramètres extrêmes au hasard (fuzz)
    if (onlyTest == 0 || onlyTest == 8)
    {
        std::cout << "8. Parametres au hasard (fuzz)..." << std::endl;
        Harness h (44100.0, 256);
        juce::Random rnd (7);
        auto& params = h.proc.getParameters();
        for (int round = 0; round < 60; ++round)
        {
            for (auto* p : params) if (rnd.nextFloat() < 0.4f) p->setValueNotifyingHost (rnd.nextFloat());
            h.set (P::masterVol, 6.0f);
            h.set (P::ceiling, -0.3f); // plafond fixe : le pic est comparé à une seule valeur
            for (int k = 0; k < 4; ++k) h.noteOn (rnd.nextInt ({ 0, 128 }), rnd.nextFloat(), rnd.nextInt (256));
            if (rnd.nextBool()) h.noteOff (rnd.nextInt ({ 0, 128 }), rnd.nextInt (256));
            h.run (0.03);
        }
        const float pk = h.peak();
        check (pk >= 0.0f, "NaN avec parametres au hasard");
        check (pk <= ceilingGain (h), "plafond depasse avec parametres au hasard : " + juce::String (pk));
    }

    // 9. Interface : création / redimensionnement / destruction
    if (onlyTest == 0 || onlyTest == 9)
    {
        std::cout << "9. Interface (creation, redimensionnement)..." << std::endl;
        Harness h;
        std::unique_ptr<juce::AudioProcessorEditor> ed (h.proc.createEditor());
        for (float s : { 0.5f, 1.0f, 1.37f, 2.0f })
            ed->setSize ((int) (IDGemsSynthEditor::baseW * s), (int) (IDGemsSynthEditor::baseH * s));
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        check (img.getWidth() > 0, "capture de l'interface impossible");
        ed.reset();
    }

    // 10. CPU : 32 voix tenues, unison absent (phase 1), 10 s de son à 48 kHz
    if (onlyTest == 0 || onlyTest == 10)
    {
        std::cout << "10. CPU (32 voix, 10 s a 48 kHz)..." << std::endl;
        Harness h (48000.0, 256);
        h.set (P::voices, 32);
        h.set (P::oscATable, 3);
        h.set (P::oscAPos, 0.5f);
        for (int n = 30; n < 62; ++n) h.noteOn (n, 0.7f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        h.run (10.0);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double ratio = ms / 10000.0;
        std::cout << "   charge : " << juce::String (ratio * 100.0, 2) << " % du temps reel (" << h.proc.activeVoices.load() << " voix)" << std::endl;
        check (h.proc.activeVoices.load() == 32, "les 32 voix ne sont pas toutes actives");
        check (ratio < 0.5, "CPU trop eleve : " + juce::String (ratio * 100.0, 1) + " %");
    }

    // ===================================================================== PHASE 2
    // 11. Warps : chaque mode, avec Osc B comme source de modulation
    if (onlyTest == 0 || onlyTest == 11)
    {
        std::cout << "11. Warps (chaque mode, source Osc B)..." << std::endl;
        for (int w = 1; w < NumWarpModes; ++w)
        {
            Harness h;
            h.set (P::oscATable, 1);
            h.set (P::oscAPos, 0.5f);
            h.set ("osca_warp1", (float) w);
            h.set ("osca_warp1amt", 0.7f);
            h.set ("osca_warp1src", (float) SrcOscB);
            h.set ("oscb_on", 1.0f);
            h.set ("oscb_semi", 7.0f);
            for (int n : { 36, 60, 84 }) h.noteOn (n);
            h.run (0.3);
            const float pk = h.peak();
            check (pk >= 0.0f, "warp " + warpNames()[w] + " : NaN");
            check (pk <= ceilingGain (h), "warp " + warpNames()[w] + " : plafond depasse");
            check (pk > 0.01f, "warp " + warpNames()[w] + " : silence");
        }
    }

    // 12. Filtres : chaque type, résonance et drive au maximum
    if (onlyTest == 0 || onlyTest == 12)
    {
        std::cout << "12. Filtres (chaque type, resonance et drive max)..." << std::endl;
        for (int t = 0; t < NumFilterTypes; ++t)
            for (float reso : { 0.0f, 1.0f })
            {
                Harness h;
                h.set (P::oscATable, 1);
                h.set (P::oscAPos, 1.0f);
                h.set ("f1_on", 1.0f);
                h.set ("f1_type", (float) t);
                h.set ("f1_cutoff", 900.0f);
                h.set ("f1_reso", reso);
                h.set ("f1_drive", reso);
                h.set ("f1_key", 0.5f);
                for (int n : { 36, 48, 67, 91 }) h.noteOn (n);
                h.run (0.25);
                h.set ("f1_cutoff", 20000.0f);
                h.run (0.1);
                h.set ("f1_cutoff", 20.0f);
                h.run (0.1);
                const float pk = h.peak();
                const auto name = filterTypeNames()[t] + " (reso " + juce::String (reso) + ")";
                check (pk >= 0.0f, "filtre " + name + " : NaN");
                check (pk <= ceilingGain (h), "filtre " + name + " : plafond depasse");
                check (pk > 0.01f, "filtre " + name + " : silence");
            }
        // série / parallèle, routages
        for (int serial = 0; serial < 2; ++serial)
            for (int route = 0; route < 4; ++route)
            {
                Harness h;
                h.set (P::filtSerial, (float) serial);
                h.set ("f1_on", 1.0f);
                h.set ("f2_on", 1.0f);
                h.set ("osca_route", (float) route);
                h.set (P::subOn, 1.0f);
                h.set (P::subRoute, (float) ((route + 1) % 4));
                h.noteOn (48);
                h.run (0.2);
                check (h.peak() > 0.01f, "routage " + juce::String (route) + (serial ? " parallele" : " serie") + " : silence");
            }
    }

    // 13. Sub et bruits
    if (onlyTest == 0 || onlyTest == 13)
    {
        std::cout << "13. Sub et banque de bruits..." << std::endl;
        for (int t = 0; t < NumNoiseTypes; ++t)
        {
            Harness h;
            h.set (P::oscAOn, 0.0f);
            h.set (P::noiseOn, 1.0f);
            h.set (P::noiseType, (float) t);
            h.set (P::noiseLevel, 1.0f);
            for (float tone : { -0.8f, 0.0f, 0.8f })
            {
                h.set (P::noiseTone, tone);
                h.noteOn (60);
                h.run (0.3);
                h.noteOff (60);
                h.run (0.05);
            }
            check (h.peak() >= 0.0f, "bruit " + noiseTypeNames()[t] + " : NaN");
            check (h.peak() > 0.005f, "bruit " + noiseTypeNames()[t] + " : silence");
        }
        for (int shape = 0; shape < 3; ++shape)
        {
            Harness h;
            h.set (P::oscAOn, 0.0f);
            h.set (P::subOn, 1.0f);
            h.set (P::subShape, (float) shape);
            h.noteOn (48);
            h.run (0.2);
            check (h.peak() > 0.05f, "sub forme " + juce::String (shape) + " : silence");
        }
    }

    // 14. Modes de jeu : mono, legato, glide, sans clic
    if (onlyTest == 0 || onlyTest == 14)
    {
        std::cout << "14. Mono / legato / glide..." << std::endl;
        for (int mode : { VoiceMono, VoiceLegato })
        {
            Harness h;
            h.sineSetup();
            h.set (P::voiceMode, (float) mode);
            h.set (P::glide, 120.0f);
            h.set (P::velSens, 0.0f);
            h.set (P::env1Attack, 0.0f);
            h.set (P::env1Sustain, 1.0f);
            // jeu legato : chaque note chevauche la précédente
            h.noteOn (45, 1.0f);
            h.run (0.1);
            for (int k = 0; k < 8; ++k)
            {
                h.noteOn (47 + k * 2, 1.0f, 100);
                h.noteOff (45 + k * 2, 300);
                h.run (0.1);
            }
            h.run (0.1);
            check (h.proc.activeVoices.load() == 1, juce::String (mode == VoiceMono ? "mono" : "legato") + " : " + juce::String (h.proc.activeVoices.load()) + " voix actives au lieu d'1");
            check (h.maxStep() < 0.04f, juce::String (mode == VoiceMono ? "mono" : "legato") + " : clic de " + juce::String (h.maxStep()));
            // retour à la note tenue quand on relâche la dernière
            h.noteOn (60, 1.0f);
            h.run (0.05);
            h.noteOff (60);
            h.run (0.05);
            check (h.proc.activeVoices.load() == 1, "la note tenue n'est pas reprise apres relachement");
        }
        {
            // poly + glide : pas de NaN, pas de clic
            Harness h;
            h.sineSetup();
            h.set (P::glide, 200.0f);
            h.set (P::velSens, 0.0f);
            for (int n : { 40, 47, 52, 59 }) { h.noteOn (n, 0.6f); h.run (0.05); }
            check (h.maxStep() < 0.04f, "poly + glide : clic de " + juce::String (h.maxStep()));
        }
    }

    // 15. Unison et inharmonicité : changements en cours de note sans clic, sans NaN
    if (onlyTest == 0 || onlyTest == 15)
    {
        std::cout << "15. Unison et inharmonicite..." << std::endl;
        Harness h;
        h.sineSetup();
        h.set (P::velSens, 0.0f);
        h.noteOn (45, 1.0f);
        h.run (0.1);
        const size_t from = h.outL.size();
        for (int u : { 3, 7, 16, 1, 5 }) { h.set ("osca_unison", (float) u); h.run (0.05); }
        check (h.maxStep (from) < 0.05f, "changement d'unison : clic de " + juce::String (h.maxStep (from)));
        for (float st : { 0.3f, -0.6f, 1.0f, 0.0f })
        {
            h.set ("osca_stretch", st);
            h.set ("osca_shift", st * 0.5f);
            h.run (0.05);
        }
        check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h), "inharmonicite : NaN ou plafond");
        // les deux oscillateurs en unison 16 + additif : test de charge plus loin
    }

    // 16. Samples : chargement, note d'origine dans le nom, boucle, multisample, sample -> wavetable
    if (onlyTest == 0 || onlyTest == 16)
    {
        std::cout << "16. Samples (Sample, Multisample, sample -> wavetable)..." << std::endl;
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("idgs_tests");
        dir.deleteRecursively();
        dir.createDirectory();
        auto writeTone = [&] (const juce::String& name, double hz, double secs)
        {
            auto f = dir.getChildFile (name);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 44100.0, 1, 16, {}, 0));
            if (w != nullptr) os.release();
            juce::AudioBuffer<float> b (1, (int) (secs * 44100.0));
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.6f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / 44100.0)
                                  + 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * 3.0 * i / 44100.0));
            if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            return f;
        };
        auto a2 = writeTone ("Test_A2.wav", 110.0, 0.5);
        auto c4 = writeTone ("Test_C4.wav", 261.63, 0.5);
        check (SampleSet::noteFromName ("Piano_C3") == 60 && SampleSet::noteFromName ("Pad A#2") == 58
                   && SampleSet::noteFromName ("Bass Eb1") == 39 && SampleSet::noteFromName ("Vox_Take2") == -1,
               "detection de la note dans le nom de fichier");
        {
            Harness h;
            h.set ("osca_mode", (float) ModeSample);
            const auto msg = h.proc.dropFiles (0, { a2 });
            check (h.proc.currentSamples (0) != nullptr, "sample non charge : " + msg);
            check ((int) h.proc.getValue ("osca_root") == 57, "note d'origine A2 non detectee : " + juce::String (h.proc.getValue ("osca_root")));
            h.noteOn (57);
            h.run (1.5); // plus long que le sample : la boucle doit continuer
            const size_t tail = h.outL.size() - 4096;
            check (h.peak (tail) > 0.02f, "boucle : plus de son apres la fin du sample");
            h.noteOff (57);
            h.set ("osca_loop", 0.0f);
            h.run (0.5);
            h.noteOn (57);
            h.run (1.5);
            check (h.peak (h.outL.size() - 4096) < 1.0e-4f, "sans boucle : le sample devrait s'arreter");
            check (h.peak() <= ceilingGain (h), "sample : plafond depasse");
            // état : le chemin du sample est sauvé et rechargé
            juce::MemoryBlock mb;
            h.proc.getStateInformation (mb);
            Harness b;
            b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            check (b.proc.currentSamples (0) != nullptr, "sample non recharge depuis l'etat");
        }
        {
            Harness h;
            h.set ("oscb_mode", (float) ModeMulti);
            h.set ("oscb_on", 1.0f);
            h.set (P::oscAOn, 0.0f);
            h.proc.dropFiles (1, { a2, c4 });
            const auto* set = h.proc.currentSamples (1);
            check (set != nullptr && set->zones.size() == 2, "multisample : 2 zones attendues");
            if (set != nullptr && set->zones.size() == 2)
                check (set->zones[0]->root == 57 && set->zones[1]->root == 72 && set->zoneFor (40) == 0 && set->zoneFor (100) == 1,
                       "multisample : repartition des zones incorrecte");
            for (int n : { 40, 57, 72, 90 }) h.noteOn (n);
            h.run (0.3);
            check (h.peak() > 0.02f, "multisample : silence");
        }
        {
            Harness h;
            h.proc.dropFiles (0, { c4 });
            const auto& t = h.proc.displayTable (0);
            check ((int) h.proc.getValue (P::oscATable) == WavetableBank::get().size(), "sample -> wavetable : table personnalisee non selectionnee");
            check (t.getNumFrames() > 1, "sample -> wavetable : une seule frame");
            h.noteOn (60);
            h.run (0.2);
            check (h.peak() > 0.02f, "sample -> wavetable : silence");
            juce::MemoryBlock mb;
            h.proc.getStateInformation (mb);
            Harness b;
            b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            check (b.proc.displayTable (0).getNumFrames() == t.getNumFrames(), "wavetable personnalisee non restauree");
        }
        dir.deleteRecursively();
    }

    // 17. Bouton dé : 20 wavetables aléatoires, balayées sur toute la position
    if (onlyTest == 0 || onlyTest == 17)
    {
        std::cout << "17. Wavetables aleatoires (de)..." << std::endl;
        Harness h;
        for (int k = 0; k < 20; ++k)
        {
            h.proc.randomTable (0);
            h.set (P::oscAPos, 0.0f);
            h.noteOn (48 + k);
            h.run (0.05);
            h.set (P::oscAPos, 1.0f);
            h.run (0.05);
            h.noteOff (48 + k);
        }
        check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h), "wavetables aleatoires : NaN ou plafond");
    }

    // 18. Presets d'usine : chacun joue, sans NaN ni dépassement ; sauvegarde / rechargement d'un preset
    if (onlyTest == 0 || onlyTest == 18)
    {
        std::cout << "18. Presets d'usine et sauvegarde..." << std::endl;
        Harness h;
        auto& pm = *h.proc.presets;
        int factory = 0;
        for (int i = 0; i < (int) pm.getAll().size(); ++i)
        {
            if (pm.getAll()[(size_t) i].factoryIndex < 0) continue;
            ++factory;
            pm.load (i);
            const size_t from = h.outL.size();
            for (int n : { 36, 48, 55, 64 }) h.noteOn (n, 0.9f);
            h.run (0.4);
            for (int n : { 36, 48, 55, 64 }) h.noteOff (n);
            h.run (0.1);
            const float pk = h.peak (from);
            check (pk >= 0.0f && pk <= ceilingGain (h), "preset " + pm.getAll()[(size_t) i].name + " : NaN ou plafond");
            check (pk > 0.01f, "preset " + pm.getAll()[(size_t) i].name + " : silence");
            h.proc.panic();
            h.run (0.05);
        }
        check (factory >= 8, "moins de 8 presets d'usine");
        pm.load (2);
        const float detune = h.proc.getValue ("osca_detune");
        check (pm.save ("Test Claude", "IDGems", "Utilisateur", "test"), "sauvegarde du preset impossible");
        pm.load (0);
        int idx = -1;
        for (int i = 0; i < (int) pm.getAll().size(); ++i) if (pm.getAll()[(size_t) i].name == "Test Claude") idx = i;
        check (idx >= 0 && pm.load (idx), "preset sauvegarde introuvable");
        check (std::abs (h.proc.getValue ("osca_detune") - detune) < 1.0e-4f, "preset recharge : valeur differente");
        if (idx >= 0) pm.getAll()[(size_t) idx].file.deleteFile();
    }

    // 19. CPU au pire cas : 32 voix, 2 oscs en unison 16, additif, 2 filtres, sub, bruit
    if (onlyTest == 0 || onlyTest == 19)
    {
        std::cout << "19. CPU au pire cas (32 voix x 2 oscs x unison 16 + additif + 2 filtres)..." << std::endl;
        Harness h (48000.0, 256);
        h.set (P::voices, 32);
        for (int o = 0; o < 2; ++o)
        {
            h.set (oscId (o, "on").toRawUTF8(), 1.0f);
            h.set (oscId (o, "unison").toRawUTF8(), 16.0f);
            h.set (oscId (o, "warp1").toRawUTF8(), (float) WarpFM);
            h.set (oscId (o, "warp1amt").toRawUTF8(), 0.3f);
        }
        h.set ("osca_stretch", 0.3f);
        h.set ("f1_on", 1.0f); h.set ("f1_type", (float) FiltLadder);
        h.set ("f2_on", 1.0f); h.set ("f2_type", (float) FiltCombPlus);
        h.set (P::subOn, 1.0f);
        h.set (P::noiseOn, 1.0f);
        for (int n = 30; n < 62; ++n) h.noteOn (n, 0.7f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        h.run (3.0);
        const double ratio = (juce::Time::getMillisecondCounterHiRes() - t0) / 3000.0;
        std::cout << "   charge : " << juce::String (ratio * 100.0, 1) << " % du temps reel (pire cas, " << h.proc.activeVoices.load() << " voix)" << std::endl;
        check (h.peak() >= 0.0f, "pire cas : NaN");
    }

    // 20. CPU d'un patch courant : 16 voix, Osc A unison 7 + Osc B, filtre LP24
    if (onlyTest == 0 || onlyTest == 20)
    {
        std::cout << "20. CPU d'un patch courant (16 voix, unison 7, filtre)..." << std::endl;
        Harness h (48000.0, 256);
        h.set ("osca_unison", 7.0f);
        h.set ("oscb_on", 1.0f);
        h.set ("f1_on", 1.0f);
        for (int n = 40; n < 56; ++n) h.noteOn (n, 0.7f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        h.run (5.0);
        const double ratio = (juce::Time::getMillisecondCounterHiRes() - t0) / 5000.0;
        std::cout << "   charge : " << juce::String (ratio * 100.0, 1) << " % du temps reel" << std::endl;
        check (ratio < 0.5, "CPU trop eleve pour un patch courant : " + juce::String (ratio * 100.0, 1) + " %");
    }

    // ===================================================================== PHASE 3
    // 21. Modulation : connexions, effet sur le son, sauvegarde
    if (onlyTest == 0 || onlyTest == 21)
    {
        std::cout << "21. Modulation (macros, LFO, enveloppes, sauvegarde)..." << std::endl;
        auto rmsOf = [] (Harness& h, size_t from)
        {
            double e = 0.0;
            for (size_t i = from; i < h.outL.size(); ++i) e += (double) h.outL[i] * h.outL[i];
            return (float) std::sqrt (e / (double) juce::jmax ((size_t) 1, h.outL.size() - from));
        };
        {
            // macro 1 -> niveau de l'Osc A : macro à 0 + profondeur -1 = silence, macro à 1 = son
            Harness h;
            const int lvl = h.proc.paramIndex ("osca_level");
            check (h.proc.modTargets.isModulatable (lvl), "le niveau de l'Osc A devrait etre modulable");
            h.proc.modShared.addConnection (MSrcMacro0, lvl, -1.0f);
            h.set ("macro1", 1.0f);
            h.noteOn (60);
            h.run (0.3);
            const size_t from = h.outL.size() - 4096;
            check (rmsOf (h, from) < 0.01f, "macro -> niveau : la modulation n'a pas d'effet");
            h.set ("macro1", 0.0f);
            const size_t from2 = h.outL.size();
            h.run (0.3);
            check (rmsOf (h, from2 + 8192) > 0.02f, "macro -> niveau : pas de son a macro 0");
        }
        {
            // LFO par voix -> coupure : le son varie dans le temps, pas de NaN
            Harness h;
            h.set ("osca_table", 1); h.set ("osca_pos", 1.0f);
            h.set ("f1_on", 1.0f); h.set ("f1_cutoff", 400.0f);
            h.set ("lfo1_sync", 0.0f); h.set ("lfo1_rate", 4.0f);
            h.proc.modShared.addConnection (MSrcLfo0, h.proc.paramIndex ("f1_cutoff"), 0.6f);
            h.proc.modShared.addConnection (MSrcLfo0, h.proc.paramIndex ("osca_level"), -0.7f);
            for (int n : { 48, 55, 60 }) h.noteOn (n);
            h.run (1.0);
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h), "LFO -> coupure : NaN ou plafond");
            float mn = 1.0f, mx = 0.0f;
            for (int b = 0; b < 8; ++b)
            {
                double e = 0.0;
                const size_t s0 = (size_t) (b * 4800 + 4800);
                for (size_t i = s0; i < s0 + 4800 && i < h.outL.size(); ++i) e += (double) h.outL[i] * h.outL[i];
                const float r = (float) std::sqrt (e / 4800.0);
                mn = juce::jmin (mn, r); mx = juce::jmax (mx, r);
            }
            check (mx > mn * 1.3f, "LFO -> niveau : le niveau ne bouge pas (" + juce::String (mn) + " / " + juce::String (mx) + ")");
        }
        {
            // ENV 2 -> pitch fin + vélocité -> mix, fuzz des profondeurs : pas de NaN
            Harness h (44100.0, 128);
            juce::Random rnd (3);
            for (int k = 0; k < 40; ++k)
            {
                const int src = rnd.nextInt (NumModSrc);
                int prm = -1;
                for (int tries = 0; tries < 50 && prm < 0; ++tries)
                {
                    const int c = rnd.nextInt (h.proc.getParameters().size());
                    if (h.proc.modTargets.isModulatable (c)) prm = c;
                }
                if (prm >= 0) h.proc.modShared.addConnection (src, prm, rnd.nextFloat() * 2.0f - 1.0f, rnd.nextBool());
            }
            h.set ("osca_unison", 4.0f);
            h.set ("f1_on", 1.0f); h.set ("f2_on", 1.0f);
            for (int k = 0; k < 30; ++k)
            {
                h.noteOn (36 + rnd.nextInt (48), rnd.nextFloat());
                h.midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, rnd.nextInt (128)), 0);
                h.midi.addEvent (juce::MidiMessage::pitchWheel (1, rnd.nextInt (16384)), 3);
                h.set (macroId (rnd.nextInt (8)).toRawUTF8(), rnd.nextFloat());
                h.run (0.05);
                if (rnd.nextBool()) h.noteOff (36 + rnd.nextInt (48));
            }
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h), "modulations au hasard : NaN ou plafond");
            // sauvegarde : connexions et forme de LFO restaurées
            h.proc.modShared.setShape (2, idg::LfoShape::preset (4));
            int count = 0;
            for (auto& sl : h.proc.modShared.slots) if (sl.src.load() >= 0) ++count;
            juce::MemoryBlock mb;
            h.proc.getStateInformation (mb);
            Harness b;
            b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            int count2 = 0;
            for (auto& sl : b.proc.modShared.slots) if (sl.src.load() >= 0) ++count2;
            check (count == count2 && count > 0, "connexions non restaurees : " + juce::String (count) + " / " + juce::String (count2));
            check (b.proc.modShared.shapes[2].pts.size() == idg::LfoShape::preset (4).pts.size(), "forme de LFO non restauree");
        }
        {
            // CPU : 16 voix, 20 modulations par voix
            Harness h (48000.0, 256);
            for (int k = 0; k < 20; ++k)
                h.proc.modShared.addConnection (MSrcLfo0 + (k % NumLfos), h.proc.paramIndex (k % 2 ? "f1_cutoff" : "osca_pos"), 0.1f + 0.01f * (float) k);
            h.set ("f1_on", 1.0f);
            for (int n = 40; n < 56; ++n) h.noteOn (n, 0.7f);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            h.run (3.0);
            const double ratio = (juce::Time::getMillisecondCounterHiRes() - t0) / 3000.0;
            std::cout << "   charge avec modulations : " << juce::String (ratio * 100.0, 1) << " % du temps reel" << std::endl;
        }
    }

    // ===================================================================== PHASE 4
    // 22. Bus et effets : chaque effet sur chaque rack, envois, boucles, mute / solo, latence
    if (onlyTest == 0 || onlyTest == 22)
    {
        std::cout << "22. Bus A/B/C + Master (chaque effet, envois, boucles, solo)..." << std::endl;
        const auto& defs = idg::effectDefs();
        for (int rk = 0; rk < idg::NumRacks; ++rk)
        {
            Harness h (48000.0, 256);
            const auto& reg = idg::rackRegistry (rk);
            // le rack Master reçoit tout ; pour les bus, on envoie les sources dans le bus testé
            if (rk < NumBuses)
                for (int s = 0; s < NumSendSources; ++s)
                    for (int b = 0; b < NumBuses; ++b) h.set (sendId (s, b).toRawUTF8(), b == rk ? 1.0f : 0.0f);
            h.set ("osca_table", 1); h.set ("osca_pos", 0.8f);
            h.set (P::noiseOn, 1.0f);
            for (int fx = 0; fx < idg::NumFx; ++fx)
            {
                h.set (reg.all[(size_t) reg.fxOn[fx]].id.toRawUTF8(), 1.0f);
                for (int n : { 40, 52, 64 }) h.noteOn (n, 0.9f);
                h.run (0.15);
                for (int n : { 40, 52, 64 }) h.noteOff (n);
                h.run (0.05);
                const float pk = h.peak();
                if (pk < 0.0f || pk > ceilingGain (h))
                    check (false, idg::rackDisplayName (rk) + " / " + defs[(size_t) fx].name + " : NaN ou plafond depasse");
                h.set (reg.all[(size_t) reg.fxOn[fx]].id.toRawUTF8(), 0.0f);
            }
            // tous les effets en même temps, séquenceurs actifs
            for (int fx = 0; fx < idg::NumFx; ++fx)
            {
                h.set (reg.all[(size_t) reg.fxOn[fx]].id.toRawUTF8(), 1.0f);
                h.proc.rack (rk).seq[fx].enabled = fx % 3 == 0;
            }
            for (int n : { 40, 52, 64 }) h.noteOn (n, 0.9f);
            h.run (1.0);
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h), idg::rackDisplayName (rk) + " : tous les effets -> NaN ou plafond");
        }
        {
            // boucle A -> B -> C -> A : coupée, pas d'explosion ; sons dans B et C
            Harness h;
            h.set ("bus_a_to_b", 1.0f); h.set ("bus_b_to_c", 1.0f); h.set ("bus_c_to_a", 1.0f);
            h.noteOn (48, 1.0f);
            h.run (1.0);
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h), "boucle de bus : NaN ou plafond");
            check (h.proc.busEdgeBlocked[2][0].load(), "la boucle C -> A aurait du etre coupee");
            check (! h.proc.busEdgeBlocked[0][1].load() && ! h.proc.busEdgeBlocked[1][2].load(), "envois A -> B -> C coupes a tort");
        }
        {
            // solo du bus B alors que tout part dans A : silence
            Harness h;
            h.set ("bus_b_solo", 1.0f);
            h.noteOn (48, 1.0f);
            h.run (0.3);
            check (h.peak (h.outL.size() - 4096) < 1.0e-4f, "solo du bus B : le bus A devrait etre muet");
            h.set ("bus_b_solo", 0.0f);
            h.set ("bus_a_mute", 1.0f);
            h.set ("send_osca_c", 1.0f);
            h.run (0.3);
            check (h.peak (h.outL.size() - 4096) > 0.02f, "envoi vers C : pas de son alors que A est mute");
        }
        {
            // mode avec latence : effets STFT actifs, latence annoncée cohérente
            Harness h;
            h.set (P::zeroLat, 0.0f);
            const auto& reg = idg::rackRegistry (0);
            h.set (reg.all[(size_t) reg.fxOn[idg::FxPitchShift]].id.toRawUTF8(), 1.0f);
            h.noteOn (60);
            h.run (0.5);
            check (h.proc.getLatencySamples() > 0, "latence non annoncee en mode normal");
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h), "pitch shift : NaN ou plafond");
        }
        {
            // suiveur du bus A -> niveau de l'Osc B : pas de NaN ; état : ordre et séquenceur restaurés
            Harness h;
            h.proc.modShared.addConnection (MSrcFollow0, h.proc.paramIndex ("oscb_level"), 0.5f);
            h.proc.modShared.addConnection (MSrcLfo0, h.proc.paramIndex (idg::rackRegistry (0).all[(size_t) idg::rackRegistry (0).fxMix[idg::FxFilter]].id), 0.5f);
            h.set ("oscb_on", 1.0f);
            h.noteOn (50);
            h.run (0.5);
            check (h.proc.followerValue[0].load() > 0.1f, "suiveur du bus A inactif");
            auto order = h.proc.rack (1).shared.getOrder();
            std::reverse (order.begin(), order.end());
            h.proc.rack (1).shared.setOrder (order);
            h.proc.rack (2).seq[5].enabled = true;
            h.proc.rack (2).seq[5].length = 7;
            juce::MemoryBlock mb;
            h.proc.getStateInformation (mb);
            Harness b;
            b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            check (b.proc.rack (1).shared.getOrder() == order, "ordre du rack non restaure");
            check (b.proc.rack (2).seq[5].enabled.load() && b.proc.rack (2).seq[5].length.load() == 7, "sequenceur de module non restaure");
        }
    }

    // ===================================================================== PHASE 5
    // 23. Osc Texture : chaque moteur, morph, empreinte, modes granulaire / spectral des Osc A / B
    if (onlyTest == 0 || onlyTest == 23)
    {
        std::cout << "23. Osc Texture (6 moteurs, morph, empreinte, modes granulaire / spectral)..." << std::endl;
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("idgs_tex");
        dir.deleteRecursively();
        dir.createDirectory();
        auto writeWav = [&] (const juce::String& name, double secs, std::function<float (double)> fn)
        {
            auto f = dir.getChildFile (name);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 44100.0, 1, 16, {}, 0));
            if (w != nullptr) os.release();
            juce::AudioBuffer<float> b (1, (int) (secs * 44100.0));
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, fn (i / 44100.0));
            if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            return f;
        };
        juce::Random wr (5);
        auto s1 = writeWav ("Tex_C3.wav", 1.5, [] (double t) { return 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 261.6 * t) * (float) (0.5 + 0.5 * std::sin (t * 7.0)); });
        auto s2 = writeWav ("Tex2.wav", 1.0, [&wr] (double) { return wr.nextFloat() * 0.6f - 0.3f; });
        auto voice = writeWav ("Voix.wav", 1.0, [] (double t) { return 0.4f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 180.0 * t) + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1100.0 * t * (1.0 + t))); });
        auto anyFile = dir.getChildFile ("data.bin");
        {
            juce::MemoryBlock mb;
            for (int i = 0; i < 200000; ++i) { const uint8_t b = (uint8_t) ((i * 37 + (i >> 7)) & 0xff); mb.append (&b, 1); }
            anyFile.replaceWithData (mb.getData(), mb.getSize());
        }
        for (int e = 0; e < NumTexEngines; ++e)
            for (float morph : { 0.0f, 0.5f })
            {
                Harness h;
                h.set (P::oscAOn, 0.0f);
                h.set ("tex_on", 1.0f);
                h.set ("tex_engine1", (float) e);
                h.set ("tex_engine2", (float) ((e + 1) % NumTexEngines));
                h.set ("tex_morph", morph);
                h.proc.dropFiles (IDGemsSynthProcessor::AssetTex1, { s1 });
                h.proc.dropFiles (IDGemsSynthProcessor::AssetTex2, { s2 });
                h.proc.dropFiles (IDGemsSynthProcessor::AssetData, { anyFile });
                h.set ("tex_r_exciter", 1.0f);
                for (int n : { 48, 60, 72 }) h.noteOn (n, 0.9f);
                h.run (0.8);
                const float pk = h.peak();
                const auto name = texEngineNames()[e] + (morph > 0.0f ? " + morph" : "");
                check (pk >= 0.0f && pk <= ceilingGain (h), "texture " + name + " : NaN ou plafond");
                check (pk > 0.005f, "texture " + name + " : silence (" + juce::String (pk) + ")");
            }
        {
            // granulaire live (Osc A + B) + empreinte spectrale
            Harness h;
            h.set ("tex_on", 1.0f);
            h.set ("tex_g_source", 1.0f);
            h.set ("fp_on", 1.0f);
            h.proc.dropFiles (IDGemsSynthProcessor::AssetFingerprint, { voice });
            const auto* fp = h.proc.currentSamples (IDGemsSynthProcessor::AssetFingerprint);
            check (fp != nullptr && fp->zones[0]->fingerprint != nullptr, "empreinte non calculee");
            h.noteOn (55, 1.0f);
            h.run (1.0);
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h) && h.peak() > 0.005f, "granulaire live + empreinte : NaN, plafond ou silence");
            // état : les emplacements sont rechargés
            juce::MemoryBlock mb;
            h.proc.getStateInformation (mb);
            Harness b;
            b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            check (b.proc.currentSamples (IDGemsSynthProcessor::AssetFingerprint) != nullptr, "empreinte non restauree");
        }
        for (int mode : { (int) ModeGranular, (int) ModeSpectral })
        {
            Harness h;
            h.set ("osca_mode", (float) mode);
            h.proc.dropFiles (0, { s1 });
            if (mode == ModeSpectral) check (h.proc.currentSamples (0) != nullptr && h.proc.currentSamples (0)->zones[0]->spectral != nullptr, "osc spectral : analyse absente");
            for (int n : { 48, 60 }) h.noteOn (n, 0.9f);
            h.run (0.8);
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h) && h.peak() > 0.005f,
                   juce::String ("Osc A mode ") + (mode == ModeGranular ? "granulaire" : "spectral") + " : NaN, plafond ou silence");
        }
        {
            // CPU : 16 voix, texture spectrale + granulaire en morph
            Harness h (48000.0, 256);
            h.set ("tex_on", 1.0f); h.set ("tex_engine1", (float) TexSpectral); h.set ("tex_engine2", (float) TexGranular); h.set ("tex_morph", 0.5f);
            h.proc.dropFiles (IDGemsSynthProcessor::AssetTex1, { s1 });
            for (int n = 40; n < 56; ++n) h.noteOn (n, 0.7f);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            h.run (3.0);
            const double ratio = (juce::Time::getMillisecondCounterHiRes() - t0) / 3000.0;
            std::cout << "   charge texture (16 voix, spectral + granulaire) : " << juce::String (ratio * 100.0, 1) << " %" << std::endl;
        }
        dir.deleteRecursively();
    }

    // ===================================================================== PHASE 6
    // 24. Jeu : arpège, séquenceur, générateur, gamme, Note Spray, micro-random, export MIDI
    if (onlyTest == 0 || onlyTest == 24)
    {
        std::cout << "24. Arpege / sequenceur / generateur / gamme / spray / micro-random / export MIDI..." << std::endl;
        auto countNotes = [] (IDGemsSynthProcessor& proc, PlaySettings ps, std::function<void (juce::MidiBuffer&, int)> feed, double secs, std::vector<int>* notes = nullptr)
        {
            NoteGen gen;
            gen.prepare (48000.0);
            idg::dsp::Rng rng;
            juce::MidiBuffer in, out;
            out.ensureSize (8192);
            int ons = 0;
            const int blocks = (int) (secs * 48000.0 / 512.0);
            for (int b = 0; b < blocks; ++b)
            {
                in.clear();
                feed (in, b);
                gen.process (in, out, 512, b * 512 / 48000.0 * 2.0, 120.0, true, ps, proc.noteSeq, rng);
                for (const auto m : out) if (m.getMessage().isNoteOn()) { ++ons; if (notes) notes->push_back (m.getMessage().getNoteNumber()); }
            }
            return ons;
        };
        auto procPtr = std::make_unique<IDGemsSynthProcessor>();
        auto& proc = *procPtr;
        auto hold = [] (juce::MidiBuffer& in, int b) { if (b == 0) { in.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0); in.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0); in.addEvent (juce::MidiMessage::noteOn (1, 67, 0.8f), 0); } };
        {
            PlaySettings ps; ps.mode = PlayArp; ps.arpRate = idgs::divisionNames().indexOf ("1/16");
            // 2 s à 120 BPM = 4 temps = 16 doubles-croches
            const int n = countNotes (proc, ps, hold, 2.0);
            check (n >= 14 && n <= 18, "arpege 1/16 : " + juce::String (n) + " notes au lieu de ~16");
        }
        {
            PlaySettings ps; ps.mode = PlaySeq; ps.seqFollow = 1; ps.seqLen = 16; ps.seqRate = idgs::divisionNames().indexOf ("1/16");
            int expected = 0;
            for (int i = 0; i < 16; ++i) expected += proc.noteSeq.on[i].load() ? proc.noteSeq.ratchet[i].load() : 0;
            const int n = countNotes (proc, ps, [] (juce::MidiBuffer&, int) {}, 2.0);
            check (std::abs (n - expected) <= 2, "sequenceur : " + juce::String (n) + " notes au lieu de " + juce::String (expected));
        }
        {
            // gamme : toutes les notes produites sont dans la gamme (C mineur), y compris le spray et le random
            PlaySettings ps; ps.mode = PlaySeq; ps.seqFollow = 1; ps.rndNotes = 1.0f; ps.sprayOn = true; ps.sprayDensity = 6; ps.sprayRange = 12;
            ps.scale.enabled = true; ps.scale.key = 0; ps.scale.type = 1;
            std::vector<int> notes;
            countNotes (proc, ps, [] (juce::MidiBuffer&, int) {}, 3.0, &notes);
            bool allIn = ! notes.empty();
            for (int nn : notes) allIn &= ps.scale.contains (nn % 12);
            check (allIn, "quantification sur gamme : note hors gamme (" + juce::String ((int) notes.size()) + " notes)");
            check (notes.size() > 20, "Note Spray : trop peu de notes (" + juce::String ((int) notes.size()) + ")");
        }
        {
            // générateur : motif différent, gamme respectée, cadenas respecté
            proc.noteSeq.lock[3] = true;
            const int lockedNote = proc.noteSeq.note[3].load();
            idg::Scale sc; sc.enabled = true; sc.key = 2; sc.type = 0;
            for (int st = 0; st < NumGenStyles; ++st)
            {
                generatePattern (proc.noteSeq, 16, st, 0.6f, 2, 0.5f, 0.5f, sc, (uint32_t) (st * 77 + 1));
                int ons = 0;
                bool inScale = true;
                for (int i = 0; i < 16; ++i)
                {
                    if (proc.noteSeq.on[i].load()) ++ons;
                    if (i != 3) inScale &= sc.contains (((62 + proc.noteSeq.note[i].load()) % 12 + 12) % 12) || ! proc.noteSeq.on[i].load();
                }
                check (ons > 0, "generateur style " + juce::String (st) + " : motif vide");
                check (inScale, "generateur style " + juce::String (st) + " : note hors gamme");
            }
            check (proc.noteSeq.note[3].load() == lockedNote, "generateur : le pas verrouille a change");
            proc.noteSeq.lock[3] = false;
        }
        {
            // mutation : le motif change au fil des cycles
            PlaySettings ps; ps.mode = PlaySeq; ps.seqFollow = 1; ps.seqMutate = 1.0f;
            const auto before = proc.noteSeq.toTree().getProperty ("steps").toString();
            countNotes (proc, ps, [] (juce::MidiBuffer&, int) {}, 6.0);
            check (proc.noteSeq.toTree().getProperty ("steps").toString() != before, "mutation : le motif n'a pas change");
        }
        {
            // dans le synthé complet : arpège + micro-random + spray, pas de NaN, son présent
            Harness h;
            h.set ("play_mode", (float) PlayArp);
            h.set ("mr_on", 1.0f); h.set ("mr_amount", 1.0f);
            h.set ("spray_on", 1.0f);
            h.set ("f1_on", 1.0f);
            for (int n : { 48, 52, 55 }) h.noteOn (n, 0.8f);
            h.run (2.0);
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h) && h.peak() > 0.01f, "arpege + micro-random + spray : NaN, plafond ou silence");
            // export MIDI
            auto f = h.proc.exportMidi (2);
            juce::MidiFile mf;
            juce::FileInputStream is (f);
            check (f.existsAsFile() && is.openedOk() && mf.readFrom (is) && mf.getNumTracks() == 1 && mf.getTrack (0)->getNumEvents() > 4,
                   "export MIDI : fichier invalide");
            f.deleteFile();
            // état : pas du séquenceur restaurés
            h.proc.noteSeq.note[5] = 11;
            juce::MemoryBlock mb;
            h.proc.getStateInformation (mb);
            Harness b;
            b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            check (b.proc.noteSeq.note[5].load() == 11, "sequenceur de notes non restaure");
        }
    }


    // 25. Système de random : seed reproductible, cadenas, plages, garde-fous, historique, morph, drift, état
    if (onlyTest == 0 || onlyTest == 25)
    {
        std::cout << "25. Random (seed, cadenas, plages, garde-fous, historique, morph, drift)..." << std::endl;
        using RT = RandomEngine::Target;
        auto norm = [] (IDGemsSynthProcessor& p, const char* id) { return p.apvts.getParameter (id)->getValue(); };
        {
            // même seed => même patch
            Harness a, b;
            a.proc.randomEngine().perform (RT {}, 4242, 1.0f, false, "t");
            b.proc.randomEngine().perform (RT {}, 4242, 1.0f, false, "t");
            int diff = 0;
            for (int i = 0; i < a.proc.getParameters().size(); ++i)
                if (std::abs (a.proc.getParameters()[i]->getValue() - b.proc.getParameters()[i]->getValue()) > 1.0e-5f) ++diff;
            check (diff == 0, "random : meme seed, patchs differents (" + juce::String (diff) + " parametres)");
        }
        {
            Harness h;
            auto& re = h.proc.randomEngine();
            const int iCut = h.proc.paramIndex ("f1_cutoff"), iPos = h.proc.paramIndex ("osca_pos");
            re.setLocked (iCut, true);
            re.setRange (iPos, 0.2f, 0.3f);
            re.setSectionLocked (SecEnv, true);
            const float cut0 = norm (h.proc, "f1_cutoff"), att0 = norm (h.proc, "env2_attack");
            bool cutOk = true, posOk = true, envOk = true, changed = false;
            const float det0 = norm (h.proc, "osca_detune");
            for (int k = 0; k < 20; ++k)
            {
                re.perform (RT {}, (uint32_t) (k + 1), 1.0f, false, "t");
                cutOk &= std::abs (norm (h.proc, "f1_cutoff") - cut0) < 1.0e-6f;
                const float p = norm (h.proc, "osca_pos");
                posOk &= p >= 0.2f - 1.0e-4f && p <= 0.3f + 1.0e-4f;
                envOk &= std::abs (norm (h.proc, "env2_attack") - att0) < 1.0e-6f;
                changed |= std::abs (norm (h.proc, "osca_detune") - det0) > 1.0e-3f;
            }
            check (cutOk, "random : parametre verrouille modifie");
            check (posOk, "random : plage de random non respectee");
            check (envOk, "random : section verrouillee modifiee");
            check (changed, "random : rien ne change");
            // dosage 0 : rien ne bouge (paramètres continus)
            const float det1 = norm (h.proc, "osca_detune");
            re.perform (RT {}, 99, 0.0f, false, "t");
            check (std::abs (norm (h.proc, "osca_detune") - det1) < 1.0e-6f, "random : dosage 0 a modifie un parametre");
            // portée : un module seulement
            RT t; t.scope = RandomEngine::Scope::Module; t.module = "oscb";
            const float res0 = norm (h.proc, "f1_reso");
            re.perform (t, 7, 1.0f, false, "t");
            check (std::abs (norm (h.proc, "f1_reso") - res0) < 1.0e-6f, "random module Osc B : le filtre a change");
            // historique et rappel
            const int n = (int) re.getHistory().size();
            check (n >= 22, "random : historique incomplet (" + juce::String (n) + ")");
            const float detH = h.proc.apvts.getParameter ("osca_detune")->getValue();
            re.perform (RT {}, 1234, 1.0f, false, "t");
            re.recall (n - 1);
            check (std::abs (norm (h.proc, "osca_detune") - detH) < 1.0e-5f, "random : rappel de l'historique incorrect");
            // état : cadenas et plages sauvés avec le projet
            juce::MemoryBlock mb;
            h.proc.getStateInformation (mb);
            Harness c;
            c.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            auto& rc = c.proc.randomEngine();
            check (rc.isLocked (iCut) && rc.hasCustomRange (iPos) && rc.sectionLocked[SecEnv], "random : cadenas / plages non restaures");
        }
        {
            // garde-fous : aucun tirage muet, saturé ou NaN
            int silent = 0, bad = 0;
            double worstMs = 0.0;
            for (int k = 0; k < 12; ++k)
            {
                Harness h;
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                h.proc.randomEngine().perform (RT {}, (uint32_t) (k * 31 + 5), 1.0f, true, "t");
                worstMs = juce::jmax (worstMs, juce::Time::getMillisecondCounterHiRes() - t0);
                h.prepareFresh();
                for (int note : { 48, 60, 64, 67 }) h.noteOn (note, 0.8f);
                h.run (1.0);
                const float pk = h.peak();
                if (pk < 0.0f || pk > ceilingGain (h)) ++bad;
                else if (pk < 0.003f) ++silent;
            }
            check (bad == 0, "garde-fous : NaN ou plafond depasse (" + juce::String (bad) + ")");
            check (silent <= 1, "garde-fous : tirages muets (" + juce::String (silent) + "/12)");
            std::cout << "    tirage avec garde-fous : " << juce::String (worstMs, 0) << " ms (pire cas)" << std::endl;
            check (worstMs < 8000.0, "garde-fous trop lents");
        }
        {
            // personnalités : chaque personnalité produit un patch sain
            for (int pers = 0; pers < personalityNames().size(); ++pers)
            {
                Harness h;
                h.set ("rand_personality", (float) pers);
                h.set ("rand_intensity", 1.0f);
                h.proc.randomEngine().perform (RT {}, (uint32_t) (pers + 11), 1.0f, false, "t");
                h.prepareFresh();
                for (int note : { 48, 60, 64 }) h.noteOn (note, 0.8f);
                h.run (0.8);
                const float pk = h.peak();
                check (pk >= 0.0f && pk <= ceilingGain (h), "personnalite " + personalityNames()[pers] + " : NaN ou plafond");
            }
        }
        {
            // morph pad : mélange de deux snapshots
            Harness h;
            auto& re = h.proc.randomEngine();
            h.set ("osca_pos", 0.0f);
            re.storeSnapshot (0, 0.0f, 0.0f);
            h.set ("osca_pos", 1.0f);
            re.storeSnapshot (1, 1.0f, 1.0f);
            h.set ("morph_x", 0.5f); h.set ("morph_y", 0.5f);
            re.applyMorph (true);
            check (std::abs (norm (h.proc, "osca_pos") - 0.5f) < 0.02f, "morph : milieu incorrect (" + juce::String (norm (h.proc, "osca_pos")) + ")");
            h.set ("morph_x", 0.0f); h.set ("morph_y", 0.0f);
            re.applyMorph (true);
            check (norm (h.proc, "osca_pos") < 0.02f, "morph : coin incorrect");
        }
        {
            // drift : la position de wavetable dérive (affichage modulé), sans NaN
            Harness h;
            (void) h.proc.randomEngine();
            h.set ("rand_drift", 1.0f);
            h.set ("rand_drift_speed", 2.0f);
            h.set ("osca_pos", 0.5f);
            h.noteOn (60, 0.8f);
            float mn = 1.0f, mx = 0.0f;
            for (int k = 0; k < 20; ++k)
            {
                h.run (0.1);
                const float v = h.proc.modShared.fieldValue01[FOscPos].load();
                if (v >= 0.0f) { mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
            }
            check (mx - mn > 0.02f, "drift : aucune derive (" + juce::String (mx - mn) + ")");
            check (h.peak() >= 0.0f && h.peak() <= ceilingGain (h), "drift : NaN ou plafond");
        }
    }


    // 26. Création : enregistreur, rolling recorder, rendus (one-shot / loop / texture, HQ), batch, Feed, motion
    if (onlyTest == 0 || onlyTest == 26)
    {
        std::cout << "26. Enregistreur / rendus / batch / feed / motion..." << std::endl;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        auto readFile = [&fm] (const juce::File& f, double& rate) -> juce::AudioBuffer<float>
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) return {};
            rate = r->sampleRate;
            juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
            r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            return b;
        };
        auto bufPeak = [] (const juce::AudioBuffer<float>& b)
        {
            float pk = 0.0f;
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = b.getSample (c, i);
                    if (! std::isfinite (v)) return -1.0f;
                    pk = juce::jmax (pk, std::abs (v));
                }
            return pk;
        };
        Harness h;
        auto* rec = h.proc.getRecorder();
        check (rec != nullptr, "enregistreur absent");
        if (rec != nullptr)
        {
            check (rec->startRecording (120.0), "enregistrement : impossible de demarrer");
            h.noteOn (60, 0.8f);
            h.run (1.0);
            h.noteOff (60);
            h.run (0.3);
            auto f = rec->stopRecording();
            double rate = 0.0;
            auto b = readFile (f, rate);
            check (b.getNumSamples() > (int) (1.0 * h.sr) && bufPeak (b) > 0.01f, "enregistrement : fichier vide ou trop court");
            f.deleteFile();
            auto roll = rec->saveRolling (0.5f, 120.0);
            b = readFile (roll, rate);
            check (std::abs (b.getNumSamples() - (int) (0.5 * h.sr)) <= 1, "rolling recorder : longueur incorrecte");
            // Feed : la prise redevient la source de la Texture
            const auto msg = h.proc.dropFiles (IDGemsSynthProcessor::AssetTex1, { roll });
            check (h.proc.currentSamples (IDGemsSynthProcessor::AssetTex1) != nullptr, "feed -> texture : son non charge (" + msg + ")");
            roll.deleteFile();
        }
        // rendus hors ligne dans chaque mode
        auto& br = h.proc.batchRenderer();
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory);
        for (int mode = 0; mode < 3; ++mode)
        {
            RenderSettings rs;
            rs.mode = mode;
            rs.bars = 1;
            rs.textureSeconds = 3.0f;
            rs.noteSeconds = 0.5f;
            rs.hq = mode == 2;
            rs.chord = mode == 1;
            rs.bpm = 120.0;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            auto f = br.renderNow (rs, tmp.getChildFile ("idgs_render_" + juce::String (mode) + ".wav"));
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            double rate = 0.0;
            auto b = readFile (f, rate);
            const float pk = bufPeak (b);
            check (pk > 0.8f && pk < 0.95f, "rendu mode " + juce::String (mode) + " : crete " + juce::String (pk) + " (normalisation -1 dB)");
            check (rate == (mode == 2 ? 96000.0 : 48000.0), "rendu mode " + juce::String (mode) + " : frequence " + juce::String (rate));
            if (mode == 1) check (b.getNumSamples() == 96000, "loop 1 mesure a 120 bpm : " + juce::String (b.getNumSamples()) + " echantillons au lieu de 96000");
            if (mode == 0) check (b.getNumSamples() < (int) (8.0 * 48000), "one-shot : la fin silencieuse n'est pas coupee");
            std::cout << "    rendu mode " << mode << " : " << juce::String (ms, 0) << " ms" << std::endl;
            f.deleteFile();
        }
        // batch : 2 variations random
        {
            RenderSettings rs;
            rs.count = 2;
            rs.noteSeconds = 0.25f;
            rs.name = "test_batch";
            check (br.start (rs), "batch : impossible de demarrer");
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            while (br.isRunning() && juce::Time::getMillisecondCounterHiRes() - t0 < 120000.0)
                br.step();
            check (! br.isRunning() && br.files().size() == 2, "batch : " + juce::String (br.files().size()) + " fichier(s) au lieu de 2");
            br.folder().deleteRecursively();
        }
        // motion recording : geste sur la macro 1, rejoué en boucle
        {
            auto& m = h.proc.motionRecorder();
            m.record (1);
            auto* mac = h.proc.apvts.getParameter (macroId (0));
            for (int k = 0; k <= 16; ++k)
            {
                mac->setValueNotifyingHost ((float) k / 16.0f);
                m.step (h.proc.getBeatPosition() + 0.25 * k);
            }
            m.step (h.proc.getBeatPosition() + 4.1); // fin de la boucle -> lecture
            check (m.getState() == MotionRecorder::Playing && m.macroUsed (0) && ! m.macroUsed (1), "motion : pas de lecture apres l'enregistrement");
            mac->setValueNotifyingHost (0.0f);
            m.play();
            const double base = std::floor (h.proc.getBeatPosition() / 4.0) * 4.0;
            m.step (base + 2.0);
            check (std::abs (mac->getValue() - 0.5f) < 0.08f, "motion : valeur rejouee " + juce::String (mac->getValue()) + " au lieu de 0.5");
            juce::MemoryBlock mb;
            h.proc.getStateInformation (mb);
            Harness c;
            c.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            check (c.proc.motionRecorder().hasData() && c.proc.motionRecorder().macroUsed (0), "motion : non restaure avec le projet");
            c.proc.motionRecorder().clear();
            m.clear();
        }
    }

    std::cout << (failures == 0 ? "=== TOUS LES TESTS PASSENT ===" : "=== " + juce::String (failures).toStdString() + " ECHEC(S) ===") << std::endl;
    return failures == 0 ? 0 : 1;
}
