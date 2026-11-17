#include "SynthPage.h"

namespace idgs::ui
{
namespace
{
/** Place des composants en ligne, chacun avec la même largeur. */
void row (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps, int padX = 2, int padY = 1)
{
    const int w = r.getWidth() / juce::jmax (1, (int) comps.size());
    for (auto* c : comps) c->setBounds (r.removeFromLeft (w).reduced (padX, padY));
}

void addAll (juce::Component& parent, std::initializer_list<juce::Component*> comps)
{
    for (auto* c : comps) parent.addAndMakeVisible (c);
}

const char* routeParamFor (int src)
{
    static const char* ids[] { "osca_route", "oscb_route", "sub_route", "noise_route", "tex_route" };
    return ids[src];
}

void setParam (IDGemsSynthProcessor& p, const juce::String& id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    }
}
} // namespace

// =============================================================================
// OscRow
// =============================================================================
OscRow::OscRow (IDGemsSynthProcessor& p, int o)
    : Section (o == 0 ? "Osc A" : "Osc B", Theme::accent),
      proc (p), osc (o),
      on (p, oscId (o, "on"), "ON"),
      mode (p, oscId (o, "mode")), table (p, oscId (o, "table")),
      dice (juce::String (juce::CharPointer_UTF8 ("D\xc3\x89"))),
      display (p, o),
      oct (p, oscId (o, "oct"), "Oct"), semi (p, oscId (o, "semi"), "Semi"), fine (p, oscId (o, "fine"), "Fine"),
      level (p, oscId (o, "level"), "Level"), pan (p, oscId (o, "pan"), "Pan"),
      unison (p, oscId (o, "unison"), "Voix", Theme::blue), detune (p, oscId (o, "detune"), "Detune", Theme::blue),
      blend (p, oscId (o, "blend"), "Blend", Theme::blue), spread (p, oscId (o, "spread"), "Spread", Theme::blue),
      warp1 (p, oscId (o, "warp1")), warp1Src (p, oscId (o, "warp1src")),
      warp2 (p, oscId (o, "warp2")), warp2Src (p, oscId (o, "warp2src")),
      warp1Amt (p, oscId (o, "warp1amt"), "Amount"), warp2Amt (p, oscId (o, "warp2amt"), "Amount"),
      phase (p, oscId (o, "phase"), "Phase", Theme::blue), phRand (p, oscId (o, "phrand"), "Rand", Theme::blue),
      stretch (p, oscId (o, "stretch"), "Stretch", Theme::blue), shift (p, oscId (o, "shift"), "Shift", Theme::blue),
      root (p, oscId (o, "root"), "Root", Theme::blue),
      loop (p, oscId (o, "loop"), "LOOP", Theme::blue)
{
    titleWidth = 64;
    dice.setTooltip (IDG_T ("Génère une wavetable aléatoire (formes, harmoniques, bruit, nombre de frames).\nLa table passe en « Personnalisée » et elle est sauvée avec le projet et les presets."));
    load.setTooltip (IDG_T ("Charger un fichier audio. Wavetable : découpé en frames. Sample : joué tel quel. "
                            "Multisample : plusieurs fichiers ou un dossier, répartis sur le clavier.\n"
                            "Tu peux aussi glisser des fichiers depuis le Finder / l'Explorateur ou le navigateur d'Ableton."));
    dice.onClick = [this] { proc.randomTable (osc); display.setMessage (IDG_T ("Nouvelle wavetable aléatoire")); };
    load.onClick = [this]
    {
        const int m = (int) proc.getValue (oscId (osc, "mode"));
        chooser = std::make_unique<juce::FileChooser> (IDG_T ("Charger un son"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        int fbFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        if (m == ModeMulti) fbFlags |= juce::FileBrowserComponent::canSelectMultipleItems | juce::FileBrowserComponent::canSelectDirectories;
        juce::Component::SafePointer<OscRow> safe (this);
        chooser->launchAsync (fbFlags, [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && ! fc.getResults().isEmpty()) safe->loadFiles (fc.getResults());
        });
    };
    pencil.setClickingTogglesState (true);
    pencil.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    pencil.setTooltip (IDG_T ("Crayon : dessine directement la frame courante de la wavetable (la table devient « Personnalisée »)."));
    pencil.onClick = [this] { display.setPencil (pencil.getToggleState()); };
    addAll (*this, { &pencil, &on, &mode, &table, &dice, &load, &display, &oct, &semi, &fine, &level, &pan,
                     &unison, &detune, &blend, &spread, &warp1, &warp1Src, &warp2, &warp2Src, &warp1Amt, &warp2Amt,
                     &phase, &phRand, &stretch, &shift, &root, &loop });
    updateVisibility();
}

void OscRow::loadFiles (const juce::Array<juce::File>& files) { display.setMessage (proc.dropFiles (osc, files)); }

void OscRow::filesDropped (const juce::StringArray& files, int, int)
{
    display.setDropHover (false);
    juce::Array<juce::File> list;
    for (auto& f : files) list.add (juce::File (f));
    loadFiles (list);
}

void OscRow::updateVisibility()
{
    const int m = (int) proc.getValue (oscId (osc, "mode"));
    lastMode = m;
    const bool wt = m == ModeWavetable;
    table.setVisible (wt);
    dice.setVisible (wt);
    pencil.setVisible (wt);
    if (! wt) { pencil.setToggleState (false, juce::dontSendNotification); display.setPencil (false); }
    stretch.setVisible (wt);
    shift.setVisible (wt);
    root.setVisible (m == ModeSample || m == ModeGranular || m == ModeSpectral);
    loop.setVisible (m == ModeSample || m == ModeMulti);
    resized();
    repaint();
}

void OscRow::tick()
{
    if ((int) proc.getValue (oscId (osc, "mode")) != lastMode) updateVisibility();
    display.tick();
}

void OscRow::paintOverChildren (juce::Graphics& g)
{
    blockTitle (g, blocks[0], "UNISON");
    blockTitle (g, blocks[1], "WARP 1");
    blockTitle (g, blocks[2], "WARP 2");
    blockTitle (g, blocks[3], lastMode == ModeWavetable ? IDG_T ("PHASE / INHARM.") : "PHASE / SAMPLE");
    if (lastMode == ModeGranular || lastMode == ModeSpectral)
    {
        g.setFont (Theme::mono (8.5f));
        g.setColour (Theme::blue.withAlpha (0.8f));
        g.drawText (IDG_T ("réglages fins : onglet TEXTURE"), display.getBounds().translated (0, display.getHeight() - 12).withHeight (12),
                    juce::Justification::centredRight);
    }
}

void OscRow::resized()
{
    auto h = header();
    on.setBounds (h.removeFromLeft (36));
    h.removeFromLeft (6);
    mode.setBounds (h.removeFromLeft (110));

    auto b = body();
    // colonne hauteur / niveau
    auto left = b.removeFromLeft (150);
    row (left.removeFromTop (left.getHeight() / 2), { &oct, &semi, &fine });
    row (left, { &level, &pan }, 10, 1);
    b.removeFromLeft (6);

    // table : sélection au-dessus de l'affichage
    auto disp = b.removeFromLeft (b.getWidth() * 40 / 100);
    auto bar = disp.removeFromTop (22);
    load.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (3);
    if (table.isVisible())
    {
        dice.setBounds (bar.removeFromRight (30));
        bar.removeFromRight (3);
        pencil.setBounds (bar.removeFromRight (62));
        bar.removeFromRight (3);
        table.setBounds (bar);
    }
    disp.removeFromTop (3);
    display.setBounds (disp);
    b.removeFromLeft (8);

    // 4 blocs : unison | warp 1 | warp 2 | phase & inharmonicité
    const int bw = b.getWidth() / 4;
    for (int i = 0; i < 4; ++i) blocks[i] = b.removeFromLeft (bw).withTrimmedLeft (i == 0 ? 0 : 6);
    auto grid = [] (juce::Rectangle<int> r, juce::Component* a, juce::Component* bb, juce::Component* c, juce::Component* d)
    {
        r.removeFromTop (13);
        auto top = r.removeFromTop (r.getHeight() / 2);
        if (a) a->setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (1));
        if (bb) bb->setBounds (top.reduced (1));
        if (c) c->setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (1));
        if (d) d->setBounds (r.reduced (1));
    };
    grid (blocks[0], &unison, &detune, &blend, &spread);
    auto warp = [] (juce::Rectangle<int> r, juce::ComboBox& m, juce::ComboBox& s, Knob& k)
    {
        r.removeFromTop (14);
        m.setBounds (r.removeFromTop (20));
        s.setBounds (r.removeFromBottom (20));
        k.setBounds (r.reduced (4, 2));
    };
    warp (blocks[1], warp1, warp1Src, warp1Amt);
    warp (blocks[2], warp2, warp2Src, warp2Amt);
    if (lastMode == ModeWavetable) grid (blocks[3], &phase, &phRand, &stretch, &shift);
    else
    {
        grid (blocks[3], &phase, &phRand, root.isVisible() ? &root : nullptr, nullptr);
        auto r = blocks[3].withTrimmedTop (13);
        r = r.withTrimmedTop (r.getHeight() / 2);
        auto cell = root.isVisible() ? r.withTrimmedLeft (r.getWidth() / 2) : r;
        if (loop.isVisible()) loop.setBounds (cell.withSizeKeepingCentre (juce::jmin (60, cell.getWidth() - 4), 22));
    }
}

