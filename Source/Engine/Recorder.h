#pragma once
#include "../Core/JuceIncludes.h"

namespace idg
{
/** Écrit un WAV 24 bits stéréo. */
bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sr);

/** Enregistreur : rolling recorder (dernières secondes en permanence) + enregistrement manuel sur disque. */
class Recorder
{
public:
    Recorder();
    ~Recorder();

    void prepare (double sampleRate);
    /** Thread audio. */
    void push (const float* L, const float* R, int n);

    // Thread interface
    juce::File saveRolling (float seconds, double bpm);
    /** Copie des `seconds` dernières secondes (thread interface). */
    juce::AudioBuffer<float> copyRolling (float seconds) const;
    /** Aperçu (crêtes) des `seconds` dernières secondes en `points` colonnes. */
    void overview (float seconds, int points, std::vector<float>& out) const;
    double getSampleRate() const { return sr; }
    juce::File lastFile() const { return lastTake; }
    bool startRecording (double bpm);
    juce::File stopRecording();
    bool isRecording() const { return activeWriter.load() != nullptr; }
    double recordedSeconds() const { return sr > 0 ? (double) recordedSamples.load() / sr : 0.0; }
    static juce::File folder();
    static juce::File makeFileName (double bpm, const juce::String& suffix);

    static constexpr int maxRollingSeconds = 60;

private:
    double sr = 44100.0;
    std::vector<float> ring[2];
    std::atomic<int> writePos { 0 };
    std::atomic<int64_t> totalWritten { 0 };
    std::atomic<int64_t> recordedSamples { 0 };

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threaded;
    juce::TimeSliceThread thread { "IDGems Synth Recorder" };
    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    juce::File currentFile, lastTake;
};
} // namespace idg
