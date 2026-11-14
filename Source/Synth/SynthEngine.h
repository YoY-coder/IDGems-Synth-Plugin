#pragma once
#include "Envelope.h"
#include "Wavetable.h"
#include "../Core/Dsp.h"
#include "../Core/Params.h"

namespace idgs
{
/** Réglages lus une fois par bloc (copie des paramètres : la voix ne touche jamais l'APVTS). */
struct VoiceParams
{
    bool oscOn = true;
    int table = 0;
    float pos = 0.0f, level = 0.75f, pan = 0.0f, phase = 0.0f, phaseRand = 0.0f;
    float pitchOffset = 0.0f;      // octave * 12 + demi-tons + cents / 100
    float bendSemis = 0.0f;        // pitch bend courant (déjà multiplié par l'amplitude)
    float velSens = 0.7f;
    int maxVoices = 16;
    Envelope::Settings env;
};

/** Une voix : Osc A (wavetable) -> enveloppe 1 (volume). */
class Voice
{
public:
    void prepare (double sampleRate);
    void start (int note, float velocity, uint64_t age, const VoiceParams& p, idg::dsp::Rng& rng);
    void release() { env.noteOff(); }

    /** Fondu de sortie court (vol de voix / réduction du nombre de voix / panic). Sans note en attente,
        la voix se libère à la fin du fondu. */
    void beginFadeOut();
    /** Vol : fondu court puis démarrage de la note en attente. */
    void steal (int note, float velocity, uint64_t age);

    void kill();

    bool isFree() const { return ! env.isActive() && ! fading; }
    bool isFading() const { return fading; }
    bool isReleasing() const { return env.isReleasing(); }
    float getEnvLevel() const { return env.getLevel(); }

    void render (float* L, float* R, int n, const VoiceParams& p, idg::dsp::Rng& rng);

    // état de jeu (lu par le moteur)
    int note = -1;
    bool keyDown = false, sustained = false;
    uint64_t age = 0;

    // note en attente pendant un vol
    int pendingNote = -1;
    float pendingVel = 0.0f;
    uint64_t pendingAge = 0;
    bool pendingKeyUp = false;

private:
    float targetLevel (const VoiceParams& p) const;

    double sr = 44100.0;
    Envelope env;
    int soundNote = 60;     // note réellement entendue (diffère de `note` pendant le fondu d'un vol)
    float velocity = 1.0f;
    float phase = 0.0f;
    bool fading = false;
    float fade = 1.0f, fadeStep = 0.0f;
    // lissages (anti-zipper)
    float sPos = 0.0f, sLevel = 0.0f, sPanL = 0.0f, sPanR = 0.0f, smoothCoef = 0.01f;
};

/** Gestion de la polyphonie et du MIDI. */
class SynthEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    void handleMidi (const juce::MidiMessage& m, const VoiceParams& p);
    void render (float* L, float* R, int n, VoiceParams& p);

    /** Toutes les voix s'éteignent en quelques millisecondes (Panic). */
    void allSoundOff();

    float getBend() const { return bend; }
    int getActiveVoices() const;
    bool sustainPedalDown() const { return sustainPedal; }

private:
    void noteOn (int note, float velocity, const VoiceParams& p);
    void noteOff (int note);
    void setSustain (bool down);
    Voice* findVictim (int maxVoices);

    std::array<Voice, MaxVoices> voices;
    idg::dsp::Rng rng;
    uint64_t ageCounter = 0;
    float bend = 0.0f; // -1..1
    bool sustainPedal = false;
    double sr = 44100.0;
};
} // namespace idgs
