#include "RandomEngine.h"
#include "../PluginProcessor.h"

namespace idgs
{
const juce::StringArray& randomSectionNames()
{
    static const juce::StringArray n { "Oscillateurs", "Filtres", "Enveloppes", "Modulations", "Effets", IDG_T ("Arpège / Séq") };
    return n;
}

// =============================================================================
// Personnalités
// =============================================================================
const Personality::Rule* Personality::find (const juce::String& id) const
{
    for (auto& r : rules)
        if (r.pattern == id || (r.pattern.containsChar ('*') && id.matchesWildcard (r.pattern, true))) return &r;
    return nullptr;
}

namespace
{
Personality::Rule R (const char* pat, float lo, float hi) { Personality::Rule r; r.pattern = pat; r.lo = lo; r.hi = hi; return r; }
Personality::Rule Pr (const char* pat, float prob) { Personality::Rule r; r.pattern = pat; r.prob = prob; return r; }
Personality::Rule W (const char* pat, std::initializer_list<float> w) { Personality::Rule r; r.pattern = pat; r.weights = w; return r; }
} // namespace

std::vector<Personality> Personality::builtIn()
{
    // poids des listes : warps (15) = Off, Sync, Bend+, Bend-, PWM, Asym+, Asym-, Flip, Mirror, Quantize, Remap, FM, AM, RM, Hard sync
    // filtres (13) = LP12, LP24, HP12, HP24, BP12, BP24, N12, N24, Ladder, Comb+, Comb-, Formant, Destructeur
    // moteurs texture (6) = Granulaire, Spectral, Chaos, Résonateur, Bruit sculpté, Data bending
    // bruits (7) = Blanc, Rose, Vinyle, Digital, Vent, Pluie, Radio ; familles FX : 0 glitch, 1 destruction, 2 pitch, 3 filtres, 4 espace, 5 mouvement
    std::vector<Personality> v;
    v.push_back ({ "Neutre", {} });
    v.push_back ({ IDG_T ("Métal"), {
        W ("osc*_warp?", { 1, 2, 0, 0, 0, 1, 1, 0, 0, 0, 0, 4, 2, 4, 3 }), R ("osc*_stretch", 0.55f, 1.0f), R ("osc*_shift", 0.5f, 0.9f),
        W ("f?_type", { 0.5f, 0.5f, 0.5f, 0.5f, 1, 1, 0, 0, 0, 4, 4, 2, 1 }), R ("f?_reso", 0.4f, 0.9f),
        W ("tex_engine?", { 0, 0.5f, 1, 5, 0, 1 }), R ("tex_r_bright", 0.6f, 1.0f), R ("tex_r_decay", 0.3f, 0.8f),
        W ("noise_type", { 1, 0, 0, 3, 0, 0, 2 }), R ("env1_decay", 0.0f, 0.4f), R ("env1_sustain", 0.0f, 0.5f),
        Pr ("fam:1", 0.6f), Pr ("fam:4", 0.35f), Pr ("fam:0", 0.25f) } });
    v.push_back ({ "Organique", {
        W ("osc*_warp?", { 5, 0, 2, 2, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0 }), R ("osc*_stretch", 0.4f, 0.6f), R ("osc*_shift", 0.4f, 0.6f),
        W ("f?_type", { 3, 3, 1, 0, 1, 1, 0, 0, 3, 0, 0, 1, 0 }), R ("f?_reso", 0.0f, 0.45f), R ("f?_drive", 0.0f, 0.3f),
        R ("env*_attack", 0.15f, 0.55f), R ("env*_release", 0.3f, 0.7f), R ("lfo*_rate", 0.0f, 0.35f),
        W ("tex_engine?", { 5, 1, 0, 1, 2, 0 }), W ("noise_type", { 0, 2, 1, 0, 3, 3, 0 }), W ("rnd?_style", { 4, 0, 3, 1 }),
        Pr ("fam:5", 0.5f), Pr ("fam:4", 0.55f), Pr ("fam:0", 0.08f), Pr ("fam:1", 0.1f) } });
    v.push_back ({ IDG_T ("Digital cassé"), {
        W ("osc*_warp?", { 0, 1, 0, 0, 1, 0, 0, 3, 3, 5, 4, 1, 1, 2, 2 }), R ("osc*_warp?amt", 0.4f, 1.0f),
        W ("f?_type", { 0.5f, 0.5f, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0.5f, 4 }), W ("tex_engine?", { 0.5f, 0.5f, 3, 0, 0, 5 }),
        W ("noise_type", { 1, 0, 0, 5, 0, 0, 3 }), R ("lfo*_rate", 0.5f, 1.0f), W ("rnd?_style", { 0, 5, 1, 2 }),
        R ("env1_attack", 0.0f, 0.15f), Pr ("fam:0", 0.85f), Pr ("fam:1", 0.6f), Pr ("fam:4", 0.15f) } });
    v.push_back ({ "Aquatique", {
        W ("f?_type", { 1, 1, 0, 0, 3, 3, 3, 2, 0, 1, 1, 4, 0 }), R ("f?_reso", 0.35f, 0.8f),
        W ("tex_engine?", { 1, 5, 0, 1, 1, 0 }), R ("tex_s_smear", 0.5f, 1.0f), W ("noise_type", { 0, 1, 0, 0, 1, 5, 0 }),
        R ("lfo*_rate", 0.0f, 0.3f), R ("env*_attack", 0.1f, 0.5f), W ("osc*_warp?", { 3, 0, 2, 2, 1, 0, 0, 0, 1, 0, 0, 1, 2, 0, 0 }),
        Pr ("fam:3", 0.6f), Pr ("fam:5", 0.7f), Pr ("fam:4", 0.65f), Pr ("fam:1", 0.05f) } });
    v.push_back ({ IDG_T ("Poussière"), {
        Pr ("noise_on", 0.95f), W ("noise_type", { 0, 1, 6, 1, 1, 1, 2 }), R ("noise_level", 0.4f, 0.9f), R ("noise_tone", 0.1f, 0.6f),
        W ("tex_engine?", { 2, 0, 0, 0, 5, 1 }), R ("tex_n_crackle", 0.5f, 1.0f), W ("f?_type", { 4, 4, 1, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0 }),
        R ("f?_cutoff", 0.35f, 0.75f), R ("env*_attack", 0.05f, 0.4f), Pr ("fam:1", 0.45f), Pr ("fam:4", 0.45f), Pr ("fam:0", 0.15f) } });
    v.push_back ({ "Cosmique", {
        R ("osc*_unison", 0.2f, 0.45f), R ("osc*_detune", 0.3f, 0.8f), R ("osc*_spread", 0.6f, 1.0f),
        W ("tex_engine?", { 4, 4, 1, 1, 1, 0 }), R ("tex_g_size", 0.5f, 1.0f), R ("tex_g_spray", 0.4f, 1.0f),
        R ("env*_attack", 0.3f, 0.7f), R ("env*_release", 0.5f, 0.9f), R ("lfo*_rate", 0.0f, 0.25f),
        W ("f?_type", { 2, 2, 1, 1, 1, 1, 1, 0, 1, 0, 0, 2, 0 }),
        Pr ("fam:4", 0.9f), Pr ("fam:2", 0.4f), Pr ("fam:5", 0.5f), Pr ("fam:1", 0.08f), Pr ("fam:0", 0.08f) } });
    return v;
}

std::vector<Personality> Personality::loadUser()
{
    std::vector<Personality> out;
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("IDGems Synth").getChildFile (IDG_T ("Personnalites"));
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        auto json = juce::JSON::parse (f);
        if (! json.isObject()) continue;
        Personality p;
        p.name = json.getProperty ("name", f.getFileNameWithoutExtension()).toString();
        if (auto* rules = json.getProperty ("rules", {}).getArray())
            for (auto& rv : *rules)
            {
                Personality::Rule r;
                r.pattern = rv.getProperty ("match", "").toString();
                if (r.pattern.isEmpty()) continue;
                r.lo = (float) rv.getProperty ("lo", -1.0);
                r.hi = (float) rv.getProperty ("hi", -1.0);
                r.prob = (float) rv.getProperty ("prob", -1.0);
                if (auto* w = rv.getProperty ("weights", {}).getArray())
                    for (auto& x : *w) r.weights.push_back ((float) x);
                p.rules.push_back (std::move (r));
            }
        out.push_back (std::move (p));
    }
    return out;
}