// =============================================================================
// SubNoiseRow
// =============================================================================
SubNoiseRow::SubNoiseRow (IDGemsSynthProcessor& p)
    : Section ("Sub / Noise", Theme::accent),
      subOn (p, P::subOn, "SUB"), noiseOn (p, P::noiseOn, "NOISE"),
      subShape (p, P::subShape), subOct (p, P::subOct), subView (p),
      subLevel (p, P::subLevel, "Level"), subPan (p, P::subPan, "Pan"),
      noiseTypes (p),
      noiseLevel (p, P::noiseLevel, "Level"), noisePan (p, P::noisePan, "Pan"), noiseTone (p, P::noiseTone, "Tone")
{
    titleWidth = 110;
    addAll (*this, { &subOn, &noiseOn, &subShape, &subOct, &subView, &subLevel, &subPan, &noiseTypes, &noiseLevel, &noisePan, &noiseTone });
}

void SubNoiseRow::paintOverChildren (juce::Graphics& g)
{
    g.setColour (Theme::line.withAlpha (0.7f));
    g.drawVerticalLine (split, (float) body().getY(), (float) body().getBottom());
}

void SubNoiseRow::resized()
{
    auto b = getLocalBounds().reduced (10, 6).withTrimmedTop (24);
    auto subArea = b.removeFromLeft (b.getWidth() * 44 / 100);
    split = subArea.getRight() + 5;
    b.removeFromLeft (10);

    // Sub : on + forme / octave | aperçu | level, pan
    auto c = subArea.removeFromLeft (92);
    subOn.setBounds (c.removeFromTop (22));
    c.removeFromTop (3);
    subShape.setBounds (c.removeFromTop (22));
    c.removeFromTop (3);
    subOct.setBounds (c.removeFromTop (22));
    subArea.removeFromLeft (6);
    row (subArea.removeFromRight (110), { &subLevel, &subPan });
    subView.setBounds (subArea.reduced (2, 2));

    // Noise : on | logos | level, pan, tone
    noiseOn.setBounds (b.removeFromLeft (64).withSizeKeepingCentre (64, 22));
    b.removeFromLeft (6);
    row (b.removeFromRight (160), { &noiseLevel, &noisePan, &noiseTone });
    noiseTypes.setBounds (b.reduced (2, 4));
}

