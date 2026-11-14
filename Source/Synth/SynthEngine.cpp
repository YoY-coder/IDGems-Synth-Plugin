#include "SynthEngine.h"

namespace idgs
{
namespace
{
constexpr float fadeMs = 3.0f;      // fondu de vol de voix / panic
constexpr float voiceHeadroom = 0.5f;

inline void panGains (float pan, float& l, float& r)
{
    // loi à puissance constante (-3 dB au centre)
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    l = std::cos (a);
    r = std::sin (a);
}
} // namespace

// =============================================================================
// Voice
// =============================================================================
void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    env.prepare (sr);
    fadeStep = 1.0f / (fadeMs * 0.001f * (float) sr);
    smoothCoef = 1.0f - std::exp (-1.0f / (0.004f * (float) sr)); // ~4 ms
    kill();
}

void Voice::kill()
{
    env.reset();
    fading = false;
    fade = 1.0f;
    note = pendingNote = -1;
    keyDown = sustained = pendingKeyUp = false;
}

void Voice::start (int n, float vel, uint64_t a, const VoiceParams& p, idg::dsp::Rng& rng)
{
    note = soundNote = n;
    velocity = vel;
    age = a;
    keyDown = true;
    sustained = false;
    fading = false;
    fade = 1.0f;
    phase = p.phase + p.phaseRand * rng.uni();
    phase -= std::floor (phase);
    // la voix part du silence : les lissages démarrent directement sur la cible
    sPos = p.pos;
    sLevel = targetLevel (p);
    panGains (p.pan, sPanL, sPanR);
    env.reset();
    env.noteOn();
}

float Voice::targetLevel (const VoiceParams& p) const
{
    const float velGain = 1.0f - p.velSens + p.velSens * velocity * velocity;
    return p.oscOn ? p.level * velGain * voiceHeadroom : 0.0f;
}

void Voice::beginFadeOut()
{
    if (isFree()) return;
    fading = true;
    pendingNote = -1;
}

void Voice::steal (int n, float vel, uint64_t a)
{
    fading = true;
    pendingNote = n;
    pendingVel = vel;
    pendingAge = a;
    pendingKeyUp = false;
    // pendant le fondu, la voix « appartient » déjà à la nouvelle note
    note = n;
    keyDown = true;
    sustained = false;
}

void Voice::render (float* L, float* R, int n, const VoiceParams& p, idg::dsp::Rng& rng)
{
    const auto& wt = WavetableBank::get()[p.table];
    float inc = 0.0f, tLevel = 0.0f;
    int mip = 0;
    auto updateNote = [&]
    {
        const float pitch = (float) soundNote + p.pitchOffset + p.bendSemis;
        const float freq = 440.0f * std::pow (2.0f, (pitch - 69.0f) / 12.0f);
        inc = juce::jlimit (0.0f, 0.49f, freq / (float) sr);
        mip = Wavetable::mipFor (inc);
        tLevel = targetLevel (p);
    };
    updateNote();
    float tl, tr;
    panGains (p.pan, tl, tr);

    for (int i = 0; i < n; ++i)
    {
        float e = env.next (p.env);
        if (fading)
        {
            fade -= fadeStep;
            if (fade <= 0.0f)
            {
                if (pendingNote < 0) { kill(); return; }
                // fin du vol : la note en attente démarre (depuis le silence)
                const bool keyUp = pendingKeyUp;
                const int pn = pendingNote;
                pendingNote = -1;
                start (pn, pendingVel, pendingAge, p, rng);
                if (keyUp) { keyDown = false; release(); }
                updateNote();
                e = env.next (p.env);
            }
        }
        if (! env.isActive() && ! fading) { note = -1; return; }

        sPos += (p.pos - sPos) * smoothCoef;
        sLevel += (tLevel - sLevel) * smoothCoef;
        sPanL += (tl - sPanL) * smoothCoef;
        sPanR += (tr - sPanR) * smoothCoef;

        const float s = wt.readMorph (sPos, mip, phase) * e * sLevel * fade;
        L[i] += s * sPanL;
        R[i] += s * sPanR;

        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
    }
}