// =============================================================================
// Moteur
// =============================================================================
namespace
{
juce::RangedAudioParameter* paramAt (IDGemsSynthProcessor& p, int i)
{
    return dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[i]);
}
bool isDiscrete (juce::RangedAudioParameter* p)
{
    return dynamic_cast<juce::AudioParameterFloat*> (p) == nullptr;
}
int rackOfId (const juce::String& id)
{
    for (int k = 0; k < idg::NumRacks; ++k) if (id.startsWith (idg::rackPrefix (k))) return k;
    return -1;
}
} // namespace

RandomEngine::RandomEngine (IDGemsSynthProcessor& p) : proc (p)
{
    personalities = Personality::builtIn();
    for (auto& u : Personality::loadUser())
    {
        bool replaced = false;
        for (auto& b : personalities) if (b.name == u.name) { b = u; replaced = true; }
        if (! replaced) userPersonalities.push_back (std::move (u));
    }
    classify();
    pushRangesToAudio();
    startTimerHz (30);
}

RandomEngine::~RandomEngine() { stopTimer(); }

void RandomEngine::classify()
{
    const auto& params = proc.getParameters();
    meta.assign ((size_t) params.size(), {});
    for (int i = 0; i < params.size(); ++i)
    {
        auto* prm = paramAt (proc, i);
        if (prm == nullptr) continue;
        const auto id = prm->getParameterID();
        auto& m = meta[(size_t) i];
        m.module = id.upToFirstOccurrenceOf ("_", false, false);
        auto range = [&m] (float lo, float hi) { m.defLo = m.lo = lo; m.defHi = m.hi = hi; };
        auto ends = [&id] (const char* s) { return id.endsWith (s); };

        if (const int rk = rackOfId (id); rk >= 0)
        {
            const auto& reg = idg::rackRegistry (rk);
            const int k = reg.find (id);
            if (k < 0) continue;
            const auto& d = reg.all[(size_t) k];
            m.section = SecFx;
            m.rack = rk;
            m.bankIndex = k;
            m.effect = d.effect;
            m.module = d.effect >= 0 ? idg::rackPrefix (rk) + "fx" + juce::String (d.effect) : idg::rackPrefix (rk) + "global";
            m.randomizable = d.randomizable && d.effect >= 0 && ! d.common;
            range (d.rmin, d.rmax);
            continue;
        }
        if (id.startsWith ("osca_") || id.startsWith ("oscb_") || id.startsWith ("sub_") || id.startsWith ("noise_")
            || id.startsWith ("tex_") || id.startsWith ("fp_"))
        {
            m.section = SecOsc;
            m.randomizable = ! (ends ("_root") || ends ("_loop") || id == "osca_on" || ends ("_phase") || id == "fp_loop");
            if (ends ("_level")) range (0.45f, 1.0f);
            else if (ends ("_oct") && id.startsWith ("osc")) range (0.25f, 0.75f);
            else if (ends ("_semi")) range (0.5f, 0.5f);
            else if (ends ("_fine")) range (0.45f, 0.55f);
            else if (ends ("_unison")) range (0.0f, 0.45f);
            else if (ends ("_table")) range (0.0f, juce::jmax (0.0f, 1.0f - 1.0f / (float) juce::jmax (1, tableChoiceNames().size() - 1)));
            else if (ends ("_route")) range (0.0f, 0.67f);
            else if (id == "sub_oct") range (0.0f, 1.0f);
            continue;
        }
        if (id.startsWith ("f1_") || id.startsWith ("f2_") || id == P::filtSerial)
        {
            m.section = SecFilters;
            if (id == P::filtSerial) m.module = "f1";
            m.randomizable = true;
            if (ends ("_cutoff")) range (0.3f, 1.0f);
            else if (ends ("_reso")) range (0.0f, 0.85f);
            else if (ends ("_mix")) range (0.6f, 1.0f);
            continue;
        }
        if (id.startsWith ("env"))
        {
            m.section = SecEnv;
            m.randomizable = true;
            if (ends ("_attack")) range (0.0f, 0.5f);
            else if (ends ("_delay") || ends ("_hold")) range (0.0f, 0.15f);
            else if (ends ("_decay")) range (0.05f, 0.7f);
            else if (ends ("_release")) range (0.05f, 0.65f);
            if (id == "env1_sustain") range (0.2f, 1.0f);
            continue;
        }
        if (id.startsWith ("lfo") || id.startsWith ("rnd"))
        {
            m.section = SecMod;
            m.randomizable = ! ends ("_retrig");
            if (ends ("_rate")) range (0.0f, 0.65f);
            else if (ends ("_mode")) range (0.0f, 0.5f);
            continue;
        }
        if (id.startsWith ("send_") || ((id.startsWith ("bus_") && ends ("_pan")) || id.startsWith ("bus_") && id.containsIgnoreCase ("_to_")))
        {
            m.section = SecFx;
            m.module = "routing";
            m.randomizable = true;
            if (id.startsWith ("bus_") && id.containsIgnoreCase ("_to_")) range (0.0f, 0.4f);
            continue;
        }
        if (id.startsWith ("arp_") || id.startsWith ("seq_") || id.startsWith ("gen_") || id.startsWith ("spray_") || id == "scale_key"
            || id == "scale_type")
        {
            m.section = SecPlay;
            m.randomizable = ! (id == "arp_hold" || id == "gen_auto" || id == "spray_on");
            if (id == "seq_len") range (0.15f, 1.0f);
            continue;
        }
    }
}

