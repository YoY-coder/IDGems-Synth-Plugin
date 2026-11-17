#include "TexturePage.h"

namespace idgs::ui
{
namespace
{
const juce::Colour texCol { 0xff33e0c0 };

void frame (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c, float alpha = 1.0f)
{
    Theme::panelFrame (g, r.toFloat().reduced (1.0f), c, alpha);
    Theme::sectionTitle (g, title, r.toFloat().reduced (10.0f, 7.0f), c);
}
} // namespace

// =============================================================================
// SoundSlot
// =============================================================================
SoundSlot::SoundSlot (IDGemsSynthProcessor& p, int s, const juce::String& t, const juce::String& h)
    : proc (p), slot (s), title (t), hint (h)
{
    setTooltip (hint);
    button.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (IDG_T ("Charger"), juce::File(), slot == IDGemsSynthProcessor::AssetData ? "*" : "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<SoundSlot> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && ! fc.getResults().isEmpty()) safe->load (fc.getResults());
        });
    };
    addAndMakeVisible (button);
    startTimerHz (5);
}

void SoundSlot::load (const juce::Array<juce::File>& files)
{
    message = proc.dropFiles (slot, files);
    overview.clear();
    repaint();
}

void SoundSlot::filesDropped (const juce::StringArray& files, int, int)
{
    hover = false;
    juce::Array<juce::File> list;
    for (auto& f : files) list.add (juce::File (f));
    load (list);
}

void SoundSlot::timerCallback()
{
    if (proc.assetsVersion.load() != lastVersion) { lastVersion = proc.assetsVersion.load(); overview.clear(); repaint(); }
}

void SoundSlot::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Theme::bg.withAlpha (0.7f));
    g.fillRect (r);
    g.setColour (hover ? texCol : Theme::line);
    g.drawRect (r, hover ? 2.0f : 1.0f);
    g.setFont (Theme::mono (9.5f, true));
    g.setColour (texCol);
    g.drawText (title, r.reduced (6.0f, 3.0f), juce::Justification::topLeft);
    const auto* set = proc.currentSamples (slot);
    auto plot = r.reduced (6.0f, 18.0f).withTrimmedBottom (8.0f);
    if (set != nullptr && ! set->zones.empty())
    {
        const auto& z = *set->zones[0];
        const int cols = juce::jmax (8, (int) plot.getWidth());
        if ((int) overview.size() != cols)
        {
            overview.assign ((size_t) cols, 0.0f);
            const int len = z.length();
            for (int c = 0; c < cols; ++c)
            {
                const int a = (int) ((juce::int64) c * len / cols), b = (int) ((juce::int64) (c + 1) * len / cols);
                float pk = 0.0f;
                for (int i = a; i < juce::jmin (b, len); i += juce::jmax (1, (b - a) / 32)) pk = juce::jmax (pk, std::abs (z.data.getSample (0, i)));
                overview[(size_t) c] = pk;
            }
        }
        float mx = 1.0e-6f;
        for (auto v : overview) mx = juce::jmax (mx, v);
        g.setColour (texCol.withAlpha (0.8f));
        for (int c = 0; c < cols; ++c)
        {
            const float h = overview[(size_t) c] / mx * plot.getHeight() * 0.48f;
            g.fillRect (plot.getX() + (float) c, plot.getCentreY() - h, 1.0f, juce::jmax (1.0f, 2.0f * h));
        }
        g.setFont (Theme::mono (9.0f));
        g.setColour (Theme::text);
        g.drawText (set->name, r.reduced (6.0f, 3.0f), juce::Justification::bottomLeft);
    }
    else
    {
        g.setFont (Theme::mono (9.5f));
        g.setColour (Theme::textDim);
        g.drawFittedText (hint, plot.toNearestInt(), juce::Justification::centred, 3);
    }
    if (message.isNotEmpty())
    {
        g.setColour (Theme::accent);
        g.setFont (Theme::mono (8.5f));
        g.drawText (message, r.reduced (6.0f, 3.0f), juce::Justification::topRight);
    }
}

