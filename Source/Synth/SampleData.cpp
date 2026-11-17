#include "SampleData.h"
#include "../Core/Dsp.h"

namespace idgs
{
namespace
{
juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager fm;
    static const bool init = [] { fm.registerBasicFormats(); return true; }();
    juce::ignoreUnused (init);
    return fm;
}

std::unique_ptr<SampleZone> readZone (const juce::File& f, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (f));
    if (reader == nullptr) { error = IDG_T ("Format non reconnu : ") + f.getFileName(); return nullptr; }
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * SampleSet::maxSeconds));
    if (len <= 0) { error = IDG_T ("Fichier vide : ") + f.getFileName(); return nullptr; }
    auto z = std::make_unique<SampleZone>();
    z->data.setSize (2, len);
    z->data.clear();
    reader->read (&z->data, 0, len, 0, true, true);
    if (reader->numChannels == 1) z->data.copyFrom (1, 0, z->data, 0, 0, len);
    z->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    z->file = f;
    // nettoyage : NaN éventuels -> 0
    for (int c = 0; c < 2; ++c)
    {
        auto* d = z->data.getWritePointer (c);
        for (int i = 0; i < len; ++i) if (! std::isfinite (d[i])) d[i] = 0.0f;
    }
    return z;
}
} // namespace

bool SampleSet::isAudioFile (const juce::File& f)
{
    return f.existsAsFile() && formats().findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

int SampleSet::noteFromName (const juce::String& fileName)
{
    // cherche, en partant de la fin, un motif note + altération + octave : « C3 », « A#2 », « Eb-1 »
    const auto s = fileName.toUpperCase();
    static const int base[] { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    for (int i = s.length() - 1; i >= 0; --i)
    {
        const auto ch = s[i];
        if (ch < 'A' || ch > 'G') continue;
        if (i > 0 && juce::CharacterFunctions::isLetter (s[i - 1])) continue; // doit être un mot séparé
        int j = i + 1, semis = base[ch - 'A'];
        if (j < s.length() && (s[j] == '#' || s[j] == 'S')) { ++semis; ++j; }
        else if (j < s.length() && s[j] == 'B') { --semis; ++j; }
        bool neg = false;
        if (j < s.length() && s[j] == '-') { neg = true; ++j; }
        if (j >= s.length() || ! juce::CharacterFunctions::isDigit (s[j])) continue;
        const int oct = (s[j] - '0') * (neg ? -1 : 1);
        if (j + 1 < s.length() && juce::CharacterFunctions::isDigit (s[j + 1])) continue;
        const int note = (oct + 2) * 12 + semis; // convention C3 = 60
        if (note >= 0 && note <= 127) return note;
    }
    return -1;
}

std::unique_ptr<SampleSet> SampleSet::load (const juce::Array<juce::File>& files, bool multi, juce::String& error)
{
    juce::Array<juce::File> list;
    for (auto& f : files)
    {
        if (f.isDirectory())
            for (auto& c : f.findChildFiles (juce::File::findFiles, false)) { if (isAudioFile (c)) list.add (c); }
        else if (isAudioFile (f)) list.add (f);
    }
    if (list.isEmpty()) { error = IDG_T ("Aucun fichier audio reconnu."); return nullptr; }
    if (! multi) list.removeRange (1, list.size());
    list.sort();

    auto set = std::make_unique<SampleSet>();
    for (int i = 0; i < juce::jmin (list.size(), 128); ++i)
        if (auto z = readZone (list[i], error)) set->zones.push_back (std::move (z));
    if (set->zones.empty()) return nullptr;

    // notes d'origine : nom du fichier, sinon répartition chromatique à partir de C3
    int fallback = 60;
    for (auto& z : set->zones)
    {
        const int n = noteFromName (z->file.getFileNameWithoutExtension());
        z->root = n >= 0 ? n : juce::jmin (127, fallback++);
    }
    std::stable_sort (set->zones.begin(), set->zones.end(), [] (auto& a, auto& b) { return a->root < b->root; });
    // zones : chaque sample couvre le clavier jusqu'à mi-chemin du suivant
    for (size_t i = 0; i < set->zones.size(); ++i)
    {
        set->zones[i]->lo = i == 0 ? 0 : (set->zones[i - 1]->root + set->zones[i]->root) / 2 + 1;
        set->zones[i]->hi = i + 1 == set->zones.size() ? 127 : (set->zones[i]->root + set->zones[i + 1]->root) / 2;
    }
    set->name = multi ? list[0].getParentDirectory().getFileName() + " (" + juce::String ((int) set->zones.size()) + " samples)"
                      : list[0].getFileNameWithoutExtension();
    return set;
}

std::unique_ptr<SpectralData> SpectralData::analyse (const float* x, int len)
{
    auto d = std::make_unique<SpectralData>();
    len = juce::jmin (len, (int) (MaxSeconds * 48000.0));
    d->numFrames = juce::jmax (1, (len - N) / Hop + 1);
    d->mags.assign ((size_t) d->numFrames * Bins, 0.0f);
    juce::dsp::FFT fft (Order);
    std::vector<float> buf ((size_t) N * 2);
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    for (int f = 0; f < d->numFrames; ++f)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int k = f * Hop + i;
            buf[(size_t) i] = k < len ? x[k] * win[(size_t) i] : 0.0f;
        }
        fft.performFrequencyOnlyForwardTransform (buf.data());
        auto* m = d->mags.data() + (size_t) f * Bins;
        for (int b = 0; b < Bins; ++b) m[b] = buf[(size_t) b] * (2.0f / (float) N);
    }
    return d;
}

