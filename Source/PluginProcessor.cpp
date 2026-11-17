#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace idgs;

IDGemsSynthProcessor::IDGemsSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsSynth", createLayout())
{
    // tables construites ici, jamais dans le thread audio
    (void) WavetableBank::get();
    (void) SineTable::get();

    auto r = [this] (const juce::String& id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    for (int o = 0; o < NumOscs; ++o)
    {
        auto& x = oscRaw[o];
        x.on = r (oscId (o, "on")); x.mode = r (oscId (o, "mode")); x.table = r (oscId (o, "table")); x.pos = r (oscId (o, "pos"));
        x.oct = r (oscId (o, "oct")); x.semi = r (oscId (o, "semi")); x.fine = r (oscId (o, "fine")); x.level = r (oscId (o, "level"));
        x.pan = r (oscId (o, "pan")); x.phase = r (oscId (o, "phase")); x.phrand = r (oscId (o, "phrand"));
        x.unison = r (oscId (o, "unison")); x.detune = r (oscId (o, "detune")); x.blend = r (oscId (o, "blend"));
        x.spread = r (oscId (o, "spread")); x.stretch = r (oscId (o, "stretch")); x.shift = r (oscId (o, "shift"));
        x.warp[0] = r (oscId (o, "warp1")); x.warpAmt[0] = r (oscId (o, "warp1amt")); x.warpSrc[0] = r (oscId (o, "warp1src"));
        x.warp[1] = r (oscId (o, "warp2")); x.warpAmt[1] = r (oscId (o, "warp2amt")); x.warpSrc[1] = r (oscId (o, "warp2src"));
        x.loop = r (oscId (o, "loop")); x.root = r (oscId (o, "root")); x.route = r (oscId (o, "route"));
    }
    for (int f = 0; f < 2; ++f)
    {
        auto& x = filtRaw[f];
        x.on = r (filtId (f, "on")); x.type = r (filtId (f, "type")); x.cutoff = r (filtId (f, "cutoff")); x.reso = r (filtId (f, "reso"));
        x.drive = r (filtId (f, "drive")); x.key = r (filtId (f, "key")); x.mix = r (filtId (f, "mix"));
    }
    raw.subOn = r (P::subOn); raw.subShape = r (P::subShape); raw.subOct = r (P::subOct); raw.subLevel = r (P::subLevel);
    raw.subPan = r (P::subPan); raw.subRoute = r (P::subRoute);
    raw.noiseOn = r (P::noiseOn); raw.noiseType = r (P::noiseType); raw.noiseLevel = r (P::noiseLevel); raw.noisePan = r (P::noisePan);
    raw.noiseTone = r (P::noiseTone); raw.noiseRoute = r (P::noiseRoute); raw.serial = r (P::filtSerial);
    raw.delay = r (P::env1Delay); raw.attack = r (P::env1Attack); raw.hold = r (P::env1Hold); raw.decay = r (P::env1Decay);
    raw.sustain = r (P::env1Sustain); raw.release = r (P::env1Release); raw.aCurve = r (P::env1ACurve); raw.dCurve = r (P::env1DCurve);
    raw.rCurve = r (P::env1RCurve); raw.voices = r (P::voices); raw.voiceMode = r (P::voiceMode); raw.glide = r (P::glide);
    raw.glideCurve = r (P::glideCurve); raw.glideLegato = r (P::glideLegato); raw.velSens = r (P::velSens);
    raw.bendRange = r (P::bendRange); raw.masterVol = r (P::masterVol); raw.ceiling = r (P::ceiling);
    for (int src = 0; src < NumSendSources; ++src)
        for (int b = 0; b < NumBuses; ++b) raw.send[src][b] = r (sendId (src, b));
    for (int b = 0; b < NumBuses; ++b)
    {
        raw.busVol[b] = r (busId (b, "vol")); raw.busPan[b] = r (busId (b, "pan"));
        raw.busMute[b] = r (busId (b, "mute")); raw.busSolo[b] = r (busId (b, "solo"));
        for (int t = 0; t < NumBuses; ++t) raw.busSend[b][t] = t == b ? nullptr : r (busSendId (b, t));
    }
    raw.masterMix = r (P::masterMix); raw.inGain = r (P::inGain); raw.outGain = r (P::outGain); raw.hpFreq = r (P::hpFreq);
    raw.monoOn = r (P::monoOn); raw.monoFreq = r (P::monoFreq); raw.ottDepth = r (P::ottDepth); raw.ottTime = r (P::ottTime);
    raw.ottUp = r (P::ottUp); raw.ottDown = r (P::ottDown); raw.ottGain = r (P::ottGain); raw.clipDrive = r (P::clipDrive);
    raw.limRelease = r (P::limRelease); raw.zeroLat = r (P::zeroLat); raw.destOS = r (P::destOS);
    raw.chaosMacro = r (macroId (NumMacros - 1));

    // racks d'effets (Bus A, B, C, Master)
    for (int k = 0; k < idg::NumRacks; ++k)
    {
        racks[(size_t) k] = std::make_unique<BusRack>();
        auto& br = *racks[(size_t) k];
        br.bank.init (apvts, idg::rackRegistry (k));
        br.rack.init (br.bank, br.shared);
    }

    juce::PropertiesFile::Options opts;
    opts.applicationName = "IDGemsSynth";
    opts.filenameSuffix = "settings";
    opts.folderName = "IDGems Synth";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (opts);

    modTargets.init (*this);
    // les paramètres des effets sont des cibles de modulation globales
    for (int k = 0; k < idg::NumRacks; ++k)
    {
        const auto& reg = idg::rackRegistry (k);
        for (int i = 0; i < (int) reg.all.size(); ++i)
            if (reg.all[(size_t) i].type == idg::PType::Float)
                modTargets.setGlobalTarget (modTargets.paramIndexForId (reg.all[(size_t) i].id), k, i);
    }
    modGlobal.init (apvts, modShared, modTargets);
    presets = std::make_unique<PresetManager> (*this);
    startTimer (500);
}

IDGemsSynthProcessor::~IDGemsSynthProcessor() { stopTimer(); }

bool IDGemsSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void IDGemsSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sr);
    modGlobal.prepare (sr);
    output.prepare (sr);
    work.setSize (2, juce::jmax (ChunkSize, samplesPerBlock), false, true, false);
    busBuf.setSize (2 * NumBuses, ChunkSize, false, true, false);
    busOut.setSize (2 * NumBuses, ChunkSize, false, true, false);
    for (auto& br : racks)
    {
        br->rack.prepare (sr, ChunkSize);
        br->rack.reset();
        for (auto& rt : br->seqRt) rt.reset();
    }
    masterDry.prepare (idg::Rack::rackLatency (false) + 8);
    dryL.assign ((size_t) ChunkSize, 0.0f);
    dryR.assign ((size_t) ChunkSize, 0.0f);
    for (auto& f : follow) f = 0.0f;
    lastLatency = -1;
    masterGain.reset (sr, 0.02);
    masterGain.setCurrentAndTargetValue (idg::dsp::dbToGain (raw.masterVol->load()));
    panicMute = 0;
    panicGain = 1.0f;
}