// =============================================================================
// RouteButtons
// =============================================================================
bool RouteButtons::sourceIn (int src) const
{
    const int r = (int) proc.getValue (routeParamFor (src));
    return filter == 0 ? (r == RouteF1 || r == RouteBoth) : (r == RouteF2 || r == RouteBoth);
}

void RouteButtons::toggleSource (int src)
{
    const int r = (int) proc.getValue (routeParamFor (src));
    bool inF1 = r == RouteF1 || r == RouteBoth, inF2 = r == RouteF2 || r == RouteBoth;
    if (filter == 0) inF1 = ! inF1; else inF2 = ! inF2;
    const int nr = inF1 && inF2 ? RouteBoth : inF1 ? RouteF1 : inF2 ? RouteF2 : RouteDirect;
    setParam (proc, routeParamFor (src), (float) nr);
}

void RouteButtons::mouseDown (const juce::MouseEvent& e)
{
    const int c = cellAt (e.position.x);
    if (c < NumSendSources) toggleSource (c);
    else setParam (proc, P::filtSerial, proc.getValue (P::filtSerial) < 0.5f ? 1.0f : 0.0f);
    repaint();
}

juce::String RouteButtons::getTooltip()
{
    if (hover == NumSendSources)
        return IDG_T ("Chaînage : allumé = série (le filtre 1 entre dans le filtre 2), éteint = parallèle.");
    return IDG_T ("Allume / éteins les sources qui passent dans ce filtre. Une source dans aucun filtre sort en direct.");
}

