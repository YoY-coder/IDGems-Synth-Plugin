#include "ExportPage.h"

namespace idgs::ui
{
namespace
{
const juce::Colour expCol { 0xff3ddc84 };
const juce::Colour recCol { 0xffff2d55 };

juce::String noteName (int n)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[n % 12]) + juce::String (n / 12 - 2);
}
} // namespace

// =============================================================================
// FileDragZone
// =============================================================================
void FileDragZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (colour.withAlpha (dragging ? 0.35f : (isMouseOver() ? 0.22f : 0.12f)));
    g.fillRect (r);
    g.setColour (colour);
    const float dash[] { 6.0f, 4.0f };
    juce::Path p;
    p.addRectangle (r);
    juce::Path d;
    juce::PathStrokeType (1.5f).createDashedStroke (d, p, dash, 2);
    g.fillPath (d);
    // petite forme d'onde
    auto icon = r.removeFromLeft (r.getHeight() * 1.2f).reduced (10.0f);
    juce::Path w;
    for (int i = 0; i <= 24; ++i)
    {
        const float x = icon.getX() + icon.getWidth() * (float) i / 24.0f;
        const float y = icon.getCentreY() + std::sin ((float) i * 0.9f) * icon.getHeight() * 0.4f * (1.0f - (float) i / 30.0f);
        if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
    }
    g.strokePath (w, juce::PathStrokeType (1.5f));
    g.setFont (Theme::mono (11.0f, true));
    g.drawFittedText (label, r.toNearestInt().reduced (4, 2), juce::Justification::centredLeft, 2);
}

void FileDragZone::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    repaint();
    const auto f = makeFile();
    if (! f.existsAsFile()) { dragging = false; repaint(); return; }
    juce::Component::SafePointer<FileDragZone> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [safe]
    {
        if (safe != nullptr) { safe->dragging = false; safe->repaint(); }
    });
}

// =============================================================================
// RollingView
// =============================================================================
void RollingView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg);
    g.fillRect (r);
    const int pts = juce::jmax (16, getWidth() / 2);
    if (auto* rec = proc.getRecorder()) rec->overview (seconds, pts, peaks);
    else peaks.assign ((size_t) pts, 0.0f);
    const float mid = r.getCentreY(), h = r.getHeight() * 0.46f;
    for (int i = 0; i < (int) peaks.size(); ++i)
    {
        const float v = juce::jlimit (0.0f, 1.0f, peaks[(size_t) i]);
        const float x = r.getX() + r.getWidth() * (float) i / (float) peaks.size();
        g.setColour (expCol.withAlpha (0.35f + 0.65f * v));
        g.fillRect (x, mid - v * h, 1.5f, juce::jmax (1.0f, 2.0f * v * h));
    }
    g.setColour (Theme::line);
    g.drawRect (r, 1.0f);
    g.setFont (Theme::mono (9.0f));
    g.setColour (Theme::textDim);
    g.drawText ("-" + juce::String (juce::roundToInt (seconds)) + " s", r.reduced (4.0f), juce::Justification::topLeft);
    g.drawText (IDG_T ("maintenant"), r.reduced (4.0f), juce::Justification::topRight);
}

// =============================================================================
// MotionView
// =============================================================================
void MotionView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg);
    g.fillRect (r);
    auto& m = proc.motionRecorder();
    for (int k = 1; k < 4 * m.getBars(); ++k)
    {
        g.setColour (Theme::line.withAlpha (k % 4 == 0 ? 0.8f : 0.3f));
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / (float) (4 * m.getBars())), r.getY(), r.getBottom());
    }
    if (m.hasData())
        for (int mac = 0; mac < NumMacros; ++mac)
        {
            if (! m.macroUsed (mac)) continue;
            juce::Path p;
            for (int i = 0; i <= 200; ++i)
            {
                const float x = (float) i / 200.0f;
                const juce::Point<float> pt { r.getX() + x * r.getWidth(), r.getBottom() - m.valueAt (mac, x) * r.getHeight() };
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (sourceColour (MSrcMacro0 + mac));
            g.strokePath (p, juce::PathStrokeType (1.8f));
        }
    if (m.getState() != MotionRecorder::Idle)
    {
        const float x = r.getX() + m.phase() * r.getWidth();
        g.setColour (m.getState() == MotionRecorder::Recording ? recCol : Theme::text);
        g.drawLine (x, r.getY(), x, r.getBottom(), 1.5f);
    }
    g.setColour (Theme::line);
    g.drawRect (r, 1.0f);
    if (! m.hasData() && m.getState() == MotionRecorder::Idle)
    {
        g.setFont (Theme::mono (10.0f));
        g.setColour (Theme::textDim);
        g.drawFittedText (IDG_T ("REC MOTION puis bouge les macros :\nle geste est rejoué en boucle, calé sur le tempo."), r.toNearestInt().reduced (8),
                          juce::Justification::centred, 3);
    }
}