void IDGemsSynthProcessor::setValue (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

// =============================================================================
// Paramètres -> réglages des voix
// =============================================================================
void IDGemsSynthProcessor::readParams (VoiceParams& p) const
{
    const int factoryCount = WavetableBank::get().size();
    for (int o = 0; o < NumOscs; ++o)
    {
        const auto& x = oscRaw[o];
        auto& op = p.osc[o];
        op.on = x.on->load() > 0.5f;
        op.mode = (int) x.mode->load();
        const int t = (int) x.table->load();
        const Wavetable* custom = assets[o].table.load();
        op.table = t >= factoryCount ? (custom != nullptr ? custom : &WavetableBank::get()[0]) : &WavetableBank::get()[t];
        op.samples = assets[o].samples.load();
        op.pos = x.pos->load();
        op.level = x.level->load();
        op.pan = x.pan->load();
        op.phase = x.phase->load();
        op.phaseRand = x.phrand->load();
        op.pitch = std::round (x.oct->load()) * 12.0f + std::round (x.semi->load()) + x.fine->load() * 0.01f;
        op.unison = (int) x.unison->load();
        op.detune = x.detune->load();
        op.blend = x.blend->load();
        op.spread = x.spread->load();
        op.stretch = x.stretch->load();
        op.shift = x.shift->load();
        for (int w = 0; w < 2; ++w)
        {
            op.warp[w] = (int) x.warp[w]->load();
            op.warpAmt[w] = x.warpAmt[w]->load();
            op.warpSrc[w] = (int) x.warpSrc[w]->load();
        }
        op.loop = x.loop->load() > 0.5f;
        op.root = (int) x.root->load();
        op.route = (int) x.route->load();
    }
    p.sub.on = raw.subOn->load() > 0.5f;
    p.sub.shape = (int) raw.subShape->load();
    p.sub.oct = (int) raw.subOct->load();
    p.sub.level = raw.subLevel->load();
    p.sub.pan = raw.subPan->load();
    p.sub.route = (int) raw.subRoute->load();
    p.noise.on = raw.noiseOn->load() > 0.5f;
    p.noise.type = (int) raw.noiseType->load();
    p.noise.level = raw.noiseLevel->load();
    p.noise.pan = raw.noisePan->load();
    p.noise.tone = raw.noiseTone->load();
    p.noise.route = (int) raw.noiseRoute->load();
    for (int f = 0; f < 2; ++f)
    {
        const auto& x = filtRaw[f];
        auto& fp = p.filt[f];
        fp.on = x.on->load() > 0.5f;
        fp.type = (int) x.type->load();
        fp.cutoff = x.cutoff->load();
        fp.reso = x.reso->load();
        fp.drive = x.drive->load();
        fp.key = x.key->load();
        fp.mix = x.mix->load();
    }
    p.serial = raw.serial->load() < 0.5f;
    p.voiceMode = (int) raw.voiceMode->load();
    p.glideMs = raw.glide->load();
    p.glideCurve = raw.glideCurve->load();
    p.glideLegato = raw.glideLegato->load() > 0.5f;
    p.velSens = raw.velSens->load();
    p.maxVoices = (int) raw.voices->load();
    p.env.delayMs = raw.delay->load();
    p.env.attackMs = raw.attack->load();
    p.env.holdMs = raw.hold->load();
    p.env.decayMs = raw.decay->load();
    p.env.sustain = raw.sustain->load();
    p.env.releaseMs = raw.release->load();
    p.env.attackCurve = raw.aCurve->load();
    p.env.decayCurve = raw.dCurve->load();
    p.env.releaseCurve = raw.rCurve->load();
}

// =============================================================================
// Rendu
// =============================================================================
float IDGemsSynthProcessor::modDisplay01 (int paramIndex) const
{
    const int f = modTargets.fieldForParam (paramIndex);
    if (f >= 0) return modShared.fieldValue01[f].load();
    const auto gt = modTargets.globalTarget (paramIndex);
    if (gt.first < 0) return -1.0f;
    const auto& p = racks[(size_t) gt.first]->bank[gt.second];
    return p.modActive ? p.display01.load() : -1.0f;
}

void IDGemsSynthProcessor::applyGlobalModulation()
{
    // modulations des effets : sources globales (LFO stéréo : canal droit décalé) ou valeur de la voix la plus récente
    for (auto& br : racks) br->bank.clearMod();
    const auto& mb = *modBlock;
    const auto* newest = engine.newestVoice();
    for (int i = 0; i < mb.numGlobal; ++i)
    {
        const auto& g = mb.gslots[i];
        auto& p = racks[(size_t) g.rack]->bank[g.index];
        for (int ch = 0; ch < 2; ++ch)
        {
            float v;
            if (sourceIsPerVoice (g.src) && ! (g.src >= MSrcLfo0 && g.src < MSrcRnd0
                                               && mb.lfo[g.src - MSrcLfo0].mode != LfoNoteVoice && mb.lfo[g.src - MSrcLfo0].mode != LfoOneShot))
                v = newest != nullptr ? newest->voiceSources()[g.src] : 0.0f;
            else v = ch == 0 ? mb.global[g.src] : mb.globalR[g.src];
            if (g.bipolar) v = v * 2.0f - 1.0f;
            p.modAcc[ch] += g.depth * v;
        }
        p.modActive = true;
    }
    for (auto& br : racks) br->bank.update (scale, 0.0f, 0.0, br->rack.activeFlags());
}

void IDGemsSynthProcessor::computeBusOrder (int order[NumBuses], bool edgeOk[NumBuses][NumBuses])
{
    // envois bus -> bus : un envoi « arrière » (vers un bus précédent) n'est gardé que s'il ne crée pas de boucle
    bool edge[NumBuses][NumBuses] {};
    for (int a = 0; a < NumBuses; ++a)
        for (int t = 0; t < NumBuses; ++t)
            edge[a][t] = t != a && raw.busSend[a][t]->load() > 0.0005f;
    for (int a = 0; a < NumBuses; ++a)
        for (int t = 0; t < NumBuses; ++t) edgeOk[a][t] = edge[a][t] && t > a;
    auto reaches = [&edgeOk] (int from, int to)
    {
        bool seen[NumBuses] {};
        int stack[NumBuses * 4], sp = 0;
        stack[sp++] = from;
        while (sp > 0)
        {
            const int x = stack[--sp];
            if (x == to) return true;
            if (seen[x]) continue;
            seen[x] = true;
            for (int y = 0; y < NumBuses; ++y) if (edgeOk[x][y] && sp < NumBuses * 4) stack[sp++] = y;
        }
        return false;
    };
    for (int a = NumBuses - 1; a >= 0; --a)
        for (int t = 0; t < a; ++t)
            if (edge[a][t] && ! reaches (t, a)) edgeOk[a][t] = true;
    for (int a = 0; a < NumBuses; ++a)
        for (int t = 0; t < NumBuses; ++t)
        {
            busEdgeActive[a][t].store (edgeOk[a][t], std::memory_order_relaxed);
            busEdgeBlocked[a][t].store (edge[a][t] && ! edgeOk[a][t], std::memory_order_relaxed);
        }
    // ordre topologique (Kahn)
    int indeg[NumBuses] {};
    for (int a = 0; a < NumBuses; ++a) for (int t = 0; t < NumBuses; ++t) if (edgeOk[a][t]) ++indeg[t];
    bool done[NumBuses] {};
    for (int k = 0; k < NumBuses; ++k)
    {
        int pick = -1;
        for (int x = 0; x < NumBuses && pick < 0; ++x) if (! done[x] && indeg[x] == 0) pick = x;
        if (pick < 0) for (int x = 0; x < NumBuses && pick < 0; ++x) if (! done[x]) pick = x;
        done[pick] = true;
        order[k] = pick;
        for (int t = 0; t < NumBuses; ++t) if (edgeOk[pick][t]) --indeg[t];
    }
}

void IDGemsSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0) return;

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    readParams (vp);
    const float bendRange = std::round (raw.bendRange->load());

    // transport de l'hôte
    double bpm = 120.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition(); q && playing) ppqNow = *q;
        }
    uiBpm.store (bpm);
    const double beatsPerSample = bpm / 60.0 / sr;

    // envois vers les bus
    for (int src = 0; src < NumSendSources; ++src)
        for (int b = 0; b < NumBuses; ++b) vp.send[src][b] = raw.send[src][b]->load();
    for (int b = 0; b < NumBuses; ++b)
    {
        vp.busActive[b] = false;
        for (int src = 0; src < NumSendSources; ++src) vp.busActive[b] |= vp.send[src][b] > 0.0f;
    }

    // modulations : sources globales + connexions (voix et effets)
    float fol[NumBuses];
    for (int b = 0; b < NumBuses; ++b) fol[b] = follow[b];
    modGlobal.process (*modBlock, n, bpm, ppqNow, playing, engine.takeNoteOns(), engine.getModWheel(), engine.getAftertouch(),
                       0.5f + 0.5f * engine.getBend(), fol);
    vp.mod = modBlock->numSlots > 0 ? modBlock.get() : nullptr;
    applyGlobalModulation();

    if (panicFlag.exchange (false))
    {
        engine.allSoundOff();
        panicMute = (int) (0.01 * sr);
    }

    // routage bus -> bus (sans boucle) et réglages de sortie
    int order[NumBuses];
    bool edgeOk[NumBuses][NumBuses];
    computeBusOrder (order, edgeOk);
    bool anySolo = false;
    for (int b = 0; b < NumBuses; ++b) anySolo |= raw.busSolo[b]->load() > 0.5f;
    const bool zeroLat = raw.zeroLat->load() > 0.5f;
    const int osFactor = (int) raw.destOS->load() == 2 ? 4 : ((int) raw.destOS->load() == 1 ? 2 : 1);
    const float chaos = 0.5f + 0.5f * raw.chaosMacro->load();
    const int latency = idg::Rack::rackLatency (zeroLat) * 2 + output.latency (false);
    if (latency != lastLatency) { lastLatency = latency; setLatencySamples (latency); }
    masterDry.delay = idg::Rack::rackLatency (zeroLat);

    idg::OutputChain::Settings os;
    os.hpFreq = raw.hpFreq->load();
    os.monoOn = raw.monoOn->load() > 0.5f;
    os.monoFreq = raw.monoFreq->load();
    os.ottDepth = raw.ottDepth->load();
    os.ottTime = raw.ottTime->load();
    os.ottUp = raw.ottUp->load();
    os.ottDown = raw.ottDown->load();
    os.ottGainDb = raw.ottGain->load();
    os.outGainDb = raw.outGain->load();
    os.clipDriveDb = raw.clipDrive->load();
    os.ceilingDb = raw.ceiling->load();
    os.releaseMs = raw.limRelease->load();
    const float inGain = idg::dsp::dbToGain (raw.inGain->load());
    const float masterMix = raw.masterMix->load();
    const float smooth = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
    const float folAtt = std::exp (-1.0f / (0.005f * (float) sr)), folRel = std::exp (-1.0f / (0.15f * (float) sr));

    auto it = midi.cbegin();
    for (int done = 0; done < n;)
    {
        const int len = juce::jmin (ChunkSize, n - done);
        for (int c = 0; c < 2 * NumBuses; ++c) { busBuf.clear (c, 0, len); busOut.clear (c, 0, len); }
        float* bL[NumBuses] { busBuf.getWritePointer (0), busBuf.getWritePointer (2), busBuf.getWritePointer (4) };
        float* bR[NumBuses] { busBuf.getWritePointer (1), busBuf.getWritePointer (3), busBuf.getWritePointer (5) };

        // ---- voix (rendu découpé aux positions exactes des événements MIDI)
        int pos = 0;
        while (pos < len)
        {
            int next = len;
            while (it != midi.cend())
            {
                const auto meta = *it;
                const int at = meta.samplePosition - done;
                if (at > pos) { next = juce::jmin (len, at); break; }
                engine.handleMidi (meta.getMessage(), vp);
                ++it;
            }
            vp.bendSemis = engine.getBend() * bendRange;
            float* oL[NumBuses] { bL[0] + pos, bL[1] + pos, bL[2] + pos };
            float* oR[NumBuses] { bR[0] + pos, bR[1] + pos, bR[2] + pos };
            engine.render (oL, oR, next - pos, vp);
            pos = next;
        }

        // ---- contexte des effets (tempo, position)
        idg::ProcCtx ctx;
        ctx.sr = sr; ctx.baseSr = sr; ctx.os = 1; ctx.bpm = bpm; ctx.ppq = ppqNow;
        ctx.spb = sr * 60.0 / juce::jmax (1.0, bpm); ctx.playing = playing; ctx.scale = &scale; ctx.zeroLatency = zeroLat;
        auto runRack = [&] (BusRack& br, float* L, float* R)
        {
            idg::RackControl rc;
            rc.chaos = chaos;
            rc.osFactor = osFactor;
            for (int fx = 0; fx < idg::NumFx; ++fx)
            {
                if (br.seq[fx].enabled.load (std::memory_order_relaxed))
                {
                    bool gate, trig;
                    br.seqRt[fx].tick (br.seq[fx], ppqNow, chaos, audioRng, gate, trig);
                    rc.gate[fx] = gate; rc.trig[fx] = trig;
                }
                else { rc.gate[fx] = true; rc.trig[fx] = false; }
            }
            br.rack.process (L, R, len, ctx, rc);
        };

        // ---- bus A / B / C dans l'ordre (envois bus -> bus vers l'aval), puis master
        float* mL = work.getWritePointer (0);
        float* mR = work.getWritePointer (1);
        juce::FloatVectorOperations::clear (mL, len);
        juce::FloatVectorOperations::clear (mR, len);
        for (int k = 0; k < NumBuses; ++k)
        {
            const int b = order[k];
            runRack (*racks[(size_t) b], bL[b], bR[b]);
            const bool muted = raw.busMute[b]->load() > 0.5f || (anySolo && raw.busSolo[b]->load() < 0.5f);
            const float vdb = raw.busVol[b]->load();
            const float vol = muted || vdb <= -59.9f ? 0.0f : idg::dsp::dbToGain (vdb);
            const float pa = (juce::jlimit (-1.0f, 1.0f, raw.busPan[b]->load()) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float tl = std::cos (pa) * 1.41421f, tr = std::sin (pa) * 1.41421f;
            float pk = 0.0f;
            for (int i = 0; i < len; ++i)
            {
                busVolSm[b] += (vol - busVolSm[b]) * smooth;
                busPanL[b] += (juce::jmin (1.0f, tl) - busPanL[b]) * smooth;
                busPanR[b] += (juce::jmin (1.0f, tr) - busPanR[b]) * smooth;
                const float l = bL[b][i] * busVolSm[b] * busPanL[b], r = bR[b][i] * busVolSm[b] * busPanR[b];
                mL[i] += l; mR[i] += r;
                const float a = juce::jmax (std::abs (l), std::abs (r));
                pk = juce::jmax (pk, a);
                follow[b] = a > follow[b] ? a + (follow[b] - a) * folAtt : a + (follow[b] - a) * folRel;
                for (int t = 0; t < NumBuses; ++t)
                    if (edgeOk[b][t])
                    {
                        const float amt = raw.busSend[b][t]->load();
                        bL[t][i] += l * amt;
                        bR[t][i] += r * amt;
                    }
            }
            if (pk > busMeter[b].load (std::memory_order_relaxed)) busMeter[b].store (pk, std::memory_order_relaxed);
            const float fv = juce::jlimit (0.0f, 1.0f, (idg::dsp::gainToDb (follow[b] + 1.0e-6f) + 48.0f) / 48.0f);
            followerValue[b].store (fv, std::memory_order_relaxed);
        }

        // ---- master : rack facultatif + dry/wet, volume, chaîne de sortie
        for (int i = 0; i < len; ++i) { mL[i] *= inGain; mR[i] *= inGain; dryL[(size_t) i] = mL[i]; dryR[(size_t) i] = mR[i]; }
        for (int i = 0; i < len; ++i) masterDry.process (dryL[(size_t) i], dryR[(size_t) i]);
        runRack (*racks[3], mL, mR);
        masterGain.setTargetValue (raw.masterVol->load() <= -59.9f ? 0.0f : idg::dsp::dbToGain (raw.masterVol->load()));
        for (int i = 0; i < len; ++i)
        {
            masterMixSm += (masterMix - masterMixSm) * smooth;
            const float g = masterGain.getNextValue();
            mL[i] = (dryL[(size_t) i] + (mL[i] - dryL[(size_t) i]) * masterMixSm) * g;
            mR[i] = (dryR[(size_t) i] + (mR[i] - dryR[(size_t) i]) * masterMixSm) * g;
        }
        output.process (mL, mR, len, os);

        // ---- Panic : fondu de 3 ms puis remise à zéro des voix, des effets et des buffers
        if (panicMute > 0)
        {
            if (panicGain <= 0.0f && panicMute <= len)
            {
                engine.reset();
                output.reset();
                for (auto& br : racks) br->rack.reset();
                masterDry.reset();
                for (auto& f : follow) f = 0.0f;
                juce::FloatVectorOperations::clear (mL, len);
                juce::FloatVectorOperations::clear (mR, len);
                panicMute = 0;
                panicGain = 1.0f;
            }
            else
            {
                const float step = 1.0f / (0.003f * (float) sr);
                for (int i = 0; i < len; ++i)
                {
                    panicGain = juce::jmax (0.0f, panicGain - step);
                    mL[i] *= panicGain;
                    mR[i] *= panicGain;
                }
                panicMute = juce::jmax (1, panicMute - len);
            }
        }

        if (buffer.getNumChannels() >= 2)
        {
            buffer.copyFrom (0, done, mL, len);
            buffer.copyFrom (1, done, mR, len);
        }
        else
        {
            auto* out = buffer.getWritePointer (0, done);
            for (int i = 0; i < len; ++i) out[i] = 0.5f * (mL[i] + mR[i]);
        }

        // mesures pour l'interface
        float pk[2] { 0.0f, 0.0f };
        for (int i = 0; i < len; ++i) { pk[0] = juce::jmax (pk[0], std::abs (mL[i])); pk[1] = juce::jmax (pk[1], std::abs (mR[i])); }
        for (int c = 0; c < 2; ++c)
            if (pk[c] > meterOut[c].load (std::memory_order_relaxed)) meterOut[c].store (pk[c], std::memory_order_relaxed);
        const float gr = output.takeGainReduction();
        if (gr < meterGR.load (std::memory_order_relaxed)) meterGR.store (gr, std::memory_order_relaxed);
        {
            int s1, n1, s2, n2;
            scopeFifo.prepareToWrite (len, s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) scopeData[(size_t) (s1 + i)] = 0.5f * (mL[i] + mR[i]);
            for (int i = 0; i < n2; ++i) scopeData[(size_t) (s2 + i)] = 0.5f * (mL[n1 + i] + mR[n1 + i]);
            scopeFifo.finishedWrite (n1 + n2);
        }

        ppqNow += (double) len * beatsPerSample;
        done += len;
    }
    for (; it != midi.cend(); ++it) engine.handleMidi ((*it).getMessage(), vp);

    activeVoices.store (engine.getActiveVoices(), std::memory_order_relaxed);
    if (const auto* v = engine.newestVoice())
    {
        const float* m = v->modulated01();
        for (int f = 0; f < NumModFields; ++f) modShared.fieldValue01[f].store (vp.mod != nullptr ? m[f] : -1.0f, std::memory_order_relaxed);
        const float* sv = v->voiceSources();
        for (int s = 0; s < NumModSrc; ++s)
            if (sourceIsPerVoice (s) && ! (s >= MSrcLfo0 && s < MSrcRnd0 && modBlock->lfo[s - MSrcLfo0].mode != LfoNoteVoice && modBlock->lfo[s - MSrcLfo0].mode != LfoOneShot))
                modShared.sourceValue[s].store (sv[s], std::memory_order_relaxed);
    }
    else for (int f = 0; f < NumModFields; ++f) modShared.fieldValue01[f].store (-1.0f, std::memory_order_relaxed);

    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (secs * sr / (double) n);
    cpuLoad.store (cpuLoad.load (std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
}

// =============================================================================
// Tables et samples
// =============================================================================
void IDGemsSynthProcessor::retire (std::shared_ptr<void> p)
{
    if (p != nullptr) retired.emplace_back (juce::Time::getMillisecondCounterHiRes(), std::move (p));
}

void IDGemsSynthProcessor::timerCallback()
{
    // destruction différée des tables / samples remplacés (le thread audio ne les lit plus)
    const double now = juce::Time::getMillisecondCounterHiRes();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& e) { return now - e.first > 2000.0; }), retired.end());
}