void RouteButtons::tick()
{
    float h = proc.getValue (P::filtSerial) * 97.0f;
    for (int s = 0; s < NumSendSources; ++s) h += proc.getValue (routeParamFor (s)) * (float) (s * 13 + 1);
    if (std::abs (h - lastHash) > 1.0e-4f) { lastHash = h; repaint(); }
}

void RouteButtons::paint (juce::Graphics& g)
{
    static const char* names[] { "OSC A", "OSC B", "SUB", "NOISE", "TEX" };
    const float w = (float) getWidth() / NumCells;
    const bool serial = proc.getValue (P::filtSerial) < 0.5f;
    for (int i = 0; i < NumCells; ++i)
    {
        auto cell = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (2.0f, 1.0f);
        const bool active = i < NumSendSources ? sourceIn (i) : serial;
        const auto c = i < NumSendSources ? Theme::accent : Theme::blue;
        g.setColour (active ? c.withAlpha (0.22f) : (i == hover ? Theme::panelHi : Theme::bg.withAlpha (0.6f)));
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (active ? c : Theme::line);
        g.drawRoundedRectangle (cell, 3.0f, 1.0f);
        g.setFont (Theme::mono (9.5f, active));
        g.setColour (active ? c.brighter (0.3f) : Theme::textDim);
        const juce::String t = i < NumSendSources ? juce::String (names[i]) : (filter == 0 ? juce::String ("-> F2") : juce::String ("F1 ->"));
        g.drawText (t, cell, juce::Justification::centred);
    }
}

// =============================================================================
// FilterPanel
// =============================================================================
FilterPanel::FilterPanel (IDGemsSynthProcessor& p, int i)
    : Section (i == 0 ? "Filtre 1" : "Filtre 2", Theme::blue),
      index (i), view (p, i), routes (p, i),
      on (p, filtId (i, "on"), "ON", Theme::blue), type (p, filtId (i, "type")),
      cutoff (p, filtId (i, "cutoff"), "Cutoff", Theme::blue), reso (p, filtId (i, "reso"), "Reso", Theme::blue),
      drive (p, filtId (i, "drive"), "Drive", Theme::blue), mix (p, filtId (i, "mix"), "Mix", Theme::blue),
      key (p, filtId (i, "key"), "Key trk", Theme::blue)
{
    titleWidth = 86;
    addAll (*this, { &view, &routes, &on, &type, &cutoff, &reso, &drive, &mix, &key });
}

void FilterPanel::resized()
{
    auto h = header();
    on.setBounds (h.removeFromLeft (36));
    h.removeFromLeft (6);
    type.setBounds (h.removeFromLeft (juce::jmin (150, h.getWidth())));
    auto b = body();
    row (b.removeFromBottom (58), { &cutoff, &reso, &drive, &mix, &key }, 2, 0);
    b.removeFromBottom (4);
    routes.setBounds (b.removeFromBottom (22));
    b.removeFromBottom (4);
    view.setBounds (b);
}

// =============================================================================
// EnvSection
// =============================================================================
EnvSection::Tab::Tab (IDGemsSynthProcessor& p, int e)
    : env (e), editor (p, e),
      delay (p, envId (e, "delay"), "Delay"), attack (p, envId (e, "attack"), "Attack"), hold (p, envId (e, "hold"), "Hold"),
      decay (p, envId (e, "decay"), "Decay"), sustain (p, envId (e, "sustain"), "Sustain"), release (p, envId (e, "release"), "Release")
{
    addAll (*this, { &editor, &delay, &attack, &hold, &decay, &sustain, &release });
}

void EnvSection::Tab::resized()
{
    auto r = getLocalBounds();
    row (r.removeFromBottom (58), { &delay, &attack, &hold, &decay, &sustain, &release }, 2, 0);
    r.removeFromBottom (4);
    editor.setBounds (r);
}

