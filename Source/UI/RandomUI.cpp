#include "RandomUI.h"

namespace idgs::ui
{
namespace
{
const juce::Colour lockColour { 0xffffc040 };
const juce::Colour rangeBlue { 0xff3d9bff };

juce::String targetKey (const RandomEngine::Target& t)
{
    using S = RandomEngine::Scope;
    switch (t.scope)
    {
        case S::Module: return t.module;
        case S::FxRack: return idg::rackPrefix (t.rack) + "rack";
        case S::FxModule: return idg::rackPrefix (t.rack) + "fx" + juce::String (t.index);
        default: return {};
    }
}
} // namespace

// =============================================================================
// DiceButton
// =============================================================================
DiceButton::DiceButton (IDGemsSynthProcessor& p, std::function<Target()> t, juce::Colour c) : proc (p), target (std::move (t)), colour (c)
{
    setTooltip (IDG_T ("Random de ce bloc (dosage, personnalité et cadenas respectés).\nClic droit : cadenas du bloc, random léger."));
    setRepaintsOnMouseActivity (true);
    startTimerHz (15);
}

DiceButton::~DiceButton()
{
    if (host != nullptr) host->removeComponentListener (this);
}

void DiceButton::attachTo (juce::Component& h, int rightInset, int top)
{
    host = &h;
    inset = rightInset;
    topY = top;
    h.addAndMakeVisible (this);
    h.addComponentListener (this);
    place();
}

void DiceButton::place()
{
    if (host != nullptr) setBounds (host->getWidth() - inset - 20, topY, 20, 18);
    toFront (false);
}

juce::String DiceButton::lockKey() const { return targetKey (target()); }

bool DiceButton::locked() const
{
    const auto t = target();
    auto& re = proc.randomEngine();
    if (t.scope == RandomEngine::Scope::Section) return re.sectionLocked[t.section];
    const auto k = targetKey (t);
    return k.isNotEmpty() && re.isModuleLocked (k);
}

void DiceButton::setLocked (bool l)
{
    const auto t = target();
    auto& re = proc.randomEngine();
    if (t.scope == RandomEngine::Scope::Section) re.setSectionLocked (t.section, l);
    else if (auto k = targetKey (t); k.isNotEmpty()) re.setModuleLocked (k, l);
}

void DiceButton::timerCallback()
{
    const int v = proc.randomEngine().version.load();
    if (v != lastVersion) { lastVersion = v; repaint(); }
    if (flash > 0.0f) { flash = juce::jmax (0.0f, flash - 0.12f); repaint(); }
}

void DiceButton::drawDice (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, int face, float alpha)
{
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    auto d = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
    g.setColour (c.withAlpha (0.15f * alpha));
    g.fillRoundedRectangle (d, s * 0.2f);
    g.setColour (c.withAlpha (alpha));
    g.drawRoundedRectangle (d.reduced (0.6f), s * 0.2f, 1.2f);
    const float q = s * 0.26f, pip = juce::jmax (2.0f, s * 0.15f);
    auto at = [&] (float x, float y) { g.fillEllipse (juce::Rectangle<float> (pip, pip).withCentre ({ d.getCentreX() + x * q, d.getCentreY() + y * q })); };
    face = juce::jlimit (1, 6, face);
    if (face % 2 == 1) at (0, 0);
    if (face >= 2) { at (-1, -1); at (1, 1); }
    if (face >= 4) { at (1, -1); at (-1, 1); }
    if (face == 6) { at (-1, 0); at (1, 0); }
}

void DiceButton::paint (juce::Graphics& g)
{
    const bool lk = locked();
    const bool over = isMouseOver();
    auto c = lk ? lockColour : colour;
    drawDice (g, getLocalBounds().toFloat().reduced (1.5f), c.interpolatedWith (juce::Colours::white, flash * 0.6f), face,
              lk ? 0.8f : (over ? 1.0f : 0.65f));
    if (lk)
    {
        auto lr = juce::Rectangle<float> (6.0f, 7.0f).withPosition ((float) getWidth() - 6.5f, 0.0f);
        g.setColour (lockColour);
        g.fillRoundedRectangle (lr.withTrimmedTop (3.0f), 1.0f);
        g.drawRoundedRectangle (lr.withTrimmedBottom (3.0f).reduced (1.0f, 0.0f), 1.5f, 1.0f);
    }
    if (proc.randomEngine().isPending() && (juce::Time::getMillisecondCounter() / 250) % 2 == 0)
    {
        g.setColour (colour.withAlpha (0.5f));
        g.drawRect (getLocalBounds().toFloat(), 1.0f);
    }
}

void DiceButton::mouseDown (const juce::MouseEvent& e)
{
    auto& re = proc.randomEngine();
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        const auto t = target();
        m.addSectionHeader ("Random : " + re.labelFor (t));
        m.addItem (1, IDG_T ("Random (dosage du panneau)"));
        m.addItem (2, IDG_T ("Random léger (15 %)"));
        m.addItem (3, IDG_T ("Random total (100 %)"));
        if (t.scope == RandomEngine::Scope::Section || targetKey (t).isNotEmpty())
        {
            m.addSeparator();
            m.addItem (4, IDG_T ("Cadenas : le random général ne touche plus ce bloc"), true, locked());
        }
        juce::Component::SafePointer<DiceButton> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            auto& rr = safe->proc.randomEngine();
            const auto tt = safe->target();
            const bool guards = safe->proc.getValue ("rand_guard") > 0.5f;
            if (r == 1) rr.request (tt);
            else if (r == 2) rr.perform (tt, 0, 0.15f, guards, rr.labelFor (tt));
            else if (r == 3) rr.perform (tt, 0, 1.0f, guards, rr.labelFor (tt));
            else if (r == 4) safe->setLocked (! safe->locked());
            safe->face = juce::Random::getSystemRandom().nextInt (6) + 1;
            safe->flash = 1.0f;
        });
        return;
    }
    // le cadenas protège du random général ; le dé du bloc lui-même passe outre
    if (locked()) setLocked (false);
    re.request (target());
    face = juce::Random::getSystemRandom().nextInt (6) + 1;
    flash = 1.0f;
    repaint();
}

