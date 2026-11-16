#include "ModUI.h"

namespace idgs::ui
{
// =============================================================================
// ModSourceHandle
// =============================================================================
ModSourceHandle::ModSourceHandle (IDGemsSynthProcessor& p, int s) : proc (p), src (s)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip (sourceName (s) + IDG_T (" : attrape la pastille et glisse-la sur un potard (ou une courbe de filtre, une wavetable) pour l'assigner.\nSurvol = ses cibles s'allument."));
    startTimerHz (20);
}

void ModSourceHandle::timerCallback()
{
    if (dragging && proc.uiDragSource.load() < 0) { dragging = false; repaint(); } // fin du glisser-déposer
    const float v = proc.modShared.sourceValue[src].load();
    const int c = proc.modShared.countForSource (src);
    const int hp = proc.uiHoverParam.load();
    if (std::abs (v - lastVal) > 0.004f || c != lastCount || hp != lastHoverParam) { lastVal = v; lastCount = c; lastHoverParam = hp; repaint(); }
}

void ModSourceHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    auto circ = r.withSizeKeepingCentre (d, d);
    const auto c = sourceColour (src);
    // la cible survolée est modulée par cette source : la pastille s'allume
    const int hp = proc.uiHoverParam.load();
    const bool targetsHovered = hp >= 0 && proc.modShared.findConnection (src, hp) >= 0;
    if (targetsHovered || dragging || proc.uiHoverSource.load() == src)
    {
        g.setColour (c.withAlpha (0.35f));
        g.fillEllipse (circ.expanded (2.5f));
    }
    g.setColour (Theme::bg);
    g.fillEllipse (circ);
    // anneau = valeur courante
    juce::Path ring;
    const float v = juce::jlimit (0.0f, 1.0f, proc.modShared.sourceValue[src].load());
    ring.addCentredArc (circ.getCentreX(), circ.getCentreY(), d * 0.42f, d * 0.42f, 0.0f, 0.0f, v * juce::MathConstants<float>::twoPi, true);
    g.setColour (c);
    g.strokePath (ring, juce::PathStrokeType (2.0f));
    g.setColour (c.withAlpha (0.5f));
    g.drawEllipse (circ.reduced (d * 0.08f), 1.0f);
    const int count = proc.modShared.countForSource (src);
    if (count > 0)
    {
        g.setFont (Theme::mono (juce::jmax (7.0f, d * 0.38f), true));
        g.setColour (c.brighter (0.4f));
        g.drawText (juce::String (count), circ, juce::Justification::centred);
    }
    else
    {
        g.setColour (c);
        g.fillEllipse (circ.withSizeKeepingCentre (d * 0.22f, d * 0.22f));
    }
}

void ModSourceHandle::mouseEnter (const juce::MouseEvent&) { proc.uiHoverSource = src; repaint(); }

void ModSourceHandle::mouseExit (const juce::MouseEvent&)
{
    if (proc.uiHoverSource.load() == src) proc.uiHoverSource = -1;
    repaint();
}

void ModSourceHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        proc.uiDragSource = src;
        juce::Image img (juce::Image::ARGB, 18, 18, true);
        {
            juce::Graphics ig (img);
            ig.setColour (sourceColour (src));
            ig.fillEllipse (1.0f, 1.0f, 16.0f, 16.0f);
        }
        container->startDragging ("mod:" + juce::String (src), this, juce::ScaledImage (img), true);
    }
}

// =============================================================================
// LfoEditor
// =============================================================================
LfoEditor::LfoEditor (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (30); }

juce::Point<float> LfoEditor::toScreen (float x, float y) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    return { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() };
}

juce::Point<float> LfoEditor::fromScreen (juce::Point<float> p) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    return { juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()), juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight()) };
}

int LfoEditor::hitPoint (juce::Point<float> p) const
{
    const auto& s = proc.modShared.shapes[lfo];
    for (int i = 0; i < (int) s.pts.size(); ++i)
        if (toScreen (s.pts[(size_t) i].x, s.pts[(size_t) i].y).getDistanceFrom (p) < 7.0f) return i;
    return -1;
}

void LfoEditor::publish (const idg::LfoShape& s)
{
    proc.modShared.setShape (lfo, s);
    repaint();
}

void LfoEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg.withAlpha (0.85f));
    g.fillRect (r);
    g.setColour (Theme::line);
    g.drawRect (r, 1.0f);
    auto in = r.reduced (6.0f);
    for (int i = 1; i < grid; ++i)
    {
        const float x = in.getX() + in.getWidth() * (float) i / (float) grid;
        g.setColour (Theme::line.withAlpha (i % 4 == 0 ? 0.9f : 0.4f));
        g.drawVerticalLine ((int) x, in.getY(), in.getBottom());
    }
    g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());

    const auto& s = proc.modShared.shapes[lfo];
    juce::Path p, fill;
    constexpr int N = 200;
    for (int i = 0; i <= N; ++i)
    {
        const float x = (float) i / N;
        auto pt = toScreen (x, s.valueAt (x));
        if (i == 0) { p.startNewSubPath (pt); fill.startNewSubPath (toScreen (0, 0)); fill.lineTo (pt); }
        else { p.lineTo (pt); fill.lineTo (pt); }
    }
    fill.lineTo (toScreen (1, 0));
    fill.closeSubPath();
    const auto c = sourceColour (MSrcLfo0);
    g.setColour (c.withAlpha (0.14f));
    g.fillPath (fill);
    g.setColour (juce::Colour (0xffff1e3c).withAlpha (0.3f));
    g.strokePath (p, juce::PathStrokeType (1.5f), juce::AffineTransform::translation (-1.5f, 0));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (2.0f));
    if (! pencil)
    {
        const float ps = s.pts.size() > 12 ? 5.0f : 9.0f;
        for (auto& pt : s.pts)
        {
            auto sp = toScreen (pt.x, pt.y);
            g.setColour (Theme::bg);
            g.fillEllipse (juce::Rectangle<float> (ps, ps).withCentre (sp));
            g.setColour (Theme::accent);
            g.drawEllipse (juce::Rectangle<float> (ps, ps).withCentre (sp), 1.2f);
        }
    }
    // tête de lecture
    const float ph = proc.modShared.lfoPhase[lfo].load();
    auto hp = toScreen (ph, s.valueAt (ph));
    g.setColour (Theme::text.withAlpha (0.35f));
    g.drawVerticalLine ((int) hp.x, in.getY(), in.getBottom());
    g.setColour (Theme::text);
    g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (hp));
    g.setColour (Theme::textDim);
    g.setFont (Theme::mono (9.0f));
    g.drawText (pencil ? IDG_T ("CRAYON : dessine sur la grille") : IDG_T ("clic : ajouter  /  glisser : déplacer  /  double-clic : supprimer  /  Maj+glisser : courbe"),
                in.withTop (in.getBottom() - 11), juce::Justification::centredLeft);
}

void LfoEditor::pencilAt (juce::Point<float> sp)
{
    auto v = fromScreen (sp);
    const int cell = juce::jlimit (0, grid - 1, (int) (v.x * (float) grid));
    std::vector<float> vals ((size_t) grid);
    for (int i = 0; i < grid; ++i) vals[(size_t) i] = work.valueAt (((float) i + 0.5f) / (float) grid);
    vals[(size_t) cell] = v.y;
    idg::LfoShape s;
    for (int i = 0; i < grid; ++i)
    {
        s.pts.push_back ({ (float) i / (float) grid, vals[(size_t) i], 0.0f });
        s.pts.push_back ({ (float) (i + 1) / (float) grid - 0.0001f, vals[(size_t) i], 0.0f });
    }
    s.pts.back().x = 1.0f;
    work = s;
    publish (s);
}

void LfoEditor::mouseDown (const juce::MouseEvent& e)
{
    work = proc.modShared.shapes[lfo];
    const auto pos = e.position;
    if (pencil) { pencilAt (pos); return; }
    dragPt = hitPoint (pos);
    curveSeg = -1;
    if (dragPt < 0)
    {
        auto v = fromScreen (pos);
        if (e.mods.isShiftDown())
        {
            for (int i = 0; i + 1 < (int) work.pts.size(); ++i)
                if (v.x >= work.pts[(size_t) i].x && v.x <= work.pts[(size_t) i + 1].x) { curveSeg = i; curveStart = work.pts[(size_t) i].curve; break; }
            return;
        }
        work.pts.push_back ({ v.x, v.y, 0.0f });
        work.sortPoints();
        dragPt = hitPoint (pos);
        publish (work);
    }
}

void LfoEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (pencil) { pencilAt (e.position); return; }
    if (curveSeg >= 0)
    {
        work.pts[(size_t) curveSeg].curve = juce::jlimit (-1.0f, 1.0f, curveStart - (float) e.getDistanceFromDragStartY() / 80.0f);
        publish (work);
        return;
    }
    if (dragPt < 0) return;
    auto v = fromScreen (e.position);
    auto& pts = work.pts;
    const int n = (int) pts.size();
    if (dragPt == 0) v.x = 0.0f;
    else if (dragPt == n - 1) v.x = 1.0f;
    else v.x = juce::jlimit (pts[(size_t) dragPt - 1].x, pts[(size_t) dragPt + 1].x, v.x);
    if (e.mods.isCommandDown()) v.y = std::round (v.y * 8.0f) / 8.0f;
    pts[(size_t) dragPt].x = v.x;
    pts[(size_t) dragPt].y = v.y;
    publish (work);
}

void LfoEditor::mouseUp (const juce::MouseEvent&) { dragPt = -1; curveSeg = -1; }

void LfoEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (pencil) return;
    work = proc.modShared.shapes[lfo];
    const int i = hitPoint (e.position);
    if (i > 0 && i < (int) work.pts.size() - 1)
    {
        work.pts.erase (work.pts.begin() + i);
        publish (work);
    }
}

// =============================================================================
// LfoPanel
// =============================================================================
LfoPanel::Ctl::Ctl (IDGemsSynthProcessor& p, int i)
    : mode (p, lfoId (i, "mode")), div (p, lfoId (i, "div")), sync (p, lfoId (i, "sync"), "SYNC", Theme::blue),
      rate (p, lfoId (i, "rate"), "Rate", Theme::blue), phase (p, lfoId (i, "phase"), "Phase", Theme::blue),
      fade (p, lfoId (i, "fade"), "Fade in", Theme::blue), smooth (p, lfoId (i, "smooth"), "Smooth", Theme::blue),
      stereo (p, lfoId (i, "stereo"), IDG_T ("Stéréo"), Theme::blue)
{
}

LfoPanel::LfoPanel (IDGemsSynthProcessor& p) : proc (p), editor (p)
{
    addAndMakeVisible (editor);
    for (int i = 0; i < NumLfos; ++i)
    {
        addAndMakeVisible (handles.add (new ModSourceHandle (p, MSrcLfo0 + i)));
        auto* c = ctls.add (new Ctl (p, i));
        for (juce::Component* comp : std::initializer_list<juce::Component*> { &c->mode, &c->div, &c->sync, &c->rate, &c->phase, &c->fade, &c->smooth, &c->stereo })
            addChildComponent (comp);
    }
    pencil.setClickingTogglesState (true);
    pencil.setColour (juce::TextButton::buttonOnColourId, Theme::blue);
    pencil.setTooltip (IDG_T ("Crayon : dessine la forme en marches sur la grille."));
    pencil.onClick = [this] { editor.setPencil (pencil.getToggleState()); };
    shapes.setTooltip (IDG_T ("Formes prédéfinies."));
    shapes.onClick = [this]
    {
        juce::PopupMenu m;
        const auto& names = idg::LfoShape::presetNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        juce::Component::SafePointer<LfoPanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapes), [safe] (int r)
        {
            if (safe != nullptr && r > 0) safe->proc.modShared.setShape (safe->selected, idg::LfoShape::preset (r - 1));
        });
    };
    gridBox.addItemList ({ "4", "8", "16", "32" }, 1);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    gridBox.setTooltip (IDG_T ("Grille du crayon."));
    gridBox.onChange = [this] { editor.setGrid (gridBox.getText().getIntValue()); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &pencil, &shapes, &gridBox }) addAndMakeVisible (c);
    select (0);
}

juce::Rectangle<int> LfoPanel::tabRect (int i) const
{
    auto a = getLocalBounds().reduced (8, 8).removeFromLeft (64);
    const int h = a.getHeight() / NumLfos;
    return a.withTrimmedTop (h * i).withHeight (h);
}

