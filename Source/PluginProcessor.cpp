#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace idgs;

IDGemsSynthProcessor::IDGemsSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsSynth", createLayout())
{
    // les wavetables d'usine sont construites ici, jamais dans le thread audio
    (void) WavetableBank::get();

    auto r = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw.oscAOn = r (P::oscAOn);       raw.oscATable = r (P::oscATable); raw.oscAPos = r (P::oscAPos);
    raw.oscAOct = r (P::oscAOct);     raw.oscASemi = r (P::oscASemi);   raw.oscAFine = r (P::oscAFine);
    raw.oscALevel = r (P::oscALevel); raw.oscAPan = r (P::oscAPan);     raw.oscAPhase = r (P::oscAPhase);
    raw.oscAPhRand = r (P::oscAPhRand);
    raw.delay = r (P::env1Delay);     raw.attack = r (P::env1Attack);   raw.hold = r (P::env1Hold);
    raw.decay = r (P::env1Decay);     raw.sustain = r (P::env1Sustain); raw.release = r (P::env1Release);
    raw.aCurve = r (P::env1ACurve);   raw.dCurve = r (P::env1DCurve);   raw.rCurve = r (P::env1RCurve);
    raw.voices = r (P::voices);       raw.velSens = r (P::velSens);     raw.bendRange = r (P::bendRange);
    raw.masterVol = r (P::masterVol); raw.ceiling = r (P::ceiling);

    juce::PropertiesFile::Options opts;
    opts.applicationName = "IDGemsSynth";
    opts.filenameSuffix = "settings";
    opts.folderName = "IDGems Synth";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (opts);
}

bool IDGemsSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void IDGemsSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sr);
    output.prepare (sr);
    work.setSize (2, juce::jmax (1, samplesPerBlock), false, true, false);
    masterGain.reset (sr, 0.02);
    masterGain.setCurrentAndTargetValue (idg::dsp::dbToGain (raw.masterVol->load()));
    setLatencySamples (output.latency (false));
    panicMute = 0;
    panicGain = 1.0f;
}

void IDGemsSynthProcessor::readParams (VoiceParams& p) const
{
    p.oscOn = raw.oscAOn->load() > 0.5f;
    p.table = (int) raw.oscATable->load();
    p.pos = raw.oscAPos->load();
    p.level = raw.oscALevel->load();
    p.pan = raw.oscAPan->load();
    p.phase = raw.oscAPhase->load();
    p.phaseRand = raw.oscAPhRand->load();
    p.pitchOffset = std::round (raw.oscAOct->load()) * 12.0f + std::round (raw.oscASemi->load()) + raw.oscAFine->load() * 0.01f;
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

    // Panic : fondu de toutes les voix, puis buffers vidés pendant le silence
    if (panicFlag.exchange (false))
    {
        engine.allSoundOff();
        panicMute = (int) (0.01 * sr);
    }

    // buffer de travail stéréo (au cas où l'hôte donne plus d'échantillons qu'annoncé, on traite par tranches)
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

        // volume général lissé
        masterGain.setTargetValue (raw.masterVol->load() <= -59.9f ? 0.0f : idg::dsp::dbToGain (raw.masterVol->load()));
        for (int i = 0; i < len; ++i)
        {
            const float g = masterGain.getNextValue();
            L[i] *= g;
            R[i] *= g;
        }

        // chaîne de sortie (reprise d'IDGems) : en phase 1, seul le limiteur agit
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

        // copie vers l'hôte
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
    // événements MIDI restants (position au-delà du bloc) : traités quand même
    for (; it != midi.cend(); ++it) engine.handleMidi ((*it).getMessage(), vp);

    activeVoices.store (engine.getActiveVoices(), std::memory_order_relaxed);
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (secs * sr / (double) n);
    cpuLoad.store (cpuLoad.load (std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
}

void IDGemsSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    #ifdef JucePlugin_VersionString
    state.setProperty ("version", JucePlugin_VersionString, nullptr);
#endif
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void IDGemsSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* IDGemsSynthProcessor::createEditor() { return new IDGemsSynthEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new IDGemsSynthProcessor(); }