// =============================================================================
// ExportPage
// =============================================================================
ExportPage::ExportPage (IDGemsSynthProcessor& p)
    : proc (p),
      rolling (p),
      recDrag (IDG_T ("GLISSER LE DERNIER ENREGISTREMENT -> DAW"), recCol, [this] { return lastAudio(); }),
      renderDrag (IDG_T ("GLISSER LE SON ACTUEL -> DAW (rendu direct)"), expCol, [this]
      {
          auto s = settings();
          auto f = BatchRenderer::rendersFolder().getNonexistentChildFile (juce::File::createLegalFileName (s.name), ".wav", false);
          return proc.batchRenderer().renderNow (s, f);
      }),
      motionView (p)
{
    // ---- enregistreur
    rec.setClickingTogglesState (true);
    rec.setColour (juce::TextButton::buttonOnColourId, recCol);
    rec.setTooltip (IDG_T ("Enregistre la sortie du synthé en WAV 24 bits (Documents/IDGems Synth/Enregistrements)."));
    rec.onClick = [this]
    {
        auto* r = proc.getRecorder();
        if (r == nullptr) return;
        if (rec.getToggleState()) { if (! r->startRecording (proc.getBpm())) rec.setToggleState (false, juce::dontSendNotification); }
        else if (auto f = r->stopRecording(); f.existsAsFile() && flash) flash (IDG_T ("Enregistré : ") + f.getFileName());
    };
    rollingSecs.addItemList ({ "5 s", "10 s", "20 s", "30 s", "60 s" }, 1);
    rollingSecs.setSelectedId (4, juce::dontSendNotification);
    rollingSecs.onChange = [this] { static const float s[] { 5, 10, 20, 30, 60 }; rolling.seconds = s[juce::jlimit (0, 4, rollingSecs.getSelectedItemIndex())]; };
    keepRolling.setTooltip (IDG_T ("Rolling recorder : la sortie est toujours enregistrée en mémoire. Ce bouton garde les dernières secondes dans un WAV."));
    keepRolling.onClick = [this]
    {
        if (auto* r = proc.getRecorder())
            if (auto f = r->saveRolling (rolling.seconds, proc.getBpm()); f.existsAsFile() && flash) flash (IDG_T ("Gardé : ") + f.getFileName());
    };
    openRec.onClick = [] { idg::Recorder::folder().startAsProcess(); };
    static const char* feedNames[] { "-> OSC A", "-> OSC B", "-> TEXTURE", "-> EMPREINTE" };
    static const int feedSlots[] { 0, 1, IDGemsSynthProcessor::AssetTex1, IDGemsSynthProcessor::AssetFingerprint };
    for (int k = 0; k < 4; ++k)
    {
        auto* b = feedButtons.add (new juce::TextButton (feedNames[k]));
        b->setTooltip (IDG_T ("Boucle Feed : le dernier enregistrement (ou les dernières secondes du rolling recorder) redevient une source du synthé."));
        b->onClick = [this, k] { feed (feedSlots[k]); };
        addAndMakeVisible (b);
    }
    recDrag.setTooltip (IDG_T ("Attrape et glisse vers une piste audio : dernier enregistrement (ou les dernières secondes du rolling recorder)."));

    // ---- rendu
    const juce::StringArray modes { "ONE-SHOT", "LOOP", "TEXTURE" };
    for (int k = 0; k < modes.size(); ++k)
    {
        auto* b = modeButtons.add (new juce::TextButton (modes[k]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4242);
        b->setColour (juce::TextButton::buttonOnColourId, expCol.withAlpha (0.7f));
        b->onClick = [this, k] { mode = k; updateModeControls(); };
        addAndMakeVisible (b);
    }
    modeButtons[0]->setToggleState (true, juce::dontSendNotification);
    modeButtons[0]->setTooltip (IDG_T ("One-shot : une note (ou un accord) puis la queue jusqu'au silence."));
    modeButtons[1]->setTooltip (IDG_T ("Loop : N mesures au tempo, la queue revient au début : boucle sans couture (arpège / séquenceur calés)."));
    modeButtons[2]->setTooltip (IDG_T ("Texture : note longue (drift léger), fondus d'entrée et de sortie."));
    for (auto* b : { &hq, &chord, &normalize, &randomize })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, expCol.withAlpha (0.6f));
        addAndMakeVisible (b);
    }
    hq.setTooltip (IDG_T ("Draft : 48 kHz. HQ : 96 kHz + suréchantillonnage du destructeur (plus long à calculer)."));
    normalize.setToggleState (true, juce::dontSendNotification);
    randomize.setToggleState (true, juce::dontSendNotification);
    randomize.setTooltip (IDG_T ("Batch : chaque fichier est une variation random du son actuel (dosage ci-contre, cadenas respectés)."));
    for (int n = 24; n <= 84; ++n) note.addItem (noteName (n), n);
    note.setSelectedId (48, juce::dontSendNotification);
    length.addItemList ({ "0.25 s", "0.5 s", "1 s", "1.5 s", "2 s", "4 s" }, 1);
    length.setSelectedId (4, juce::dontSendNotification);
    bars.addItemList ({ "1 mesure", "2 mesures", "4 mesures", "8 mesures" }, 1);
    bars.setSelectedId (2, juce::dontSendNotification);
    texLen.addItemList ({ "10 s", "20 s", "30 s", "60 s" }, 1);
    texLen.setSelectedId (2, juce::dontSendNotification);
    for (int c : { 1, 2, 4, 8, 16, 32, 50 }) count.addItem (juce::String (c) + IDG_T (" fichiers"), c);
    count.setSelectedId (8, juce::dontSendNotification);
    amount.setSliderStyle (juce::Slider::LinearBar);
    amount.setRange (0.0, 1.0, 0.01);
    amount.setValue (0.5, juce::dontSendNotification);
    amount.setTextValueSuffix (" dosage");
    amount.setColour (juce::Slider::trackColourId, expCol.withAlpha (0.5f));
    name.setText ("IDGemsSynth", false);
    name.setFont (Theme::mono (12.0f));
    name.setTooltip (IDG_T ("Nom des fichiers rendus."));
    batch.setColour (juce::TextButton::buttonColourId, expCol.withAlpha (0.35f));
    batch.onClick = [this]
    {
        auto& b = proc.batchRenderer();
        if (b.isRunning()) return;
        b.onFinished = [this] { if (flash) flash (proc.batchRenderer().status()); };
        b.start (settings());
    };
    cancel.onClick = [this] { proc.batchRenderer().cancel(); };
    openRenders.onClick = [this]
    {
        auto f = proc.batchRenderer().folder();
        (f.isDirectory() ? f : BatchRenderer::rendersFolder()).startAsProcess();
    };
    renderDrag.setTooltip (IDG_T ("Attrape et glisse vers une piste audio : le son actuel est rendu hors ligne (mode, note, Draft / HQ ci-dessus)."));

    // ---- motion
    motionBars.addItemList ({ "1 mesure", "2 mesures", "4 mesures", "8 mesures", "16 mesures" }, 1);
    motionBars.setSelectedId (2, juce::dontSendNotification);
    motionRec.setColour (juce::TextButton::buttonColourId, recCol.withAlpha (0.3f));
    motionRec.onClick = [this]
    {
        static const int b[] { 1, 2, 4, 8, 16 };
        proc.motionRecorder().record (b[juce::jlimit (0, 4, motionBars.getSelectedItemIndex())]);
    };
    motionPlay.onClick = [this]
    {
        auto& m = proc.motionRecorder();
        if (m.getState() == MotionRecorder::Playing) m.stop(); else m.play();
    };
    motionClear.onClick = [this] { proc.motionRecorder().clear(); };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &rec, &keepRolling, &openRec, &rollingSecs, &rolling, &recDrag, &note, &length, &bars, &texLen, &count, &amount, &name,
             &renderDrag, &batch, &cancel, &openRenders, &motionRec, &motionPlay, &motionClear, &motionBars, &motionView })
        addAndMakeVisible (c);
    updateModeControls();
    startTimerHz (10);
}

