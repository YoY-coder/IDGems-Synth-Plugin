#include "Recorder.h"

namespace idg
{
Recorder::Recorder() { thread.startThread(); }

Recorder::~Recorder()
{
    stopRecording();
    thread.stopThread (2000);
}

void Recorder::prepare (double sampleRate)
{
    sr = sampleRate;
    const auto size = (size_t) (sr * maxRollingSeconds) + 1;
    if (ring[0].size() != size)
    {
        ring[0].assign (size, 0.0f);
        ring[1].assign (size, 0.0f);
    }
    writePos = 0;
    totalWritten = 0;
}

void Recorder::push (const float* L, const float* R, int n)
{
    const int size = (int) ring[0].size();
    if (size > 0)
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            ring[0][(size_t) w] = L[i];
            ring[1][(size_t) w] = R[i];
            if (++w >= size) w = 0;
        }
        writePos.store (w, std::memory_order_release);
        totalWritten.fetch_add (n, std::memory_order_relaxed);
    }

    const juce::ScopedTryLock sl (writerLock);
    if (sl.isLocked())
        if (auto* wr = activeWriter.load())
        {
            const float* chans[2] { L, R };
            if (wr->write (chans, n)) recordedSamples.fetch_add (n);
        }
}

juce::File Recorder::folder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("IDGems Synth").getChildFile ("Enregistrements");
    f.createDirectory();
    return f;
}

juce::File Recorder::makeFileName (double bpm, const juce::String& suffix)
{
    auto now = juce::Time::getCurrentTime();
    auto name = "IDGemsSynth_" + juce::String (juce::roundToInt (bpm)) + "bpm_" + now.formatted ("%Y-%m-%d_%H-%M-%S") + suffix + ".wav";
    return folder().getNonexistentChildFile (name.upToLastOccurrenceOf (".wav", false, false), ".wav", false);
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sr)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen()) return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

juce::AudioBuffer<float> Recorder::copyRolling (float seconds) const
{
    const int size = (int) ring[0].size();
    if (size == 0) return {};
    const auto available = (int) juce::jmin ((int64_t) size - 1, totalWritten.load());
    const int len = juce::jmin (available, (int) (seconds * sr));
    if (len <= 0) return {};
    juce::AudioBuffer<float> buf (2, len);
    const int w = writePos.load (std::memory_order_acquire);
    for (int i = 0; i < len; ++i)
    {
        int idx = (w - len + i + size) % size;
        buf.setSample (0, i, ring[0][(size_t) idx]);
        buf.setSample (1, i, ring[1][(size_t) idx]);
    }
    return buf;
}

void Recorder::overview (float seconds, int points, std::vector<float>& out) const
{
    out.assign ((size_t) juce::jmax (0, points), 0.0f);
    const int size = (int) ring[0].size();
    if (size == 0 || points <= 0) return;
    const int len = juce::jmin (size - 1, (int) (seconds * sr));
    const int w = writePos.load (std::memory_order_acquire);
    const int per = juce::jmax (1, len / points);
    for (int p = 0; p < points; ++p)
    {
        float pk = 0.0f;
        const int start = w - len + p * per;
        for (int k = 0; k < per; k += 4)
        {
            const int idx = ((start + k) % size + size) % size;
            pk = juce::jmax (pk, std::abs (ring[0][(size_t) idx]), std::abs (ring[1][(size_t) idx]));
        }
        out[(size_t) p] = pk;
    }
}

juce::File Recorder::saveRolling (float seconds, double bpm)
{
    auto buf = copyRolling (seconds);
    if (buf.getNumSamples() == 0) return {};
    auto file = makeFileName (bpm, "_last" + juce::String (juce::roundToInt (seconds)) + "s");
    if (! writeWav (file, buf, sr)) return {};
    lastTake = file;
    return file;
}

bool Recorder::startRecording (double bpm)
{
    stopRecording();
    currentFile = makeFileName (bpm, "");
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (currentFile);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen()) return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr) return false;
    recordedSamples = 0;
    threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), thread, 32768);
    const juce::ScopedLock sl (writerLock);
    activeWriter = threaded.get();
    return true;
}

juce::File Recorder::stopRecording()
{
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter = nullptr;
    }
    threaded.reset();
    auto f = currentFile;
    currentFile = juce::File();
    if (f.existsAsFile()) lastTake = f;
    return f;
}
} // namespace idg
