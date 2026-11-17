#include "FxPage.h"

namespace idgs::ui
{
namespace
{
juce::Colour busColour (int b)
{
    static const juce::Colour c[] { juce::Colour (0xffff6a00), juce::Colour (0xff2fa8ff), juce::Colour (0xffc86bff), juce::Colour (0xffe8e6e3) };
    return c[juce::jlimit (0, 3, b)];
}
juce::String busLetter (int b) { return juce::String::charToString ((juce::juce_wchar) ('A' + b)); }

void frame (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
{
    Theme::panelFrame (g, r.toFloat().reduced (1.0f), c);
    Theme::sectionTitle (g, title, r.toFloat().reduced (10.0f, 7.0f), c);
}
} // namespace

// =============================================================================
// RoutingPanel
// =============================================================================
RoutingPanel::RoutingPanel (IDGemsSynthProcessor& p) : proc (p)
{
    for (int s = 0; s < NumSendSources; ++s)
        for (int b = 0; b < NumBuses; ++b)
            addAndMakeVisible (sends.add (new Knob (p, sendId (s, b), "-> " + busLetter (b), busColour (b))));
    for (int a = 0; a < NumBuses; ++a)
        for (int t = 0; t < NumBuses; ++t)
            if (t != a) addAndMakeVisible (busSends.add (new Knob (p, busSendId (a, t), busLetter (a) + " -> " + busLetter (t), busColour (t))));
    for (int b = 0; b < NumBuses; ++b)
    {
        addAndMakeVisible (busVol.add (new Knob (p, busId (b, "vol"), "Volume", busColour (b))));
        addAndMakeVisible (busPan.add (new Knob (p, busId (b, "pan"), "Pan", busColour (b))));
        addAndMakeVisible (mute.add (new ParamToggle (p, busId (b, "mute"), "M", Theme::red)));
        addAndMakeVisible (solo.add (new ParamToggle (p, busId (b, "solo"), "S", Theme::blue)));
        addAndMakeVisible (followers.add (new ModSourceHandle (p, MSrcFollow0 + b)));
    }
    startTimerHz (15);
}

void RoutingPanel::paint (juce::Graphics& g)
{
    frame (g, getLocalBounds(), "Routage  //  sources -> bus -> master", Theme::accent);
    static const char* names[] { "OSC A", "OSC B", "SUB", "NOISE", "TEXTURE" };
    g.setFont (Theme::mono (10.0f, true));
    for (int s = 0; s < NumSendSources; ++s)
    {
        g.setColour (Theme::text);
        g.drawText (names[s], srcLabels[s], juce::Justification::centredLeft);
    }
    for (int b = 0; b < NumBuses; ++b)
    {
        auto c = busCols[b];
        g.setColour (busColour (b).withAlpha (0.12f));
        g.fillRect (c);
        Theme::tapeLabel (g, "BUS " + busLetter (b), (float) c.getX() + 4.0f, (float) c.getY() + 2.0f, busColour (b));
        // câbles : intensité = somme des envois vers ce bus
        float total = 0.0f;
        for (int s = 0; s < NumSendSources; ++s) total += proc.getValue (sendId (s, b));
        g.setColour (busColour (b).withAlpha (juce::jlimit (0.05f, 0.7f, total * 0.3f)));
        g.fillRect (c.getX(), c.getBottom() - 3, c.getWidth(), 3);
    }
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("BUS -> BUS"), busSendArea.withHeight (14), juce::Justification::centredLeft);
    // envois bus -> bus coupés pour éviter une boucle
    int k = 0;
    for (int a = 0; a < NumBuses; ++a)
        for (int t = 0; t < NumBuses; ++t)
        {
            if (t == a) continue;
            auto* kn = busSends[k++];
            if (proc.busEdgeBlocked[a][t].load())
            {
                g.setColour (Theme::red.withAlpha (0.8f));
                g.setFont (Theme::mono (8.5f, true));
                g.drawText ("BOUCLE", kn->getBounds().withHeight (10).translated (0, -10), juce::Justification::centred);
                g.drawRect (kn->getBounds(), 1);
            }
        }
    // vu-mètres des bus
    for (int b = 0; b < NumBuses; ++b)
    {
        meter[b] = juce::jmax (proc.busMeter[b].exchange (0.0f), meter[b] * 0.85f);
        auto m = busVol[b]->getBounds().withWidth (5).translated (-8, 0);
        g.setColour (Theme::panelHi);
        g.fillRect (m);
        const float db = idg::dsp::gainToDb (meter[b] + 1.0e-6f);
        const float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * (float) m.getHeight();
        g.setColour (busColour (b));
        g.fillRect ((float) m.getX(), (float) m.getBottom() - h, (float) m.getWidth(), h);
    }
    g.setFont (Theme::mono (9.0f));
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("pastille rose = suiveur d'enveloppe du bus (source de modulation)"), stripArea.withTop (stripArea.getBottom() - 12), juce::Justification::centredLeft);
}

void RoutingPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8).withTrimmedTop (26);
    auto matrix = r.removeFromTop (r.getHeight() * 46 / 100);
    const int labelW = 64;
    const int cw = (matrix.getWidth() - labelW) / NumBuses;
    auto header = matrix.removeFromTop (20);
    for (int b = 0; b < NumBuses; ++b) busCols[b] = header.withX (header.getX() + labelW + b * cw).withWidth (cw - 4);
    const int rh = matrix.getHeight() / NumSendSources;
    for (int s = 0; s < NumSendSources; ++s)
    {
        auto row = matrix.removeFromTop (rh);
        srcLabels[s] = row.removeFromLeft (labelW);
        for (int b = 0; b < NumBuses; ++b) sends[s * NumBuses + b]->setBounds (row.removeFromLeft (cw).reduced (cw / 4, 1));
    }
    r.removeFromTop (6);
    busSendArea = r.removeFromTop (r.getHeight() * 32 / 100);
    {
        auto a = busSendArea.withTrimmedTop (16);
        const int w = a.getWidth() / 6;
        for (int i = 0; i < busSends.size(); ++i) busSends[i]->setBounds (a.removeFromLeft (w).reduced (3, 2));
    }
    r.removeFromTop (6);
    stripArea = r;
    auto strips = r.withTrimmedBottom (14);
    const int sw = strips.getWidth() / NumBuses;
    for (int b = 0; b < NumBuses; ++b)
    {
        auto c = strips.removeFromLeft (sw).reduced (8, 0);
        auto top = c.removeFromTop (22);
        mute[b]->setBounds (top.removeFromLeft (26));
        top.removeFromLeft (3);
        solo[b]->setBounds (top.removeFromLeft (26));
        followers[b]->setBounds (top.removeFromRight (20).reduced (1));
        c.removeFromTop (2);
        auto knobs = c;
        busVol[b]->setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).reduced (6, 0));
        busPan[b]->setBounds (knobs.reduced (4, 0));
    }
}

// =============================================================================
// StepGrid
// =============================================================================
int StepGrid::stepAt (float x) const
{
    if (data == nullptr) return -1;
    const int len = juce::jlimit (1, idg::MaxSteps, data->length.load());
    return juce::jlimit (0, len - 1, (int) (x / ((float) getWidth() / (float) len)));
}