void LfoPanel::select (int i)
{
    selected = i;
    editor.setLfo (i);
    for (int k = 0; k < ctls.size(); ++k)
    {
        auto* c = ctls[k];
        const bool v = k == i;
        for (juce::Component* comp : std::initializer_list<juce::Component*> { &c->mode, &c->sync, &c->phase, &c->fade, &c->smooth, &c->stereo })
            comp->setVisible (v);
        const bool sync = proc.getValue (lfoId (k, "sync")) > 0.5f;
        c->div.setVisible (v && sync);
        c->rate.setVisible (v && ! sync);
    }
    lastSync = -1;
    resized();
    repaint();
}

void LfoPanel::tick()
{
    const int sync = proc.getValue (lfoId (selected, "sync")) > 0.5f ? 1 : 0;
    if (sync != lastSync) { lastSync = sync; ctls[selected]->div.setVisible (sync == 1); ctls[selected]->rate.setVisible (sync == 0); }
}

void LfoPanel::paint (juce::Graphics& g)
{
    Theme::panelFrame (g, getLocalBounds().toFloat().reduced (1.0f), Theme::blue);
    for (int i = 0; i < NumLfos; ++i)
    {
        auto r = tabRect (i).reduced (1).toFloat();
        const bool sel = i == selected;
        g.setColour (sel ? Theme::blue.withAlpha (0.2f) : Theme::bg.withAlpha (0.4f));
        g.fillRect (r);
        if (sel) { g.setColour (Theme::blue); g.fillRect (r.withWidth (3.0f)); }
        g.setFont (Theme::mono (9.5f, sel));
        g.setColour (sel ? Theme::text : Theme::textDim);
        g.drawText ("LFO " + juce::String (i + 1), r.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
    }
}

void LfoPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < NumLfos; ++i)
        if (tabRect (i).contains (e.getPosition())) select (i);
}

void LfoPanel::resized()
{
    for (int i = 0; i < NumLfos; ++i)
    {
        auto t = tabRect (i);
        handles[i]->setBounds (t.removeFromRight (juce::jmin (18, t.getHeight())).reduced (1));
    }
    auto r = getLocalBounds().reduced (8, 8);
    r.removeFromLeft (70);
    auto top = r.removeFromTop (22);
    auto* c = ctls[selected];
    c->mode.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (4);
    c->sync.setBounds (top.removeFromLeft (52));
    top.removeFromLeft (4);
    c->div.setBounds (top.removeFromLeft (70));
    gridBox.setBounds (top.removeFromRight (48));
    top.removeFromRight (3);
    pencil.setBounds (top.removeFromRight (64));
    top.removeFromRight (3);
    shapes.setBounds (top.removeFromRight (64));
    r.removeFromTop (4);
    auto knobs = r.removeFromBottom (54);
    const int kw = knobs.getWidth() / 5;
    c->rate.setBounds (knobs.removeFromLeft (kw).reduced (2, 0));
    c->phase.setBounds (knobs.removeFromLeft (kw).reduced (2, 0));
    c->fade.setBounds (knobs.removeFromLeft (kw).reduced (2, 0));
    c->smooth.setBounds (knobs.removeFromLeft (kw).reduced (2, 0));
    c->stereo.setBounds (knobs.reduced (2, 0));
    r.removeFromBottom (4);
    editor.setBounds (r);
}

// =============================================================================
// SourcesPanel
// =============================================================================
SourcesPanel::Ctl::Ctl (IDGemsSynthProcessor& p, int i)
    : style (p, rndId (i, "style")), div (p, rndId (i, "div")), sync (p, rndId (i, "sync"), "SYNC", Theme::blue),
      retrig (p, rndId (i, "retrig"), "NOTE", Theme::blue), rate (p, rndId (i, "rate"), "Rate", juce::Colour (0xffb26bff))
{
}

SourcesPanel::SourcesPanel (IDGemsSynthProcessor& p) : proc (p)
{
    for (int i = 0; i < NumRandMods; ++i)
    {
        addAndMakeVisible (rndHandles.add (new ModSourceHandle (p, MSrcRnd0 + i)));
        auto* c = ctls.add (new Ctl (p, i));
        for (juce::Component* comp : std::initializer_list<juce::Component*> { &c->style, &c->div, &c->sync, &c->retrig, &c->rate })
            addChildComponent (comp);
        c->retrig.setTooltip (IDG_T ("Nouveau tirage à chaque note jouée."));
    }
    for (int s : { (int) MSrcVelocity, (int) MSrcNote, (int) MSrcNoteRandom, (int) MSrcModWheel, (int) MSrcAftertouch, (int) MSrcPitchBend })
        addAndMakeVisible (playHandles.add (new ModSourceHandle (p, s)));
    selected = -1;
    juce::MouseEvent* none = nullptr;
    juce::ignoreUnused (none);
    // premier random sélectionné
    for (int i = 0; i < ctls.size(); ++i)
        for (juce::Component* comp : std::initializer_list<juce::Component*> { &ctls[i]->style, &ctls[i]->div, &ctls[i]->sync, &ctls[i]->retrig, &ctls[i]->rate })
            comp->setVisible (i == 0);
    selected = 0;
}

