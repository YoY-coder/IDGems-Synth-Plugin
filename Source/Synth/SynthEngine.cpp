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
    for (auto& e : modEnv) e.prepare (sr);
    for (auto& o : osc) o.prepare (sr);
    noise.prepare (sr);
    texv.prepare (sr);
    for (auto& f : fpf) f.prepare (sr);
    for (auto& fb : filt) for (auto& f : fb) f.prepare (sr);
    fadeStep = 1.0f / (fadeMs * 0.001f * (float) sr);
    smoothCoef = 1.0f - std::exp (-1.0f / (0.004f * (float) sr)); // ~4 ms
    kill();
}

void Voice::kill()
{
    env.reset();
    for (auto& e : modEnv) e.reset();
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
    texv.start (p.tex, n, rng);
    for (auto& f : fpf) f.reset();
    for (auto& fb : filt) for (auto& f : fb) f.reset();
    subPh = 0.0f;
    subLast = noiseLast = 0.0f;

    // la voix part du silence : les niveaux démarrent directement sur leur cible
    lvl[0] = p.osc[0].on ? p.osc[0].level : 0.0f;
    lvl[1] = p.osc[1].on ? p.osc[1].level : 0.0f;
    lvl[2] = p.sub.on ? p.sub.level : 0.0f;
    lvl[3] = p.noise.on ? p.noise.level : 0.0f;
    lvl[4] = p.tex.on ? p.tex.level : 0.0f;
    panGains (p.sub.pan, panL[0], panR[0]);
    panGains (p.noise.pan, panL[1], panR[1]);
    panGains (p.tex.pan, panL[2], panR[2]);

    env.reset();
    env.noteOn();
    for (auto& e : modEnv) { e.reset(); e.noteOn(); }
    for (int i = 0; i < NumLfos; ++i) { lfoPh[i] = 0.0f; lfoSm[i] = -1.0f; }
    noteRand = rng.uni();
    ageSec = 0.0f;
    for (auto& v : mod01) v = -1.0f;
    local = p;
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

void Voice::computeModulation (const VoiceParams& p)
{
    local = p;
    for (auto& o : local.osc) o.tex = &local.tex;
    const ModBlock* mb = p.mod;
    ageSec += (float) ControlRate / (float) sr;
    if (mb == nullptr || mb->numSlots == 0) return;

    // ---- sources propres à la voix
    for (int e = 1; e < NumEnvelopes; ++e)
        if (mb->srcUsed[MSrcEnv0 + e])
        {
            float v = 0.0f;
            for (int k = 0; k < ControlRate; ++k) v = modEnv[e].next (mb->env[e]);
            srcVal[MSrcEnv0 + e] = v;
        }
    srcVal[MSrcEnv0] = env.getLevel();
    for (int i = 0; i < NumLfos; ++i)
    {
        if (! mb->srcUsed[MSrcLfo0 + i]) continue;
        const auto& L = mb->lfo[i];
        float v;
        if (L.mode == LfoNoteVoice || L.mode == LfoOneShot)
        {
            float ph = lfoPh[i] + L.phase;
            ph = L.mode == LfoOneShot ? juce::jlimit (0.0f, 0.9999f, ph) : ph - std::floor (ph);
            v = L.table[juce::jlimit (0, idg::LfoShape::tableSize - 1, (int) (ph * idg::LfoShape::tableSize))];
            lfoPh[i] += L.inc * (float) ControlRate;
            if (L.mode != LfoOneShot) lfoPh[i] -= std::floor (lfoPh[i]);
            const float a = L.smoothMs <= 0.01f ? 0.0f : std::exp (-(float) ControlRate / (L.smoothMs * 0.001f * (float) sr));
            lfoSm[i] = lfoSm[i] < 0.0f ? v : v + (lfoSm[i] - v) * a;
            v = lfoSm[i];
        }
        else v = mb->global[MSrcLfo0 + i];
        if (L.fadeMs > 0.5f) v *= juce::jmin (1.0f, ageSec * 1000.0f / L.fadeMs);
        srcVal[MSrcLfo0 + i] = v;
    }
    srcVal[MSrcVelocity] = velocity;
    srcVal[MSrcNote] = (float) note / 127.0f;
    srcVal[MSrcNoteRandom] = noteRand;

    // ---- somme des modulations par cible (en fraction de la course du paramètre)
    float acc[NumModFields] {};
    for (int s = 0; s < mb->numSlots; ++s)
    {
        const auto& a = mb->slots[s];
        float v = sourceIsPerVoice (a.src) ? srcVal[a.src] : mb->global[a.src];
        if (a.bipolar) v = v * 2.0f - 1.0f;
        acc[a.field] += a.depth * v;
    }

    // ---- application aux réglages locaux de la voix
    const auto& T = *mb->targets;
    for (int f = 0; f < NumModFields; ++f)
    {
        if (! mb->fieldUsed[f]) { mod01[f] = -1.0f; continue; }
        const float n01 = juce::jlimit (0.0f, 1.0f, mb->base01[f] + acc[f]);
        mod01[f] = n01;
        const float real = T.toReal (f, n01);
        if (f < NumOscFields * NumOscs)
        {
            auto& o = local.osc[f / NumOscFields];
            switch (f % NumOscFields)
            {
                case FOscPos: o.pos = real; break;
                case FOscLevel: o.level = real; break;
                case FOscPan: o.pan = real; break;
                case FOscSemi: o.pitch += real - T.toReal (f, mb->base01[f]); break;
                case FOscFine: o.pitch += (real - T.toReal (f, mb->base01[f])) * 0.01f; break;
                case FOscDetune: o.detune = real; break;
                case FOscBlend: o.blend = real; break;
                case FOscSpread: o.spread = real; break;
                case FOscStretch: o.stretch = real; break;
                case FOscShift: o.shift = real; break;
                case FOscWarp1: o.warpAmt[0] = real; break;
                case FOscWarp2: o.warpAmt[1] = real; break;
                default: break;
            }
            continue;
        }
        if (f >= FFilt0 && f < FFilt0 + 2 * NumFiltFields)
        {
            auto& fp = local.filt[(f - FFilt0) / NumFiltFields];
            switch ((f - FFilt0) % NumFiltFields)
            {
                case FFCutoff: fp.cutoff = real; break;
                case FFReso: fp.reso = real; break;
                case FFDrive: fp.drive = real; break;
                case FFKey: fp.key = real; break;
                case FFMix: fp.mix = real; break;
                default: break;
            }
            continue;
        }
        switch (f)
        {
            case FSubLevel: local.sub.level = real; break;
            case FSubPan: local.sub.pan = real; break;
            case FNoiseLevel: local.noise.level = real; break;
            case FNoisePan: local.noise.pan = real; break;
            case FNoiseTone: local.noise.tone = real; break;
            case FEnvAttack: local.env.attackMs = real; break;
            case FEnvDecay: local.env.decayMs = real; break;
            case FEnvSustain: local.env.sustain = real; break;
            case FEnvRelease: local.env.releaseMs = real; break;
            case FGlide: local.glideMs = real; break;
            case FTexLevel: local.tex.level = real; break;
            case FTexPan: local.tex.pan = real; break;
            case FTexMorph: local.tex.morph = real; break;
            case FTexGPos: local.tex.gPos = real; break;
            case FTexGSize: local.tex.gSize = real; break;
            case FTexGDensity: local.tex.gDensity = real; break;
            case FTexGSpray: local.tex.gSpray = real; break;
            case FTexSShift: local.tex.sShift = real; break;
            case FTexSStretch: local.tex.sStretch = real; break;
            case FTexSSmear: local.tex.sSmear = real; break;
            case FTexCAmount: local.tex.cAmount = real; break;
            case FTexRDecay: local.tex.rDecay = real; break;
            case FTexRBright: local.tex.rBright = real; break;
            case FTexNColor: local.tex.nColor = real; break;
            case FTexDOffset: local.tex.dOffset = real; break;
            case FFpAmount: local.fp.amount = real; break;
            case FFpShift: local.fp.shift = real; break;
            default: break;
        }
    }
}

void Voice::control (const VoiceParams& base)
{
    computeModulation (base);
    const VoiceParams& p = local;
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
    texActive = p.tex.on || lvl[4] > 1.0e-5f;
    if (texActive) texv.control (p.tex, pitchNow);
    for (int b = 0; b < NumBuses; ++b) if (p.busActive[b]) fpf[b].control (p.fp, ControlRate);
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
        for (int b = 0; b < NumBuses; ++b)
            if (p.busActive[b]) filt[b][f].setParams (fp.type, fp.cutoff * kt, fp.reso, fp.drive, fp.mix);
    }
}

