#include "PlayPage.h"

namespace idgs::ui
{
namespace
{
const juce::Colour playCol { 0xffffd23f };

void frame (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c, float alpha = 1.0f)
{
    Theme::panelFrame (g, r.toFloat().reduced (1.0f), c, alpha);
    Theme::sectionTitle (g, title, r.toFloat().reduced (10.0f, 7.0f), c);
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

void layoutGrid (juce::Rectangle<int> r, juce::OwnedArray<juce::Component>& cs, int cols)
{
    const int rows = (cs.size() + cols - 1) / cols;
    const int cw = r.getWidth() / juce::jmax (1, cols), ch = r.getHeight() / juce::jmax (1, rows);
    for (int i = 0; i < cs.size(); ++i)
    {
        auto cell = juce::Rectangle<int> (r.getX() + (i % cols) * cw, r.getY() + (i / cols) * ch, cw, ch).reduced (3, 2);
        auto* c = cs[i];
        if (dynamic_cast<juce::ComboBox*> (c) != nullptr || dynamic_cast<juce::TextButton*> (c) != nullptr)
            c->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 22));
        else c->setBounds (cell);
    }
}
} // namespace

// =============================================================================
// PlayModeButtons
// =============================================================================
void PlayModeButtons::paint (juce::Graphics& g)
{
    const juce::String labels[] { "DIRECT", IDG_T ("ARPÈGE"), IDG_T ("SÉQUENCEUR"), IDG_T ("GÉNÉRATEUR") };
    const float w = (float) getWidth() / NumPlayModes;
    for (int i = 0; i < NumPlayModes; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (3.0f);
        const bool sel = i == last;
        g.setColour (sel ? playCol.withAlpha (0.25f) : Theme::bg.withAlpha (0.6f));
        g.fillRect (r);
        g.setColour (sel ? playCol : Theme::line);
        g.drawRect (r, sel ? 2.0f : 1.0f);
        Theme::cutText (g, labels[i], r, 13.0f, sel ? playCol : Theme::textDim, juce::Justification::centred, sel ? 0.3f : 0.0f, true);
    }
}

void PlayModeButtons::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, NumPlayModes - 1, (int) (e.position.x / ((float) getWidth() / NumPlayModes)));
    setParam (proc, "play_mode", (float) i);
}

// =============================================================================
// NoteSeqEditor
// =============================================================================
juce::Rectangle<float> NoteSeqEditor::laneRect (int lane) const
{
    auto r = getLocalBounds().toFloat().withTrimmedLeft (64.0f);
    const float noteH = r.getHeight() * 0.42f;
    const float small = (r.getHeight() - noteH) / (float) (NumLanes - 1);
    if (lane == LNote) return r.withHeight (noteH);
    return r.withY (r.getY() + noteH + small * (float) (lane - 1)).withHeight (small);
}

int NoteSeqEditor::stepAt (float x) const
{
    const int len = (int) proc.getValue ("seq_len");
    auto r = laneRect (LNote);
    return juce::jlimit (0, len - 1, (int) ((x - r.getX()) / (r.getWidth() / (float) len)));
}

