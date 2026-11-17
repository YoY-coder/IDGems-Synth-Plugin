#pragma once
#include "../Core/Dsp.h"
#include "../Core/Params.h"
#include "../Fx/Scale.h"

namespace idgs
{
constexpr int MaxNoteSteps = 32;

/** Pas du séquenceur de notes (écrits par l'interface / le générateur, lus par l'audio). */
struct NoteSeqData
{
    std::atomic<bool> on[MaxNoteSteps], slide[MaxNoteSteps], lock[MaxNoteSteps];
    std::atomic<int> note[MaxNoteSteps], ratchet[MaxNoteSteps];
    std::atomic<float> vel[MaxNoteSteps], gate[MaxNoteSteps], prob[MaxNoteSteps];
    std::atomic<int> current { -1 }, version { 0 };

    NoteSeqData() { clear(); }
    void clear();
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);
};

/** Réglages du bloc (lus par le processeur). */
struct PlaySettings
{
    int mode = PlayDirect;
    int arpMode = ArpUp, arpOct = 1, arpRate = 4;
    float arpGate = 0.6f, arpSwing = 0.0f;
    bool arpHold = false;
    int seqRate = 4, seqLen = 16, seqFollow = 0;
    float seqSwing = 0.0f, seqMutate = 0.0f, rndNotes = 0.0f, rndOct = 0.0f, rndRatchet = 0.0f;
    int genStyle = GenEuclid, genRange = 2, genAuto = 0;
    float genDensity = 0.5f, genJump = 0.3f, genAccent = 0.4f;
    bool sprayOn = false;
    int sprayDensity = 4, sprayRange = 7, sprayMax = 8;
    float sprayTime = 300.0f, sprayDur = 150.0f, sprayVel = 0.5f;
    idg::Scale scale;
};

/** Générateur de patterns MIDI : remplit le séquenceur (les pas verrouillés sont gardés). Sans allocation. */
void generatePattern (NoteSeqData& d, int len, int style, float density, int rangeOct, float jump, float accent,
                      const idg::Scale& scale, uint32_t seed);

/** Couche de jeu entre le MIDI reçu et le moteur : direct / arpège / séquenceur / générateur, Note Spray,
    quantification sur gamme. Événements planifiés à l'échantillon près, aucune allocation dans process(). */
class NoteGen
{
public:
    void prepare (double sampleRate);
    void reset();

    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, double ppqStart, double bpm, bool hostPlaying,
                  const PlaySettings& s, NoteSeqData& seq, idg::dsp::Rng& rng);

    // dernier accord joué (pour l'export MIDI de l'arpège)
    std::atomic<int> lastChord[8] {};
    std::atomic<int> lastChordSize { 0 };
    std::atomic<int> genCounter { 0 };

private:
    struct Ev { int delay; int note; float vel; bool on; bool spray; };
    void schedule (int delay, int note, float vel, bool on, bool spray = false);
    void noteOnWithSpray (juce::MidiBuffer& out, int pos, int note, float vel, int lenSamples, const PlaySettings& s, idg::dsp::Rng& rng);
    void triggerStep (juce::MidiBuffer& out, int pos, int64_t step, double stepSamples, const PlaySettings& s, NoteSeqData& seq, idg::dsp::Rng& rng);
    void endOfCycle (const PlaySettings& s, NoteSeqData& seq, idg::dsp::Rng& rng);
    int quantize (int note, const PlaySettings& s) const { return juce::jlimit (0, 127, s.scale.enabled ? (int) s.scale.snapMidi ((float) note) : note); }
    void allOff (juce::MidiBuffer& out, int pos);

    double sr = 44100.0;
    // notes tenues (ordre joué) et vélocités
    int held[32] {}, numHeld = 0;
    float heldVel[128] {};
    int latched[32] {}, numLatched = 0;
    bool latchArmed = false;
    // planification
    Ev pend[512];
    int numPend = 0;
    int sounding[128] {};       // compte des notes générées en cours (pour tout couper proprement)
    int sprayActive = 0;
    // horloge
    double clockPpq = 0.0;
    int64_t lastStep = INT64_MIN;
    int arpIndex = 0, cycles = 0, lastMode = -1;
    int lastSeqLen = 16;
};
} // namespace idgs