void SourcesPanel::paint (juce::Graphics& g)
{
    Theme::panelFrame (g, getLocalBounds().toFloat().reduced (1.0f), juce::Colour (0xffb26bff));
    g.setFont (Theme::mono (9.0f, true));
    for (int i = 0; i < NumRandMods; ++i)
    {
        auto cell = rndHandles[i]->getBounds().getUnion (rndHandles[i]->getBounds().translated (0, 14)).toFloat().expanded (3.0f, 2.0f);
        if (i == selected) { g.setColour (juce::Colour (0xffb26bff).withAlpha (0.2f)); g.fillRect (cell); }
        g.setColour (Theme::textDim);
        g.drawText ("RND " + juce::String (i + 1), rndHandles[i]->getBounds().translated (0, rndHandles[i]->getHeight()).withHeight (12).expanded (12, 0),
                    juce::Justification::centred);
    }
    static const char* names[] { "VELO", "NOTE", "RND N", "MOLET", "AFTER", "BEND" };
    for (int i = 0; i < playHandles.size(); ++i)
    {
        g.setColour (Theme::textDim);
        g.drawText (names[i], playHandles[i]->getBounds().translated (0, playHandles[i]->getHeight()).withHeight (12).expanded (12, 0),
                    juce::Justification::centred);
    }
    g.setColour (Theme::line);
    g.drawHorizontalLine (playArea.getY() - 3, (float) playArea.getX(), (float) playArea.getRight());
}

void SourcesPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < NumRandMods; ++i)
        if (rndHandles[i]->getBounds().expanded (12, 16).contains (e.getPosition()))
        {
            selected = i;
            for (int k = 0; k < ctls.size(); ++k)
                for (juce::Component* comp : std::initializer_list<juce::Component*> { &ctls[k]->style, &ctls[k]->div, &ctls[k]->sync, &ctls[k]->retrig, &ctls[k]->rate })
                    comp->setVisible (k == i);
            resized();
            repaint();
        }
}

void SourcesPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    rndArea = r.removeFromTop (r.getHeight() * 32 / 100);
    r.removeFromTop (4);
    ctlArea = r.removeFromTop (r.getHeight() * 52 / 100);
    r.removeFromTop (8);
    playArea = r;
    const int hs = juce::jmin (26, rndArea.getHeight() - 14);
    const int w = rndArea.getWidth() / NumRandMods;
    for (int i = 0; i < NumRandMods; ++i)
        rndHandles[i]->setBounds (juce::Rectangle<int> (rndArea.getX() + i * w, rndArea.getY(), w, hs).withSizeKeepingCentre (hs, hs));
    const int pw = playArea.getWidth() / playHandles.size();
    const int ps = juce::jmin (24, playArea.getHeight() - 14);
    for (int i = 0; i < playHandles.size(); ++i)
        playHandles[i]->setBounds (juce::Rectangle<int> (playArea.getX() + i * pw, playArea.getY(), pw, ps).withSizeKeepingCentre (ps, ps));
    if (selected >= 0)
    {
        auto* c = ctls[selected];
        auto a = ctlArea;
        auto left = a.removeFromLeft (a.getWidth() * 62 / 100);
        auto row1 = left.removeFromTop (left.getHeight() / 2).withSizeKeepingCentre (left.getWidth(), 22);
        c->style.setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (2, 0));
        c->retrig.setBounds (row1.reduced (2, 0));
        auto row2 = left.withSizeKeepingCentre (left.getWidth(), 22);
        c->sync.setBounds (row2.removeFromLeft (row2.getWidth() / 2).reduced (2, 0));
        c->div.setBounds (row2.reduced (2, 0));
        c->rate.setBounds (a.reduced (2, 0));
    }
}