void IDGemsSynthProcessor::setTable (int osc, std::unique_ptr<Wavetable> t)
{
    auto& a = assets[osc];
    std::shared_ptr<Wavetable> sp (std::move (t));
    a.table.store (sp.get());
    retire (std::move (a.tableOwner));
    a.tableOwner = std::move (sp);
    if (a.tableOwner != nullptr) setValue (oscId (osc, "table"), (float) WavetableBank::get().size()); // « Personnalisée »
    ++assetsVersion;
}

void IDGemsSynthProcessor::setSamples (int osc, std::unique_ptr<SampleSet> s, const juce::Array<juce::File>& files)
{
    auto& a = assets[osc];
    std::shared_ptr<SampleSet> sp (std::move (s));
    a.samples.store (sp.get());
    retire (std::move (a.samplesOwner));
    a.samplesOwner = std::move (sp);
    a.files = files;
    ++assetsVersion;
}

juce::String IDGemsSynthProcessor::dropFiles (int osc, const juce::Array<juce::File>& files)
{
    if (files.isEmpty()) return {};
    const int mode = (int) getValue (oscId (osc, "mode"));
    juce::String error;
    if (mode == ModeWavetable)
    {
        // sample -> wavetable : découpage automatique en frames
        juce::File f;
        for (auto& x : files) if (SampleSet::isAudioFile (x)) { f = x; break; }
        if (! f.existsAsFile()) return IDG_T ("Aucun fichier audio reconnu.");
        auto mono = SampleSet::loadMono (f, error);
        if (mono.empty()) return error;
        auto t = Wavetable::fromSample (f.getFileNameWithoutExtension(), mono.data(), (int) mono.size());
        juce::String msg = t->getName() + " : " + juce::String (t->getNumFrames()) + " frames";
        setTable (osc, std::move (t));
        return msg;
    }
    auto set = SampleSet::load (files, mode == ModeMulti, error);
    if (set == nullptr) return error;
    if (mode == ModeSample)
    {
        const int note = SampleSet::noteFromName (set->zones[0]->file.getFileNameWithoutExtension());
        if (note >= 0) setValue (oscId (osc, "root"), (float) note);
    }
    juce::Array<juce::File> list;
    for (auto& z : set->zones) list.add (z->file);
    const auto msg = set->name;
    setSamples (osc, std::move (set), list);
    return msg;
}

