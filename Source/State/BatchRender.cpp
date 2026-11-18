#include "BatchRender.h"
#include "../PluginProcessor.h"
#include "../Engine/Recorder.h"

namespace idgs
{
namespace
{
/** Tête de lecture factice : tempo fixe, transport qui avance (arpège / séquenceur calés). */
struct FakePlayHead : public juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0, sr = 48000.0;
    int64_t samples = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (bpm);
        i.setPpqPosition (ppq);
        i.setTimeInSamples (samples);
        i.setIsPlaying (true);
        i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return i;
    }
    void advance (int n) { samples += n; ppq += (double) n * bpm / 60.0 / sr; }
};
} // namespace

struct BatchRenderer::Job
{
    std::unique_ptr<IDGemsSynthProcessor> rp;
    FakePlayHead head;
    RenderSettings s;
    juce::File file;
    double sr = 48000.0;
    int64_t pos = 0, noteOffAt = 0, maxLen = 0, preroll = 0, loopLen = 0, silentRun = 0;
    bool noteOffSent = false, done = false;
    int latency = -1;
    std::vector<float> L, R;
    juce::AudioBuffer<float> buf { 2, 512 };
    juce::MidiBuffer midi;
    uint32_t seed = 0;
};

BatchRenderer::BatchRenderer (IDGemsSynthProcessor& p) : proc (p) {}
BatchRenderer::~BatchRenderer() { stopTimer(); }

juce::File BatchRenderer::rendersFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("IDGems Synth").getChildFile ("Rendus");
    f.createDirectory();
    return f;
}

std::unique_ptr<BatchRenderer::Job> BatchRenderer::makeJob (const RenderSettings& s, const juce::File& file, uint32_t seed, bool randomize)
{
    auto j = std::make_unique<Job>();
    j->s = s;
    j->file = file;
    j->seed = seed;
    j->sr = s.hq ? 96000.0 : 48000.0;
    j->rp = std::make_unique<IDGemsSynthProcessor>();
    auto& rp = *j->rp;
    rp.setLightweight();
    rp.setPlayConfigDetails (0, 2, j->sr, 512);
    rp.restoreState (proc.captureState(), true);
    rp.shareAssetsFrom (proc);
    rp.noteSeq.fromTree (proc.noteSeq.toTree());
    if (randomize)
    {
        auto& re = rp.randomEngine();
        re.fromTree (proc.randomEngine().toTree());
        re.perform ({}, seed, s.amount, rp.getValue ("rand_guard") > 0.5f, "Batch");
    }
    rp.setValue ("zero_latency", 0.0f); // hors ligne : pleine qualité, latence compensée
    if (s.hq) rp.setValue ("dest_os", juce::jmax (1.0f, rp.getValue ("dest_os")));
    rp.setValue ("rand_drift", s.mode == RenderSettings::Texture ? juce::jmax (0.15f, rp.getValue ("rand_drift")) : rp.getValue ("rand_drift"));
    j->head.bpm = s.bpm;
    j->head.sr = j->sr;
    rp.setPlayHead (&j->head);
    rp.prepareToPlay (j->sr, 512);

    const double beat = 60.0 / s.bpm * j->sr;
    switch (s.mode)
    {
        case RenderSettings::Loop:
            j->preroll = (int64_t) (4.0 * beat);
            j->loopLen = (int64_t) (4.0 * beat * s.bars);
            j->noteOffAt = j->preroll + j->loopLen;
            j->maxLen = j->noteOffAt + (int64_t) (2.0 * j->sr);
            break;
        case RenderSettings::Texture:
            j->noteOffAt = (int64_t) (s.textureSeconds * j->sr);
            j->maxLen = j->noteOffAt + (int64_t) (4.0 * j->sr);
            break;
        default:
            j->noteOffAt = (int64_t) (s.noteSeconds * j->sr);
            j->maxLen = j->noteOffAt + (int64_t) (8.0 * j->sr);
            break;
    }
    j->L.reserve ((size_t) j->maxLen + 8192);
    j->R.reserve ((size_t) j->maxLen + 8192);
    return j;
}