void NoteSeqEditor::paint (juce::Graphics& g)
{
    auto& d = proc.noteSeq;
    const int len = (int) proc.getValue ("seq_len");
    const int cur = d.current.load();
    const bool active = (int) proc.getValue ("play_mode") >= PlaySeq;
    const juce::String names[] { "NOTE", IDG_T ("VÉLOCITÉ"), "GATE", "PROBA", "RATCHET", "SLIDE", "CADENAS" };
    g.setFont (Theme::mono (9.0f, true));
    for (int l = 0; l < NumLanes; ++l)
    {
        auto lr = laneRect (l);
        g.setColour (Theme::bg.withAlpha (0.7f));
        g.fillRect (lr.reduced (0.0f, 1.0f));
        g.setColour (Theme::textDim);
        g.drawText (names[l], juce::Rectangle<float> (0.0f, lr.getY(), 60.0f, lr.getHeight()), juce::Justification::centredLeft);
    }
    const float w = laneRect (LNote).getWidth() / (float) len;
    auto nr = laneRect (LNote);
    g.setColour (Theme::line);
    g.drawHorizontalLine ((int) nr.getCentreY(), nr.getX(), nr.getRight());
    for (int i = 0; i < len; ++i)
    {
        const float x = nr.getX() + (float) i * w;
        const bool on = d.on[i].load();
        const auto c = (on ? playCol : Theme::textDim).withAlpha (active ? 1.0f : 0.5f);
        if (i % 4 == 0) { g.setColour (Theme::line.withAlpha (0.6f)); g.drawVerticalLine ((int) x, nr.getY(), (float) getHeight()); }
        if (i == cur && active) { g.setColour (Theme::text.withAlpha (0.12f)); g.fillRect (x, 0.0f, w, (float) getHeight()); }
        // note : barre depuis le centre (+/- 24 demi-tons)
        const int note = d.note[i].load();
        const float y = nr.getCentreY() - (float) note / 24.0f * nr.getHeight() * 0.48f;
        auto bar = juce::Rectangle<float> (x + 2.0f, juce::jmin (y, nr.getCentreY()), w - 4.0f, juce::jmax (2.0f, std::abs (y - nr.getCentreY())));
        g.setColour (c.withAlpha (on ? 0.35f : 0.15f));
        g.fillRect (bar);
        g.setColour (c);
        g.fillRect (x + 2.0f, y - 2.0f, w - 4.0f, 4.0f);
        g.setFont (Theme::mono (8.0f));
        g.drawText ((note > 0 ? "+" : "") + juce::String (note), juce::Rectangle<float> (x, nr.getBottom() - 11.0f, w, 10.0f), juce::Justification::centred);
        // couloirs de valeurs
        auto bar01 = [&] (int lane, float v)
        {
            auto lr = laneRect (lane).reduced (0.0f, 2.0f);
            g.setColour (c.withAlpha (on ? 0.8f : 0.3f));
            g.fillRect (x + 2.0f, lr.getBottom() - v * lr.getHeight(), w - 4.0f, v * lr.getHeight());
        };
        bar01 (LVel, d.vel[i].load());
        bar01 (LGate, d.gate[i].load());
        bar01 (LProb, d.prob[i].load());
        auto rr = laneRect (LRatchet);
        g.setColour (c);
        const int rt = d.ratchet[i].load();
        for (int k = 0; k < rt; ++k) g.fillRect (x + 3.0f + (float) k * (w - 6.0f) / 4.0f, rr.getY() + 3.0f, (w - 6.0f) / 4.0f - 1.0f, rr.getHeight() - 6.0f);
        auto toggleBox = [&] (int lane, bool v, juce::Colour col)
        {
            auto lr = laneRect (lane);
            auto b = juce::Rectangle<float> (x, lr.getY(), w, lr.getHeight()).reduced (w * 0.25f, lr.getHeight() * 0.2f);
            g.setColour (v ? col : Theme::line);
            if (v) g.fillRect (b); else g.drawRect (b, 1.0f);
        };
        toggleBox (LSlide, d.slide[i].load(), Theme::blue);
        toggleBox (LLock, d.lock[i].load(), Theme::red);
    }
    g.setFont (Theme::mono (8.5f));
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("clic = pas on/off  //  glisser vertical = hauteur  //  couloirs : glisser la valeur  //  cadenas = jamais muté"),
                getLocalBounds().toFloat().withTrimmedLeft (64.0f).withHeight (11.0f), juce::Justification::centredLeft);
}

void NoteSeqEditor::edit (const juce::MouseEvent& e, bool first)
{
    auto& d = proc.noteSeq;
    const int i = first ? stepAt (e.position.x) : stepAt (e.position.x);
    if (i < 0) return;
    auto lr = laneRect (dragLane);
    const float v = juce::jlimit (0.0f, 1.0f, (lr.getBottom() - e.position.y) / lr.getHeight());
    switch (dragLane)
    {
        case LNote:
        {
            if (first) { dragStep = i; startNote = d.note[i].load(); startY = e.position.y; return; }
            const int dn = (int) std::round ((startY - e.position.y) / (laneRect (LNote).getHeight() * 0.48f / 24.0f));
            d.note[dragStep] = juce::jlimit (-24, 24, startNote + dn);
            d.on[dragStep] = true;
            break;
        }
        case LVel: d.vel[i] = juce::jmax (0.05f, v); break;
        case LGate: d.gate[i] = juce::jmax (0.05f, v); break;
        case LProb: d.prob[i] = v; break;
        case LRatchet: if (first) d.ratchet[i] = d.ratchet[i].load() % 4 + 1; break;
        case LSlide: if (first) d.slide[i] = ! d.slide[i].load(); break;
        case LLock: if (first) d.lock[i] = ! d.lock[i].load(); break;
        default: break;
    }
    ++d.version;
}