void SoundSlot::resized()
{
    button.setBounds (getLocalBounds().reduced (4).removeFromTop (18).removeFromRight (70));
}

// =============================================================================
// EnginePanel
// =============================================================================
EnginePanel::EnginePanel (IDGemsSynthProcessor& p, int e) : proc (p), engine (e)
{
    auto knob = [&] (const char* key, const juce::String& label) { controls.add (new Knob (p, texId (key), label, texCol)); };
    auto combo = [&] (const char* key) { controls.add (new ParamCombo (p, texId (key))); };
    auto toggle = [&] (const char* key, const juce::String& label) { controls.add (new ParamToggle (p, texId (key), label, texCol)); };
    switch (e)
    {
        case TexGranular:
            knob ("g_pos", "Position"); knob ("g_size", "Taille"); knob ("g_density", IDG_T ("Densité")); knob ("g_spray", "Spray");
            knob ("g_pitchrand", "Pitch rnd"); knob ("g_panrand", "Pan rnd"); knob ("g_reverse", "Reverse"); knob ("g_shape", "Forme");
            toggle ("g_freeze", "FREEZE"); combo ("g_source");
            break;
        case TexSpectral:
            knob ("s_speed", "Vitesse"); knob ("s_smear", "Smear"); knob ("s_shift", "Shift"); knob ("s_stretch", "Stretch");
            knob ("s_harm", "Harmoniques"); knob ("s_morph", "Morph 1>2"); knob ("s_scramble", "Scramble"); toggle ("s_freeze", "FREEZE");
            break;
        case TexChaos: combo ("c_type"); knob ("c_amount", IDG_T ("Désordre")); knob ("c_rate", "Vitesse"); break;
        case TexResonator: combo ("r_type"); combo ("r_exciter"); knob ("r_decay", IDG_T ("Résonance")); knob ("r_bright", IDG_T ("Brillance")); knob ("r_excite", "Excitation"); break;
        case TexNoise: knob ("n_color", "Couleur"); knob ("n_density", IDG_T ("Densité")); knob ("n_crackle", "Crackle"); knob ("n_focus", "Focus note"); break;
        default: knob ("d_offset", "Position"); combo ("d_format"); knob ("d_rate", "Vitesse"); break;
    }
    for (auto* c : controls) addAndMakeVisible (c);
    tick();
}

void EnginePanel::tick()
{
    const bool on = proc.getValue (texId ("on")) > 0.5f;
    const int e1 = (int) proc.getValue (texId ("engine1")), e2 = (int) proc.getValue (texId ("engine2"));
    const float m = proc.getValue (texId ("morph"));
    const int state = (on ? 1 : 0) + (e1 == engine ? 2 : 0) + (e2 == engine && m > 0.001f ? 4 : 0);
    if (state != lastState) { lastState = state; repaint(); }
}

void EnginePanel::paint (juce::Graphics& g)
{
    const bool used = (lastState & 6) != 0;
    const bool on = (lastState & 1) != 0;
    juce::String t = texEngineNames()[engine];
    if (lastState & 2) t += "  //  MOTEUR 1";
    if (lastState & 4) t += "  //  MOTEUR 2";
    frame (g, getLocalBounds(), t, used && on ? texCol : Theme::textDim, used ? 1.0f : 0.5f);
}

void EnginePanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6).withTrimmedTop (24);
    const int n = controls.size();
    const int cols = n > 6 ? (n + 1) / 2 : n;
    const int rows = (n + cols - 1) / cols;
    const int cw = r.getWidth() / juce::jmax (1, cols), ch = r.getHeight() / juce::jmax (1, rows);
    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<int> (r.getX() + (i % cols) * cw, r.getY() + (i / cols) * ch, cw, ch).reduced (2, 1);
        auto* c = controls[i];
        if (dynamic_cast<juce::ComboBox*> (c) != nullptr || dynamic_cast<juce::TextButton*> (c) != nullptr)
            c->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 22));
        else c->setBounds (cell);
    }
}

// =============================================================================
// FingerprintView
// =============================================================================
void FingerprintView::timerCallback()
{
    if (proc.assetsVersion.load() != lastVersion) { lastVersion = proc.assetsVersion.load(); image = {}; }
    repaint();
}

