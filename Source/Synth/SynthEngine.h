#pragma once
#include "Envelope.h"
#include "Oscillator.h"
#include "NoiseGen.h"
#include "VoiceFilter.h"

namespace idgs
{
struct SubParams { bool on = false; int shape = 0, oct = 0; float level = 0.6f, pan = 0.0f; int route = RouteDirect; };
struct NoiseParams { bool on = false; int type = 0; float level = 0.3f, pan = 0.0f, tone = 0.0f; int route = RouteF1; };
struct FilterParams { bool on = false; int type = FiltLP24; float cutoff = 2000.0f, reso = 0.2f, drive = 0.0f, key = 0.0f, mix = 1.0f; };

/** Réglages lus une fois par bloc (copie des paramètres : la voix ne touche jamais l'APVTS). */
struct VoiceParams
{
    OscParams osc[NumOscs];
    SubParams sub;
    NoiseParams noise;
    FilterParams filt[2];
    bool serial = true;
    int voiceMode = VoicePoly;
    float glideMs = 0.0f, glideCurve = -0.4f;
    bool glideLegato = false;
    float bendSemis = 0.0f;
    float velSens = 0.7f;
    int maxVoices = 16;
    Envelope::Settings env;
};

/** Une voix : Osc A + Osc B + Sub + Noise -> 2 filtres (série / parallèle) -> enveloppe 1 (volume). */
class Voice
{
public:
    static constexpr int ControlRate = 16; // échantillons entre deux mises à jour de contrôle

    void prepare (double sampleRate);
    void start (int note, float velocity, uint64_t age, const VoiceParams& p, idg::dsp::Rng& rng, float fromPitch, bool glide);
    /** Legato : la note change sans redéclencher l'enveloppe (avec ou sans glide). */
    void legatoTo (int note, const VoiceParams& p);
    void release() { env.noteOff(); }

    void beginFadeOut();
    void steal (int note, float velocity, uint64_t age, float fromPitch, bool glide);
    void kill();

    bool isFree() const { return ! env.isActive() && ! fading; }
    bool isFading() const { return fading; }
    bool isReleasing() const { return env.isReleasing(); }
    float getPitch() const { return curPitch; }

    void render (float* L, float* R, int n, const VoiceParams& p, idg::dsp::Rng& rng);

    // état de jeu (lu par le moteur)
    int note = -1;
    bool keyDown = false, sustained = false;
    uint64_t age = 0;

    // note en attente pendant un vol
    int pendingNote = -1;
    float pendingVel = 0.0f, pendingFrom = 0.0f;
    bool pendingGlide = false;
    uint64_t pendingAge = 0;
    bool pendingKeyUp = false;

private:
    void control (const VoiceParams& p);

    double sr = 44100.0;
    Envelope env;
    OscUnit osc[NumOscs];
    NoiseGen noise;
    VoiceFilter filt[2];
    float velocity = 1.0f;
    bool fading = false;
    float fade = 1.0f, fadeStep = 0.0f;
    int ctrl = 0;

    // glide
    float curPitch = 60.0f, gFrom = 60.0f, gTo = 60.0f, gT = 1.0f;
    float pitchNow = 60.0f;

    // sub
    float subPh = 0.0f, subInc = 0.0f, subPos = 0.0f, subLast = 0.0f, noiseLast = 0.0f;
    int subMip = 0;

    // niveaux lissés (anti-zipper) : Osc A, Osc B, Sub, Noise
    float lvl[4] {}, panL[2] { 0.7071f, 0.7071f }, panR[2] { 0.7071f, 0.7071f };
    float smoothCoef = 0.01f;
    bool oscActive[NumOscs] { true, true };
    bool subActive = false, noiseActive = false;
};

/** Gestion de la polyphonie, des modes de jeu et du MIDI. */
class SynthEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    void handleMidi (const juce::MidiMessage& m, const VoiceParams& p);
    void render (float* L, float* R, int n, const VoiceParams& p);

    /** Toutes les voix s'éteignent en quelques millisecondes (Panic). */
    void allSoundOff();

    float getBend() const { return bend; }
    int getActiveVoices() const;

private:
    void checkMode (const VoiceParams& p);
    void noteOn (int note, float velocity, const VoiceParams& p);
    void noteOff (int note, const VoiceParams& p);
    void setSustain (bool down);
    Voice* findVictim (int maxVoices);
    Voice* findFree (int maxVoices);
    Voice* monoVoice();
    void startVoice (int note, float vel, const VoiceParams& p, float from, bool glide, int maxV);
    void removeHeld (int note);

    std::vector<Voice> voices = std::vector<Voice> ((size_t) MaxVoices); // sur le tas (~1 Mo) : jamais sur la pile
    idg::dsp::Rng rng;
    uint64_t ageCounter = 0;
    float bend = 0.0f; // -1..1
    bool sustainPedal = false;
    double sr = 44100.0;
    float lastPitch = -1.0f;
    int lastMode = VoicePoly;
    int monoIdx = -1;
    std::array<int, 32> held {};
    int numHeld = 0;
};
} // namespace idgs