// =============================================================================
// MorphPad
// =============================================================================
juce::Colour MorphPad::snapColour (int k)
{
    static const juce::Colour c[] { juce::Colour (0xffff6a00), juce::Colour (0xff2fa8ff), juce::Colour (0xffff2d55), juce::Colour (0xff3ddc84),
                                    juce::Colour (0xffb06cff), juce::Colour (0xffffc040), juce::Colour (0xff00e5d4), juce::Colour (0xffff7ad9) };
    return c[k & 7];
}

juce::Point<float> MorphPad::toPx (float x, float y) const
{
    auto a = padArea();
    return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
}

juce::Point<float> MorphPad::fromPx (juce::Point<float> p) const
{
    auto a = padArea();
    return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
}

void MorphPad::timerCallback()
{
    const float x = proc.getValue ("morph_x"), y = proc.getValue ("morph_y");
    const int v = proc.randomEngine().version.load();
    if (x != lx || y != ly || v != lastVersion) { lx = x; ly = y; lastVersion = v; repaint(); }
}

void MorphPad::paint (juce::Graphics& g)
{
    auto a = padArea();
    g.setColour (Theme::bg);
    g.fillRect (a);
    g.setColour (Theme::line.withAlpha (0.5f));
    for (int k = 1; k < 8; ++k)
    {
        g.drawVerticalLine ((int) (a.getX() + a.getWidth() * k / 8.0f), a.getY(), a.getBottom());
        g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * k / 8.0f), a.getX(), a.getRight());
    }
    auto& re = proc.randomEngine();
    const auto cur = toPx (proc.getValue ("morph_x"), proc.getValue ("morph_y"));

    // influence de chaque snapshot (même formule que le moteur)
    float w[RandomEngine::NumSnapshots] {}, total = 0.0f;
    const float mx = proc.getValue ("morph_x"), my = proc.getValue ("morph_y");
    for (int k = 0; k < RandomEngine::NumSnapshots; ++k)
        if (re.snapshots[k].valid)
        {
            const float d2 = juce::square (re.snapshots[k].x - mx) + juce::square (re.snapshots[k].y - my);
            w[k] = 1.0f / (d2 * d2 + 1.0e-6f);
            total += w[k];
        }
    for (int k = 0; k < RandomEngine::NumSnapshots; ++k)
    {
        const auto& s = re.snapshots[k];
        if (! s.valid) continue;
        const auto p = toPx (s.x, s.y);
        const float inf = total > 0.0f ? w[k] / total : 0.0f;
        const auto c = snapColour (k);
        g.setColour (c.withAlpha (0.15f + 0.6f * inf));
        g.drawLine ({ p, cur }, 1.0f + 3.0f * inf);
        const float rr = 9.0f + 10.0f * inf;
        g.setColour (c.withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (p));
        g.setColour (c);
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (p));
        g.setColour (Theme::bg);
        g.setFont (Theme::mono (9.0f, true));
        g.drawText (juce::String (k + 1), juce::Rectangle<float> (14.0f, 14.0f).withCentre (p), juce::Justification::centred);
    }
    // curseur
    g.setColour (Theme::text);
    g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (cur), 2.0f);
    g.drawLine (cur.x - 12.0f, cur.y, cur.x - 6.0f, cur.y, 1.5f);
    g.drawLine (cur.x + 6.0f, cur.y, cur.x + 12.0f, cur.y, 1.5f);
    g.drawLine (cur.x, cur.y - 12.0f, cur.x, cur.y - 6.0f, 1.5f);
    g.drawLine (cur.x, cur.y + 6.0f, cur.x, cur.y + 12.0f, 1.5f);
    Theme::panelFrame (g, a.expanded (2.0f), Theme::blue, 0.6f);
    if (re.numSnapshots() < 2)
    {
        g.setFont (Theme::mono (10.0f));
        g.setColour (Theme::textDim);
        g.drawFittedText (IDG_T ("Mémorise au moins 2 snapshots (boutons 1 à 8)\npuis déplace le curseur pour fondre les sons."),
                          a.reduced (10.0f).toNearestInt(), juce::Justification::centred, 3);
    }
}