void StepGrid::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg.withAlpha (0.7f));
    g.fillRect (r);
    if (data == nullptr) return;
    const int len = juce::jlimit (1, idg::MaxSteps, data->length.load());
    const float w = r.getWidth() / (float) len;
    const int cur = data->enabled.load() ? data->current.load() : -1;
    for (int i = 0; i < len; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + (float) i * w, r.getY(), w, r.getHeight()).reduced (1.5f, 2.0f);
        const bool on = data->on[i].load();
        const float pr = data->prob[i].load();
        g.setColour ((i / 4) % 2 ? Theme::panelHi : Theme::panel);
        g.fillRect (cell);
        if (on)
        {
            const float h = cell.getHeight() * (0.15f + 0.85f * pr);
            g.setColour (colour.withAlpha (data->enabled.load() ? 0.85f : 0.35f));
            g.fillRect (cell.withTop (cell.getBottom() - h));
        }
        const int rt = data->ratchet[i].load();
        for (int k = 1; k < rt; ++k)
        {
            g.setColour (Theme::text);
            g.fillEllipse (cell.getCentreX() - 6.0f + (float) k * 4.0f, cell.getY() + 3.0f, 3.0f, 3.0f);
        }
        if (i == cur)
        {
            g.setColour (Theme::text);
            g.drawRect (cell, 1.5f);
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    if (data == nullptr) return;
    const int s = stepAt (e.position.x);
    if (s < 0) return;
    if (e.mods.isPopupMenu()) { data->ratchet[s] = data->ratchet[s].load() % 4 + 1; repaint(); return; }
    dragStep = s;
    dragStartProb = data->prob[s].load();
    data->on[s] = ! data->on[s].load();
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (data == nullptr || dragStep < 0 || std::abs (e.getDistanceFromDragStartY()) < 4) return;
    data->on[dragStep] = true;
    data->prob[dragStep] = juce::jlimit (0.05f, 1.0f, dragStartProb - (float) e.getDistanceFromDragStartY() / 80.0f);
    repaint();
}

// =============================================================================
// RackPanel
// =============================================================================
RackPanel::RackPanel (IDGemsSynthProcessor& p) : proc (p), grid (p)
{
    addAndMakeVisible (grid);
    seqRate.addItemList (idg::seqRateNames(), 1);
    for (int i = 1; i <= idg::MaxSteps; ++i) seqLen.addItem (juce::String (i) + " pas", i);
    seqSwing.setSliderStyle (juce::Slider::LinearBar);
    seqSwing.setRange (0.0, 1.0, 0.01);
    seqSwing.textFromValueFunction = [] (double v) { return "Swing " + juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    seqOn.setTooltip (IDG_T ("Séquenceur du module : l'effet n'agit que sur les pas allumés (probabilité, ratchets).\n"
                             "Grille : clic = pas on/off, glisser vertical = probabilité, clic droit = ratchets."));
    seqRandom.setTooltip (IDG_T ("Motif aléatoire pour ce séquenceur."));
    auto seq = [this] { return &proc.rack (rack).seq[fx]; };
    seqOn.onClick = [this, seq] { seq()->enabled = seqOn.getToggleState(); };
    seqRate.onChange = [this, seq] { seq()->rate = seqRate.getSelectedItemIndex(); };
    seqLen.onChange = [this, seq] { seq()->length = seqLen.getSelectedId(); grid.repaint(); };
    seqSwing.onValueChange = [this, seq] { seq()->swing = (float) seqSwing.getValue(); };
    seqRandom.onClick = [this, seq]
    {
        auto& r = juce::Random::getSystemRandom();
        auto* d = seq();
        for (int i = 0; i < idg::MaxSteps; ++i)
        {
            d->on[i] = r.nextFloat() < 0.45f;
            d->prob[i] = 0.5f + 0.5f * r.nextFloat();
            d->ratchet[i] = r.nextFloat() < 0.15f ? 2 + r.nextInt (3) : 1;
        }
        d->enabled = true;
        seqOn.setToggleState (true, juce::dontSendNotification);
        grid.repaint();
    };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &seqOn, &seqRate, &seqLen, &seqSwing, &seqRandom }) addAndMakeVisible (c);
    selectRack (0);
    startTimerHz (15);
}