void IDGemsSynthProcessor::setTableFromCycles (int osc, const std::vector<float>& cycles, const juce::String& name)
{
    setTable (osc, Wavetable::fromFrames (name, cycles));
}

void IDGemsSynthProcessor::randomTable (int osc)
{
    const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt();
    setTable (osc, Wavetable::random (seed));
}

const Wavetable& IDGemsSynthProcessor::displayTable (int osc) const
{
    const int t = (int) getValue (oscId (osc, "table"));
    const auto* custom = assets[osc].table.load();
    if (t >= WavetableBank::get().size()) return custom != nullptr ? *custom : WavetableBank::get()[0];
    return WavetableBank::get()[t];
}

juce::String IDGemsSynthProcessor::assetName (int osc) const
{
    const int mode = (int) getValue (oscId (osc, "mode"));
    if (mode == ModeWavetable) return displayTable (osc).getName();
    const auto* s = assets[osc].samples.load();
    return s != nullptr ? s->name : IDG_T ("(glisse un fichier audio ici)");
}

// =============================================================================
// État
// =============================================================================
juce::ValueTree IDGemsSynthProcessor::captureState()
{
    auto state = apvts.copyState();
#ifdef JucePlugin_VersionString
    state.setProperty ("version", JucePlugin_VersionString, nullptr);
#endif
    if (presets != nullptr) state.setProperty ("presetName", presets->currentName(), nullptr);
    juce::ValueTree extra ("Assets");
    for (int o = 0; o < NumOscs; ++o)
    {
        juce::ValueTree a ("Osc");
        a.setProperty ("index", o, nullptr);
        if (auto* t = assets[o].tableOwner.get())
        {
            // la table personnalisée est sauvée avec le projet (cycles bruts, frame complète)
            juce::MemoryBlock mb;
            for (int f = 0; f < t->getNumFrames(); ++f) mb.append (t->table (f, 0), sizeof (float) * Wavetable::Size);
            a.setProperty ("tableName", t->getName(), nullptr);
            a.setProperty ("tableData", mb.toBase64Encoding(), nullptr);
        }
        juce::StringArray paths;
        for (auto& f : assets[o].files) paths.add (f.getFullPathName());
        if (! paths.isEmpty()) a.setProperty ("samples", paths.joinIntoString ("|"), nullptr);
        extra.appendChild (a, nullptr);
    }
    state.removeChild (state.getChildWithName ("Assets"), nullptr);
    state.appendChild (extra, nullptr);

    // modulations : connexions (par identifiants) + formes de LFO
    juce::ValueTree mod ("Modulation");
    for (auto& slot : modShared.slots)
    {
        const int src = slot.src.load();
        if (src < 0) continue;
        juce::ValueTree c ("CONN");
        c.setProperty ("src", sourceKey (src), nullptr);
        c.setProperty ("dst", modTargets.paramId (slot.param.load()), nullptr);
        c.setProperty ("depth", slot.depth.load(), nullptr);
        c.setProperty ("bipolar", slot.bipolar.load(), nullptr);
        mod.appendChild (c, nullptr);
    }
    for (int i = 0; i < NumLfos; ++i)
    {
        juce::ValueTree l ("LFOSHAPE");
        l.setProperty ("index", i, nullptr);
        l.setProperty ("pts", modShared.shapes[i].toString(), nullptr);
        mod.appendChild (l, nullptr);
    }
    state.removeChild (state.getChildWithName ("Modulation"), nullptr);
    state.appendChild (mod, nullptr);

    // racks : ordre des effets + séquenceurs de modules
    juce::ValueTree rk ("Racks");
    for (int k = 0; k < idg::NumRacks; ++k)
    {
        juce::ValueTree r ("RACK");
        r.setProperty ("index", k, nullptr);
        juce::StringArray ord;
        for (int v : racks[(size_t) k]->shared.getOrder()) ord.add (juce::String (v));
        r.setProperty ("order", ord.joinIntoString (","), nullptr);
        for (int fx = 0; fx < idg::NumFx; ++fx)
        {
            auto t = racks[(size_t) k]->seq[fx].toTree();
            t.setProperty ("fx", fx, nullptr);
            r.appendChild (t, nullptr);
        }
        rk.appendChild (r, nullptr);
    }
    state.removeChild (state.getChildWithName ("Racks"), nullptr);
    state.appendChild (rk, nullptr);
    return state;
}