void Voice::render (float* const* busL, float* const* busR, int n, const VoiceParams& base, idg::dsp::Rng& rng)
{
    const auto& basic = WavetableBank::get()[0];
    const VoiceParams& p = local; // réglages modulés (mis à jour par control())
    const float velGain = (1.0f - base.velSens + base.velSens * velocity * velocity) * voiceHeadroom;
    const float bothGain = base.serial ? 1.0f : 0.7071f;
    float tLvl[NumSendSources], tpl[3], tpr[3];
    auto targets = [&]
    {
        tLvl[0] = p.osc[0].on ? p.osc[0].level : 0.0f;
        tLvl[1] = p.osc[1].on ? p.osc[1].level : 0.0f;
        tLvl[2] = p.sub.on ? p.sub.level : 0.0f;
        tLvl[3] = p.noise.on ? p.noise.level : 0.0f;
        tLvl[4] = p.tex.on ? p.tex.level : 0.0f;
        panGains (p.sub.pan, tpl[0], tpr[0]);
        panGains (p.noise.pan, tpl[1], tpr[1]);
        panGains (p.tex.pan, tpl[2], tpr[2]);
    };
    targets();

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
                start (pn, pendingVel, pendingAge, base, rng, pendingFrom, pendingGlide);
                if (keyUp) { keyDown = false; release(); }
                e = env.next (p.env);
            }
        }
        if (! env.isActive() && ! fading) { note = -1; return; }
        if (ctrl <= 0) { control (base); targets(); ctrl = ControlRate; }
        --ctrl;

        for (int k = 0; k < NumSendSources; ++k) lvl[k] += (tLvl[k] - lvl[k]) * smoothCoef;
        for (int k = 0; k < 3; ++k) { panL[k] += (tpl[k] - panL[k]) * smoothCoef; panR[k] += (tpr[k] - panR[k]) * smoothCoef; }

        // ---- sources
        const float mods[NumModSources] { osc[0].last, osc[1].last, subLast, noiseLast };
        float src[NumSendSources][2] {};
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
        if (texActive)
        {
            float tl, tr;
            texv.tick (p.tex, osc[0].last, osc[0].last + osc[1].last, rng, tl, tr);
            src[4][0] = tl * panL[2] * 1.4142f;
            src[4][1] = tr * panR[2] * 1.4142f;
        }

        const float g = e * velGain * fade;
        const int routes[NumSendSources] { p.osc[0].route, p.osc[1].route, p.sub.route, p.noise.route, p.tex.route };
        for (int bus = 0; bus < NumBuses; ++bus)
        {
            if (! base.busActive[bus]) continue;
            // ---- routage vers les filtres (pondéré par l'envoi de chaque source vers ce bus)
            float f1[2] {}, f2[2] {}, direct[2] {};
            for (int s = 0; s < NumSendSources; ++s)
            {
                const float w = lvl[s] * base.send[s][bus];
                if (w <= 0.0f) continue;
                const float gl = src[s][0] * w, gr = src[s][1] * w;
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
            if (p.filt[0].on) filt[bus][0].process (f1[0], f1[1]);
            if (p.serial)
            {
                f2[0] += f1[0]; f2[1] += f1[1];
                if (p.filt[1].on) filt[bus][1].process (f2[0], f2[1]);
                outL = f2[0] + direct[0];
                outR = f2[1] + direct[1];
            }
            else
            {
                if (p.filt[1].on) filt[bus][1].process (f2[0], f2[1]);
                outL = f1[0] + f2[0] + direct[0];
                outR = f1[1] + f2[1] + direct[1];
            }
            fpf[bus].process (outL, outR); // empreinte spectrale (si active)
            busL[bus][i] += outL * g;
            busR[bus][i] += outR * g;
        }
    }
    for (auto& fb : filt) for (auto& f : fb) f.sanitize();
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
    modWheel = aftertouch = 0.0f;
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

const Voice* SynthEngine::newestVoice() const
{
    const Voice* best = nullptr;
    for (const auto& v : voices)
        if (! v.isFree() && (best == nullptr || v.age > best->age)) best = &v;
    return best;
}

void SynthEngine::noteOn (int n, float vel, const VoiceParams& p)
{
    ++ageCounter;
    ++noteOnCount;
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
    else if (m.isController() && m.getControllerNumber() == 1) modWheel = (float) m.getControllerValue() / 127.0f;
    else if (m.isChannelPressure()) aftertouch = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch()) aftertouch = (float) m.getAfterTouchValue() / 127.0f;
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

void SynthEngine::render (float* const* busL, float* const* busR, int n, const VoiceParams& p)
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
        if (! v.isFree()) v.render (busL, busR, n, p, rng);
}
} // namespace idgs