bool BatchRenderer::renderChunk (Job& j, double budgetMs)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    auto notes = [&j] { std::vector<int> n { j.s.note }; if (j.s.chord) { n.push_back (j.s.note + 7); n.push_back (j.s.note + 12); } return n; };
    while (! j.done)
    {
        j.midi.clear();
        if (j.pos == 0) for (int n : notes()) j.midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        if (! j.noteOffSent && j.pos + 512 > j.noteOffAt)
        {
            for (int n : notes()) j.midi.addEvent (juce::MidiMessage::noteOff (1, n), (int) juce::jmax ((int64_t) 0, j.noteOffAt - j.pos));
            j.noteOffSent = true;
        }
        j.buf.clear();
        j.rp->processBlock (j.buf, j.midi);
        j.head.advance (512);
        if (j.latency < 0) j.latency = j.rp->getLatencySamples();
        float pk = 0.0f;
        for (int i = 0; i < 512; ++i)
        {
            const float l = j.buf.getSample (0, i), r = j.buf.getSample (1, i);
            j.L.push_back (std::isfinite (l) ? l : 0.0f);
            j.R.push_back (std::isfinite (r) ? r : 0.0f);
            pk = juce::jmax (pk, std::abs (l), std::abs (r));
        }
        j.pos += 512;
        // fin : longueur max, ou silence 0,3 s après la note (sauf boucle : longueur fixe)
        if (j.noteOffSent && j.s.mode != RenderSettings::Loop)
        {
            j.silentRun = pk < 1.0e-4f ? j.silentRun + 512 : 0;
            if (j.silentRun > (int64_t) (0.3 * j.sr) && j.pos > j.noteOffAt + j.latency) j.done = true;
        }
        if (j.pos >= j.maxLen + juce::jmax (0, j.latency)) j.done = true;
        if (juce::Time::getMillisecondCounterHiRes() - t0 > budgetMs) break;
    }
    return j.done;
}

void BatchRenderer::finishJob (Job& j)
{
    // compensation de latence
    const size_t lat = (size_t) juce::jmax (0, j.latency);
    if (lat > 0 && lat < j.L.size())
    {
        j.L.erase (j.L.begin(), j.L.begin() + (std::ptrdiff_t) lat);
        j.R.erase (j.R.begin(), j.R.begin() + (std::ptrdiff_t) lat);
    }
    std::vector<float> L, R;
    if (j.s.mode == RenderSettings::Loop)
    {
        // boucle sans couture : on garde [préroll, préroll + longueur] et la queue revient au début
        const size_t a = (size_t) j.preroll, len = (size_t) j.loopLen;
        L.assign (len, 0.0f);
        R.assign (len, 0.0f);
        for (size_t i = 0; i < len && a + i < j.L.size(); ++i) { L[i] = j.L[a + i]; R[i] = j.R[a + i]; }
        for (size_t i = 0; a + len + i < j.L.size(); ++i)
        {
            L[i % len] += j.L[a + len + i];
            R[i % len] += j.R[a + len + i];
        }
    }
    else
    {
        // fin : on coupe le silence final
        size_t end = j.L.size();
        while (end > 0 && std::abs (j.L[end - 1]) < 1.0e-4f && std::abs (j.R[end - 1]) < 1.0e-4f) --end;
        end = juce::jmin (j.L.size(), end + (size_t) (0.05 * j.sr));
        L.assign (j.L.begin(), j.L.begin() + (std::ptrdiff_t) end);
        R.assign (j.R.begin(), j.R.begin() + (std::ptrdiff_t) end);
        // fondus (texture : entrée et sortie douces ; one-shot : sortie seulement)
        const size_t fo = juce::jmin (L.size(), (size_t) (0.02 * j.sr));
        for (size_t i = 0; i < fo; ++i)
        {
            const float g = (float) i / (float) fo;
            L[L.size() - 1 - i] *= g;
            R[R.size() - 1 - i] *= g;
        }
        if (j.s.mode == RenderSettings::Texture)
        {
            const size_t fi = juce::jmin (L.size(), (size_t) (0.03 * j.sr));
            for (size_t i = 0; i < fi; ++i) { const float g = (float) i / (float) fi; L[i] *= g; R[i] *= g; }
        }
    }
    if (L.empty()) { L.assign (64, 0.0f); R.assign (64, 0.0f); }
    if (j.s.normalize)
    {
        float pk = 0.0f;
        for (size_t i = 0; i < L.size(); ++i) pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
        if (pk > 1.0e-4f)
        {
            const float g = juce::Decibels::decibelsToGain (-1.0f) / pk;
            for (size_t i = 0; i < L.size(); ++i) { L[i] *= g; R[i] *= g; }
        }
    }
    juce::AudioBuffer<float> out (2, (int) L.size());
    out.copyFrom (0, 0, L.data(), (int) L.size());
    out.copyFrom (1, 0, R.data(), (int) R.size());
    if (! idg::writeWav (j.file, out, j.sr)) j.file = juce::File();
    j.rp->setPlayHead (nullptr);
}