void EnvSection::Tab::paintOverChildren (juce::Graphics& g)
{
    if (env == 0) return;
    g.setFont (Theme::mono (9.0f));
    g.setColour (Theme::blue.withAlpha (0.8f));
    g.drawText (IDG_T ("source de modulation : attrape la pastille de l'onglet et glisse-la sur un potard"),
                editor.getBounds().reduced (8, 4), juce::Justification::bottomLeft);
}

EnvSection::EnvSection (IDGemsSynthProcessor& p) : proc (p)
{
    for (int e = 0; e < NumEnvelopes; ++e)
    {
        auto* t = tabs.add (new Tab (p, e));
        addChildComponent (t);
        addChildComponent (handles.add (new ModSourceHandle (p, MSrcEnv0 + e)));
    }
    tabs[0]->setVisible (true);
    updateVisibleCount();
    setTooltip (IDG_T ("ENV 1 règle le volume des voix. Toutes les enveloppes sont des sources de modulation : attrape la pastille d'un onglet.\n"
                       "Une nouvelle enveloppe se débloque quand toutes celles affichées sont assignées (jusqu'à 8)."));
}

void EnvSection::updateVisibleCount()
{
    // déblocage façon Vital : une enveloppe de plus dès que toutes les enveloppes visibles servent
    int n = VisibleEnvelopes;
    while (n < NumEnvelopes)
    {
        bool all = true;
        for (int e = 1; e < n; ++e) if (! proc.modShared.sourceUsed (MSrcEnv0 + e)) { all = false; break; }
        if (! all) break;
        ++n;
    }
    if (n != visibleCount)
    {
        visibleCount = n;
        resized();
        repaint();
    }
    for (int e = 0; e < NumEnvelopes; ++e) handles[e]->setVisible (e < visibleCount);
}

juce::Rectangle<int> EnvSection::tabRect (int i) const
{
    auto a = tabArea().reduced (0, 6);
    const int rows = visibleCount < NumEnvelopes ? visibleCount + 1 : visibleCount;
    const int h = a.getHeight() / juce::jmax (1, rows);
    return a.withTrimmedTop (h * i).withHeight (h);
}

void EnvSection::paint (juce::Graphics& g)
{
    Theme::panelFrame (g, getLocalBounds().toFloat().reduced (1.0f), Theme::accent);
    const int rows = visibleCount < NumEnvelopes ? visibleCount + 1 : visibleCount;
    for (int i = 0; i < rows; ++i)
    {
        auto r = tabRect (i).reduced (6, 2).toFloat();
        const bool sel = i == selected;
        if (i < visibleCount)
        {
            g.setColour (sel ? Theme::accent.withAlpha (0.2f) : Theme::bg.withAlpha (0.4f));
            g.fillRect (r);
            if (sel) { g.setColour (Theme::accent); g.fillRect (r.withWidth (3.0f)); }
            g.setFont (Theme::mono (10.5f, sel));
            g.setColour (sel ? Theme::text : Theme::textDim);
            g.drawText ("ENV " + juce::String (i + 1), r.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
            if (i == 0)
            {
                g.setFont (Theme::mono (7.5f));
                g.setColour (Theme::accent.withAlpha (0.8f));
                g.drawText ("VOL", r.withTrimmedLeft (6.0f).withTrimmedTop (r.getHeight() * 0.62f), juce::Justification::centredLeft);
            }
        }
        else
        {
            g.setColour (Theme::line);
            g.drawRect (r, 1.0f);
            g.setFont (Theme::mono (8.5f));
            g.setColour (Theme::textDim.withAlpha (0.7f));
            g.drawText ("+ ENV " + juce::String (visibleCount + 1), r, juce::Justification::centred);
        }
    }
}

void EnvSection::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < visibleCount; ++i)
        if (tabRect (i).contains (e.getPosition()))
        {
            selected = i;
            for (int t = 0; t < tabs.size(); ++t) tabs[t]->setVisible (t == selected);
            repaint();
        }
}

void EnvSection::resized()
{
    auto r = getLocalBounds().reduced (8, 8);
    r.removeFromLeft (tabArea().getWidth());
    for (auto* t : tabs) t->setBounds (r);
    for (int e = 0; e < NumEnvelopes; ++e)
    {
        auto t = tabRect (e).reduced (6, 2);
        handles[e]->setBounds (t.removeFromRight (juce::jmin (18, t.getHeight())).reduced (1));
    }
}