void MorphPad::mouseDown (const juce::MouseEvent& e)
{
    auto& re = proc.randomEngine();
    dragSnap = -1;
    dragCursor = false;
    for (int k = 0; k < RandomEngine::NumSnapshots; ++k)
        if (re.snapshots[k].valid && toPx (re.snapshots[k].x, re.snapshots[k].y).getDistanceFrom (e.position) < 10.0f)
        {
            dragSnap = k;
            if (e.mods.isPopupMenu())
            {
                re.clearSnapshot (k);
                dragSnap = -1;
            }
            return;
        }
    dragCursor = true;
    mouseDrag (e);
}

void MorphPad::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = fromPx (e.position);
    if (dragSnap >= 0) proc.randomEngine().moveSnapshot (dragSnap, p.x, p.y);
    else if (dragCursor)
    {
        proc.setValue ("morph_x", p.x);
        proc.setValue ("morph_y", p.y);
        proc.randomEngine().applyMorph (true);
    }
    repaint();
}

void MorphPad::mouseUp (const juce::MouseEvent&) { dragSnap = -1; dragCursor = false; }

// =============================================================================
// HistoryList
// =============================================================================
void HistoryList::timerCallback()
{
    const int v = proc.randomEngine().version.load();
    if (v != lastVersion) { lastVersion = v; scroll = 0; repaint(); }
}

