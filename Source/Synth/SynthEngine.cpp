#include "SynthEngine.h"

namespace idgs
{
namespace
{
constexpr float fadeMs = 3.0f;      // fondu de vol de voix / panic
constexpr float voiceHeadroom = 0.5f;

inline void panGains (float pan, float& l, float& r)
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    l = std::cos (a);
    r = std::sin (a);
}

inline bool usesSource (const OscParams& o, int src)
{
    for (int w = 0; w < 2; ++w)
        if (o.warpSrc[w] == src && o.warpAmt[w] > 0.0f
            && (o.warp[w] == WarpFM || o.warp[w] == WarpAM || o.warp[w] == WarpRM || o.warp[w] == WarpHardSync))
            return true;
    return false;
}
} // namespace

// =============================================================================
// Voice
// =============================================================================
void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    env.prepare (sr);
    for (auto& o : osc) o.prepare (sr);
    noise.prepare (sr);
    for (auto& f : filt) f.prepare (sr);
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

void Voice::start (int n, float vel, uint64_t a, const VoiceParams& p, idg::dsp::Rng& rng, float fromPitch, bool glide)
{
    note = n;
    velocity = vel;
    age = a;
    keyDown = true;
    sustained = false;
    fading = false;
    fade = 1.0f;

    curPitch = glide ? fromPitch : (float) n;
    gFrom = curPitch;
    gTo = (float) n;
    gT = glide ? 0.0f : 1.0f;

    for (int o = 0; o < NumOscs; ++o) osc[o].start (p.osc[o], n, rng);
    noise.reset (rng.next());
    for (auto& f : filt) f.reset();
    subPh = 0.0f;
    subLast = noiseLast = 0.0f;

    // la voix part du silence : les niveaux démarrent directement sur leur cible
    lvl[0] = p.osc[0].on ? p.osc[0].level : 0.0f;
    lvl[1] = p.osc[1].on ? p.osc[1].level : 0.0f;
    lvl[2] = p.sub.on ? p.sub.level : 0.0f;
    lvl[3] = p.noise.on ? p.noise.level : 0.0f;
    panGains (p.sub.pan, panL[0], panR[0]);
    panGains (p.noise.pan, panL[1], panR[1]);

    env.reset();
    env.noteOn();
    ctrl = 0;
}

void Voice::legatoTo (int n, const VoiceParams& p)
{
    note = n;
    keyDown = true;
    sustained = false;
    gFrom = curPitch;
    gTo = (float) n;
    gT = p.glideMs > 0.5f ? 0.0f : 1.0f;
    if (gT >= 1.0f) curPitch = (float) n;
}

void Voice::beginFadeOut()
{
    if (isFree()) return;
    fading = true;
    pendingNote = -1;
}

void Voice::steal (int n, float vel, uint64_t a, float fromPitch, bool glide)
{
    fading = true;
    pendingNote = n;
    pendingVel = vel;
    pendingAge = a;
    pendingFrom = fromPitch;
    pendingGlide = glide;
    pendingKeyUp = false;
    note = n;
    keyDown = true;
    sustained = false;
}

void Voice::control (const VoiceParams& p)
{
    // glide
    if (gT < 1.0f)
    {
        const float len = juce::jmax (1.0f, p.glideMs * 0.001f * (float) sr);
        gT = juce::jmin (1.0f, gT + (float) ControlRate / len);
        curPitch = gFrom + (gTo - gFrom) * Envelope::shape (gT, p.glideCurve);
    }
    else curPitch = gTo;
    pitchNow = curPitch + p.bendSemis;

    for (int o = 0; o < NumOscs; ++o)
    {
        // un osc coupé continue de tourner s'il sert de source de modulation à l'autre
        oscActive[o] = p.osc[o].on || lvl[o] > 1.0e-4f || usesSource (p.osc[1 - o], o == 0 ? SrcOscA : SrcOscB);
        if (oscActive[o]) osc[o].control (p.osc[o], pitchNow);
    }

    subActive = p.sub.on || lvl[2] > 1.0e-5f || usesSource (p.osc[0], SrcSub) || usesSource (p.osc[1], SrcSub);
    noiseActive = p.noise.on || lvl[3] > 1.0e-5f || usesSource (p.osc[0], SrcNoise) || usesSource (p.osc[1], SrcNoise);
    const float noteHz = 440.0f * std::pow (2.0f, (pitchNow - 69.0f) / 12.0f);
    noise.control (p.noise.type, p.noise.tone, noteHz, ControlRate);

    const float subHz = noteHz * (p.sub.oct == 0 ? 0.5f : 0.25f);
    subInc = juce::jlimit (0.0f, 0.49f, subHz / (float) sr);
    subMip = Wavetable::mipFor (subInc);
    subPos = p.sub.shape == 0 ? 0.0f : (p.sub.shape == 1 ? 1.0f / 3.0f : 1.0f);

    for (int f = 0; f < 2; ++f)
    {
        const auto& fp = p.filt[f];
        if (! fp.on) continue;
        const float kt = std::pow (2.0f, (pitchNow - 60.0f) / 12.0f * fp.key);
        filt[f].setParams (fp.type, fp.cutoff * kt, fp.reso, fp.drive, fp.mix);
    }
}