void RandomEngine::pushRangesToAudio()
{
    for (int i = 0; i < (int) meta.size(); ++i)
    {
        auto& m = meta[(size_t) i];
        const bool frozen = ! m.randomizable || isFrozen (i);
        if (m.rack >= 0)
        {
            auto& mp = proc.rack (m.rack).bank[m.bankIndex];
            mp.locked = frozen;
            mp.rmin = m.lo;
            mp.rmax = m.hi;
            continue;
        }
        const int f = proc.modTargets.fieldForParam (i);
        if (f >= 0)
        {
            proc.modShared.driftOk[f] = ! frozen && f != FOscSemi && f != FOscFine;
            proc.modShared.fieldLo[f] = m.lo;
            proc.modShared.fieldHi[f] = m.hi;
        }
    }
}

void RandomEngine::setLocked (int i, bool l)
{
    if (i < 0 || i >= (int) meta.size()) return;
    meta[(size_t) i].locked = l;
    pushRangesToAudio();
    changed();
}

juce::Range<float> RandomEngine::getRange (int i) const
{
    if (i < 0 || i >= (int) meta.size()) return { 0.0f, 1.0f };
    return { meta[(size_t) i].lo, meta[(size_t) i].hi };
}

void RandomEngine::setRange (int i, float lo, float hi)
{
    if (i < 0 || i >= (int) meta.size()) return;
    if (hi < lo) std::swap (lo, hi);
    meta[(size_t) i].lo = juce::jlimit (0.0f, 1.0f, lo);
    meta[(size_t) i].hi = juce::jlimit (0.0f, 1.0f, hi);
    pushRangesToAudio();
    changed();
}

void RandomEngine::resetRange (int i)
{
    if (i < 0 || i >= (int) meta.size()) return;
    meta[(size_t) i].lo = meta[(size_t) i].defLo;
    meta[(size_t) i].hi = meta[(size_t) i].defHi;
    pushRangesToAudio();
    changed();
}

