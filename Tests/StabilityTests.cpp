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
    IDGemsSynthProcessor proc;
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
    IDGemsSynthProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (IDGemsSynthEditor::baseW, IDGemsSynthEditor::baseH);
    juce::ignoreUnused (tab);
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

    // 1. Wavetables : pas de NaN, crête normalisée, pas d'harmonique au-dessus de Nyquist à la note la plus aiguë
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
    {
        std::cout << "8. Parametres au hasard (fuzz)..." << std::endl;
        Harness h (44100.0, 256);
        juce::Random rnd (7);
        auto& params = h.proc.getParameters();
        for (int round = 0; round < 60; ++round)
        {
            for (auto* p : params) if (rnd.nextFloat() < 0.4f) p->setValueNotifyingHost (rnd.nextFloat());
            h.set (P::masterVol, 6.0f);
            for (int k = 0; k < 4; ++k) h.noteOn (rnd.nextInt ({ 0, 128 }), rnd.nextFloat(), rnd.nextInt (256));
            if (rnd.nextBool()) h.noteOff (rnd.nextInt ({ 0, 128 }), rnd.nextInt (256));
            h.run (0.03);
        }
        const float pk = h.peak();
        check (pk >= 0.0f, "NaN avec parametres au hasard");
        check (pk <= ceilingGain (h), "plafond depasse avec parametres au hasard : " + juce::String (pk));
    }

    // 9. Interface : création / redimensionnement / destruction
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

    std::cout << (failures == 0 ? "=== TOUS LES TESTS PASSENT ===" : "=== " + juce::String (failures).toStdString() + " ECHEC(S) ===") << std::endl;
    return failures == 0 ? 0 : 1;
}
