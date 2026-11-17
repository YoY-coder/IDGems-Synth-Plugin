#pragma once
#include "Envelope.h"
#include "Oscillator.h"
#include "NoiseGen.h"
#include "VoiceFilter.h"
#include "Texture.h"
#include "../Modulation/ModEngine.h"

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
    TexParams tex;
    FpParams fp;
    FilterParams filt[2];
    bool serial = true;
    int voiceMode = VoicePoly;
    float glideMs = 0.0f, glideCurve = -0.4f;
    bool glideLegato = false;
    float bendSemis = 0.0f;
    float velSens = 0.7f;
    int maxVoices = 16;
    float send[NumSendSources][NumBuses] { { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 } };
    bool busActive[NumBuses] { true, false, false };
    Envelope::Settings env;
    const ModBlock* mod = nullptr;   // modulations du bloc (nul = aucune)
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
    void release() { env.noteOff(); for (auto& e : modEnv) e.noteOff(); }

    void beginFadeOut();
    void steal (int note, float velocity, uint64_t age, float fromPitch, bool glide);
    void kill();

    bool isFree() const { return ! env.isActive() && ! fading; }
    bool isFading() const { return fading; }
    bool isReleasing() const { return env.isReleasing(); }
    float getPitch() const { return curPitch; }
    /** Valeurs modulées (0..1) des cibles pour l'affichage (-1 = non modulé). */
    const float* modulated01() const { return mod01; }
    /** Valeurs courantes des sources propres à la voix (enveloppes, LFO par voix, vélocité...). */
    const float* voiceSources() const { return srcVal; }

    /** Rendu additionné dans les buffers des bus (busL[b], busR[b]). */
    void render (float* const* busL, float* const* busR, int n, const VoiceParams& p, idg::dsp::Rng& rng);

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
    void computeModulation (const VoiceParams& p);

    double sr = 44100.0;
    Envelope env;
    OscUnit osc[NumOscs];
    NoiseGen noise;
    VoiceFilter filt[NumBuses][2];   // un jeu de filtres par bus utilisé (les sources partagées vers plusieurs bus restent justes)
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
    float lvl[NumSendSources] {}, panL[3] { 0.7071f, 0.7071f, 0.7071f }, panR[3] { 0.7071f, 0.7071f, 0.7071f };
    TextureVoice texv;
    FingerprintFilter fpf[NumBuses];
    bool texActive = false;
    float smoothCoef = 0.01f;
    bool oscActive[NumOscs] { true, true };
    bool subActive = false, noiseActive = false;

    // modulation
    VoiceParams local;                       // réglages modulés de cette voix (recalculés à cadence de contrôle)
    Envelope modEnv[NumEnvelopes];           // ENV 2 à 8 (l'index 0 n'est pas utilisé : ENV 1 = env)
    float lfoPh[NumLfos] {}, lfoSm[NumLfos] {};
    float noteRand = 0.0f, ageSec = 0.0f;
    float srcVal[NumModSrc] {};
    float mod01[NumModFields] {};
};

/** Gestion de la polyphonie, des modes de jeu et du MIDI. */
class SynthEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    void handleMidi (const juce::MidiMessage& m, const VoiceParams& p);
    void render (float* const* busL, float* const* busR, int n, const VoiceParams& p);

    /** Toutes les voix s'éteignent en quelques millisecondes (Panic). */
    void allSoundOff();

    float getBend() const { return bend; }
    float getModWheel() const { return modWheel; }
    float getAftertouch() const { return aftertouch; }
    int takeNoteOns() { const int n = noteOnCount; noteOnCount = 0; return n; }
    int getActiveVoices() const;
    /** Voix jouée le plus récemment (affichage des modulations), ou nullptr. */
    const Voice* newestVoice() const;

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
    float modWheel = 0.0f, aftertouch = 0.0f;
    int noteOnCount = 0;
    bool sustainPedal = false;
    double sr = 44100.0;
    float lastPitch = -1.0f;
    int lastMode = VoicePoly;
    int monoIdx = -1;
    std::array<int, 32> held {};
    int numHeld = 0;
};
} // namespace idgs