bool RandomEngine::hasCustomRange (int i) const
{
    if (i < 0 || i >= (int) meta.size()) return false;
    const auto& m = meta[(size_t) i];
    return std::abs (m.lo - m.defLo) > 1.0e-4f || std::abs (m.hi - m.defHi) > 1.0e-4f;
}

void RandomEngine::setSectionLocked (int s, bool l)
{
    sectionLocked[s] = l;
    pushRangesToAudio();
    changed();
}

void RandomEngine::setModuleLocked (const juce::String& module, bool l)
{
    if (l) lockedModules.addIfNotAlreadyThere (module);
    else lockedModules.removeString (module);
    pushRangesToAudio();
    changed();
}

bool RandomEngine::isFrozen (int i) const
{
    const auto& m = meta[(size_t) i];
    return m.locked || (m.section >= 0 && sectionLocked[m.section]) || lockedModules.contains (m.module)
           || (m.rack >= 0 && lockedModules.contains (idg::rackPrefix (m.rack) + "rack"));
}

bool RandomEngine::selected (int i, const Target& t) const
{
    const auto& m = meta[(size_t) i];
    if (m.section < 0) return false;
    switch (t.scope)
    {
        case Scope::All: return true;
        case Scope::Section: return m.section == t.section;
        case Scope::Module: return m.module == t.module;
        case Scope::FxRack: return m.rack == t.rack;
        case Scope::FxFamily:
            return m.rack == t.rack && m.effect >= 0 && (int) idg::effectDefs()[(size_t) m.effect].family == t.index;
        case Scope::FxModule: return m.rack == t.rack && m.effect == t.index;
    }
    return false;
}

const Personality* RandomEngine::currentPersonality() const
{
    if (userPersonality >= 0 && userPersonality < (int) userPersonalities.size()) return &userPersonalities[(size_t) userPersonality];
    const int k = (int) proc.getValue ("rand_personality");
    return k > 0 && k < (int) personalities.size() ? &personalities[(size_t) k] : nullptr;
}

juce::StringArray RandomEngine::userPersonalityNames() const
{
    juce::StringArray n;
    for (auto& p : userPersonalities) n.add (p.name);
    return n;
}

float RandomEngine::drawValue (int i, juce::Random& r, const Personality* pers, float intensity, float cur)
{
    juce::ignoreUnused (cur);
    auto* prm = paramAt (proc, i);
    const auto& m = meta[(size_t) i];
    float lo = m.lo, hi = m.hi;
    const Personality::Rule* rule = pers != nullptr ? pers->find (prm->getParameterID()) : nullptr;
    if (rule != nullptr && rule->lo >= 0.0f && rule->hi >= 0.0f)
    {
        lo += (rule->lo - lo) * intensity;
        hi += (rule->hi - hi) * intensity;
    }
    if (auto* b = dynamic_cast<juce::AudioParameterBool*> (prm))
    {
        juce::ignoreUnused (b);
        float p = 0.5f;
        if (rule != nullptr && rule->prob >= 0.0f) p += (rule->prob - p) * intensity;
        return r.nextFloat() < p ? 1.0f : 0.0f;
    }
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (prm))
    {
        const int n = c->choices.size();
        if (rule != nullptr && (int) rule->weights.size() == n && n > 1)
        {
            float total = 0.0f;
            std::vector<float> w ((size_t) n);
            for (int k = 0; k < n; ++k)
            {
                const float x = (float) k / (float) (n - 1);
                const float uniform = x >= lo - 1.0e-4f && x <= hi + 1.0e-4f ? 1.0f : 0.0f;
                w[(size_t) k] = uniform * (1.0f - intensity) + rule->weights[(size_t) k] * intensity;
                total += w[(size_t) k];
            }
            if (total > 0.0f)
            {
                float x = r.nextFloat() * total;
                for (int k = 0; k < n; ++k)
                {
                    x -= w[(size_t) k];
                    if (x <= 0.0f) return (float) k / (float) (n - 1);
                }
                return 1.0f;
            }
        }
    }
    return juce::jlimit (0.0f, 1.0f, lo + r.nextFloat() * (hi - lo));
}

void RandomEngine::request (const Target& t, uint32_t seed)
{
    if (seed == 0) seed = (uint32_t) juce::Random::getSystemRandom().nextInt (999999) + 1;
    const int sync = (int) proc.getValue ("rand_sync");
    if (sync == 0 || ! proc.isHostRunning())
    {
        perform (t, seed, proc.getValue ("rand_amount"), proc.getValue ("rand_guard") > 0.5f, labelFor (t));
        return;
    }
    pending.active = true;
    pending.t = t;
    pending.seed = seed;
    pending.counter = syncCounter();
    changed();
}

int64_t RandomEngine::syncCounter() const
{
    static const double beats[] { 1.0, 1.0, 4.0, 8.0, 16.0 };
    const int sync = juce::jlimit (0, 4, (int) proc.getValue ("rand_sync"));
    return (int64_t) std::floor (proc.getBeatPosition() / beats[sync]);
}

juce::String RandomEngine::labelFor (const Target& t) const
{
    switch (t.scope)
    {
        case Scope::All: return IDG_T ("Général");
        case Scope::Section: return randomSectionNames()[t.section];
        case Scope::Module: return t.module.toUpperCase();
        case Scope::FxRack: return idg::rackDisplayName (t.rack);
        case Scope::FxFamily: return idg::rackDisplayName (t.rack) + " " + idg::familyNames()[t.index];
        case Scope::FxModule: return idg::rackDisplayName (t.rack) + " " + idg::effectDefs()[(size_t) t.index].shortName;
    }
    return {};
}