void IDGemsSynthProcessor::restoreState (const juce::ValueTree& tree)
{
    if (! tree.hasType (apvts.state.getType())) return;
    auto copy = tree.createCopy();
    auto extra = copy.getChildWithName ("Assets");
    copy.removeChild (extra, nullptr);
    auto mod = copy.getChildWithName ("Modulation");
    copy.removeChild (mod, nullptr);
    auto rk = copy.getChildWithName ("Racks");
    copy.removeChild (rk, nullptr);
    for (int k = 0; k < idg::NumRacks; ++k)
    {
        auto& br = *racks[(size_t) k];
        std::array<int, idg::NumFx> ord {};
        for (int i = 0; i < idg::NumFx; ++i) ord[(size_t) i] = i;
        for (auto& sq : br.seq) sq.clear(), sq.enabled = false;
        auto r = rk.getChildWithProperty ("index", k);
        if (r.isValid())
        {
            auto items = juce::StringArray::fromTokens (r.getProperty ("order").toString(), ",", "");
            if (items.size() == idg::NumFx) for (int i = 0; i < idg::NumFx; ++i) ord[(size_t) i] = items[i].getIntValue();
            for (auto c : r)
                if (c.hasType ("SEQ")) { const int fx = c.getProperty ("fx", -1); if (fx >= 0 && fx < idg::NumFx) br.seq[fx].fromTree (c); }
        }
        br.shared.setOrder (ord);
    }
    modShared.clearAll();
    for (int i = 0; i < NumLfos; ++i) modShared.setShape (i, idg::LfoShape::preset (0));
    for (auto c : mod)
    {
        if (c.hasType ("CONN"))
        {
            const int src = sourceFromKey (c.getProperty ("src").toString());
            const int dst = modTargets.paramIndexForId (c.getProperty ("dst").toString());
            if (src >= 0 && modTargets.isModulatable (dst))
                modShared.addConnection (src, dst, (float) c.getProperty ("depth", 0.3f), (bool) c.getProperty ("bipolar", false));
        }
        else if (c.hasType ("LFOSHAPE"))
        {
            const int i = c.getProperty ("index", -1);
            if (i >= 0 && i < NumLfos) modShared.setShape (i, idg::LfoShape::fromString (c.getProperty ("pts").toString()));
        }
    }

    // les paramètres absents (presets d'une version plus ancienne) reprennent leur valeur par défaut
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! copy.getChildWithProperty ("id", rp->getParameterID()).isValid())
                rp->setValueNotifyingHost (rp->getDefaultValue());
    apvts.replaceState (copy);

    for (int o = 0; o < NumOscs; ++o)
    {
        auto a = extra.getChildWithProperty ("index", o);
        std::unique_ptr<Wavetable> table;
        if (a.isValid() && a.hasProperty ("tableData"))
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (a.getProperty ("tableData").toString()) && mb.getSize() >= sizeof (float) * Wavetable::Size)
            {
                std::vector<float> cycles (mb.getSize() / sizeof (float));
                std::memcpy (cycles.data(), mb.getData(), cycles.size() * sizeof (float));
                table = Wavetable::fromFrames (a.getProperty ("tableName").toString(), cycles);
            }
        }
        const int t = (int) getValue (oscId (o, "table"));
        // ne pas forcer « Personnalisée » si le preset utilise une table d'usine
        auto& as = assets[o];
        std::shared_ptr<Wavetable> sp (std::move (table));
        as.table.store (sp.get());
        retire (std::move (as.tableOwner));
        as.tableOwner = std::move (sp);
        juce::ignoreUnused (t);

        juce::Array<juce::File> files;
        if (a.isValid())
            for (auto& path : juce::StringArray::fromTokens (a.getProperty ("samples").toString(), "|", ""))
                if (path.isNotEmpty()) files.add (juce::File (path));
        std::unique_ptr<SampleSet> set;
        if (! files.isEmpty())
        {
            juce::String error;
            set = SampleSet::load (files, (int) getValue (oscId (o, "mode")) == ModeMulti || files.size() > 1, error);
        }
        setSamples (o, std::move (set), files);
    }
    ++assetsVersion;
}

void IDGemsSynthProcessor::resetToInit()
{
    for (auto& br : racks)
    {
        std::array<int, idg::NumFx> ord {};
        for (int i = 0; i < idg::NumFx; ++i) ord[(size_t) i] = i;
        br->shared.setOrder (ord);
        for (auto& sq : br->seq) { sq.clear(); sq.enabled = false; }
    }
    modShared.clearAll();
    for (int i = 0; i < NumLfos; ++i) modShared.setShape (i, idg::LfoShape::preset (0));
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (int o = 0; o < NumOscs; ++o)
    {
        setSamples (o, nullptr, {});
        auto& as = assets[o];
        as.table.store (nullptr);
        retire (std::move (as.tableOwner));
    }
    ++assetsVersion;
}

void IDGemsSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureState().createXml()) copyXmlToBinary (*xml, destData);
}

void IDGemsSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        restoreState (tree);
        if (presets != nullptr && tree.hasProperty ("presetName")) presets->setCurrentName (tree.getProperty ("presetName").toString());
    }
}

juce::AudioProcessorEditor* IDGemsSynthProcessor::createEditor() { return new IDGemsSynthEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new IDGemsSynthProcessor(); }