void HistoryList::paint (juce::Graphics& g)
{
    auto& re = proc.randomEngine();
    const auto& h = re.getHistory();
    g.setColour (Theme::bg);
    g.fillRect (getLocalBounds());
    if (h.empty())
    {
        g.setFont (Theme::mono (10.0f));
        g.setColour (Theme::textDim);
        g.drawFittedText (IDG_T ("Aucun tirage pour l'instant.\nChaque random est gardé ici : clic = rappel, étoile = favori."),
                          getLocalBounds().reduced (8), juce::Justification::centred, 4);
        return;
    }
    const int rows = getHeight() / rowH;
    for (int r = 0; r < rows; ++r)
    {
        const int idx = (int) h.size() - 1 - (r + scroll);
        if (idx < 0) break;
        const auto& e = h[(size_t) idx];
        auto row = juce::Rectangle<int> (0, r * rowH, getWidth(), rowH);
        if (idx == re.getCursor()) { g.setColour (Theme::accent.withAlpha (0.22f)); g.fillRect (row); }
        else if (r % 2) { g.setColour (Theme::panelHi.withAlpha (0.5f)); g.fillRect (row); }
        g.setFont (Theme::mono (12.0f, true));
        g.setColour (e.favourite ? Theme::accent : Theme::line.brighter (0.4f));
        g.drawText (e.favourite ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86")),
                    row.removeFromLeft (20), juce::Justification::centred);
        g.setFont (Theme::mono (10.0f));
        g.setColour (Theme::textDim);
        g.drawText ("#" + juce::String (idx + 1), row.removeFromLeft (34), juce::Justification::centredLeft);
        g.setColour (Theme::text);
        g.drawText (juce::String (e.seed), row.removeFromRight (52).withTrimmedRight (6), juce::Justification::centredRight);
        g.drawText (e.label, row, juce::Justification::centredLeft, true);
    }
}

void HistoryList::mouseDown (const juce::MouseEvent& e)
{
    auto& re = proc.randomEngine();
    const int idx = (int) re.getHistory().size() - 1 - (e.y / rowH + scroll);
    if (idx < 0) return;
    if (e.x < 20) re.toggleFavourite (idx);
    else re.recall (idx);
    repaint();
}

void HistoryList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int maxScroll = juce::jmax (0, (int) proc.randomEngine().getHistory().size() - getHeight() / rowH);
    scroll = juce::jlimit (0, maxScroll, scroll - (w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0)) * 2);
    repaint();
}