void RandomEngine::setNorm (int i, float v)
{
    if (auto* p = paramAt (proc, i))
        if (std::abs (p->getValue() - v) > 1.0e-6f) p->setValueNotifyingHost (v);
}

uint32_t RandomEngine::perform (const Target& t, uint32_t seed, float amount, bool useGuards, const juce::String& label)
{
    if (seed == 0) seed = (uint32_t) juce::Random::getSystemRandom().nextInt (999999) + 1;
    pending.active = false;
    glide.active = false;
    const auto before = proc.captureState();
    if (history.empty())
    {
        Entry e;
        e.label = IDG_T ("Départ (avant random)");
        e.state = before;
        history.push_back (std::move (e));
    }
    const float glideMs = proc.getValue ("rand_glide");
    lastMessage.clear();

    float beforeDb = -100.0f;
    if (useGuards)
    {
        float gr = 0.0f;
        guardMeasure (beforeDb, gr);
    }

    uint32_t s = seed;
    std::vector<std::pair<int, float>> targets;
    bool accepted = ! useGuards;
    float afterDb = -100.0f;
    const int attempts = useGuards ? 6 : 1;
    for (int a = 0; a < attempts; ++a)
    {
        if (a > 0)
        {
            proc.restoreState (before, true);
            s = s * 1664525u + 1013904223u;
            s = s % 999999u + 1u;
        }
        targets = draw (t, s, amount);
        for (auto& v : targets) setNorm (v.first, v.second);
        if (! useGuards) break;
        float gr = 0.0f;
        const bool ok = guardMeasure (afterDb, gr);
        const bool silent = afterDb < -55.0f;
        const bool saturated = gr < -10.0f;
        if (ok && ! silent && ! saturated) { accepted = true; break; }
        lastMessage = silent ? IDG_T ("Garde-fou : tirage muet rejeté") : IDG_T ("Garde-fou : tirage saturé rejeté");
    }
    if (useGuards && ! accepted) lastMessage = IDG_T ("Garde-fous : aucun tirage propre trouvé, dernier gardé");

    // égalisation du volume entre deux tirages
    if (useGuards && beforeDb > -55.0f && afterDb > -70.0f)
    {
        const float delta = juce::jlimit (-12.0f, 12.0f, beforeDb - afterDb) * 0.8f;
        if (std::abs (delta) > 0.5f)
            proc.setValue (P::masterVol, juce::jlimit (-30.0f, 6.0f, proc.getValue (P::masterVol) + delta));
    }
    seedShown = s;

    // glide : on repart des valeurs d'avant et on glisse vers les valeurs tirées (paramètres continus)
    if (glideMs > 1.0f)
    {
        glide.moves.clear();
        auto beforeParams = before; // valeurs d'avant lues dans l'état sauvegardé
        for (auto& v : targets)
        {
            auto* p = paramAt (proc, v.first);
            if (p == nullptr || isDiscrete (p)) continue;
            auto child = beforeParams.getChildWithProperty ("id", p->getParameterID());
            if (! child.isValid()) continue;
            const float from = p->convertTo0to1 ((float) child.getProperty ("value"));
            glide.moves.push_back ({ v.first, { from, v.second } });
            setNorm (v.first, from);
        }
        if (! glide.moves.empty())
        {
            glide.active = true;
            glide.start = juce::Time::getMillisecondCounterHiRes();
            glide.duration = glideMs;
            glide.label = label;
            glide.seed = s;
            changed();
            return s;
        }
    }
    addHistory (s, label);
    changed();
    return s;
}