void NoteSeqEditor::mouseDown (const juce::MouseEvent& e)
{
    dragLane = -1;
    for (int l = 0; l < NumLanes; ++l) if (laneRect (l).contains (e.position)) dragLane = l;
    if (dragLane < 0) return;
    edit (e, true);
}

void NoteSeqEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0) return;
    if (dragLane == LNote && std::abs (e.getDistanceFromDragStartY()) < 3) return;
    edit (e, false);
}

void NoteSeqEditor::mouseUp (const juce::MouseEvent& e)
{
    // clic simple sur une note (sans glisser) : pas allumé / éteint
    if (dragLane == LNote && dragStep >= 0 && e.getDistanceFromDragStart() < 3)
    {
        proc.noteSeq.on[dragStep] = ! proc.noteSeq.on[dragStep].load();
        ++proc.noteSeq.version;
    }
    dragLane = -1;
}

// =============================================================================
// MidiDragZone
// =============================================================================
MidiDragZone::MidiDragZone (IDGemsSynthProcessor& p) : proc (p)
{
    bars.addItemList ({ "1 mesure", "2 mesures", "4 mesures", "8 mesures" }, 1);
    bars.setSelectedId (2, juce::dontSendNotification);
    addAndMakeVisible (bars);
    setTooltip (IDG_T ("Attrape cette zone et glisse-la sur une piste MIDI d'Ableton : le motif (arpège, séquence ou pattern généré) devient un clip."));
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void MidiDragZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (playCol.withAlpha (dragging ? 0.35f : 0.15f));
    g.fillRect (r);
    g.setColour (playCol);
    g.drawRect (r, 1.5f);
    // petit clip MIDI dessiné
    auto icon = r.removeFromLeft (r.getHeight() * 1.3f).reduced (8.0f);
    for (int k = 0; k < 5; ++k)
        g.fillRect (icon.getX() + (float) k * icon.getWidth() / 5.0f, icon.getY() + icon.getHeight() * (0.15f + 0.15f * (float) ((k * 3) % 5)),
                    icon.getWidth() / 6.0f, icon.getHeight() * 0.12f);
    g.setFont (Theme::mono (11.0f, true));
    g.drawText (IDG_T ("GLISSER LE MOTIF MIDI -> ABLETON"), r.withTrimmedRight (110.0f), juce::Justification::centredLeft);
}

void MidiDragZone::resized() { bars.setBounds (getLocalBounds().reduced (8).removeFromRight (100).withSizeKeepingCentre (100, 22)); }

void MidiDragZone::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    repaint();
    static const int barCounts[] { 1, 2, 4, 8 };
    const auto f = proc.exportMidi (barCounts[juce::jlimit (0, 3, bars.getSelectedItemIndex())]);
    juce::Component::SafePointer<MidiDragZone> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [safe]
    {
        if (safe != nullptr) { safe->dragging = false; safe->repaint(); }
    });
}

// =============================================================================
// ScaleView
// =============================================================================
void ScaleView::paint (juce::Graphics& g)
{
    idg::Scale sc;
    sc.enabled = true;
    sc.key = (int) proc.getValue ("scale_key");
    sc.type = (int) proc.getValue ("scale_type");
    const bool on = proc.getValue ("scale_on") > 0.5f;
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float w = r.getWidth() / 7.0f;
    static const int whites[] { 0, 2, 4, 5, 7, 9, 11 };
    static const int blacks[] { 1, 3, -1, 6, 8, 10 };
    for (int i = 0; i < 7; ++i)
    {
        auto k = juce::Rectangle<float> (r.getX() + (float) i * w, r.getY(), w - 1.0f, r.getHeight());
        const bool in = sc.contains (whites[i]);
        g.setColour (in && on ? playCol.withAlpha (0.85f) : (in ? Theme::textDim : Theme::panelHi));
        g.fillRect (k);
        if (whites[i] == sc.key) { g.setColour (Theme::red); g.fillRect (k.removeFromBottom (4.0f)); }
    }
    for (int i = 0; i < 6; ++i)
    {
        if (blacks[i] < 0) continue;
        auto k = juce::Rectangle<float> (r.getX() + ((float) i + 0.68f) * w, r.getY(), w * 0.64f, r.getHeight() * 0.58f);
        const bool in = sc.contains (blacks[i]);
        g.setColour (Theme::bg);
        g.fillRect (k);
        g.setColour (in && on ? playCol : (in ? Theme::textDim.darker (0.3f) : Theme::line));
        g.fillRect (k.reduced (2.0f));
        if (blacks[i] == sc.key) { g.setColour (Theme::red); g.fillRect (k.removeFromBottom (3.0f)); }
    }
}