juce::File BatchRenderer::renderNow (const RenderSettings& s, const juce::File& target)
{
    auto j = makeJob (s, target, 0, false);
    while (! renderChunk (*j, 1.0e9)) {}
    finishJob (*j);
    return j->file;
}

bool BatchRenderer::start (const RenderSettings& s)
{
    if (running) return false;
    settings = s;
    rendered.clear();
    index = 0;
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
    outFolder = rendersFolder().getChildFile (juce::File::createLegalFileName (s.name) + "_" + stamp);
    outFolder.createDirectory();
    running = true;
    statusText = "Rendu 1 / " + juce::String (s.count) + "...";
    startTimerHz (40);
    return true;
}

void BatchRenderer::cancel()
{
    if (! running) return;
    stopTimer();
    job.reset();
    running = false;
    statusText = IDG_T ("Rendu annulé (") + juce::String (rendered.size()) + IDG_T (" fichier(s) gardé(s))");
    if (onFinished) onFinished();
}

float BatchRenderer::progress() const
{
    if (! running) return rendered.isEmpty() ? 0.0f : 1.0f;
    float inner = 0.0f;
    if (job != nullptr && job->maxLen > 0) inner = juce::jlimit (0.0f, 1.0f, (float) job->pos / (float) job->maxLen);
    return juce::jlimit (0.0f, 1.0f, ((float) index + inner) / (float) juce::jmax (1, settings.count));
}

void BatchRenderer::timerCallback()
{
    if (! running) { stopTimer(); return; }
    if (job == nullptr)
    {
        const uint32_t seed = (uint32_t) juce::Random::getSystemRandom().nextInt (999999) + 1;
        static const char* modeNames[] { "oneshot", "loop", "texture" };
        auto name = juce::File::createLegalFileName (settings.name) + "_" + modeNames[juce::jlimit (0, 2, settings.mode)] + "_"
                    + juce::String (index + 1).paddedLeft ('0', 2) + (settings.randomize ? "_s" + juce::String (seed) : juce::String()) + ".wav";
        job = makeJob (settings, outFolder.getChildFile (name), seed, settings.randomize);
    }
    if (renderChunk (*job, 18.0))
    {
        finishJob (*job);
        if (job->file.existsAsFile()) rendered.add (job->file);
        job.reset();
        ++index;
        if (index >= settings.count)
        {
            stopTimer();
            running = false;
            statusText = juce::String (rendered.size()) + IDG_T (" fichier(s) rendu(s) dans ") + outFolder.getFileName();
            if (onFinished) onFinished();
            return;
        }
        statusText = "Rendu " + juce::String (index + 1) + " / " + juce::String (settings.count) + "...";
    }
}
} // namespace idgs