void Voice::render (float* L, float* R, int n, const VoiceParams& p, idg::dsp::Rng& rng)
{
    const auto& basic = WavetableBank::get()[0];
    const float velGain = (1.0f - p.velSens + p.velSens * velocity * velocity) * voiceHeadroom;
    const float tLvl[4] { p.osc[0].on ? p.osc[0].level : 0.0f, p.osc[1].on ? p.osc[1].level : 0.0f,
                          p.sub.on ? p.sub.level : 0.0f, p.noise.on ? p.noise.level : 0.0f };
    float tpl[2], tpr[2];
    panGains (p.sub.pan, tpl[0], tpr[0]);
    panGains (p.noise.pan, tpl[1], tpr[1]);
    const float bothGain = p.serial ? 1.0f : 0.7071f;

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
                start (pn, pendingVel, pendingAge, p, rng, pendingFrom, pendingGlide);
                if (keyUp) { keyDown = false; release(); }
                e = env.next (p.env);
            }
        }
        if (! env.isActive() && ! fading) { note = -1; return; }
        if (ctrl <= 0) { control (p); ctrl = ControlRate; }
        --ctrl;

        for (int k = 0; k < 4; ++k) lvl[k] += (tLvl[k] - lvl[k]) * smoothCoef;
        for (int k = 0; k < 2; ++k) { panL[k] += (tpl[k] - panL[k]) * smoothCoef; panR[k] += (tpr[k] - panR[k]) * smoothCoef; }

        // ---- sources
        const float mods[NumModSources] { osc[0].last, osc[1].last, subLast, noiseLast };
        float src[4][2] {};
        if (oscActive[0]) osc[0].tick (p.osc[0], mods, osc[1].wrapped, src[0][0], src[0][1]);
        if (oscActive[1]) osc[1].tick (p.osc[1], mods, osc[0].wrapped, src[1][0], src[1][1]);
        if (subActive)
        {
            subPh += subInc;
            if (subPh >= 1.0f) subPh -= 1.0f;
            subLast = basic.readMorph (subPos, subMip, subPh);
            src[2][0] = subLast * panL[0];
            src[2][1] = subLast * panR[0];
        }
        if (noiseActive)
        {
            float nl, nr;
            noise.tick (nl, nr);
            noiseLast = 0.5f * (nl + nr);
            src[3][0] = nl * panL[1] * 1.4142f;
            src[3][1] = nr * panR[1] * 1.4142f;
        }

        // ---- routage vers les filtres
        float f1[2] {}, f2[2] {}, direct[2] {};
        const int routes[4] { p.osc[0].route, p.osc[1].route, p.sub.route, p.noise.route };
        for (int s = 0; s < 4; ++s)
        {
            const float gl = src[s][0] * lvl[s], gr = src[s][1] * lvl[s];
            switch (routes[s])
            {
                case RouteF1: f1[0] += gl; f1[1] += gr; break;
                case RouteF2: f2[0] += gl; f2[1] += gr; break;
                case RouteBoth:
                    if (p.serial) { f1[0] += gl; f1[1] += gr; }
                    else { f1[0] += gl * bothGain; f1[1] += gr * bothGain; f2[0] += gl * bothGain; f2[1] += gr * bothGain; }
                    break;
                default: direct[0] += gl; direct[1] += gr; break;
            }
        }

        float outL, outR;
        if (p.filt[0].on) filt[0].process (f1[0], f1[1]);
        if (p.serial)
        {
            f2[0] += f1[0]; f2[1] += f1[1];
            if (p.filt[1].on) filt[1].process (f2[0], f2[1]);
            outL = f2[0] + direct[0];
            outR = f2[1] + direct[1];
        }
        else
        {
            if (p.filt[1].on) filt[1].process (f2[0], f2[1]);
            outL = f1[0] + f2[0] + direct[0];
            outR = f1[1] + f2[1] + direct[1];
        }

        const float g = e * velGain * fade;
        L[i] += outL * g;
        R[i] += outR * g;
    }
    for (auto& f : filt) f.sanitize();
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
    numHeld = 0;
    monoIdx = -1;
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
    numHeld = 0;
    monoIdx = -1;
}