void EnvSection::tick()
{
    tabs[selected]->editor.tick();
    updateVisibleCount();
}

// =============================================================================
// FuturePanel
// =============================================================================
FuturePanel::FuturePanel (const juce::String& t, juce::StringArray tn, juce::StringArray c, juce::String n)
    : Section (t, Theme::textDim), tabNames (std::move (tn)), cells (std::move (c)), note (std::move (n))
{
    titleWidth = 140;
}

void FuturePanel::paint (juce::Graphics& g)
{
    Theme::panelFrame (g, getLocalBounds().toFloat().reduced (1.0f), Theme::textDim, 0.6f);
    Theme::sectionTitle (g, title, getLocalBounds().toFloat().reduced (10.0f, 7.0f), Theme::textDim);
    auto b = body();
    auto noteArea = b.removeFromBottom (16);
    if (! tabNames.isEmpty())
    {
        auto tabs = b.removeFromLeft (62);
        const int h = juce::jmin (26, tabs.getHeight() / tabNames.size());
        g.setFont (Theme::mono (9.5f));
        for (auto& t : tabNames)
        {
            auto r = tabs.removeFromTop (h).reduced (2).toFloat();
            g.setColour (Theme::bg.withAlpha (0.4f));
            g.fillRect (r);
            g.setColour (Theme::textDim.withAlpha (0.6f));
            g.drawText (t, r, juce::Justification::centred);
        }
        b.removeFromLeft (8);
    }
    if (! cells.isEmpty())
    {
        const int cols = juce::jmin (5, cells.size());
        const int rows = (cells.size() + cols - 1) / cols;
        auto grid = b;
        const int cw = grid.getWidth() / cols, ch = grid.getHeight() / rows;
        g.setFont (Theme::mono (9.0f));
        for (int i = 0; i < cells.size(); ++i)
        {
            auto r = juce::Rectangle<int> (grid.getX() + (i % cols) * cw, grid.getY() + (i / cols) * ch, cw, ch).reduced (2).toFloat();
            g.setColour (Theme::bg.withAlpha (0.45f));
            g.fillRect (r);
            g.setColour (Theme::line);
            g.drawRect (r, 1.0f);
            g.setColour (Theme::textDim.withAlpha (0.75f));
            g.drawText (cells[i], r, juce::Justification::centred);
        }
    }
    else
    {
        // aperçu d'une forme de LFO dessinée (pointillés)
        juce::Path p;
        auto r = b.reduced (6, 10).toFloat();
        for (int i = 0; i <= 64; ++i)
        {
            const float t = (float) i / 64.0f;
            const float v = 0.5f + 0.4f * std::sin (t * juce::MathConstants<float>::twoPi) * (1.0f - t * 0.5f);
            if (i == 0) p.startNewSubPath (r.getX() + t * r.getWidth(), r.getBottom() - v * r.getHeight());
            else p.lineTo (r.getX() + t * r.getWidth(), r.getBottom() - v * r.getHeight());
        }
        juce::Path dashed;
        const float dashes[] { 4.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (Theme::textDim.withAlpha (0.5f));
        g.fillPath (dashed);
    }
    g.setFont (Theme::mono (10.0f));
    g.setColour (Theme::textDim);
    g.drawText (note, noteArea, juce::Justification::centredLeft);
}

// =============================================================================
// VoicePanel
// =============================================================================
VoicePanel::VoicePanel (IDGemsSynthProcessor& p)
    : Section ("Voix", Theme::blue),
      mode (p, P::voiceMode), legato (p, P::glideLegato, "LEGATO", Theme::blue),
      voices (p, P::voices, "Voix", Theme::blue), bendRange (p, P::bendRange, "Bend", Theme::blue),
      velSens (p, P::velSens, IDG_T ("VÉLOCITÉ"), Theme::blue), glide (p, P::glide, "Glide", Theme::blue),
      glideCurve (p, P::glideCurve, "Courbe", Theme::blue)
{
    titleWidth = 56;
    addAll (*this, { &mode, &legato, &voices, &bendRange, &velSens, &glide, &glideCurve });
}

void VoicePanel::resized()
{
    auto h = header();
    mode.setBounds (h.removeFromLeft (100));
    h.removeFromLeft (6);
    legato.setBounds (h.removeFromLeft (70));
    row (body(), { &voices, &bendRange, &velSens, &glide, &glideCurve }, 2, 0);
}

// =============================================================================
// SynthPage
// =============================================================================
SynthPage::SynthPage (IDGemsSynthProcessor& p)
    : oscA (p, 0), oscB (p, 1), subNoise (p), f1 (p, 0), f2 (p, 1), envs (p), lfo (p), sources (p), voice (p)
{
    addAll (*this, { &oscA, &oscB, &subNoise, &f1, &f2, &envs, &lfo, &sources, &voice });
    // dés de random par bloc
    using T = RandomEngine::Target;
    auto mod = [] (juce::String m) { return [m] { T t; t.scope = RandomEngine::Scope::Module; t.module = m; return t; }; };
    auto sec = [] (int s) { return [s] { T t; t.scope = RandomEngine::Scope::Section; t.section = s; return t; }; };
    dice.add (new DiceButton (p, mod ("osca")))->attachTo (oscA);
    dice.add (new DiceButton (p, mod ("oscb")))->attachTo (oscB);
    dice.add (new DiceButton (p, mod ("sub")))->attachTo (subNoise, 32);
    dice.add (new DiceButton (p, mod ("noise")))->attachTo (subNoise);
    dice.add (new DiceButton (p, mod ("f1")))->attachTo (f1);
    dice.add (new DiceButton (p, mod ("f2")))->attachTo (f2);
    dice.add (new DiceButton (p, sec (SecEnv)))->attachTo (envs, 8, 4);
    dice.add (new DiceButton (p, sec (SecMod)))->attachTo (sources);
}

void SynthPage::tick()
{
    oscA.tick();
    oscB.tick();
    subNoise.tick();
    f1.tick();
    f2.tick();
    envs.tick();
    lfo.tick();
}

void SynthPage::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    const int gap = 6;
    auto right = r.removeFromRight (r.getWidth() * 37 / 100);
    r.removeFromRight (gap);

    // centre : oscillateurs empilés, puis les deux filtres
    const int oscH = r.getHeight() * 25 / 100;
    oscA.setBounds (r.removeFromTop (oscH));
    r.removeFromTop (gap);
    oscB.setBounds (r.removeFromTop (oscH));
    r.removeFromTop (gap);
    subNoise.setBounds (r.removeFromTop (r.getHeight() * 28 / 100));
    r.removeFromTop (gap);
    f1.setBounds (r.removeFromLeft ((r.getWidth() - gap) / 2));
    r.removeFromLeft (gap);
    f2.setBounds (r);

    // droite : enveloppes, LFO, random / jeu, voix
    envs.setBounds (right.removeFromTop (right.getHeight() * 38 / 100));
    right.removeFromTop (gap);
    voice.setBounds (right.removeFromBottom (100));
    right.removeFromBottom (gap);
    sources.setBounds (right.removeFromBottom (right.getHeight() * 45 / 100));
    right.removeFromBottom (gap);
    lfo.setBounds (right);
}