juce::Rectangle<int> RackPanel::tabRect (int i) const
{
    auto r = getLocalBounds().reduced (12, 8).withTrimmedTop (26).withHeight (24);
    const int w = 110;
    return r.withX (r.getX() + i * (w + 4)).withWidth (w);
}

juce::Rectangle<int> RackPanel::rowRect (int position) const
{
    const int h = listArea.getHeight() / idg::NumFx;
    return { listArea.getX(), listArea.getY() + position * h, listArea.getWidth(), h };
}

int RackPanel::positionAt (juce::Point<int> p) const
{
    const int h = juce::jmax (1, listArea.getHeight() / idg::NumFx);
    return juce::jlimit (0, idg::NumFx - 1, (p.y - listArea.getY()) / h);
}

void RackPanel::selectRack (int r)
{
    rack = r;
    famMix.clear();
    const auto& reg = idg::rackRegistry (rack);
    for (int f = 0; f < idg::NumFamilies; ++f)
    {
        auto* k = famMix.add (new Knob (proc, reg.all[(size_t) reg.famMix[f]].id, idg::familyNames()[f], idg::familyColour (f)));
        addAndMakeVisible (k);
    }
    selectEffect (fx);
}

void RackPanel::selectEffect (int newFx)
{
    fx = newFx;
    rebuildEffectPanel();
    auto& d = proc.rack (rack).seq[fx];
    seqOn.setToggleState (d.enabled.load(), juce::dontSendNotification);
    seqRate.setSelectedItemIndex (d.rate.load(), juce::dontSendNotification);
    seqLen.setSelectedId (d.length.load(), juce::dontSendNotification);
    seqSwing.setValue (d.swing.load(), juce::dontSendNotification);
    seqSwing.updateText();
    grid.setTarget (&d, idg::familyColour ((int) idg::effectDefs()[(size_t) fx].family));
    resized();
    repaint();
}

void RackPanel::rebuildEffectPanel()
{
    fxControls.clear();
    const auto& reg = idg::rackRegistry (rack);
    const auto& def = idg::effectDefs()[(size_t) fx];
    const auto col = idg::familyColour ((int) def.family);
    fxOn = std::make_unique<ParamToggle> (proc, reg.all[(size_t) reg.fxOn[fx]].id, "ON", col);
    fxPar = std::make_unique<ParamToggle> (proc, reg.all[(size_t) reg.fxPar[fx]].id, IDG_T ("PARALLÈLE"), Theme::blue);
    fxMix = std::make_unique<Knob> (proc, reg.all[(size_t) reg.fxMix[fx]].id, "Mix", col);
    fxMode = std::make_unique<ParamCombo> (proc, reg.all[(size_t) reg.fxMode[fx]].id);
    fxTrans = std::make_unique<ParamCombo> (proc, reg.all[(size_t) reg.fxTrans[fx]].id);
    fxPar->setTooltip (IDG_T ("En parallèle avec l'effet précédent dans la chaîne."));
    for (juce::Component* c : std::initializer_list<juce::Component*> { fxOn.get(), fxPar.get(), fxMix.get(), fxMode.get(), fxTrans.get() })
        addAndMakeVisible (c);
    for (int k = 0; k < reg.fxCount[fx]; ++k)
    {
        const auto& pd = reg.all[(size_t) (reg.fxFirst[fx] + k)];
        const auto label = def.params[(size_t) k].name;
        juce::Component* c;
        if (pd.type == idg::PType::Bool) c = new ParamToggle (proc, pd.id, label.toUpperCase(), col);
        else if (pd.type == idg::PType::Choice) c = new ParamCombo (proc, pd.id);
        else c = new Knob (proc, pd.id, label, col);
        fxControls.add (c);
        addAndMakeVisible (c);
    }
}

void RackPanel::timerCallback()
{
    repaint (listArea);
}