// =============================================================================
// RandomPanel
// =============================================================================
RandomPanel::RandomPanel (IDGemsSynthProcessor& p) : proc (p), pad (p), history (p)
{
    addAndMakeVisible (pad);
    addAndMakeVisible (history);
    for (int k = 0; k < RandomEngine::NumSnapshots; ++k)
    {
        auto* b = snapButtons.add (new juce::TextButton (juce::String (k + 1)));
        b->setTooltip (IDG_T ("Snapshot ") + juce::String (k + 1)
                       + IDG_T (" : clic = mémoriser le son actuel (ou y aller s'il existe), clic droit = remplacer / effacer."));
        b->onClick = [this, k]
        {
            auto& re = proc.randomEngine();
            if (juce::ModifierKeys::currentModifiers.isPopupMenu()) return;
            auto& s = re.snapshots[k];
            if (! s.valid)
            {
                // place le nouveau snapshot sur un cercle autour du centre
                const float a = juce::MathConstants<float>::twoPi * (float) k / 8.0f;
                re.storeSnapshot (k, 0.5f + 0.38f * std::cos (a), 0.5f + 0.38f * std::sin (a));
                if (flash) flash (IDG_T ("Snapshot ") + juce::String (k + 1) + IDG_T (" mémorisé"));
            }
            else
            {
                proc.setValue ("morph_x", s.x);
                proc.setValue ("morph_y", s.y);
                re.applyMorph (true);
            }
        };
        b->addMouseListener (this, false);
        b->getProperties().set ("snap", k);
        addAndMakeVisible (b);
    }
    for (int s = 0; s < NumRandomSections; ++s)
    {
        auto* d = sectionDice.add (new DiceButton (p, [s] { RandomEngine::Target t; t.scope = RandomEngine::Scope::Section; t.section = s; return t; }));
        addAndMakeVisible (d);
        auto* l = sectionLocks.add (new juce::TextButton (randomSectionNames()[s]));
        l->setClickingTogglesState (true);
        l->setColour (juce::TextButton::buttonOnColourId, lockColour.withAlpha (0.55f));
        l->setTooltip (IDG_T ("Cadenas de section : allumé = le random général ne touche plus cette section."));
        l->onClick = [this, s, l] { proc.randomEngine().setSectionLocked (s, l->getToggleState()); };
        addAndMakeVisible (l);
    }
    allDice.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.3f));
    allDice.onClick = [this] { proc.randomEngine().request ({}); };
    allDice.setTooltip (IDG_T ("Random de tout le patch (cadenas respectés)."));
    seed.setFont (Theme::mono (13.0f));
    seed.setInputRestrictions (7, "0123456789");
    seed.setJustification (juce::Justification::centred);
    seed.setTooltip (IDG_T ("Seed : le même numéro redonne exactement le même tirage (même patch de départ, même dosage)."));
    replay.onClick = [this]
    {
        const auto s = (uint32_t) seed.getText().getLargeIntValue();
        if (s == 0) return;
        auto& re = proc.randomEngine();
        re.perform ({}, s, proc.getValue ("rand_amount"), proc.getValue ("rand_guard") > 0.5f, "Seed " + juce::String (s));
    };
    replay.setTooltip (IDG_T ("Rejoue le tirage du seed tapé (random général)."));
    close.onClick = [this] { setVisible (false); };
    reloadPers.setTooltip (IDG_T ("Recharger les personnalités (fichiers JSON dans Documents/IDGems Synth/Personnalites)"));
    reloadPers.onClick = [this] { refreshPersonalities(); };
    personality.setTooltip (idgs::helpFor ("rand_personality"));
    personality.onChange = [this]
    {
        const int id = personality.getSelectedId();
        auto& re = proc.randomEngine();
        if (id >= 1000) re.userPersonality = id - 1000;
        else if (id >= 1) { re.userPersonality = -1; proc.setValue ("rand_personality", (float) (id - 1)); }
    };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &allDice, &seed, &replay, &close, &personality, &reloadPers })
        addAndMakeVisible (c);

    amount = std::make_unique<Knob> (p, "rand_amount", "Dosage");
    intensity = std::make_unique<Knob> (p, "rand_intensity", IDG_T ("Intensité"), Theme::blue);
    glide = std::make_unique<Knob> (p, "rand_glide", "Glide");
    drift = std::make_unique<Knob> (p, "rand_drift", "Drift", Theme::blue);
    driftSpeed = std::make_unique<Knob> (p, "rand_drift_speed", "Vitesse", Theme::blue);
    mutateAmount = std::make_unique<Knob> (p, "rand_mutate_amount", "Mutation");
    sync = std::make_unique<ParamCombo> (p, "rand_sync");
    mutateEvery = std::make_unique<ParamCombo> (p, "rand_mutate_every");
    mutate = std::make_unique<ParamToggle> (p, "rand_mutate", "MUTATE AUTO");
    guard = std::make_unique<ParamToggle> (p, "rand_guard", IDG_T ("GARDE-FOUS"), juce::Colour (0xff3ddc84));
    for (juce::Component* c : std::initializer_list<juce::Component*> { amount.get(), intensity.get(), glide.get(), drift.get(), driftSpeed.get(),
                                                                         mutateAmount.get(), sync.get(), mutateEvery.get(), mutate.get(), guard.get() })
        addAndMakeVisible (c);
    sync->setTooltip (idgs::helpFor ("rand_sync"));
    mutateEvery->setTooltip (idgs::helpFor ("rand_mutate"));
    mutate->setTooltip (idgs::helpFor ("rand_mutate"));
    guard->setTooltip (idgs::helpFor ("rand_guard"));
    refreshPersonalities();
}

void RandomPanel::refreshPersonalities()
{
    personality.clear (juce::dontSendNotification);
    for (int k = 0; k < personalityNames().size(); ++k) personality.addItem (personalityNames()[k], k + 1);
    auto& re = proc.randomEngine();
    const auto user = re.userPersonalityNames();
    if (! user.isEmpty())
    {
        personality.addSeparator();
        for (int k = 0; k < user.size(); ++k) personality.addItem (user[k], 1000 + k);
    }
    personality.setSelectedId (re.userPersonality >= 0 ? 1000 + re.userPersonality : (int) proc.getValue ("rand_personality") + 1,
                               juce::dontSendNotification);
}

void RandomPanel::open()
{
    setVisible (true);
    toFront (true);
    refreshPersonalities();
    seed.setText (juce::String (proc.randomEngine().lastSeed()), false);
}

juce::Rectangle<int> RandomPanel::box() const { return getLocalBounds().withSizeKeepingCentre (1060, 640); }

void RandomPanel::timerCallback() {}