// =============================================================================
// MacroColumn
// =============================================================================
MacroColumn::MacroColumn (IDGemsSynthProcessor& p)
{
    for (int m = 0; m < NumMacros; ++m)
    {
        const bool chaos = m == NumMacros - 1;
        auto* k = knobs.add (new Knob (p, macroId (m), chaos ? "Chaos" : "Macro " + juce::String (m + 1), chaos ? Theme::red : Theme::accent));
        addAndMakeVisible (k);
        addAndMakeVisible (handles.add (new ModSourceHandle (p, MSrcMacro0 + m)));
    }
}

void MacroColumn::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    Theme::panelFrame (g, r, Theme::accent, 0.8f);
    const float h = r.getHeight() / NumMacros;
    g.setColour (Theme::line.withAlpha (0.6f));
    for (int m = 1; m < NumMacros; ++m) g.drawHorizontalLine ((int) (r.getY() + h * (float) m), r.getX() + 8.0f, r.getRight() - 8.0f);
}

void MacroColumn::resized()
{
    auto r = getLocalBounds().reduced (6, 8);
    const int h = r.getHeight() / NumMacros;
    for (int m = 0; m < knobs.size(); ++m)
    {
        auto cell = r.removeFromTop (h);
        handles[m]->setBounds (cell.getRight() - 16, cell.getY() + 2, 16, 16);
        knobs[m]->setBounds (cell.reduced (6, 6));
    }
}
} // namespace idgs::ui