Voice* SynthEngine::findFree (int maxVoices)
{
    for (int i = 0; i < maxVoices; ++i)
        if (voices[(size_t) i].isFree()) return &voices[(size_t) i];
    return nullptr;
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

Voice* SynthEngine::monoVoice()
{
    if (monoIdx < 0) return nullptr;
    auto& v = voices[(size_t) monoIdx];
    if (v.isFree() || (v.isFading() && v.pendingNote < 0)) return nullptr;
    return &v;
}

void SynthEngine::startVoice (int n, float vel, const VoiceParams& p, float from, bool glide, int maxV)
{
    if (auto* v = findFree (maxV))
    {
        v->start (n, vel, ageCounter, p, rng, from, glide);
        monoIdx = (int) (v - voices.data());
        return;
    }
    if (auto* v = findVictim (maxV))
    {
        v->steal (n, vel, ageCounter, from, glide);
        monoIdx = (int) (v - voices.data());
    }
}

void SynthEngine::removeHeld (int n)
{
    for (int i = 0; i < numHeld; ++i)
        if (held[(size_t) i] == n)
        {
            for (int j = i; j < numHeld - 1; ++j) held[(size_t) j] = held[(size_t) j + 1];
            --numHeld;
            return;
        }
}

void SynthEngine::noteOn (int n, float vel, const VoiceParams& p)
{
    ++ageCounter;
    bool anyHeld = numHeld > 0;
    for (const auto& v : voices) if (! v.isFree() && v.keyDown) anyHeld = true;
    const bool glideOn = p.glideMs > 0.5f && lastPitch >= 0.0f && (! p.glideLegato || anyHeld);
    const float from = lastPitch >= 0.0f ? lastPitch : (float) n;

    removeHeld (n);
    if (numHeld < (int) held.size()) held[(size_t) numHeld++] = n;

    if (p.voiceMode == VoicePoly)
    {
        const int maxV = juce::jlimit (1, MaxVoices, p.maxVoices);
        // la même note rejouée : l'ancienne voix passe en release (empilement naturel, comme un vrai poly)
        for (auto& v : voices)
            if (! v.isFree() && v.note == n && (v.keyDown || v.sustained) && ! v.isFading())
            {
                v.keyDown = false;
                v.sustained = false;
                v.release();
            }
        startVoice (n, vel, p, from, glideOn, maxV);
    }
    else
    {
        auto* cur = monoVoice();
        if (p.voiceMode == VoiceLegato && cur != nullptr && (cur->keyDown || cur->sustained) && cur->pendingNote < 0)
        {
            cur->legatoTo (n, p); // chevauchement : pas de nouvelle attaque
        }
        else
        {
            // Mono : nouvelle attaque ; l'ancienne note s'éteint en fondu court, la nouvelle part de sa hauteur
            const float monoFrom = cur != nullptr ? cur->getPitch() : from;
            const bool monoGlide = p.glideMs > 0.5f && lastPitch >= 0.0f && (! p.glideLegato || cur != nullptr);
            if (cur != nullptr) cur->beginFadeOut();
            startVoice (n, vel, p, monoFrom, monoGlide, MaxVoices);
        }
    }
    lastPitch = (float) n;
}

void SynthEngine::noteOff (int n, const VoiceParams& p)
{
    removeHeld (n);

    if (p.voiceMode != VoicePoly)
    {
        auto* cur = monoVoice();
        if (cur == nullptr || cur->note != n) return;
        if (numHeld > 0)
        {
            // retour à la note encore tenue (priorité à la dernière jouée), sans nouvelle attaque
            const int prev = held[(size_t) numHeld - 1];
            cur->legatoTo (prev, p);
            lastPitch = (float) prev;
            return;
        }
        cur->keyDown = false;
        if (sustainPedal) cur->sustained = true;
        else if (cur->pendingNote >= 0) cur->pendingKeyUp = true;
        else cur->release();
        return;
    }

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
    checkMode (p);
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
    else if (m.isNoteOff()) noteOff (m.getNoteNumber(), p);
    else if (m.isPitchWheel()) bend = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isSustainPedalOn()) setSustain (true);
    else if (m.isSustainPedalOff()) setSustain (false);
    else if (m.isAllSoundOff()) allSoundOff();
    else if (m.isAllNotesOff())
    {
        sustainPedal = false;
        numHeld = 0;
        for (auto& v : voices)
            if (! v.isFree()) { v.keyDown = v.sustained = false; if (v.pendingNote >= 0) v.pendingKeyUp = true; else v.release(); }
    }
}

void SynthEngine::checkMode (const VoiceParams& p)
{
    // changement de mode de jeu : les notes en cours sont relâchées proprement
    if (p.voiceMode == lastMode) return;
    lastMode = p.voiceMode;
    numHeld = 0;
    monoIdx = -1;
    for (auto& v : voices)
        if (! v.isFree()) { v.keyDown = v.sustained = false; if (v.pendingNote >= 0) v.pendingKeyUp = true; else v.release(); }
}

void SynthEngine::render (float* L, float* R, int n, const VoiceParams& p)
{
    checkMode (p);
    if (p.voiceMode == VoicePoly)
    {
        const int maxV = juce::jlimit (1, MaxVoices, p.maxVoices);
        // si on réduit le nombre de voix, celles en trop s'éteignent en fondu
        for (int i = maxV; i < MaxVoices; ++i)
            if (! voices[(size_t) i].isFree() && ! voices[(size_t) i].isFading()) voices[(size_t) i].beginFadeOut();
    }

    for (auto& v : voices)
        if (! v.isFree()) v.render (L, R, n, p, rng);
}
} // namespace idgs
