#include "SynthPage.h"

namespace idgs::ui
{
namespace
{
/** Place des composants en ligne, chacun avec la même largeur. */
void row (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps, int padX = 3, int padY = 1)
{
    const int w = r.getWidth() / juce::jmax (1, (int) comps.size());
    for (auto* c : comps) c->setBounds (r.removeFromLeft (w).reduced (padX, padY));
}

void addAll (juce::Component& parent, std::initializer_list<juce::Component*> comps)
{
    for (auto* c : comps) parent.addAndMakeVisible (c);
}
} // namespace

// =============================================================================
// OscPanel
// =============================================================================
OscPanel::OscPanel (IDGemsSynthProcessor& p, int o)
    : Section (o == 0 ? "Osc A" : "Osc B", Theme::accent),
      proc (p), osc (o),
      on (p, oscId (o, "on"), "ON"),
      mode (p, oscId (o, "mode")), table (p, oscId (o, "table")), route (p, oscId (o, "route")),
      display (p, o), unisonView (p, o),
      pos (p, oscId (o, "pos"), "WT Pos"), level (p, oscId (o, "level"), "Level"), pan (p, oscId (o, "pan"), "Pan"),
      oct (p, oscId (o, "oct"), "Oct"), semi (p, oscId (o, "semi"), "Semi"), fine (p, oscId (o, "fine"), "Fine"),
      phase (p, oscId (o, "phase"), "Phase"), phRand (p, oscId (o, "phrand"), "Rand"),
      unison (p, oscId (o, "unison"), "Unison", Theme::blue), detune (p, oscId (o, "detune"), "Detune", Theme::blue),
      blend (p, oscId (o, "blend"), "Blend", Theme::blue), spread (p, oscId (o, "spread"), "Spread", Theme::blue),
      stretch (p, oscId (o, "stretch"), "Stretch", Theme::blue), shift (p, oscId (o, "shift"), "Shift", Theme::blue),
      root (p, oscId (o, "root"), "Root", Theme::blue),
      loop (p, oscId (o, "loop"), "LOOP", Theme::blue),
      warp1 (p, oscId (o, "warp1")), warp1Src (p, oscId (o, "warp1src")),
      warp2 (p, oscId (o, "warp2")), warp2Src (p, oscId (o, "warp2src")),
      warp1Amt (p, oscId (o, "warp1amt"), "Amount"), warp2Amt (p, oscId (o, "warp2amt"), "Amount")
{
    titleWidth = 64;
    dice.setTooltip (IDG_T ("Génère une wavetable aléatoire (formes, harmoniques, bruit, nombre de frames).\nLa table passe en « Personnalisée » et elle est sauvée avec le projet et les presets."));
    load.setTooltip (IDG_T ("Charger un fichier audio. Mode Wavetable : découpé en frames. Mode Sample : joué tel quel. "
                            "Mode Multisample : plusieurs fichiers ou un dossier, répartis sur le clavier.\n"
                            "Tu peux aussi glisser des fichiers depuis l'explorateur / le Finder ou le navigateur d'Ableton."));
    dice.onClick = [this] { proc.randomTable (osc); display.setMessage (IDG_T ("Nouvelle wavetable aléatoire")); };
    load.onClick = [this]
    {
        const int m = (int) proc.getValue (oscId (osc, "mode"));
        chooser = std::make_unique<juce::FileChooser> (IDG_T ("Charger un son"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        int fbFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        if (m == ModeMulti) fbFlags |= juce::FileBrowserComponent::canSelectMultipleItems | juce::FileBrowserComponent::canSelectDirectories;
        juce::Component::SafePointer<OscPanel> safe (this);
        chooser->launchAsync (fbFlags, [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && ! fc.getResults().isEmpty()) safe->loadFiles (fc.getResults());
        });
    };
    addAll (*this, { &on, &mode, &table, &route, &dice, &load, &display, &unisonView, &pos, &level, &pan, &oct, &semi, &fine, &phase, &phRand,
                     &unison, &detune, &blend, &spread, &stretch, &shift, &root, &loop,
                     &warp1, &warp1Src, &warp2, &warp2Src, &warp1Amt, &warp2Amt });
    updateVisibility();
}

void OscPanel::loadFiles (const juce::Array<juce::File>& files)
{
    display.setMessage (proc.dropFiles (osc, files));
}

void OscPanel::filesDropped (const juce::StringArray& files, int, int)
{
    display.setDropHover (false);
    juce::Array<juce::File> list;
    for (auto& f : files) list.add (juce::File (f));
    loadFiles (list);
}

void OscPanel::updateVisibility()
{
    const int m = (int) proc.getValue (oscId (osc, "mode"));
    lastMode = m;
    const bool wt = m == ModeWavetable;
    table.setVisible (wt);
    dice.setVisible (wt);
    stretch.setVisible (wt);
    shift.setVisible (wt);
    root.setVisible (m == ModeSample);
    loop.setVisible (! wt);
    pos.setLabel (wt ? "WT Pos" : "Start");
    resized();
}

void OscPanel::tick()
{
    if ((int) proc.getValue (oscId (osc, "mode")) != lastMode) updateVisibility();
    display.tick();
    unisonView.tick();
}

void OscPanel::paintOverChildren (juce::Graphics& g)
{
    g.setFont (Theme::mono (9.5f, true));
    g.setColour (Theme::textDim);
    g.drawText ("WARP 1", warpLabel1, juce::Justification::centredLeft);
    g.drawText ("WARP 2", warpLabel2, juce::Justification::centredLeft);
    g.drawText ("UNISON", unisonLabel, juce::Justification::centredLeft);
}

void OscPanel::resized()
{
    auto h = header();
    on.setBounds (h.removeFromLeft (36));
    h.removeFromLeft (6);
    mode.setBounds (h.removeFromLeft (112));
    h.removeFromLeft (6);
    route.setBounds (h.removeFromRight (84));
    h.removeFromRight (6);
    load.setBounds (h.removeFromRight (72));
    h.removeFromRight (4);
    if (table.isVisible())
    {
        dice.setBounds (h.removeFromRight (30));
        h.removeFromRight (4);
        table.setBounds (h);
    }

    auto b = body();
    auto bottom = b.removeFromBottom (128);
    auto top = b;
    display.setBounds (top.removeFromLeft (top.getWidth() * 47 / 100));
    top.removeFromLeft (8);
    const int rh = top.getHeight() / 3;
    auto w1 = top.removeFromTop (rh), w2 = top.removeFromTop (rh), r3 = top;
    auto warpRow = [] (juce::Rectangle<int> r, juce::Rectangle<int>& label, juce::ComboBox& m, juce::ComboBox& s, Knob& k)
    {
        label = r.removeFromLeft (46);
        k.setBounds (r.removeFromRight (64));
        auto c = r.withSizeKeepingCentre (r.getWidth(), 22);
        m.setBounds (c.removeFromLeft (c.getWidth() * 55 / 100).reduced (2, 0));
        s.setBounds (c.reduced (2, 0));
    };
    warpRow (w1, warpLabel1, warp1, warp1Src, warp1Amt);
    warpRow (w2, warpLabel2, warp2, warp2Src, warp2Amt);
    if (lastMode == ModeWavetable) row (r3.withTrimmedLeft (46), { &stretch, &shift });
    else
    {
        auto rr = r3.withTrimmedLeft (46);
        if (root.isVisible()) root.setBounds (rr.removeFromLeft (rr.getWidth() / 2).reduced (3, 1));
        loop.setBounds (rr.withSizeKeepingCentre (juce::jmin (70, rr.getWidth()), 22));
    }

    auto k1 = bottom.removeFromTop (64);
    row (k1, { &pos, &level, &pan, &oct, &semi, &fine, &phase, &phRand });
    auto k2 = bottom;
    unisonLabel = k2.removeFromLeft (56);
    row (k2.removeFromLeft (k2.getWidth() * 4 / 7), { &unison, &detune, &blend, &spread });
    unisonView.setBounds (k2.reduced (6, 4));
}

// =============================================================================
// Sub / Noise
// =============================================================================
SubPanel::SubPanel (IDGemsSynthProcessor& p)
    : Section ("Sub", Theme::accent),
      view (p),
      on (p, P::subOn, "ON"), route (p, P::subRoute), shape (p, P::subShape), oct (p, P::subOct),
      level (p, P::subLevel, "Level"), pan (p, P::subPan, "Pan")
{
    titleWidth = 44;
    addAll (*this, { &view, &on, &route, &shape, &oct, &level, &pan });
}

void SubPanel::resized()
{
    auto h = header();
    on.setBounds (h.removeFromLeft (36));
    route.setBounds (h.removeFromRight (84));
    auto b = body();
    auto c = b.removeFromTop (26);
    shape.setBounds (c.removeFromLeft (c.getWidth() / 2).reduced (2, 1));
    oct.setBounds (c.reduced (2, 1));
    b.removeFromTop (4);
    row (b.removeFromBottom (62), { &level, &pan }, 6, 1);
    view.setBounds (b.reduced (0, 2));
}

NoisePanel::NoisePanel (IDGemsSynthProcessor& p)
    : Section ("Noise", Theme::accent),
      types (p),
      on (p, P::noiseOn, "ON"), route (p, P::noiseRoute),
      level (p, P::noiseLevel, "Level"), pan (p, P::noisePan, "Pan"), tone (p, P::noiseTone, "Tone")
{
    titleWidth = 58;
    addAll (*this, { &types, &on, &route, &level, &pan, &tone });
}

void NoisePanel::resized()
{
    auto h = header();
    on.setBounds (h.removeFromLeft (36));
    route.setBounds (h.removeFromRight (84));
    auto b = body();
    types.setBounds (b.removeFromTop (juce::jmin (64, b.getHeight() / 2)).reduced (0, 2));
    b.removeFromTop (4);
    row (b, { &level, &pan, &tone }, 4, 4);
}

// =============================================================================
// Filtres
// =============================================================================
FilterPanel::FilterPanel (IDGemsSynthProcessor& p, int i)
    : Section (i == 0 ? "Filtre 1" : "Filtre 2", Theme::blue),
      index (i), view (p, i),
      on (p, filtId (i, "on"), "ON", Theme::blue), type (p, filtId (i, "type")),
      cutoff (p, filtId (i, "cutoff"), "Cutoff", Theme::blue), reso (p, filtId (i, "reso"), "Reso", Theme::blue),
      drive (p, filtId (i, "drive"), "Drive", Theme::blue), key (p, filtId (i, "key"), "Key", Theme::blue),
      mix (p, filtId (i, "mix"), "Mix", Theme::blue)
{
    titleWidth = 86;
    if (i == 0)
    {
        serial = std::make_unique<ParamCombo> (p, P::filtSerial);
        addAndMakeVisible (*serial);
    }
    addAll (*this, { &view, &on, &type, &cutoff, &reso, &drive, &key, &mix });
}

void FilterPanel::resized()
{
    auto h = header();
    on.setBounds (h.removeFromLeft (36));
    h.removeFromLeft (6);
    type.setBounds (h.removeFromLeft (120));
    if (serial != nullptr) serial->setBounds (h.removeFromRight (100));
    auto b = body();
    row (b.removeFromBottom (62), { &cutoff, &reso, &drive, &key, &mix }, 2, 1);
    view.setBounds (b.reduced (0, 2));
}

// =============================================================================
// Enveloppe / Voix
// =============================================================================
EnvPanel::EnvPanel (IDGemsSynthProcessor& p)
    : Section (IDG_T ("Env 1  //  volume"), Theme::accent),
      view (p),
      delay (p, P::env1Delay, "Delay"), attack (p, P::env1Attack, "Attack"), hold (p, P::env1Hold, "Hold"),
      decay (p, P::env1Decay, "Decay"), sustain (p, P::env1Sustain, "Sustain"), release (p, P::env1Release, "Release"),
      aCurve (p, P::env1ACurve, "A curve", Theme::blue), dCurve (p, P::env1DCurve, "D curve", Theme::blue), rCurve (p, P::env1RCurve, "R curve", Theme::blue)
{
    addAll (*this, { &view, &delay, &attack, &hold, &decay, &sustain, &release, &aCurve, &dCurve, &rCurve });
}

void EnvPanel::resized()
{
    auto b = body();
    auto right = b.removeFromRight (b.getWidth() * 38 / 100);
    view.setBounds (b);
    right.removeFromLeft (6);
    auto r1 = right.removeFromTop (right.getHeight() / 2);
    row (r1, { &delay, &attack, &hold, &decay, &sustain });
    row (right.withTrimmedRight (right.getWidth() / 5), { &release, &aCurve, &dCurve, &rCurve });
}

VoicePanel::VoicePanel (IDGemsSynthProcessor& p)
    : Section ("Voix  //  jeu", Theme::blue),
      mode (p, P::voiceMode), legato (p, P::glideLegato, "LEGATO", Theme::blue),
      voices (p, P::voices, "Voix", Theme::blue), glide (p, P::glide, "Glide", Theme::blue),
      glideCurve (p, P::glideCurve, "Courbe", Theme::blue), velSens (p, P::velSens, IDG_T ("VÉLOCITÉ"), Theme::blue),
      bendRange (p, P::bendRange, "Bend", Theme::blue)
{
    titleWidth = 120;
    addAll (*this, { &mode, &legato, &voices, &glide, &glideCurve, &velSens, &bendRange });
}

void VoicePanel::resized()
{
    auto h = header();
    mode.setBounds (h.removeFromLeft (juce::jmin (110, h.getWidth())));
    auto b = body();
    auto r1 = b.removeFromTop (b.getHeight() / 2);
    row (r1, { &voices, &velSens, &bendRange });
    auto r2 = b;
    legato.setBounds (r2.removeFromRight (r2.getWidth() / 3).withSizeKeepingCentre (juce::jmin (96, r2.getWidth() / 3), 24));
    row (r2, { &glide, &glideCurve });
}

// =============================================================================
// SynthPage
// =============================================================================
SynthPage::SynthPage (IDGemsSynthProcessor& p)
    : oscA (p, 0), oscB (p, 1), sub (p), noise (p), f1 (p, 0), f2 (p, 1), env (p), voice (p)
{
    addAll (*this, { &oscA, &oscB, &sub, &noise, &f1, &f2, &env, &voice });
}

void SynthPage::tick()
{
    oscA.tick();
    oscB.tick();
    sub.tick();
    noise.tick();
    f1.tick();
    f2.tick();
    env.tick();

}

void SynthPage::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    const int gap = 8;
    auto top = r.removeFromTop (r.getHeight() * 43 / 100);
    r.removeFromTop (gap);
    auto mid = r.removeFromTop (r.getHeight() * 54 / 100);
    r.removeFromTop (gap);
    auto bottom = r;

    const int half = (top.getWidth() - gap) / 2;
    oscA.setBounds (top.removeFromLeft (half));
    top.removeFromLeft (gap);
    oscB.setBounds (top);

    const int w = mid.getWidth();
    sub.setBounds (mid.removeFromLeft (w * 15 / 100));
    mid.removeFromLeft (gap);
    noise.setBounds (mid.removeFromLeft (w * 19 / 100));
    mid.removeFromLeft (gap);
    const int fw = (mid.getWidth() - gap) / 2;
    f1.setBounds (mid.removeFromLeft (fw));
    mid.removeFromLeft (gap);
    f2.setBounds (mid);

    voice.setBounds (bottom.removeFromRight (juce::jmax (300, bottom.getWidth() * 25 / 100)));
    bottom.removeFromRight (gap);
    env.setBounds (bottom);
}
} // namespace idgs::ui