void FingerprintView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg.withAlpha (0.8f));
    g.fillRect (r);
    const auto* set = proc.currentSamples (IDGemsSynthProcessor::AssetFingerprint);
    const FingerprintData* d = set != nullptr && ! set->zones.empty() ? set->zones[0]->fingerprint.get() : nullptr;
    if (d == nullptr)
    {
        g.setFont (Theme::mono (10.0f));
        g.setColour (Theme::textDim);
        g.drawText (IDG_T ("Glisse un son « empreinte » (voix, objet...) dans l'emplacement à gauche"), r, juce::Justification::centred);
        return;
    }
    if (image.isNull())
    {
        // sonagramme des bandes (une colonne par trame)
        const int w = juce::jmin (d->numFrames, 600), h = FingerprintData::NumBands;
        image = juce::Image (juce::Image::RGB, w, h, true);
        for (int x = 0; x < w; ++x)
        {
            const int f = x * d->numFrames / w;
            for (int b = 0; b < h; ++b)
            {
                const float e = std::sqrt (d->env[(size_t) f * FingerprintData::NumBands + (size_t) b]);
                image.setPixelAt (x, h - 1 - b, Theme::bg.interpolatedWith (texCol, e).interpolatedWith (juce::Colours::white, e * e * 0.5f));
            }
        }
    }
    g.drawImage (image, r, juce::RectanglePlacement::stretchToFit);
}

// =============================================================================
// TexturePage
// =============================================================================
TexturePage::TexturePage (IDGemsSynthProcessor& p)
    : proc (p),
      on (p, texId ("on"), "ON", texCol), engine1 (p, texId ("engine1")), engine2 (p, texId ("engine2")),
      morph (p, texId ("morph"), "Morph", texCol), level (p, texId ("level"), "Level", texCol), pan (p, texId ("pan"), "Pan", texCol),
      oct (p, texId ("oct"), "Oct", texCol), semi (p, texId ("semi"), "Semi", texCol), root (p, texId ("root"), "Root", texCol),
      sound1 (p, IDGemsSynthProcessor::AssetTex1, "SON 1", IDG_T ("Glisse un son ici : source du granulaire et du spectral.")),
      sound2 (p, IDGemsSynthProcessor::AssetTex2, IDG_T ("SON 2 (MORPH SPECTRAL)"), IDG_T ("Second son : le spectral peut morpher du son 1 vers le son 2.")),
      data (p, IDGemsSynthProcessor::AssetData, "DATA BENDING", IDG_T ("Glisse N'IMPORTE QUEL fichier (image, .exe, .pdf...) : ses octets sont lus comme de l'audio brut.")),
      fpSlot (p, IDGemsSynthProcessor::AssetFingerprint, "EMPREINTE", IDG_T ("Son dont l'enveloppe spectrale sera plaquée sur le synthé (voix, objet, texture).")),
      fpOn (p, "fp_on", "ON", texCol), fpLoop (p, "fp_loop", "LOOP", texCol), fpRes (p, "fp_res"),
      fpAmount (p, "fp_amount", IDG_T ("Quantité"), texCol), fpShift (p, "fp_shift", "Formants", texCol), fpSpeed (p, "fp_speed", "Vitesse", texCol),
      fpView (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &on, &engine1, &engine2, &morph, &level, &pan, &oct, &semi, &root,
                                                                         &sound1, &sound2, &data, &fpSlot, &fpOn, &fpLoop, &fpRes, &fpAmount,
                                                                         &fpShift, &fpSpeed, &fpView })
        addAndMakeVisible (c);
    for (int e = 0; e < NumTexEngines; ++e) addAndMakeVisible (engines.add (new EnginePanel (p, e)));
    dice = std::make_unique<DiceButton> (p, [] { RandomEngine::Target t; t.scope = RandomEngine::Scope::Module; t.module = "tex"; return t; });
    dice->attachTo (*this, 20, 14);
    startTimerHz (10);
}