void ExportPage::updateModeControls()
{
    length.setVisible (mode == 0);
    bars.setVisible (mode == 1);
    texLen.setVisible (mode == 2);
}

RenderSettings ExportPage::settings() const
{
    RenderSettings s;
    s.mode = mode;
    s.hq = hq.getToggleState();
    s.chord = chord.getToggleState();
    s.normalize = normalize.getToggleState();
    s.randomize = randomize.getToggleState();
    s.amount = (float) amount.getValue();
    s.note = note.getSelectedId() > 0 ? note.getSelectedId() : 48;
    static const float lens[] { 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f };
    s.noteSeconds = lens[juce::jlimit (0, 5, length.getSelectedItemIndex())];
    static const int barCounts[] { 1, 2, 4, 8 };
    s.bars = barCounts[juce::jlimit (0, 3, bars.getSelectedItemIndex())];
    static const float tex[] { 10.0f, 20.0f, 30.0f, 60.0f };
    s.textureSeconds = tex[juce::jlimit (0, 3, texLen.getSelectedItemIndex())];
    s.count = juce::jmax (1, count.getSelectedId());
    s.bpm = proc.getBpm();
    s.name = name.getText().trim().isEmpty() ? juce::String ("IDGemsSynth") : name.getText().trim();
    return s;
}