void RackPanel::paint (juce::Graphics& g)
{
    frame (g, getLocalBounds(), "Rack  //  effets IDGems", busColour (rack));
    for (int i = 0; i < idg::NumRacks; ++i)
    {
        auto t = tabRect (i).toFloat();
        const bool sel = i == rack;
        g.setColour (sel ? busColour (i).withAlpha (0.25f) : Theme::bg.withAlpha (0.5f));
        g.fillRect (t);
        g.setColour (sel ? busColour (i) : Theme::line);
        g.drawRect (t, 1.0f);
        g.setFont (Theme::mono (11.0f, sel));
        g.setColour (sel ? Theme::text : Theme::textDim);
        g.drawText (idg::rackDisplayName (i).toUpperCase(), t, juce::Justification::centred);
    }
    // liste des effets dans l'ordre du rack
    const auto& reg = idg::rackRegistry (rack);
    const auto order = proc.rack (rack).shared.getOrder();
    const auto& defs = idg::effectDefs();
    for (int pos = 0; pos < idg::NumFx; ++pos)
    {
        const int e = order[(size_t) pos];
        auto row = rowRect (pos).toFloat().reduced (1.0f, 0.5f);
        const auto col = idg::familyColour ((int) defs[(size_t) e].family);
        const bool on = proc.getValue (reg.all[(size_t) reg.fxOn[e]].id) > 0.5f;
        const bool sel = e == fx;
        g.setColour (sel ? col.withAlpha (0.22f) : Theme::bg.withAlpha (0.45f));
        g.fillRect (row);
        // pastille on/off
        auto dot = row.removeFromLeft (row.getHeight()).reduced (row.getHeight() * 0.25f);
        g.setColour (on ? col : Theme::line);
        if (on) g.fillEllipse (dot); else g.drawEllipse (dot, 1.2f);
        // activité
        const float act = juce::jlimit (0.0f, 1.0f, proc.rack (rack).shared.activity[e].load() * 3.0f);
        g.setColour (col.withAlpha (0.5f));
        g.fillRect (row.removeFromRight (40.0f).reduced (2.0f, row.getHeight() * 0.3f).withWidth (36.0f * act));
        g.setFont (Theme::mono (juce::jmin (11.0f, row.getHeight() * 0.7f), sel));
        g.setColour (on ? Theme::text : Theme::textDim);
        const bool par = proc.getValue (reg.all[(size_t) reg.fxPar[e]].id) > 0.5f;
        const bool seq = proc.rack (rack).seq[e].enabled.load();
        g.drawText ((par ? "|| " : "") + defs[(size_t) e].name + (seq ? "  [SEQ]" : ""), row.withTrimmedLeft (3.0f), juce::Justification::centredLeft);
    }
    // titre de l'effet sélectionné
    const auto& d = defs[(size_t) fx];
    Theme::tapeLabel (g, d.name.toUpperCase(), (float) panelArea.getX(), (float) panelArea.getY(), idg::familyColour ((int) d.family), 15.0f);
    g.setFont (Theme::mono (10.0f));
    g.setColour (Theme::textDim);
    g.drawFittedText (d.help, panelArea.withTrimmedTop (20).withHeight (26), juce::Justification::topLeft, 2);
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("glisser une ligne = réordonner  //  clic sur la pastille = on/off"), listArea.withTop (listArea.getBottom() + 2).withHeight (12),
                juce::Justification::centredLeft);
}

void RackPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < idg::NumRacks; ++i)
        if (tabRect (i).contains (e.getPosition())) { selectRack (i); return; }
    if (! listArea.contains (e.getPosition())) return;
    const int pos = positionAt (e.getPosition());
    const auto order = proc.rack (rack).shared.getOrder();
    const int eff = order[(size_t) pos];
    auto row = rowRect (pos);
    if (e.x < row.getX() + row.getHeight())
    {
        const auto& reg = idg::rackRegistry (rack);
        if (auto* prm = proc.apvts.getParameter (reg.all[(size_t) reg.fxOn[eff]].id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
            prm->endChangeGesture();
        }
    }
    dragFrom = pos;
    dragMoved = false;
    if (eff != fx) selectEffect (eff);
    repaint();
}

void RackPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    const int to = positionAt (e.getPosition());
    if (to == dragFrom) return;
    auto order = proc.rack (rack).shared.getOrder();
    const int moving = order[(size_t) dragFrom];
    if (to > dragFrom) std::rotate (order.begin() + dragFrom, order.begin() + dragFrom + 1, order.begin() + to + 1);
    else std::rotate (order.begin() + to, order.begin() + dragFrom, order.begin() + dragFrom + 1);
    juce::ignoreUnused (moving);
    proc.rack (rack).shared.setOrder (order);
    dragFrom = to;
    dragMoved = true;
    repaint();
}

void RackPanel::mouseUp (const juce::MouseEvent&) { dragFrom = -1; }

void RackPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8).withTrimmedTop (26 + 30);
    famArea = r.removeFromTop (58);
    {
        auto a = famArea;
        const int w = a.getWidth() / idg::NumFamilies;
        for (auto* k : famMix) k->setBounds (a.removeFromLeft (w).reduced (8, 0));
    }
    r.removeFromTop (6);
    listArea = r.removeFromLeft (230).withTrimmedBottom (14);
    r.removeFromLeft (10);
    panelArea = r;
    auto p = r.withTrimmedTop (50);
    auto common = p.removeFromTop (24);
    if (fxOn) fxOn->setBounds (common.removeFromLeft (46));
    common.removeFromLeft (4);
    if (fxMode) fxMode->setBounds (common.removeFromLeft (84));
    common.removeFromLeft (4);
    if (fxTrans) fxTrans->setBounds (common.removeFromLeft (96));
    common.removeFromLeft (4);
    if (fxPar) fxPar->setBounds (common.removeFromLeft (96));
    p.removeFromTop (6);
    seqArea = p.removeFromBottom (p.getHeight() * 40 / 100);
    // réglages : Mix + paramètres de l'effet en grille
    const int n = fxControls.size() + 1;
    const int cols = juce::jmin (6, juce::jmax (3, (n + 1) / 2));
    const int rows = (n + cols - 1) / cols;
    const int cw = p.getWidth() / cols, ch = juce::jmin (70, p.getHeight() / juce::jmax (1, rows));
    for (int i = 0; i < n; ++i)
    {
        juce::Component* c = i == 0 ? (juce::Component*) fxMix.get() : fxControls[i - 1];
        if (c == nullptr) continue;
        auto cell = juce::Rectangle<int> (p.getX() + (i % cols) * cw, p.getY() + (i / cols) * ch, cw, ch).reduced (3, 2);
        if (dynamic_cast<juce::ComboBox*> (c) != nullptr || dynamic_cast<juce::TextButton*> (c) != nullptr)
            c->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 22));
        else c->setBounds (cell);
    }
    auto s = seqArea;
    auto bar = s.removeFromTop (22);
    seqOn.setBounds (bar.removeFromLeft (120));
    bar.removeFromLeft (4);
    seqRate.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    seqLen.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (4);
    seqRandom.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (4);
    seqSwing.setBounds (bar);
    s.removeFromTop (4);
    grid.setBounds (s);
}