// =============================================================================
// SynthEngine
// =============================================================================
void SynthEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& v : voices) v.prepare (sr);
    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices) v.kill();
    bend = 0.0f;
    sustainPedal = false;
}

int SynthEngine::getActiveVoices() const
{
    int c = 0;
    for (const auto& v : voices) if (! v.isFree()) ++c;
    return c;
}

void SynthEngine::allSoundOff()
{
    for (auto& v : voices) v.beginFadeOut();
    sustainPedal = false;
}

Voice* SynthEngine::findVictim (int maxVoices)
{
    // priorité : voix déjà en fondu de sortie > voix en release / touche relâchée > note tenue ;
    // à priorité égale, la plus ancienne. Une voix qui attend déjà une note n'est volée qu'en dernier recours.
    Voice* best = nullptr;
    int bestScore = 1 << 30;
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        int score;
        if (v.isFading() && v.pendingNote < 0) score = 0;
        else if (v.isReleasing() || (! v.keyDown && ! v.sustained)) score = 1;
        else if (v.pendingNote < 0) score = 2;
        else score = 3;
        if (best == nullptr || score < bestScore || (score == bestScore && v.age < best->age))
        {
            best = &v;
            bestScore = score;
        }
    }
    return best;
}

void SynthEngine::noteOn (int n, float vel, const VoiceParams& p)
{
    const int maxV = juce::jlimit (1, MaxVoices, p.maxVoices);
    ++ageCounter;

    // la même note rejouée : l'ancienne voix passe en release (empilement naturel, comme un vrai poly)
    for (auto& v : voices)
        if (! v.isFree() && v.note == n && (v.keyDown || v.sustained) && ! v.isFading())
        {
            v.keyDown = false;
            v.sustained = false;
            v.release();
        }

    for (int i = 0; i < maxV; ++i)
        if (voices[(size_t) i].isFree())
        {
            voices[(size_t) i].start (n, vel, ageCounter, p, rng);
            return;
        }

    if (auto* v = findVictim (maxV))
        v->steal (n, vel, ageCounter);
}

void SynthEngine::noteOff (int n)
{
    for (auto& v : voices)
    {
        if (v.isFree() || v.note != n || ! v.keyDown) continue;
        v.keyDown = false;
        if (v.isFading() && v.pendingNote >= 0)
        {
            if (sustainPedal) v.sustained = true;
            else v.pendingKeyUp = true;
            continue;
        }
        if (sustainPedal) v.sustained = true;
        else v.release();
    }
}

void SynthEngine::setSustain (bool down)
{
    sustainPedal = down;
    if (down) return;
    for (auto& v : voices)
        if (! v.isFree() && v.sustained && ! v.keyDown)
        {
            v.sustained = false;
            if (v.isFading() && v.pendingNote >= 0) v.pendingKeyUp = true;
            else v.release();
        }
}

void SynthEngine::handleMidi (const juce::MidiMessage& m, const VoiceParams& p)
{
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
    else if (m.isNoteOff()) noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) bend = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isSustainPedalOn()) setSustain (true);
    else if (m.isSustainPedalOff()) setSustain (false);
    else if (m.isAllSoundOff()) allSoundOff();
    else if (m.isAllNotesOff())
    {
        sustainPedal = false;
        for (auto& v : voices)
            if (! v.isFree()) { v.keyDown = v.sustained = false; if (v.pendingNote >= 0) v.pendingKeyUp = true; else v.release(); }
    }
}

void SynthEngine::render (float* L, float* R, int n, VoiceParams& p)
{
    const int maxV = juce::jlimit (1, MaxVoices, p.maxVoices);
    // si on réduit le nombre de voix, celles en trop s'éteignent en fondu
    for (int i = maxV; i < MaxVoices; ++i)
        if (! voices[(size_t) i].isFree() && ! voices[(size_t) i].isFading()) voices[(size_t) i].beginFadeOut();

    for (auto& v : voices)
        if (! v.isFree()) v.render (L, R, n, p, rng);
}
} // namespace idgs