juce::File ExportPage::lastAudio()
{
    auto* r = proc.getRecorder();
    if (r == nullptr) return {};
    auto f = r->lastFile();
    if (f.existsAsFile()) return f;
    return r->saveRolling (rolling.seconds, proc.getBpm());
}

void ExportPage::feed (int slot)
{
    const auto f = lastAudio();
    if (! f.existsAsFile()) { if (flash) flash (IDG_T ("Rien à réinjecter : joue ou enregistre d'abord.")); return; }
    const auto msg = proc.dropFiles (slot, { f });
    if (flash) flash ("Feed : " + (msg.isNotEmpty() ? msg : f.getFileName()));
}

void ExportPage::timerCallback()
{
    if (auto* r = proc.getRecorder())
    {
        const bool on = r->isRecording();
        if (rec.getToggleState() != on) rec.setToggleState (on, juce::dontSendNotification);
        rec.setButtonText (on ? "STOP  " + juce::String (r->recordedSeconds(), 1) + " s" : "REC");
    }
    auto& m = proc.motionRecorder();
    motionPlay.setButtonText (m.getState() == MotionRecorder::Playing ? "STOP" : "LECTURE");
    motionRec.setButtonText (m.getState() == MotionRecorder::Recording ? IDG_T ("ENREGISTREMENT...") : "REC MOTION");
    const bool running = proc.batchRenderer().isRunning();
    batch.setEnabled (! running);
    cancel.setEnabled (running);
    repaint (progressArea);
}