std::vector<std::pair<int, float>> RandomEngine::draw (const Target& t, uint32_t seed, float amount)
{
    juce::Random r ((juce::int64) seed);
    const auto* pers = currentPersonality();
    const float intensity = pers != nullptr ? proc.getValue ("rand_intensity") : 0.0f;
    std::vector<std::pair<int, float>> out;

    auto frozen = [this] (int i) { return isFrozen (i); };

    // 1. paramètres
    for (int i = 0; i < (int) meta.size(); ++i)
    {
        const auto& m = meta[(size_t) i];
        if (! m.randomizable || ! selected (i, t) || frozen (i)) continue;
        auto* p = paramAt (proc, i);
        const float cur = p->getValue();
        const float tgt = drawValue (i, r, pers, intensity, cur);
        if (! isDiscrete (p)) out.push_back ({ i, cur + (tgt - cur) * amount });
        else if (r.nextFloat() < amount * (m.section == SecPlay ? 0.6f : 1.0f)) out.push_back ({ i, tgt });
    }

    // 2. effets allumés / éteints (par rack)
    const bool fxScope = t.scope == Scope::All || (t.scope == Scope::Section && t.section == SecFx) || t.scope == Scope::FxRack
                         || t.scope == Scope::FxFamily || t.scope == Scope::FxModule;
    if (fxScope && ! sectionLocked[SecFx])
    {
        const auto& defs = idg::effectDefs();
        for (int rk = 0; rk < idg::NumRacks; ++rk)
        {
            if ((t.scope == Scope::FxRack || t.scope == Scope::FxFamily || t.scope == Scope::FxModule) && rk != t.rack) continue;
            const auto& reg = idg::rackRegistry (rk);
            auto& br = proc.rack (rk);
            auto onIndex = [&] (int fx) { return proc.paramIndex (reg.all[(size_t) reg.fxOn[fx]].id); };
            auto canToggle = [&] (int fx)
            {
                const int pi = onIndex (fx);
                return pi >= 0 && ! br.shared.fxLocked[fx].load() && ! isFrozen (pi);
            };
            auto famProb = [&] (int fx, float base)
            {
                if (pers == nullptr) return base;
                if (auto* rule = pers->find ("fam:" + juce::String ((int) defs[(size_t) fx].family)); rule != nullptr && rule->prob >= 0.0f)
                    return base + (rule->prob - base) * intensity;
                return base;
            };
            if (t.scope == Scope::FxModule)
            {
                if (canToggle (t.index)) out.push_back ({ onIndex (t.index), 1.0f });
                continue;
            }
            // bus : 1 à 4 effets en moyenne ; master : plus sobre
            const float base = (rk == idg::NumRacks - 1 ? 0.6f : 1.0f + 2.5f * amount) / (float) idg::NumFx;
            const bool busUsed = rk == idg::NumRacks - 1 || proc.busIsUsed (rk);
            if (! busUsed && t.scope == Scope::All) continue;
            int count = 0;
            for (int fx = 0; fx < idg::NumFx; ++fx)
            {
                if (t.scope == Scope::FxFamily && (int) defs[(size_t) fx].family != t.index) continue;
                const int pi = onIndex (fx);
                if (pi < 0) continue;
                bool on = paramAt (proc, pi)->getValue() > 0.5f;
                const float pChange = t.scope == Scope::FxFamily ? amount * 0.7f : amount * 0.8f;
                if (canToggle (fx) && r.nextFloat() < pChange)
                    on = r.nextFloat() < famProb (fx, t.scope == Scope::FxFamily ? 0.5f : base);
                if (on) ++count;
                out.push_back ({ pi, on ? 1.0f : 0.0f });
            }
            if (count == 0 && (t.scope == Scope::FxFamily || (t.scope == Scope::FxRack && rk != idg::NumRacks - 1)))
                for (int k = 0; k < 8; ++k)
                {
                    const int fx = r.nextInt (idg::NumFx);
                    if ((t.scope != Scope::FxFamily || (int) defs[(size_t) fx].family == t.index) && canToggle (fx))
                    {
                        out.push_back ({ onIndex (fx), 1.0f });
                        break;
                    }
                }
            // séquenceurs de modules
            for (int fx = 0; fx < idg::NumFx; ++fx)
            {
                if (t.scope == Scope::FxFamily && (int) defs[(size_t) fx].family != t.index) continue;
                auto& sq = br.seq[fx];
                if (sq.locked.load() || ! canToggle (fx) || r.nextFloat() > amount * 0.25f) continue;
                static const int lengths[] { 4, 8, 8, 16, 16, 16, 32 };
                static const int rates[] { 1, 3, 3, 4, 5 };
                sq.enabled = r.nextFloat() < 0.5f;
                sq.length = lengths[r.nextInt (7)];
                sq.rate = rates[r.nextInt (5)];
                const float density = 0.25f + r.nextFloat() * 0.6f;
                for (int k = 0; k < idg::MaxSteps; ++k)
                {
                    sq.on[k] = r.nextFloat() < density;
                    sq.prob[k] = r.nextFloat() < amount * 0.4f ? 0.4f + r.nextFloat() * 0.6f : 1.0f;
                    sq.ratchet[k] = r.nextFloat() < amount * 0.2f ? 2 + r.nextInt (3) : 1;
                }
            }
        }
    }

    // 3. modulations : formes de LFO et profondeur des connexions existantes
    const bool modScope = t.scope == Scope::All || (t.scope == Scope::Section && t.section == SecMod)
                          || (t.scope == Scope::Module && t.module.startsWith ("lfo"));
    if (modScope && ! sectionLocked[SecMod])
    {
        const int numShapes = idg::LfoShape::presetNames().size();
        for (int l = 0; l < NumLfos; ++l)
        {
            const auto mod = "lfo" + juce::String (l + 1);
            if (lockedModules.contains (mod) || (t.scope == Scope::Module && t.module != mod)) continue;
            if (r.nextFloat() < amount * 0.6f) proc.modShared.setShape (l, idg::LfoShape::preset (r.nextInt (numShapes)));
        }
        if (t.scope != Scope::Module)
            for (auto& slot : proc.modShared.slots)
            {
                if (slot.src.load() < 0 || r.nextFloat() > amount * 0.7f) continue;
                const int pi = slot.param.load();
                if (pi >= 0 && pi < (int) meta.size() && meta[(size_t) pi].locked) continue;
                const float d = slot.depth.load();
                const float nd = juce::jlimit (-1.0f, 1.0f, d + (r.nextFloat() * 2.0f - 1.0f) * 0.4f * amount);
                slot.depth = std::abs (nd) < 0.03f ? (d < 0.0f ? -0.05f : 0.05f) : nd;
            }
        ++proc.modShared.version;
    }

    // 4. jeu : nouveau motif de séquenceur
    const bool playScope = t.scope == Scope::All || (t.scope == Scope::Section && t.section == SecPlay);
    if (playScope && ! sectionLocked[SecPlay] && (int) proc.getValue ("play_mode") >= PlaySeq && r.nextFloat() < amount)
        proc.generateNow();

    return out;
}