// =============================================================================
// PlayPage
// =============================================================================
PlayPage::PlayPage (IDGemsSynthProcessor& p)
    : proc (p), modes (p), midiDrag (p), seqEditor (p), scaleView (p)
{
    auto knob = [&p] (juce::OwnedArray<juce::Component>& a, const char* id, const juce::String& l) { a.add (new Knob (p, id, l, playCol)); };
    auto combo = [&p] (juce::OwnedArray<juce::Component>& a, const char* id) { a.add (new ParamCombo (p, id)); };
    auto toggle = [&p] (juce::OwnedArray<juce::Component>& a, const char* id, const juce::String& l) { a.add (new ParamToggle (p, id, l, playCol)); };

    combo (seqCtl, "seq_rate"); knob (seqCtl, "seq_len", "Longueur"); knob (seqCtl, "seq_swing", "Swing"); combo (seqCtl, "seq_follow");
    knob (seqCtl, "seq_mutate", "Mutation"); knob (seqCtl, "seq_rnd_notes", "Notes rnd"); knob (seqCtl, "seq_rnd_oct", "Octaves rnd");
    knob (seqCtl, "seq_rnd_ratchet", "Ratchets rnd");
    combo (arpCtl, "arp_mode"); combo (arpCtl, "arp_rate"); toggle (arpCtl, "arp_hold", "HOLD");
    knob (arpCtl, "arp_oct", "Octaves"); knob (arpCtl, "arp_gate", "Gate"); knob (arpCtl, "arp_swing", "Swing");
    combo (genCtl, "gen_style"); combo (genCtl, "gen_auto");
    knob (genCtl, "gen_density", IDG_T ("Densité")); knob (genCtl, "gen_range", IDG_T ("Étendue")); knob (genCtl, "gen_jump", "Sauts");
    knob (genCtl, "gen_accent", "Accents");
    toggle (scaleCtl, "scale_on", "GAMME"); combo (scaleCtl, "scale_key"); combo (scaleCtl, "scale_type");
    toggle (sprayCtl, "spray_on", "SPRAY"); knob (sprayCtl, "spray_density", IDG_T ("Densité")); knob (sprayCtl, "spray_range", IDG_T ("Étendue"));
    knob (sprayCtl, "spray_time", "Dispersion"); knob (sprayCtl, "spray_dur", IDG_T ("Durée")); knob (sprayCtl, "spray_vel", "Vel rnd");
    knob (sprayCtl, "spray_max", "Voix max");
    toggle (microCtl, "mr_on", "ON"); knob (microCtl, "mr_amount", IDG_T ("Quantité"));
    toggle (microCtl, "mr_pitch", "PITCH"); toggle (microCtl, "mr_grain", "GRAIN"); toggle (microCtl, "mr_cutoff", "CUTOFF");
    toggle (microCtl, "mr_pan", "PAN"); toggle (microCtl, "mr_phase", "PHASE");
    for (auto* arr : { &seqCtl, &arpCtl, &genCtl, &scaleCtl, &sprayCtl, &microCtl })
        for (auto* c : *arr) addAndMakeVisible (c);

    seqRandom.setTooltip (IDG_T ("Motif aléatoire (les pas verrouillés sont gardés)."));
    seqRandom.onClick = [this]
    {
        idg::Scale sc;
        sc.enabled = proc.getValue ("scale_on") > 0.5f;
        sc.key = (int) proc.getValue ("scale_key");
        sc.type = (int) proc.getValue ("scale_type");
        generatePattern (proc.noteSeq, (int) proc.getValue ("seq_len"), GenWalk, 0.6f, 1, 0.4f, 0.4f, sc, (uint32_t) juce::Random::getSystemRandom().nextInt());
    };
    seqClear.onClick = [this]
    {
        auto& d = proc.noteSeq;
        for (int i = 0; i < MaxNoteSteps; ++i) if (! d.lock[i].load()) { d.on[i] = false; d.note[i] = 0; }
        ++d.version;
    };
    generate.setColour (juce::TextButton::buttonColourId, playCol.withAlpha (0.4f));
    generate.setTooltip (IDG_T ("Génère un nouveau motif dans le séquenceur (style et réglages ci-dessous). Passe en mode Générateur pour l'entendre."));
    generate.onClick = [this] { proc.generateNow(); setParam (proc, "play_mode", (float) PlayGen); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &modes, &midiDrag, &seqEditor, &seqRandom, &seqClear, &generate, &scaleView })
        addAndMakeVisible (c);
}