void ExportPage::paint (juce::Graphics& g)
{
    auto frame = [&g] (juce::Rectangle<int> r, const juce::String& t, juce::Colour c)
    {
        Theme::panelFrame (g, r.toFloat(), c);
        Theme::sectionTitle (g, t, r.toFloat().reduced (10.0f, 7.0f), c);
    };
    frame (recArea, IDG_T ("ENREGISTREUR // ROLLING // FEED"), recCol);
    frame (renderArea, IDG_T ("RENDU // BATCH"), expCol);
    frame (motionArea, "MOTION RECORDING", Theme::blue);

    auto small = [&g] (juce::Rectangle<int> r, const juce::String& t)
    {
        g.setFont (Theme::mono (9.0f, true));
        g.setColour (Theme::textDim);
        g.drawText (t, r, juce::Justification::centredLeft);
    };
    small (feedButtons[0]->getBounds().translated (0, -16).withWidth (300).withHeight (14), IDG_T ("BOUCLE FEED : RÉINJECTER LE SON DANS..."));
    small (modeButtons[0]->getBounds().translated (0, -16).withWidth (300).withHeight (14), IDG_T ("MODE DE RENDU"));
    small (count.getBounds().translated (0, -16).withWidth (300).withHeight (14), IDG_T ("BATCH : N VARIATIONS"));

    // progression du batch
    auto& b = proc.batchRenderer();
    auto pa = progressArea.toFloat();
    g.setColour (Theme::bg);
    g.fillRect (pa);
    g.setColour (expCol.withAlpha (0.7f));
    g.fillRect (pa.withWidth (pa.getWidth() * b.progress()));
    g.setColour (Theme::line);
    g.drawRect (pa, 1.0f);
    g.setFont (Theme::mono (10.0f));
    g.setColour (Theme::text);
    g.drawText (b.status().isNotEmpty() ? b.status() : IDG_T ("Prêt."), pa.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
}

void ExportPage::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    recArea = r.removeFromLeft (r.getWidth() * 36 / 100);
    r.removeFromLeft (10);
    motionArea = r.removeFromRight (r.getWidth() * 40 / 100);
    r.removeFromRight (10);
    renderArea = r;

    // enregistreur
    {
        auto a = recArea.reduced (14, 10).withTrimmedTop (30);
        auto row = a.removeFromTop (44);
        rec.setBounds (row.removeFromLeft (150).reduced (0, 4));
        row.removeFromLeft (10);
        openRec.setBounds (row.removeFromRight (80).reduced (0, 8));
        a.removeFromTop (10);
        rolling.setBounds (a.removeFromTop (130));
        a.removeFromTop (8);
        auto rr = a.removeFromTop (30);
        keepRolling.setBounds (rr.removeFromLeft (200));
        rr.removeFromLeft (6);
        rollingSecs.setBounds (rr.removeFromLeft (90));
        a.removeFromTop (12);
        recDrag.setBounds (a.removeFromTop (64));
        a.removeFromTop (30);
        auto fr = a.removeFromTop (34);
        const int w = fr.getWidth() / 4;
        for (auto* b : feedButtons) b->setBounds (fr.removeFromLeft (w).reduced (3, 0));
    }
    // rendu
    {
        auto a = renderArea.reduced (14, 10).withTrimmedTop (46);
        auto mr = a.removeFromTop (32);
        const int w = mr.getWidth() / 3;
        for (auto* b : modeButtons) b->setBounds (mr.removeFromLeft (w).reduced (3, 0));
        a.removeFromTop (10);
        auto o = a.removeFromTop (28);
        note.setBounds (o.removeFromLeft (80));
        o.removeFromLeft (6);
        auto lenR = o.removeFromLeft (110);
        length.setBounds (lenR); bars.setBounds (lenR); texLen.setBounds (lenR);
        o.removeFromLeft (6);
        chord.setBounds (o.removeFromLeft (80));
        o.removeFromLeft (6);
        hq.setBounds (o.removeFromLeft (50));
        o.removeFromLeft (6);
        normalize.setBounds (o);
        a.removeFromTop (12);
        renderDrag.setBounds (a.removeFromTop (64));
        a.removeFromTop (34);
        auto c = a.removeFromTop (28);
        count.setBounds (c.removeFromLeft (120));
        c.removeFromLeft (6);
        randomize.setBounds (c.removeFromLeft (170));
        c.removeFromLeft (6);
        amount.setBounds (c);
        a.removeFromTop (8);
        name.setBounds (a.removeFromTop (26));
        a.removeFromTop (10);
        auto bb = a.removeFromTop (34);
        openRenders.setBounds (bb.removeFromRight (80));
        bb.removeFromRight (6);
        cancel.setBounds (bb.removeFromRight (70));
        bb.removeFromRight (6);
        batch.setBounds (bb);
        a.removeFromTop (10);
        progressArea = a.removeFromTop (24);
    }
    // motion
    {
        auto a = motionArea.reduced (14, 10).withTrimmedTop (30);
        auto row = a.removeFromTop (32);
        motionRec.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (6);
        motionPlay.setBounds (row.removeFromLeft (90));
        row.removeFromLeft (6);
        motionClear.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (6);
        motionBars.setBounds (row);
        a.removeFromTop (10);
        motionView.setBounds (a.removeFromTop (juce::jmin (a.getHeight(), 260)));
    }
}

} // namespace idgs::ui