void TexturePage::paint (juce::Graphics& g)
{
    frame (g, headArea, IDG_T ("Osc Texture  //  matière sonore"), texCol);
    frame (g, fpArea, IDG_T ("Empreinte spectrale  //  vocoder"), texCol);
    g.setFont (Theme::mono (9.0f, true));
    g.setColour (Theme::textDim);
    g.drawText ("MOTEUR 1", engine1.getBounds().translated (0, -12).withHeight (12), juce::Justification::centredLeft);
    g.drawText ("MOTEUR 2", engine2.getBounds().translated (0, -12).withHeight (12), juce::Justification::centredLeft);
    // fondu entre les deux moteurs
    const float m = proc.getValue (texId ("morph"));
    auto bar = morphArea.toFloat();
    g.setColour (Theme::panelHi);
    g.fillRect (bar);
    g.setGradientFill (juce::ColourGradient (texCol.withAlpha (1.0f - m), bar.getX(), 0.0f, Theme::blue.withAlpha (0.2f + m * 0.8f), bar.getRight(), 0.0f, false));
    g.fillRect (bar.reduced (1.0f));
    g.setColour (Theme::text);
    g.fillRect (bar.getX() + m * bar.getWidth() - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
    g.setFont (Theme::mono (8.5f));
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("routage vers les filtres : bouton TEX sous les filtres (onglet SYNTH)  //  envois vers les bus : onglet FX"),
                headArea.reduced (12, 6).removeFromBottom (12), juce::Justification::centredLeft);
}

void TexturePage::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    auto top = r.removeFromTop (r.getHeight() * 26 / 100);
    headArea = top.removeFromLeft (top.getWidth() * 45 / 100);
    top.removeFromLeft (6);
    slotArea = top;
    r.removeFromTop (6);
    fpArea = r.removeFromBottom (r.getHeight() * 28 / 100);
    r.removeFromBottom (6);
    engineArea = r;

    {
        auto h = headArea.reduced (12, 8).withTrimmedTop (22).withTrimmedBottom (14);
        auto row1 = h.removeFromTop (22 + 12).withTrimmedTop (12);
        on.setBounds (row1.removeFromLeft (40));
        row1.removeFromLeft (8);
        engine1.setBounds (row1.removeFromLeft (130));
        row1.removeFromLeft (8);
        morphArea = row1.removeFromLeft (row1.getWidth() - 138).reduced (0, 7);
        row1.removeFromLeft (8);
        engine2.setBounds (row1);
        h.removeFromTop (4);
        const int w = h.getWidth() / 6;
        for (auto* k : { &morph, &level, &pan, &oct, &semi, &root }) k->setBounds (h.removeFromLeft (w).reduced (3, 0));
    }
    {
        auto s = slotArea;
        const int w = (s.getWidth() - 12) / 3;
        sound1.setBounds (s.removeFromLeft (w));
        s.removeFromLeft (6);
        sound2.setBounds (s.removeFromLeft (w));
        s.removeFromLeft (6);
        data.setBounds (s);
    }
    {
        const int cw = (engineArea.getWidth() - 12) / 3, ch = (engineArea.getHeight() - 6) / 2;
        for (int e = 0; e < engines.size(); ++e)
            engines[e]->setBounds (engineArea.getX() + (e % 3) * (cw + 6), engineArea.getY() + (e / 3) * (ch + 6), cw, ch);
    }
    {
        auto f = fpArea.reduced (12, 8).withTrimmedTop (22);
        fpSlot.setBounds (f.removeFromLeft (220));
        f.removeFromLeft (8);
        auto ctl = f.removeFromRight (420);
        fpView.setBounds (f.reduced (0, 2));
        auto row = ctl.removeFromTop (24);
        fpOn.setBounds (row.removeFromLeft (44).reduced (2, 0));
        fpLoop.setBounds (row.removeFromLeft (60).reduced (2, 0));
        fpRes.setBounds (row.removeFromLeft (120).reduced (2, 0));
        const int w = ctl.getWidth() / 3;
        for (auto* k : { &fpAmount, &fpShift, &fpSpeed }) k->setBounds (ctl.removeFromLeft (w).reduced (4, 2));
    }
}
} // namespace idgs::ui
