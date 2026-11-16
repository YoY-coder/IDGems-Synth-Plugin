#include "ModSystem.h"

namespace idgs
{
juce::String sourceKey (int s)
{
    if (s >= MSrcMacro0 && s < MSrcLfo0) return "macro" + juce::String (s - MSrcMacro0 + 1);
    if (s >= MSrcLfo0 && s < MSrcRnd0) return "lfo" + juce::String (s - MSrcLfo0 + 1);
    if (s >= MSrcRnd0 && s < MSrcEnv0) return "rnd" + juce::String (s - MSrcRnd0 + 1);
    if (s >= MSrcEnv0 && s < MSrcVelocity) return "env" + juce::String (s - MSrcEnv0 + 1);
    switch (s)
    {
        case MSrcVelocity: return "velocity";
        case MSrcNote: return "note";
        case MSrcNoteRandom: return "noterandom";
        case MSrcModWheel: return "modwheel";
        case MSrcAftertouch: return "aftertouch";
        case MSrcPitchBend: return "pitchbend";
        default: return {};
    }
}

int sourceFromKey (const juce::String& key)
{
    for (int s = 0; s < NumModSrc; ++s) if (sourceKey (s) == key) return s;
    return -1;
}

juce::String sourceName (int s)
{
    if (s >= MSrcMacro0 && s < MSrcLfo0) return s - MSrcMacro0 == NumMacros - 1 ? juce::String ("Chaos") : "Macro " + juce::String (s - MSrcMacro0 + 1);
    if (s >= MSrcLfo0 && s < MSrcRnd0) return "LFO " + juce::String (s - MSrcLfo0 + 1);
    if (s >= MSrcRnd0 && s < MSrcEnv0) return "Random " + juce::String (s - MSrcRnd0 + 1);
    if (s >= MSrcEnv0 && s < MSrcVelocity) return "Env " + juce::String (s - MSrcEnv0 + 1);
    switch (s)
    {
        case MSrcVelocity: return IDG_T ("Vélocité");
        case MSrcNote: return "Note";
        case MSrcNoteRandom: return "Random note";
        case MSrcModWheel: return "Molette";
        case MSrcAftertouch: return "Aftertouch";
        case MSrcPitchBend: return "Pitch bend";
        default: return {};
    }
}

juce::Colour sourceColour (int s)
{
    if (s >= MSrcMacro0 && s < MSrcLfo0) return s - MSrcMacro0 == NumMacros - 1 ? juce::Colour (0xffff2d55) : juce::Colour (0xffff6a00);
    if (s >= MSrcLfo0 && s < MSrcRnd0) return juce::Colour (0xff2fa8ff);
    if (s >= MSrcRnd0 && s < MSrcEnv0) return juce::Colour (0xffb26bff);
    if (s >= MSrcEnv0 && s < MSrcVelocity) return juce::Colour (0xff3ee8b0);
    return juce::Colour (0xffffd23f);
}

bool sourceIsPerVoice (int s)
{
    return (s >= MSrcEnv0 && s < MSrcVelocity) || s == MSrcVelocity || s == MSrcNote || s == MSrcNoteRandom || (s >= MSrcLfo0 && s < MSrcRnd0);
}

bool sourceIsBipolar (int s) { return s == MSrcPitchBend; }

// =============================================================================
ModShared::ModShared()
{
    for (int i = 0; i < NumLfos; ++i) { tableIndex[i] = 0; lfoPhase[i] = 0.0f; setShape (i, idg::LfoShape::preset (0)); }
    for (auto& v : sourceValue) v = 0.0f;
    for (auto& v : fieldValue01) v = -1.0f;
}

void ModShared::setShape (int lfo, const idg::LfoShape& s)
{
    shapes[lfo] = s;
    const int next = 1 - tableIndex[lfo].load();
    s.render (tables[lfo][next]);
    tableIndex[lfo] = next;
}

int ModShared::addConnection (int src, int param, float depth, bool bipolar)
{
    if (const int existing = findConnection (src, param); existing >= 0) return existing;
    for (int i = 0; i < MaxModSlots; ++i)
        if (slots[i].src.load() < 0)
        {
            slots[i].depth = depth;
            slots[i].bipolar = bipolar;
            slots[i].param = param;
            slots[i].src = src; // en dernier : le slot devient actif une fois complet
            ++version;
            return i;
        }
    return -1;
}

int ModShared::findConnection (int src, int param) const
{
    for (int i = 0; i < MaxModSlots; ++i)
        if (slots[i].src.load() == src && slots[i].param.load() == param) return i;
    return -1;
}

int ModShared::countForSource (int src) const
{
    int c = 0;
    for (auto& s : slots) if (s.src.load() == src) ++c;
    return c;
}

void ModShared::clearAll()
{
    for (auto& s : slots) { s.src = -1; s.param = -1; }
    ++version;
}

// =============================================================================
void ModTargets::init (juce::AudioProcessor& p)
{
    const auto& params = p.getParameters();
    toField.assign ((size_t) params.size(), -1);
    ids.clear();
    std::map<juce::String, int> idToIndex;
    for (int i = 0; i < params.size(); ++i)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[i]))
        {
            ids.add (rp->getParameterID());
            idToIndex[rp->getParameterID()] = i;
        }
        else ids.add ({});

    auto bind = [&] (int field, const juce::String& id)
    {
        auto it = idToIndex.find (id);
        if (it == idToIndex.end()) return;
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[it->second]);
        fields[(size_t) field].param = it->second;
        fields[(size_t) field].prm = rp;
        fields[(size_t) field].range = rp->getNormalisableRange();
        toField[(size_t) it->second] = field;
    };
    static const char* oscKeys[NumOscFields] { "pos", "level", "pan", "semi", "fine", "detune", "blend", "spread", "stretch", "shift", "warp1amt", "warp2amt" };
    for (int o = 0; o < NumOscs; ++o)
        for (int k = 0; k < NumOscFields; ++k) bind (o * NumOscFields + k, oscId (o, oscKeys[k]));
    bind (FSubLevel, P::subLevel);
    bind (FSubPan, P::subPan);
    bind (FNoiseLevel, P::noiseLevel);
    bind (FNoisePan, P::noisePan);
    bind (FNoiseTone, P::noiseTone);
    static const char* fKeys[NumFiltFields] { "cutoff", "reso", "drive", "key", "mix" };
    for (int f = 0; f < 2; ++f)
        for (int k = 0; k < NumFiltFields; ++k) bind (FFilt0 + f * NumFiltFields + k, filtId (f, fKeys[k]));
    bind (FEnvAttack, P::env1Attack);
    bind (FEnvDecay, P::env1Decay);
    bind (FEnvSustain, P::env1Sustain);
    bind (FEnvRelease, P::env1Release);
    bind (FGlide, P::glide);
}

juce::String ModTargets::paramId (int paramIndex) const { return paramIndex >= 0 && paramIndex < ids.size() ? ids[paramIndex] : juce::String(); }
int ModTargets::paramIndexForId (const juce::String& id) const { return ids.indexOf (id); }
} // namespace idgs