bool RandomEngine::guardMeasure (float& rmsDb, float& grDb)
{
    rmsDb = -100.0f;
    grDb = 0.0f;
    if (probe == nullptr)
    {
        probe = std::make_unique<IDGemsSynthProcessor>();
        probe->setLightweight();
        probe->setPlayConfigDetails (0, 2, 44100.0, 512);
    }
    probe->restoreState (proc.captureState(), true);
    probe->shareAssetsFrom (proc);
    probe->prepareToPlay (44100.0, 512);
    probe->meterGR = 1.0f;

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double sum = 0.0;
    int64_t count = 0;
    const int blocks = 48; // ~0,56 s
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int note : { 48, 60, 64, 67 }) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        if (b == 40)
            for (int note : { 48, 60, 64, 67 }) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        probe->processBlock (buf, midi);
        if (b >= 4)
            for (int c = 0; c < 2; ++c)
            {
                const float* d = buf.getReadPointer (c);
                for (int k = 0; k < 512; ++k)
                {
                    if (! std::isfinite (d[k])) return false;
                    sum += (double) d[k] * d[k];
                }
                count += 512;
            }
    }
    rmsDb = (float) juce::Decibels::gainToDecibels (std::sqrt (sum / (double) juce::jmax ((int64_t) 1, count)), -100.0);
    grDb = juce::Decibels::gainToDecibels (juce::jmax (1.0e-5f, probe->meterGR.load()), -100.0f);
    return true;
}

void RandomEngine::timerCallback()
{
    if (pending.active && syncCounter() != pending.counter)
    {
        pending.active = false;
        perform (pending.t, pending.seed, proc.getValue ("rand_amount"), proc.getValue ("rand_guard") > 0.5f, labelFor (pending.t));
    }

    if (glide.active)
    {
        const double x = juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - glide.start) / juce::jmax (1.0, glide.duration));
        const float e = (float) (x * x * (3.0 - 2.0 * x));
        for (auto& m : glide.moves) setNorm (m.first, m.second.first + (m.second.second - m.second.first) * e);
        if (x >= 1.0)
        {
            glide.active = false;
            addHistory (glide.seed, glide.label);
            changed();
        }
    }

    // mutations automatiques (calées sur les mesures quand l'hôte joue)
    if (proc.getValue ("rand_mutate") > 0.5f && proc.isHostRunning())
    {
        static const int bars[] { 1, 2, 4, 8, 16 };
        const auto bar = (int64_t) std::floor (proc.getBeatPosition() / 4.0);
        if (lastMutateBar < 0 || bar < lastMutateBar) lastMutateBar = bar;
        if (bar - lastMutateBar >= bars[juce::jlimit (0, 4, (int) proc.getValue ("rand_mutate_every"))])
        {
            lastMutateBar = bar;
            perform ({}, 0, proc.getValue ("rand_mutate_amount"), false, "Mutate");
        }
    }
    else lastMutateBar = -1;

    applyMorph();
}

void RandomEngine::addHistory (uint32_t seed, const juce::String& label)
{
    Entry e;
    e.seed = seed;
    e.label = label;
    e.state = proc.captureState();
    history.push_back (std::move (e));
    while (history.size() > 100)
    {
        auto it = std::find_if (history.begin(), history.end(), [] (const Entry& x) { return ! x.favourite; });
        if (it == history.end()) break;
        history.erase (it);
    }
    cursor = (int) history.size() - 1;
}

void RandomEngine::recall (int index)
{
    if (index < 0 || index >= (int) history.size()) return;
    glide.active = false;
    cursor = index;
    seedShown = history[(size_t) index].seed;
    proc.restoreState (history[(size_t) index].state, true);
    changed();
}

void RandomEngine::toggleFavourite (int index)
{
    if (index < 0 || index >= (int) history.size()) return;
    history[(size_t) index].favourite = ! history[(size_t) index].favourite;
    changed();
}

// ---- snapshots / morph
void RandomEngine::storeSnapshot (int i, float x, float y)
{
    auto& s = snapshots[i];
    s.valid = true;
    s.x = x;
    s.y = y;
    const auto& params = proc.getParameters();
    s.values.resize ((size_t) params.size());
    for (int k = 0; k < params.size(); ++k) s.values[(size_t) k] = params[k]->getValue();
    rebuildMorphList();
    changed();
}

void RandomEngine::clearSnapshot (int i)
{
    snapshots[i] = {};
    rebuildMorphList();
    changed();
}

void RandomEngine::rebuildMorphList()
{
    morphParams.clear();
    for (int i = 0; i < (int) meta.size(); ++i)
        if (meta[(size_t) i].section >= 0) morphParams.push_back (i);
    lastMorphX = lastMorphY = -1.0f;
}

int RandomEngine::numSnapshots() const
{
    int n = 0;
    for (auto& s : snapshots) n += s.valid ? 1 : 0;
    return n;
}

