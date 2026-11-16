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

    juce::PropertiesFile::Options opts;
    opts.applicationName = "IDGemsSynth";
    opts.filenameSuffix = "settings";
    opts.folderName = "IDGems Synth";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (opts);

    modTargets.init (*this);
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
    work.setSize (2, juce::jmax (1, samplesPerBlock), false, true, false);
    masterGain.reset (sr, 0.02);
    masterGain.setCurrentAndTargetValue (idg::dsp::dbToGain (raw.masterVol->load()));
    setLatencySamples (output.latency (false));
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
void IDGemsSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0) return;

    // le clavier à l'écran injecte ses notes dans le flux MIDI
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    readParams (vp);
    const float bendRange = std::round (raw.bendRange->load());

    // transport de l'hôte (synchro des LFO / random)
    double bpm = 120.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition(); q && playing) ppqNow = *q;
        }
    if (! playing) ppqNow += (double) n / sr * bpm / 60.0;
    uiBpm.store (bpm);

    // modulations : sources globales + connexions, pour tout le bloc
    modGlobal.process (*modBlock, n, bpm, ppqNow, playing, engine.takeNoteOns(), engine.getModWheel(), engine.getAftertouch(),
                       0.5f + 0.5f * engine.getBend());
    vp.mod = modBlock->numSlots > 0 ? modBlock.get() : nullptr;

    if (panicFlag.exchange (false))
    {
        engine.allSoundOff();
        panicMute = (int) (0.01 * sr);
    }

    const int cap = work.getNumSamples();
    auto it = midi.cbegin();
    for (int done = 0; done < n;)
    {
        const int len = juce::jmin (cap, n - done);
        float* L = work.getWritePointer (0);
        float* R = work.getWritePointer (1);
        juce::FloatVectorOperations::clear (L, len);
        juce::FloatVectorOperations::clear (R, len);

        // rendu découpé aux positions exactes des événements MIDI
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
            engine.render (L + pos, R + pos, next - pos, vp);
            pos = next;
        }

        masterGain.setTargetValue (raw.masterVol->load() <= -59.9f ? 0.0f : idg::dsp::dbToGain (raw.masterVol->load()));
        for (int i = 0; i < len; ++i)
        {
            const float g = masterGain.getNextValue();
            L[i] *= g;
            R[i] *= g;
        }

        // chaîne de sortie (reprise d'IDGems) : en phase 2, seul le limiteur agit
        idg::OutputChain::Settings os;
        os.hpFreq = 5.0f;
        os.monoOn = false;
        os.ottDepth = 0.0f;
        os.ceilingDb = raw.ceiling->load();
        os.releaseMs = 80.0f;
        output.process (L, R, len, os);

        // Panic : fondu de sortie de 3 ms, puis (au bloc suivant, une fois le silence atteint)
        // remise à zéro des voix et des buffers internes
        if (panicMute > 0)
        {
            if (panicGain <= 0.0f && panicMute <= len)
            {
                engine.reset();
                output.reset();
                juce::FloatVectorOperations::clear (L, len);
                juce::FloatVectorOperations::clear (R, len);
                panicMute = 0;
                panicGain = 1.0f;
            }
            else
            {
                const float step = 1.0f / (0.003f * (float) sr);
                for (int i = 0; i < len; ++i)
                {
                    panicGain = juce::jmax (0.0f, panicGain - step);
                    L[i] *= panicGain;
                    R[i] *= panicGain;
                }
                panicMute = juce::jmax (1, panicMute - len);
            }
        }

        if (buffer.getNumChannels() >= 2)
        {
            buffer.copyFrom (0, done, L, len);
            buffer.copyFrom (1, done, R, len);
        }
        else
        {
            auto* out = buffer.getWritePointer (0, done);
            for (int i = 0; i < len; ++i) out[i] = 0.5f * (L[i] + R[i]);
        }

        // mesures pour l'interface
        float pk[2] { 0.0f, 0.0f };
        for (int i = 0; i < len; ++i) { pk[0] = juce::jmax (pk[0], std::abs (L[i])); pk[1] = juce::jmax (pk[1], std::abs (R[i])); }
        for (int c = 0; c < 2; ++c)
            if (pk[c] > meterOut[c].load (std::memory_order_relaxed)) meterOut[c].store (pk[c], std::memory_order_relaxed);
        const float gr = output.takeGainReduction();
        if (gr < meterGR.load (std::memory_order_relaxed)) meterGR.store (gr, std::memory_order_relaxed);
        {
            int s1, n1, s2, n2;
            scopeFifo.prepareToWrite (len, s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) scopeData[(size_t) (s1 + i)] = 0.5f * (L[i] + R[i]);
            for (int i = 0; i < n2; ++i) scopeData[(size_t) (s2 + i)] = 0.5f * (L[n1 + i] + R[n1 + i]);
            scopeFifo.finishedWrite (n1 + n2);
        }

        done += len;
    }
    for (; it != midi.cend(); ++it) engine.handleMidi ((*it).getMessage(), vp);

    activeVoices.store (engine.getActiveVoices(), std::memory_order_relaxed);
    // affichage des modulations : voix la plus récente
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