// =============================================================================
// ModListPage
// =============================================================================
ModListPage::Row::Row (IDGemsSynthProcessor& p, int s) : proc (p), slot (s)
{
    depth.setSliderStyle (juce::Slider::LinearBar);
    depth.setRange (-1.0, 1.0, 0.01);
    depth.setValue (p.modShared.slots[s].depth.load(), juce::dontSendNotification);
    depth.setTextValueSuffix (" ");
    depth.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    depth.setColour (juce::Slider::trackColourId, sourceColour (p.modShared.slots[s].src.load()).withAlpha (0.6f));
    depth.onValueChange = [this] { proc.modShared.slots[slot].depth = (float) depth.getValue(); ++proc.modShared.version; };
    bipolar.setToggleState (p.modShared.slots[s].bipolar.load(), juce::dontSendNotification);
    bipolar.onClick = [this] { proc.modShared.slots[slot].bipolar = bipolar.getToggleState(); ++proc.modShared.version; };
    remove.onClick = [this] { proc.modShared.removeConnection (slot); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &depth, &bipolar, &remove }) addAndMakeVisible (c);
}

void ModListPage::Row::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
    g.setColour (Theme::panelHi.withAlpha (0.6f));
    g.fillRect (r);
    const int src = proc.modShared.slots[slot].src.load();
    const int prm = proc.modShared.slots[slot].param.load();
    g.setColour (sourceColour (src));
    g.fillEllipse (r.getX() + 6.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setFont (Theme::mono (12.0f, true));
    g.drawText (sourceName (src), juce::Rectangle<float> (r.getX() + 22.0f, r.getY(), 140.0f, r.getHeight()), juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.drawText ("->", juce::Rectangle<float> (r.getX() + 160.0f, r.getY(), 30.0f, r.getHeight()), juce::Justification::centred);
    g.setColour (Theme::text);
    juce::String name;
    if (prm >= 0 && prm < proc.getParameters().size()) name = proc.getParameters()[prm]->getName (40);
    g.drawText (name, juce::Rectangle<float> (r.getX() + 192.0f, r.getY(), 220.0f, r.getHeight()), juce::Justification::centredLeft);
}

void ModListPage::Row::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    r.removeFromLeft (420);
    remove.setBounds (r.removeFromRight (30));
    r.removeFromRight (6);
    bipolar.setBounds (r.removeFromRight (100));
    r.removeFromRight (6);
    depth.setBounds (r);
}

ModListPage::ModListPage (IDGemsSynthProcessor& p) : proc (p)
{
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);
    startTimerHz (5);
}

void ModListPage::timerCallback()
{
    if (! isShowing()) return;
    if (proc.modShared.version.load() != lastVersion) rebuild();
}

void ModListPage::rebuild()
{
    lastVersion = proc.modShared.version.load();
    rows.clear();
    for (int i = 0; i < MaxModSlots; ++i)
        if (proc.modShared.slots[i].src.load() >= 0) content.addAndMakeVisible (rows.add (new Row (proc, i)));
    resized();
    repaint();
}

void ModListPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12, 10).toFloat();
    Theme::panelFrame (g, r, Theme::accent);
    Theme::sectionTitle (g, IDG_T ("Modulations  //  ") + juce::String (rows.size()) + " / " + juce::String (MaxModSlots), r.reduced (10.0f, 7.0f), Theme::accent);
    if (rows.isEmpty())
    {
        g.setFont (Theme::mono (13.0f));
        g.setColour (Theme::textDim);
        g.drawText (IDG_T ("Aucune modulation. Dans l'onglet SYNTH, attrape la pastille d'une source (macro, LFO, enveloppe, random,"),
                    r.withTrimmedTop (r.getHeight() * 0.4f).withHeight (20.0f), juce::Justification::centred);
        g.drawText (IDG_T ("vélocité...) et lâche-la sur un potard, une courbe de filtre ou une wavetable."),
                    r.withTrimmedTop (r.getHeight() * 0.4f + 22.0f).withHeight (20.0f), juce::Justification::centred);
    }
}

void ModListPage::resized()
{
    auto r = getLocalBounds().reduced (22, 18).withTrimmedTop (24);
    view.setBounds (r);
    content.setSize (r.getWidth() - 10, juce::jmax (r.getHeight(), rows.size() * 32));
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * 32, content.getWidth(), 32);
}
} // namespace idgs::ui