void RandomEngine::applyMorph (bool force)
{
    const float mx = proc.getValue ("morph_x"), my = proc.getValue ("morph_y");
    if (! force && std::abs (mx - lastMorphX) < 1.0e-4f && std::abs (my - lastMorphY) < 1.0e-4f) return;
    const bool first = lastMorphX < 0.0f;
    lastMorphX = mx;
    lastMorphY = my;
    if (numSnapshots() < 2 || (first && ! force)) return;

    float w[NumSnapshots] {};
    float total = 0.0f;
    int nearest = -1;
    float best = 1.0e9f;
    for (int k = 0; k < NumSnapshots; ++k)
    {
        if (! snapshots[k].valid) continue;
        const float d2 = juce::square (snapshots[k].x - mx) + juce::square (snapshots[k].y - my);
        w[k] = 1.0f / (d2 * d2 + 1.0e-6f);
        total += w[k];
        if (d2 < best) { best = d2; nearest = k; }
    }
    for (int i : morphParams)
    {
        if (meta[(size_t) i].locked) continue;
        auto* p = paramAt (proc, i);
        if (p == nullptr) continue;
        float v;
        if (isDiscrete (p)) v = snapshots[nearest].values[(size_t) i];
        else
        {
            v = 0.0f;
            for (int k = 0; k < NumSnapshots; ++k)
                if (snapshots[k].valid) v += w[k] * snapshots[k].values[(size_t) i];
            v /= total;
        }
        if (std::abs (p->getValue() - v) > 1.0e-4f) p->setValueNotifyingHost (v);
    }
}

// ---- état
juce::ValueTree RandomEngine::toTree() const
{
    juce::ValueTree t ("RANDOM");
    const auto& params = proc.getParameters();
    for (int i = 0; i < (int) meta.size(); ++i)
    {
        const auto& m = meta[(size_t) i];
        if (! m.locked && ! hasCustomRange (i)) continue;
        juce::ValueTree p ("P");
        p.setProperty ("id", dynamic_cast<juce::RangedAudioParameter*> (params[i])->getParameterID(), nullptr);
        if (m.locked) p.setProperty ("lock", true, nullptr);
        if (hasCustomRange (i)) { p.setProperty ("lo", m.lo, nullptr); p.setProperty ("hi", m.hi, nullptr); }
        t.appendChild (p, nullptr);
    }
    juce::StringArray secs;
    for (int s = 0; s < NumRandomSections; ++s) if (sectionLocked[s]) secs.add (juce::String (s));
    t.setProperty ("sections", secs.joinIntoString (","), nullptr);
    t.setProperty ("modules", lockedModules.joinIntoString (","), nullptr);
    for (int k = 0; k < NumSnapshots; ++k)
    {
        const auto& s = snapshots[k];
        if (! s.valid) continue;
        juce::ValueTree st ("SNAP");
        st.setProperty ("index", k, nullptr);
        st.setProperty ("x", s.x, nullptr);
        st.setProperty ("y", s.y, nullptr);
        // valeurs par identifiant (robuste aux ajouts de paramètres)
        for (int i = 0; i < (int) s.values.size() && i < params.size(); ++i)
            if (meta[(size_t) i].section >= 0)
                st.setProperty (dynamic_cast<juce::RangedAudioParameter*> (params[i])->getParameterID(), s.values[(size_t) i], nullptr);
        t.appendChild (st, nullptr);
    }
    return t;
}

void RandomEngine::fromTree (const juce::ValueTree& t)
{
    for (int i = 0; i < (int) meta.size(); ++i) { meta[(size_t) i].locked = false; meta[(size_t) i].lo = meta[(size_t) i].defLo; meta[(size_t) i].hi = meta[(size_t) i].defHi; }
    for (auto& s : sectionLocked) s = false;
    lockedModules.clear();
    for (auto& s : snapshots) s = {};
    if (t.isValid())
    {
        for (auto c : t)
        {
            if (c.hasType ("P"))
            {
                const int i = proc.paramIndex (c.getProperty ("id").toString());
                if (i < 0 || i >= (int) meta.size()) continue;
                meta[(size_t) i].locked = c.getProperty ("lock", false);
                if (c.hasProperty ("lo")) { meta[(size_t) i].lo = c.getProperty ("lo"); meta[(size_t) i].hi = c.getProperty ("hi"); }
            }
            else if (c.hasType ("SNAP"))
            {
                const int k = c.getProperty ("index", -1);
                if (k < 0 || k >= NumSnapshots) continue;
                auto& s = snapshots[k];
                s.valid = true;
                s.x = c.getProperty ("x", 0.5f);
                s.y = c.getProperty ("y", 0.5f);
                const auto& params = proc.getParameters();
                s.values.resize ((size_t) params.size());
                for (int i = 0; i < params.size(); ++i)
                {
                    auto id = dynamic_cast<juce::RangedAudioParameter*> (params[i])->getParameterID();
                    s.values[(size_t) i] = c.hasProperty (id) ? (float) c.getProperty (id) : params[i]->getValue();
                }
            }
        }
        for (auto& x : juce::StringArray::fromTokens (t.getProperty ("sections").toString(), ",", ""))
            if (x.isNotEmpty() && x.getIntValue() >= 0 && x.getIntValue() < NumRandomSections) sectionLocked[x.getIntValue()] = true;
        lockedModules = juce::StringArray::fromTokens (t.getProperty ("modules").toString(), ",", "");
        lockedModules.removeEmptyStrings();
    }
    rebuildMorphList();
    pushRangesToAudio();
    changed();
}
} // namespace idgs