void RandomPanel::mouseDown (const juce::MouseEvent& e)
{
    if (auto* b = dynamic_cast<juce::TextButton*> (e.eventComponent); b != nullptr && b->getProperties().contains ("snap"))
    {
        if (! e.mods.isPopupMenu()) return;
        const int k = b->getProperties()["snap"];
        juce::PopupMenu m;
        m.addItem (1, IDG_T ("Remplacer par le son actuel"));
        m.addItem (2, IDG_T ("Effacer"), proc.randomEngine().snapshots[k].valid);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (b), [this, k] (int r)
        {
            auto& re = proc.randomEngine();
            if (r == 1) re.storeSnapshot (k, re.snapshots[k].valid ? re.snapshots[k].x : proc.getValue ("morph_x"),
                                          re.snapshots[k].valid ? re.snapshots[k].y : proc.getValue ("morph_y"));
            else if (r == 2) re.clearSnapshot (k);
        });
        return;
    }
    if (e.eventComponent == this && ! box().contains (e.getPosition())) setVisible (false);
}

void RandomPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    auto b = box();
    g.setColour (Theme::bg);
    g.fillRect (b);
    Theme::panelFrame (g, b.toFloat(), Theme::accent);
    Theme::cutText (g, "RANDOM", b.toFloat().reduced (16, 10).withHeight (26), 18.0f, Theme::accent, juce::Justification::centredLeft, 0.5f, true);

    auto title = [&g] (juce::Rectangle<int> r, const juce::String& t)
    {
        g.setFont (Theme::mono (10.0f, true));
        g.setColour (Theme::textDim);
        g.drawText (t, r.withHeight (14), juce::Justification::centredLeft);
    };
    title (padArea.translated (0, -18), "MORPH PAD  -  SNAPSHOTS");
    title (secArea.translated (0, -18), IDG_T ("SECTIONS  (dé = random, bouton = cadenas)"));
    title (setArea.translated (0, -18), IDG_T ("RÉGLAGES"));
    title (histArea.translated (0, -18), "HISTORIQUE");

    // état : seed, tirage en attente, garde-fous
    auto& re = proc.randomEngine();
    g.setFont (Theme::mono (10.0f));
    auto info = b.reduced (16, 10).withHeight (26).withTrimmedLeft (130).withTrimmedRight (60);
    juce::String s = "Seed " + juce::String (re.lastSeed());
    if (re.isPending()) s << IDG_T ("   -   tirage en attente du temps / de la mesure...");
    if (re.isGliding()) s << "   -   glide...";
    if (re.lastMessage.isNotEmpty()) s << "   -   " << re.lastMessage;
    g.setColour (Theme::text);
    g.drawText (s, info, juce::Justification::centredLeft);
    // couleurs des boutons de snapshots
    for (int k = 0; k < snapButtons.size(); ++k)
    {
        auto r = snapButtons[k]->getBounds().toFloat();
        g.setColour (re.snapshots[k].valid ? MorphPad::snapColour (k) : Theme::line);
        g.fillRect (r.withY (r.getBottom() + 2.0f).withHeight (3.0f));
    }
}

