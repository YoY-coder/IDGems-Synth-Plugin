#include "MotionRecorder.h"
#include "../PluginProcessor.h"

namespace idgs
{
void MotionRecorder::record (int b)
{
    bars = juce::jlimit (1, 16, b);
    frames.clear();
    for (int m = 0; m < NumMacros; ++m) { startValues[m] = proc.getValue (macroId (m)); used[m] = false; }
    startBeat = proc.getBeatPosition();
    state = Recording;
}

void MotionRecorder::play()
{
    if (frames.empty()) return;
    startBeat = std::floor (proc.getBeatPosition() / (4.0 * bars)) * (4.0 * bars);
    state = Playing;
}

void MotionRecorder::stop() { state = Idle; }

void MotionRecorder::clear()
{
    state = Idle;
    frames.clear();
    for (auto& u : used) u = false;
}

float MotionRecorder::valueAt (int m, float x) const
{
    if (frames.empty()) return 0.0f;
    if (x <= frames.front().t) return frames.front().v[m];
    for (size_t i = 1; i < frames.size(); ++i)
        if (frames[i].t >= x)
        {
            const auto& a = frames[i - 1];
            const auto& b = frames[i];
            const float u = b.t > a.t ? (x - a.t) / (b.t - a.t) : 1.0f;
            return a.v[m] + (b.v[m] - a.v[m]) * u;
        }
    return frames.back().v[m];
}

void MotionRecorder::tick (double beat)
{
    if (state == Idle) return;
    if (beat < 0.0) beat = proc.getBeatPosition();
    const double len = 4.0 * bars;
    if (state == Recording)
    {
        const double t = beat - startBeat;
        if (t < 0.0) { startBeat = beat; return; }
        if (t >= len)
        {
            state = frames.empty() ? Idle : Playing;
            startBeat = beat - std::fmod (t, len);
            return;
        }
        Frame f;
        f.t = (float) (t / len);
        for (int m = 0; m < NumMacros; ++m)
        {
            f.v[m] = proc.getValue (macroId (m));
            if (std::abs (f.v[m] - startValues[m]) > 1.0e-3f) used[m] = true;
        }
        if (frames.empty() || f.t > frames.back().t) frames.push_back (f);
        curPhase = f.t;
        return;
    }
    // lecture en boucle (seules les macros touchées pendant l'enregistrement sont rejouées)
    double t = std::fmod (beat - startBeat, len);
    if (t < 0.0) t += len;
    curPhase = (float) (t / len);
    for (int m = 0; m < NumMacros; ++m)
        if (used[m])
            if (auto* p = proc.apvts.getParameter (macroId (m)))
            {
                const float v = valueAt (m, curPhase);
                if (std::abs (p->getValue() - v) > 1.0e-4f) p->setValueNotifyingHost (v);
            }
}

juce::ValueTree MotionRecorder::toTree() const
{
    juce::ValueTree t ("MOTION");
    t.setProperty ("bars", bars, nullptr);
    juce::StringArray u, data;
    for (int m = 0; m < NumMacros; ++m) if (used[m]) u.add (juce::String (m));
    t.setProperty ("used", u.joinIntoString (","), nullptr);
    for (auto& f : frames)
    {
        juce::String s (f.t, 4);
        for (float v : f.v) s << ":" << juce::String (v, 4);
        data.add (s);
    }
    t.setProperty ("frames", data.joinIntoString (";"), nullptr);
    t.setProperty ("playing", state == Playing, nullptr);
    return t;
}

void MotionRecorder::fromTree (const juce::ValueTree& t)
{
    clear();
    if (! t.isValid()) return;
    bars = juce::jlimit (1, 16, (int) t.getProperty ("bars", 2));
    for (auto& s : juce::StringArray::fromTokens (t.getProperty ("used").toString(), ",", ""))
        if (s.isNotEmpty() && s.getIntValue() >= 0 && s.getIntValue() < NumMacros) used[s.getIntValue()] = true;
    for (auto& s : juce::StringArray::fromTokens (t.getProperty ("frames").toString(), ";", ""))
    {
        auto parts = juce::StringArray::fromTokens (s, ":", "");
        if (parts.size() != NumMacros + 1) continue;
        Frame f;
        f.t = parts[0].getFloatValue();
        for (int m = 0; m < NumMacros; ++m) f.v[m] = parts[m + 1].getFloatValue();
        frames.push_back (f);
    }
    if ((bool) t.getProperty ("playing", false) && ! frames.empty()) play();
}
} // namespace idgs