// =============================================================================
// OutputPanel
// =============================================================================
OutputPanel::OutputPanel (IDGemsSynthProcessor& p)
{
    auto add = [&] (const char* id, const juce::String& label, juce::Colour c = Theme::accent)
    {
        addAndMakeVisible (knobs.add (new Knob (p, id, label, c)));
    };
    add (P::inGain, "Gain in");
    add (P::masterMix, "Master D/W");
    add (P::ottDepth, "OTT", Theme::blue);
    add (P::ottTime, "Temps", Theme::blue);
    add (P::ottUp, "Up", Theme::blue);
    add (P::ottDown, "Down", Theme::blue);
    add (P::ottGain, "Gain OTT", Theme::blue);
    add (P::clipDrive, "Clip", Theme::red);
    add (P::ceiling, "Plafond", Theme::red);
    add (P::limRelease, "Release", Theme::red);
    add (P::hpFreq, "Passe-haut");
    add (P::monoFreq, "Mono < Hz");
    add (P::outGain, "Gain out");
    monoOn = std::make_unique<ParamToggle> (p, P::monoOn, "MONO");
    zeroLat = std::make_unique<ParamToggle> (p, P::zeroLat, "0 LATENCE", Theme::blue);
    destOS = std::make_unique<ParamCombo> (p, P::destOS);
    for (juce::Component* c : std::initializer_list<juce::Component*> { monoOn.get(), zeroLat.get(), destOS.get() }) addAndMakeVisible (c);
}

void OutputPanel::paint (juce::Graphics& g)
{
    frame (g, getLocalBounds(), IDG_T ("Master  //  OTT -> soft clip -> limiteur"), Theme::red);
    const juce::String titles[] { "MASTER", "OTT", "CLIP / LIMITEUR", "NETTOYAGE", "OPTIONS" };
    g.setFont (Theme::mono (9.0f, true));
    for (int i = 0; i < 5; ++i)
    {
        g.setColour (Theme::textDim);
        g.drawText (titles[i], groups[i].withHeight (12), juce::Justification::centredLeft);
        if (i > 0) { g.setColour (Theme::line); g.drawVerticalLine (groups[i].getX() - 5, (float) groups[i].getY(), (float) groups[i].getBottom()); }
    }
}

void OutputPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8).withTrimmedTop (24);
    const int unit = r.getWidth() / 16;
    const int widths[] { 2, 5, 3, 4, 2 };
    for (int i = 0; i < 5; ++i) groups[i] = r.removeFromLeft (unit * widths[i]).withTrimmedLeft (i ? 8 : 0);
    auto place = [] (juce::Rectangle<int> g, std::initializer_list<juce::Component*> cs)
    {
        g.removeFromTop (12);
        const int w = g.getWidth() / juce::jmax (1, (int) cs.size());
        for (auto* c : cs) c->setBounds (g.removeFromLeft (w).reduced (2, 0));
    };
    place (groups[0], { knobs[0], knobs[1] });
    place (groups[1], { knobs[2], knobs[3], knobs[4], knobs[5], knobs[6] });
    place (groups[2], { knobs[7], knobs[8], knobs[9] });
    {
        auto g = groups[3];
        auto left = g.removeFromLeft (g.getWidth() / 4).withTrimmedTop (12);
        monoOn->setBounds (left.withSizeKeepingCentre (left.getWidth() - 4, 22));
        place (g.withTrimmedTop (0), { knobs[10], knobs[11], knobs[12] });
    }
    {
        auto g = groups[4].withTrimmedTop (14);
        zeroLat->setBounds (g.removeFromTop (24).reduced (2, 0));
        g.removeFromTop (6);
        destOS->setBounds (g.removeFromTop (22).reduced (2, 0));
        destOS->setTooltip (IDG_T ("Suréchantillonnage de la famille Destruction."));
    }
}

// =============================================================================
// FxPage
// =============================================================================
FxPage::FxPage (IDGemsSynthProcessor& p) : routing (p), rackPanel (p), output (p)
{
    addAndMakeVisible (routing);
    addAndMakeVisible (rackPanel);
    addAndMakeVisible (output);
}

void FxPage::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    output.setBounds (r.removeFromBottom (128));
    r.removeFromBottom (6);
    routing.setBounds (r.removeFromLeft (r.getWidth() * 36 / 100));
    r.removeFromLeft (6);
    rackPanel.setBounds (r);
}
} // namespace idgs::ui