std::unique_ptr<FingerprintData> FingerprintData::analyse (const float* x, int len, double sr)
{
    auto d = std::make_unique<FingerprintData>();
    const int hop = juce::jmax (1, (int) (sr / FrameRate));
    d->numFrames = juce::jmax (1, len / hop);
    d->env.assign ((size_t) d->numFrames * NumBands, 0.0f);
    // banc de passe-bandes (un par bande), énergie RMS par trame
    std::vector<idg::dsp::SVF> bp ((size_t) NumBands);
    for (int b = 0; b < NumBands; ++b) bp[(size_t) b].set (sr, bandFreq (b), 4.0f);
    float mx = 1.0e-9f;
    for (int f = 0; f < d->numFrames; ++f)
    {
        float acc[NumBands] {};
        for (int i = 0; i < hop; ++i)
        {
            const int k = f * hop + i;
            const float v = k < len ? x[k] : 0.0f;
            for (int b = 0; b < NumBands; ++b) { const float y = bp[(size_t) b].bandpass (0, v); acc[b] += y * y; }
        }
        for (int b = 0; b < NumBands; ++b)
        {
            const float e = std::sqrt (acc[b] / (float) hop);
            d->env[(size_t) f * NumBands + (size_t) b] = e;
            mx = juce::jmax (mx, e);
        }
    }
    for (auto& e : d->env) e = juce::jlimit (0.0f, 1.0f, e / mx);
    return d;
}

void SampleSet::computeSpectral()
{
    for (auto& z : zones)
    {
        if (z->spectral != nullptr) continue;
        std::vector<float> mono ((size_t) z->length());
        for (int i = 0; i < z->length(); ++i) mono[(size_t) i] = 0.5f * (z->data.getSample (0, i) + z->data.getSample (1, i));
        z->spectral = SpectralData::analyse (mono.data(), (int) mono.size());
    }
}

void SampleSet::computeFingerprint()
{
    for (auto& z : zones)
    {
        if (z->fingerprint != nullptr) continue;
        std::vector<float> mono ((size_t) z->length());
        for (int i = 0; i < z->length(); ++i) mono[(size_t) i] = 0.5f * (z->data.getSample (0, i) + z->data.getSample (1, i));
        z->fingerprint = FingerprintData::analyse (mono.data(), (int) mono.size(), z->sampleRate);
    }
}

std::unique_ptr<SampleSet> SampleSet::loadRaw (const juce::File& f, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! f.existsAsFile() || ! f.loadFileAsData (mb) || mb.getSize() < 64) { error = IDG_T ("Fichier illisible : ") + f.getFileName(); return nullptr; }
    const size_t bytes = juce::jmin (mb.getSize(), (size_t) (4 * 1024 * 1024));
    auto set = std::make_unique<SampleSet>();
    auto z = std::make_unique<SampleZone>();
    const int len = (int) bytes;
    z->data.setSize (2, len);
    z->data.clear();
    const auto* b = static_cast<const uint8_t*> (mb.getData());
    auto* c8 = z->data.getWritePointer (0);
    auto* c16 = z->data.getWritePointer (1);
    for (int i = 0; i < len; ++i) c8[i] = ((float) b[i] - 128.0f) / 128.0f;
    for (int i = 0; i + 1 < len; i += 2)
    {
        const auto v = (int16_t) (uint16_t) (b[i] | (b[i + 1] << 8));
        c16[i / 2] = (float) v / 32768.0f;
    }
    z->sampleRate = 44100.0;
    z->file = f;
    set->zones.push_back (std::move (z));
    set->name = f.getFileName() + " (" + juce::String ((int) (bytes / 1024)) + " Ko)";
    return set;
}

std::vector<float> SampleSet::loadMono (const juce::File& f, juce::String& error)
{
    std::vector<float> out;
    auto z = readZone (f, error);
    if (z == nullptr) return out;
    out.resize ((size_t) z->length());
    const float* l = z->data.getReadPointer (0);
    const float* r = z->data.getReadPointer (1);
    for (size_t i = 0; i < out.size(); ++i) out[i] = 0.5f * (l[i] + r[i]);
    return out;
}
} // namespace idgs