void RandomPanel::resized()
{
    auto b = box().reduced (16, 10);
    auto head = b.removeFromTop (28);
    close.setBounds (head.removeFromRight (28).reduced (0, 2));
    b.removeFromTop (24);

    // colonne 1 : morph pad + snapshots
    padArea = b.removeFromLeft (360);
    auto pa = padArea;
    auto sb = pa.removeFromBottom (32);
    pa.removeFromBottom (8);
    pad.setBounds (pa);
    const int bw = sb.getWidth() / RandomEngine::NumSnapshots;
    for (int k = 0; k < snapButtons.size(); ++k) snapButtons[k]->setBounds (sb.removeFromLeft (bw).reduced (2, 0).withTrimmedBottom (6));
    b.removeFromLeft (20);

    // colonne 2 : sections
    secArea = b.removeFromLeft (230);
    auto sa = secArea;
    allDice.setBounds (sa.removeFromTop (40).reduced (0, 4));
    sa.removeFromTop (8);
    for (int s = 0; s < NumRandomSections; ++s)
    {
        auto row = sa.removeFromTop (34);
        sectionDice[s]->setBounds (row.removeFromLeft (30).withSizeKeepingCentre (24, 22));
        row.removeFromLeft (6);
        sectionLocks[s]->setBounds (row.reduced (0, 4));
        sectionLocks[s]->setToggleState (proc.randomEngine().sectionLocked[s], juce::dontSendNotification);
    }
    sa.removeFromTop (14);
    auto pr = sa.removeFromTop (26);
    reloadPers.setBounds (pr.removeFromRight (26));
    pr.removeFromRight (4);
    personality.setBounds (pr);
    sa.removeFromTop (12);
    auto sr = sa.removeFromTop (26);
    seed.setBounds (sr.removeFromLeft (110));
    sr.removeFromLeft (6);
    replay.setBounds (sr);
    b.removeFromLeft (20);

    // colonne 3 : réglages
    setArea = b.removeFromLeft (210);
    auto st = setArea;
    auto knobRow = [] (juce::Rectangle<int> r, std::initializer_list<juce::Component*> cs)
    {
        const int w = r.getWidth() / (int) cs.size();
        for (auto* c : cs) c->setBounds (r.removeFromLeft (w).reduced (2, 0));
    };
    knobRow (st.removeFromTop (70), { amount.get(), intensity.get(), glide.get() });
    st.removeFromTop (6);
    sync->setBounds (st.removeFromTop (24));
    st.removeFromTop (12);
    knobRow (st.removeFromTop (70), { drift.get(), driftSpeed.get(), mutateAmount.get() });
    st.removeFromTop (8);
    mutate->setBounds (st.removeFromTop (26));
    st.removeFromTop (6);
    mutateEvery->setBounds (st.removeFromTop (24));
    st.removeFromTop (14);
    guard->setBounds (st.removeFromTop (28));
    b.removeFromLeft (20);

    // colonne 4 : historique
    histArea = b;
    history.setBounds (histArea);
}

// =============================================================================
// RandomCluster
// =============================================================================
RandomCluster::RandomCluster (IDGemsSynthProcessor& p) : proc (p), amount (p, "rand_amount", "Dose")
{
    addAndMakeVisible (amount);
    addAndMakeVisible (more);
    more.setTooltip (IDG_T ("Panneau du random : morph pad, snapshots, historique, seed, personnalités, drift, mutate, garde-fous."));
    more.onClick = [this] { if (openPanel) openPanel(); };
    setTooltip (IDG_T ("RANDOM : clic = nouveau son (dosage, personnalité, cadenas respectés).\nClic droit = annuler (tirage précédent de l'historique)."));
    startTimerHz (15);
}

void RandomCluster::timerCallback()
{
    const int v = proc.randomEngine().version.load();
    if (v != lastVersion) { lastVersion = v; repaint(); }
    if (flash > 0.0f || proc.randomEngine().isPending()) { flash = juce::jmax (0.0f, flash - 0.08f); repaint(); }
}

void RandomCluster::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (50);
    more.setBounds (r.removeFromRight (26).reduced (0, 15));
    amount.setBounds (r.reduced (0, 2));
}

void RandomCluster::paint (juce::Graphics& g)
{
    auto d = diceArea().toFloat();
    const bool pending = proc.randomEngine().isPending();
    const float blink = pending && (juce::Time::getMillisecondCounter() / 200) % 2 == 0 ? 1.0f : 0.0f;
    DiceButton::drawDice (g, d, Theme::accent.interpolatedWith (juce::Colours::white, juce::jmax (flash, blink) * 0.7f), face,
                          isMouseOver (true) ? 1.0f : 0.85f);
    if (flash > 0.0f)
    {
        g.setColour (Theme::accent.withAlpha (flash * 0.35f));
        g.drawRoundedRectangle (d.expanded (3.0f * flash), 6.0f, 2.0f);
    }
}

void RandomCluster::mouseDown (const juce::MouseEvent& e)
{
    if (! diceArea().contains (e.getPosition())) return;
    auto& re = proc.randomEngine();
    if (e.mods.isPopupMenu())
    {
        const int c = re.getCursor();
        if (c > 0) re.recall (c - 1);
        return;
    }
    re.request ({});
    face = juce::Random::getSystemRandom().nextInt (6) + 1;
    flash = 1.0f;
    repaint();
}
} // namespace idgs::ui