void PlayPage::paint (juce::Graphics& g)
{
    frame (g, seqArea, IDG_T ("Séquenceur de notes  //  32 pas"), playCol);
    frame (g, arpArea, IDG_T ("Arpégiateur"), playCol);
    frame (g, genArea, IDG_T ("Générateur de patterns MIDI"), playCol);
    frame (g, scaleArea, IDG_T ("Gamme  //  quantification"), playCol);
    frame (g, sprayArea, "Note Spray", playCol);
    frame (g, microArea, "Micro-random par voix", playCol);
}

void PlayPage::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    topArea = r.removeFromTop (54);
    modes.setBounds (topArea.removeFromLeft (topArea.getWidth() * 60 / 100));
    topArea.removeFromLeft (8);
    midiDrag.setBounds (topArea);
    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (150);
    r.removeFromTop (0);
    auto right = r.removeFromRight (r.getWidth() * 32 / 100);
    r.removeFromRight (6);
    seqArea = r;
    {
        auto s = seqArea.reduced (12, 8).withTrimmedTop (24);
        auto ctl = s.removeFromBottom (62);
        auto btn = ctl.removeFromRight (90);
        seqRandom.setBounds (btn.removeFromTop (28).reduced (2));
        seqClear.setBounds (btn.removeFromTop (28).reduced (2));
        layoutGrid (ctl, seqCtl, 8);
        s.removeFromBottom (6);
        seqEditor.setBounds (s);
    }
    arpArea = right.removeFromTop (right.getHeight() / 2);
    right.removeFromTop (6);
    genArea = right;
    layoutGrid (arpArea.reduced (12, 8).withTrimmedTop (24), arpCtl, 3);
    {
        auto gr = genArea.reduced (12, 8).withTrimmedTop (24);
        generate.setBounds (gr.removeFromBottom (30).reduced (2));
        layoutGrid (gr, genCtl, 2 + 1);
    }
    const int bw = (bottom.getWidth() - 12) / 3;
    scaleArea = bottom.removeFromLeft (bw);
    bottom.removeFromLeft (6);
    sprayArea = bottom.removeFromLeft (bw);
    bottom.removeFromLeft (6);
    microArea = bottom;
    {
        auto s = scaleArea.reduced (12, 8).withTrimmedTop (24);
        auto top = s.removeFromTop (24);
        const int w = top.getWidth() / 3;
        for (auto* c : scaleCtl) c->setBounds (top.removeFromLeft (w).reduced (2, 0));
        s.removeFromTop (6);
        scaleView.setBounds (s);
    }
    layoutGrid (sprayArea.reduced (12, 8).withTrimmedTop (24), sprayCtl, 7);
    {
        auto m = microArea.reduced (12, 8).withTrimmedTop (24);
        auto left = m.removeFromLeft (m.getWidth() * 35 / 100);
        microCtl[0]->setBounds (left.removeFromTop (24).reduced (2, 0));
        microCtl[1]->setBounds (left.reduced (2, 2));
        const int w = m.getWidth() / 3, h = m.getHeight() / 2;
        for (int i = 2; i < microCtl.size(); ++i)
            microCtl[i]->setBounds (juce::Rectangle<int> (m.getX() + ((i - 2) % 3) * w, m.getY() + ((i - 2) / 3) * h, w, h).withSizeKeepingCentre (w - 6, 24));
    }
}
} // namespace idgs::ui
