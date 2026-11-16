#include "Widgets.h"
#include "../Synth/Envelope.h"
#include "../Synth/Oscillator.h"

namespace idgs::ui
{
namespace
{
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
}

// =============================================================================
// Knob
// =============================================================================
Knob::Knob (IDGemsSynthProcessor& p, const juce::String& paramId, const juce::String& l, juce::Colour c)
    : proc (p), label (l), colour (c)
{
    param = p.apvts.getParameter (paramId);
    paramIdx = p.paramIndex (paramId);
    field = p.modTargets.fieldForParam (paramIdx);
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (startAngle, endAngle, true);
    setVelocityBasedMode (false);
    setMouseDragSensitivity (220);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, *this);
    if (param != nullptr)
        setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    if (field >= 0) startTimerHz (20);
}

juce::String Knob::getTooltip()
{
    const auto help = param != nullptr ? idgs::helpFor (param->getParameterID()) : juce::String();
    juce::String t = label + (help.isNotEmpty() ? "\n" + help : juce::String()) + IDG_T ("\nDouble-clic : valeur par défaut");
    if (field >= 0)
    {
        juce::String mods;
        for (auto& sl : proc.modShared.slots)
            if (sl.src.load() >= 0 && sl.param.load() == paramIdx)
                mods << "\n  " << sourceName (sl.src.load()) << " : " << juce::String (juce::roundToInt (sl.depth.load() * 100.0f)) << " %"
                     << (sl.bipolar.load() ? IDG_T (" (bipolaire)") : juce::String());
        t << (mods.isNotEmpty() ? IDG_T ("\nModulations :") + mods : juce::String())
          << IDG_T ("\nModulable : glisse une source ici. Alt / Ctrl + glisser = profondeur. Clic droit = modulations.");
    }
    return t;
}

void Knob::tick()
{
    const float m = field >= 0 ? proc.modShared.fieldValue01[field].load() : -1.0f;
    const int ver = proc.modShared.version.load(), hs = proc.uiHoverSource.load(), ds = proc.uiDragSource.load();
    if (std::abs (m - lastMod) > 0.002f || ver != lastVersion || hs != lastHoverSrc || ds != lastDragSrc)
    {
        lastMod = m; lastVersion = ver; lastHoverSrc = hs; lastDragSrc = ds;
        repaint();
    }
}

void Knob::mouseEnter (const juce::MouseEvent& e)
{
    hovering = true;
    proc.uiHoverParam = paramIdx;
    juce::Slider::mouseEnter (e);
    repaint();
}

void Knob::mouseExit (const juce::MouseEvent& e)
{
    hovering = false;
    if (proc.uiHoverParam.load() == paramIdx) proc.uiHoverParam = -1;
    juce::Slider::mouseExit (e);
    repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showModMenu(); return; }
    depthSlot = -1;
    if (field >= 0 && (e.mods.isAltDown() || e.mods.isCtrlDown() || e.mods.isCommandDown()))
    {
        // profondeur de la dernière modulation de ce potard
        for (int i = MaxModSlots - 1; i >= 0; --i)
            if (proc.modShared.slots[i].src.load() >= 0 && proc.modShared.slots[i].param.load() == paramIdx) { depthSlot = i; break; }
        if (depthSlot >= 0) { depthStart = proc.modShared.slots[depthSlot].depth.load(); return; }
    }
    juce::Slider::mouseDown (e);
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (depthSlot >= 0)
    {
        const float d = juce::jlimit (-1.0f, 1.0f, depthStart - (float) e.getDistanceFromDragStartY() / 150.0f);
        proc.modShared.slots[depthSlot].depth = d;
        ++proc.modShared.version;
        repaint();
        return;
    }
    juce::Slider::mouseDrag (e);
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (depthSlot >= 0) { depthSlot = -1; return; }
    juce::Slider::mouseUp (e);
}

bool Knob::isInterestedInDragSource (const SourceDetails& d)
{
    return field >= 0 && d.description.toString().startsWith ("mod:");
}

void Knob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue();
    if (src >= 0 && src < NumModSrc) proc.modShared.addConnection (src, paramIdx, 0.25f, sourceIsBipolar (src));
    repaint();
}

void Knob::showModMenu()
{
    if (field < 0) return;
    juce::PopupMenu m;
    m.addSectionHeader (IDG_T ("Modulations de « ") + label + IDG_T (" »"));
    int count = 0;
    for (int i = 0; i < MaxModSlots; ++i)
    {
        auto& sl = proc.modShared.slots[i];
        if (sl.src.load() < 0 || sl.param.load() != paramIdx) continue;
        ++count;
        juce::PopupMenu sub;
        sub.addItem (1000 + i, "Inverser");
        sub.addItem (2000 + i, "Bipolaire", true, sl.bipolar.load());
        sub.addItem (3000 + i, "Supprimer");
        m.addSubMenu (sourceName (sl.src.load()) + "  " + juce::String (juce::roundToInt (sl.depth.load() * 100.0f)) + " %", sub);
    }
    if (count == 0) m.addItem (-1, IDG_T ("Aucune : glisse une source (macro, LFO, enveloppe...) sur ce potard"), false);
    else { m.addSeparator(); m.addItem (9999, IDG_T ("Supprimer toutes les modulations")); }
    juce::Component::SafePointer<Knob> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& ms = safe->proc.modShared;
        if (r == 9999) { for (int i = 0; i < MaxModSlots; ++i) if (ms.slots[i].param.load() == safe->paramIdx) ms.removeConnection (i); }
        else if (r >= 3000) ms.removeConnection (r - 3000);
        else if (r >= 2000) { ms.slots[r - 2000].bipolar = ! ms.slots[r - 2000].bipolar.load(); ++ms.version; }
        else if (r >= 1000) { ms.slots[r - 1000].depth = -ms.slots[r - 1000].depth.load(); ++ms.version; }
        safe->repaint();
    });
}

void Knob::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    auto bounds = getLocalBounds().toFloat();
    const float labelH = 13.0f;
    auto dialArea = bounds.withTrimmedBottom (labelH);
    const float size = juce::jmin (dialArea.getWidth(), dialArea.getHeight()) - 4.0f;
    auto dial = juce::Rectangle<float> (size, size).withCentre (dialArea.getCentre());
    const auto centre = dial.getCentre();
    const float radius = size * 0.5f;
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    const float norm = param->getValue();
    auto angleOf = [] (float n) { return startAngle + juce::jlimit (0.0f, 1.0f, n) * (endAngle - startAngle); };

    auto arc = [&] (float r, float a0, float a1, juce::Colour col, float thick)
    {
        juce::Path pth;
        pth.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        g.setColour (col);
        g.strokePath (pth, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    };

    const auto range = param->getNormalisableRange();
    const bool bipolar = range.start < 0.0f && range.end > 0.0f;
    const float zeroN = bipolar ? param->convertTo0to1 (0.0f) : 0.0f;
    const float ringR = radius - 3.0f;

    // rail éteint
    arc (ringR, startAngle, endAngle, Theme::line.withAlpha (0.9f * alpha), 2.0f);
    // tube néon : plus la valeur est forte, plus il bloome
    {
        const float amount = bipolar ? std::abs (norm - zeroN) / juce::jmax (zeroN, 1.0f - zeroN) : norm;
        const float intensity = juce::jlimit (0.0f, 1.0f, amount);
        const float bloom = (0.2f + 0.8f * intensity) * alpha;
        const float a0 = bipolar ? angleOf (zeroN) : startAngle, a1 = angleOf (norm);
        if (std::abs (a1 - a0) > 0.01f)
        {
            const float spread = 2.0f + 7.0f * intensity;
            for (int k = 4; k >= 1; --k)
                arc (ringR, a0, a1, colour.withAlpha (bloom * 0.11f * (float) (5 - k) / 4.0f), 3.0f + spread * (float) k * 0.55f);
            arc (ringR, a0, a1, colour.withAlpha (juce::jmin (1.0f, 0.75f + 0.25f * bloom) * alpha), 3.2f);
            arc (ringR, a0, a1, colour.interpolatedWith (juce::Colours::white, 0.35f + 0.45f * intensity).withAlpha ((0.5f + 0.5f * bloom) * alpha), 1.2f);
            const float ae = a1 - juce::MathConstants<float>::halfPi;
            const juce::Point<float> tip { centre.x + std::cos (ae) * ringR, centre.y + std::sin (ae) * ringR };
            const float gs = 5.0f + 9.0f * intensity;
            g.setColour (colour.withAlpha (bloom * 0.35f));
            g.fillEllipse (juce::Rectangle<float> (gs, gs).withCentre (tip));
            g.setColour (juce::Colours::white.withAlpha ((0.6f + 0.4f * bloom) * alpha));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (tip));
        }
    }

    // ---- modulations : anneaux intérieurs colorés (couleur de la source), point = valeur modulée
    if (field >= 0)
    {
        float innerR = ringR - 5.0f;
        const int hs = proc.uiHoverSource.load(), ds = proc.uiDragSource.load();
        bool hot = false;
        for (auto& sl : proc.modShared.slots)
        {
            const int src = sl.src.load();
            if (src < 0 || sl.param.load() != paramIdx) continue;
            const float depth = sl.depth.load();
            const auto c = sourceColour (src).withAlpha ((hs < 0 || hs == src ? 0.95f : 0.35f) * alpha);
            if (hs == src) hot = true;
            if (sl.bipolar.load()) arc (innerR, angleOf (norm - std::abs (depth)), angleOf (norm + std::abs (depth)), c, 2.2f);
            else arc (innerR, angleOf (norm), angleOf (norm + depth), c, 2.2f);
            innerR -= 3.0f;
            if (innerR < radius * 0.45f) break;
        }
        const float mod = proc.modShared.fieldValue01[field].load();
        if (mod >= 0.0f && std::abs (mod - norm) > 0.002f)
        {
            const float a = angleOf (mod) - juce::MathConstants<float>::halfPi;
            g.setColour (Theme::text.withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ centre.x + std::cos (a) * ringR, centre.y + std::sin (a) * ringR }));
        }
        // prévisualisation : source survolée -> ses cibles s'allument ; glisser en cours -> cibles possibles
        if (hot || dropHover || ds >= 0)
        {
            const auto c = ds >= 0 ? sourceColour (ds) : sourceColour (hs);
            g.setColour (c.withAlpha (dropHover ? 0.45f : (hot ? 0.3f : 0.12f)));
            g.drawEllipse (dial.expanded (1.5f), dropHover ? 3.0f : 2.0f);
        }
    }

    // corps octogonal
    auto body = dial.reduced (size * 0.25f);
    juce::Path oct;
    for (int i = 0; i < 8; ++i)
    {
        const float a = juce::MathConstants<float>::pi * (0.125f + (float) i * 0.25f);
        juce::Point<float> pt { centre.x + std::cos (a) * body.getWidth() * 0.52f, centre.y + std::sin (a) * body.getWidth() * 0.52f };
        if (i == 0) oct.startNewSubPath (pt); else oct.lineTo (pt);
    }
    oct.closeSubPath();
    g.setColour (Theme::panelHi.withAlpha (alpha));
    g.fillPath (oct);
    g.setColour (Theme::line.brighter (0.35f).withAlpha (alpha));
    g.strokePath (oct, juce::PathStrokeType (1.0f));
    {
        const float a = angleOf (norm) - juce::MathConstants<float>::halfPi;
        const float r0 = body.getWidth() * 0.12f, r1 = body.getWidth() * 0.5f;
        g.setColour (colour.withAlpha (alpha));
        g.drawLine (centre.x + std::cos (a) * r0, centre.y + std::sin (a) * r0, centre.x + std::cos (a) * r1, centre.y + std::sin (a) * r1, 2.2f);
    }

    // libellé / valeur
    auto lab = bounds.removeFromBottom (labelH);
    const bool showValue = hovering || isMouseButtonDown();
    g.setFont (Theme::mono (10.0f, isMouseButtonDown()));
    g.setColour ((showValue ? colour : Theme::textDim).withAlpha (alpha));
    g.drawFittedText (showValue ? param->getCurrentValueAsText() : label.toUpperCase(), lab.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

// =============================================================================
// ParamToggle / ParamCombo
// =============================================================================
ParamToggle::ParamToggle (IDGemsSynthProcessor& p, const juce::String& id, const juce::String& caption, juce::Colour on)
{
    setButtonText (caption);
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, on);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, id, *this);
    setTooltip (idgs::helpFor (id));
}

ParamCombo::ParamCombo (IDGemsSynthProcessor& p, const juce::String& id)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
        addItemList (c->choices, 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, id, *this);
    setTooltip (idgs::helpFor (id));
}

// =============================================================================
// Meter
// =============================================================================
void Meter::tick()
{
    for (int c = 0; c < 2; ++c)
    {
        const float v = proc.meterOut[c].exchange (0.0f);
        level[c] = juce::jmax (v, level[c] * 0.85f);
        if (v >= peakHold[c]) { peakHold[c] = v; holdTime[c] = 30; }
        else if (--holdTime[c] <= 0) peakHold[c] *= 0.95f;
    }
    gr = juce::jmin (proc.meterGR.exchange (1.0f), gr + (1.0f - gr) * 0.2f);
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto toY = [&] (float lin)
    {
        const float db = idg::dsp::gainToDb (lin);
        const float n = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        return r.getBottom() - n * r.getHeight();
    };
    const float w = (r.getWidth() - 8.0f) / 3.0f;
    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (r.getX() + (float) c * (w + 2.0f), r.getY(), w, r.getHeight());
        g.setColour (Theme::panelHi);
        g.fillRect (bar);
        const float y = toY (level[c]);
        juce::ColourGradient grad (Theme::blue, 0.0f, bar.getBottom(), Theme::red, 0.0f, bar.getY(), false);
        grad.addColour (0.75, Theme::accent);
        g.setGradientFill (grad);
        g.fillRect (bar.withTop (y));
        g.setColour (Theme::text);
        g.fillRect (bar.getX(), toY (peakHold[c]), bar.getWidth(), 1.5f);
    }
    // réduction de gain du limiteur (descend du haut)
    auto grBar = juce::Rectangle<float> (r.getRight() - w, r.getY(), w, r.getHeight());
    g.setColour (Theme::panelHi);
    g.fillRect (grBar);
    const float grDb = -idg::dsp::gainToDb (gr);
    g.setColour (Theme::red);
    g.fillRect (grBar.withHeight (juce::jlimit (0.0f, 1.0f, grDb / 12.0f) * grBar.getHeight()));
}

// =============================================================================
// OscDisplay
// =============================================================================
void OscDisplay::tick()
{
    const int t = (int) proc.getValue (oscId (osc, "table"));
    const int mode = (int) proc.getValue (oscId (osc, "mode"));
    const float pos = proc.getValue (oscId (osc, "pos"));
    int ver = proc.assetsVersion.load();
    for (auto* k : { "warp1", "warp1amt", "warp2", "warp2amt", "stretch", "shift" })
        ver += (int) (proc.getValue (oscId (osc, k)) * 997.0f) * 7919;
    const bool msgVisible = message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 4000;
    if (t != lastTable || mode != lastMode || ver != lastVersion || std::abs (pos - lastPos) > 1.0e-4f || (message.isNotEmpty() && ! msgVisible))
    {
        if (ver != lastVersion) overview.clear();
        if (! msgVisible) message.clear();
        lastTable = t; lastMode = mode; lastVersion = ver; lastPos = pos;
        repaint();
    }
}

void OscDisplay::setPos (float v)
{
    if (auto* p = proc.apvts.getParameter (oscId (osc, "pos"))) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

void OscDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int mode = (int) proc.getValue (oscId (osc, "mode"));
    if (pencil && mode == ModeWavetable)
    {
        // copie de la frame courante dans le tampon d'édition
        const auto& wt = proc.displayTable (osc);
        editFrame = juce::roundToInt (proc.getValue (oscId (osc, "pos")) * (float) (wt.getNumFrames() - 1));
        editBuf.assign (wt.table (editFrame, 0), wt.table (editFrame, 0) + Wavetable::Size);
        lastDraw = e.position;
        drawTo (e.position);
        return;
    }
    if (auto* p = proc.apvts.getParameter (oscId (osc, "pos"))) { p->beginChangeGesture(); dragStartPos = p->getValue(); }
    if (mode != ModeWavetable) setPos (e.position.x / (float) juce::jmax (1, getWidth()));
}

void OscDisplay::drawTo (juce::Point<float> pt)
{
    if (editBuf.empty()) return;
    auto r = getLocalBounds().toFloat().reduced (8.0f, 14.0f);
    auto idxOf = [&] (float x) { return juce::jlimit (0, Wavetable::Size - 1, (int) ((x - r.getX()) / r.getWidth() * (float) Wavetable::Size)); };
    auto valOf = [&] (float y) { return juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - y) / (r.getHeight() * 0.45f)); };
    int i0 = idxOf (lastDraw.x), i1 = idxOf (pt.x);
    float v0 = valOf (lastDraw.y), v1 = valOf (pt.y);
    if (i0 > i1) { std::swap (i0, i1); std::swap (v0, v1); }
    for (int i = i0; i <= i1; ++i)
        editBuf[(size_t) i] = i1 == i0 ? v1 : v0 + (v1 - v0) * (float) (i - i0) / (float) (i1 - i0);
    lastDraw = pt;
    repaint();
}

void OscDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! editBuf.empty()) { drawTo (e.position); return; }
    if ((int) proc.getValue (oscId (osc, "mode")) == ModeWavetable)
    {
        // vers le haut = frames suivantes ; Maj = réglage fin
        const float sens = e.mods.isShiftDown() ? 600.0f : 150.0f;
        setPos (dragStartPos - (float) e.getDistanceFromDragStartY() / sens);
    }
    else setPos (e.position.x / (float) juce::jmax (1, getWidth()));
}

void OscDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! editBuf.empty())
    {
        // la table modifiée remplace la table de l'oscillateur (devient « Personnalisée »)
        const auto& wt = proc.displayTable (osc);
        std::vector<float> cycles;
        for (int f = 0; f < wt.getNumFrames(); ++f)
        {
            if (f == editFrame) cycles.insert (cycles.end(), editBuf.begin(), editBuf.end());
            else cycles.insert (cycles.end(), wt.table (f, 0), wt.table (f, 0) + Wavetable::Size);
        }
        const auto name = wt.getName().endsWith ("*") ? wt.getName() : wt.getName() + " *";
        proc.setTableFromCycles (osc, cycles, name);
        editBuf.clear();
        repaint();
        return;
    }
    if (auto* p = proc.apvts.getParameter (oscId (osc, "pos"))) p->endChangeGesture();
}

void OscDisplay::itemDropped (const SourceDetails& d)
{
    modHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue();
    const int prm = proc.paramIndex (oscId (osc, "pos"));
    if (src >= 0 && src < NumModSrc && prm >= 0) proc.modShared.addConnection (src, prm, 0.25f, sourceIsBipolar (src));
    setMessage (IDG_T ("Modulation : ") + sourceName (src) + IDG_T (" -> position"));
}

void OscDisplay::paintPencil (juce::Graphics& g, juce::Rectangle<float> full)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f, 14.0f);
    const auto& wt = proc.displayTable (osc);
    const int frame = editBuf.empty() ? juce::roundToInt (juce::jmax (0.0f, lastPos) * (float) (wt.getNumFrames() - 1)) : editFrame;
    const float* data = editBuf.empty() ? wt.table (frame, 0) : editBuf.data();
    g.setColour (Theme::line.withAlpha (0.6f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    for (int k = 1; k < 8; ++k) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / 8.0f), r.getY(), r.getBottom());
    juce::Path p;
    const int pts = juce::jmax (64, (int) r.getWidth());
    for (int i = 0; i <= pts; ++i)
    {
        const int idx = juce::jlimit (0, Wavetable::Size - 1, i * Wavetable::Size / pts);
        const float x = r.getX() + r.getWidth() * (float) i / (float) pts;
        const float y = r.getCentreY() - data[idx] * r.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (Theme::accent.withAlpha (0.3f));
    g.strokePath (p, juce::PathStrokeType (5.0f));
    g.setColour (Theme::accent.brighter (0.3f));
    g.strokePath (p, juce::PathStrokeType (1.8f));
    g.setFont (Theme::mono (9.5f));
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("CRAYON  //  FRAME ") + juce::String (frame + 1) + " / " + juce::String (wt.getNumFrames()) + IDG_T ("  //  dessine la forme"),
                full.reduced (6.0f, 3.0f), juce::Justification::topRight);
}

void OscDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    if (auto* p = proc.apvts.getParameter (oscId (osc, "pos"))) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
}

void OscDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Theme::bg.withAlpha (0.65f));
    g.fillRect (r);
    lastMode = (int) proc.getValue (oscId (osc, "mode"));
    lastPos = proc.getValue (oscId (osc, "pos"));
    if (lastMode == ModeWavetable) { if (pencil) paintPencil (g, r); else paintTable (g, r); }
    else paintSample (g, r);
    if (modHover)
    {
        g.setColour (Theme::blue.withAlpha (0.2f));
        g.fillRect (r);
        g.setColour (Theme::blue);
        g.drawRect (r, 2.0f);
        g.setFont (Theme::mono (12.0f, true));
        g.drawText (IDG_T ("DÉPOSER : MODULER LA POSITION"), r, juce::Justification::centred);
    }

    g.setFont (Theme::mono (10.0f, true));
    g.setColour (Theme::textDim);
    g.drawText (proc.assetName (osc).toUpperCase(), r.reduced (6.0f, 3.0f), juce::Justification::topLeft);

    if (message.isNotEmpty())
    {
        g.setColour (Theme::accent);
        g.drawText (message, r.reduced (6.0f, 3.0f), juce::Justification::bottomLeft);
    }
    if (dropHover)
    {
        g.setColour (Theme::accent.withAlpha (0.18f));
        g.fillRect (r);
        g.setColour (Theme::accent);
        g.drawRect (r, 2.0f);
        g.setFont (Theme::mono (12.0f, true));
        const juce::String what = lastMode == ModeWavetable ? IDG_T ("DÉPOSER : SAMPLE -> WAVETABLE")
                                : lastMode == ModeSample ? IDG_T ("DÉPOSER : SAMPLE") : IDG_T ("DÉPOSER : MULTISAMPLE (fichiers ou dossier)");
        g.drawText (what, r, juce::Justification::centred);
    }
}

void OscDisplay::paintTable (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& wt = proc.displayTable (osc);
    const int frames = wt.getNumFrames();
    const float pos = juce::jmax (0.0f, lastPos);
    constexpr int pts = 128;
    const float depthX = r.getWidth() * 0.16f, depthY = r.getHeight() * 0.30f;
    auto plot = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + depthY + 4.0f, r.getWidth() - depthX - 8.0f, r.getHeight() - depthY - 14.0f);

    // warps de l'oscillateur appliqués à l'affichage (ceux qui ne dépendent pas d'une autre source)
    int wm[2];
    float wa[2];
    for (int w = 0; w < 2; ++w)
    {
        wm[w] = (int) proc.getValue (oscId (osc, w == 0 ? "warp1" : "warp2"));
        wa[w] = proc.getValue (oscId (osc, w == 0 ? "warp1amt" : "warp2amt"));
    }
    auto pathFor = [&] (float framePos, float offset, bool warped = false)
    {
        juce::Path p;
        for (int i = 0; i <= pts; ++i)
        {
            float ph = (float) i / (float) pts;
            float gain = 1.0f;
            if (warped)
                for (int w = 0; w < 2; ++w) ph = OscUnit::warpPhase (wm[w], wa[w], ph, 0.0f, gain);
            const float v = wt.readMorph (framePos, 2, juce::jlimit (0.0f, 0.9999f, ph)) * gain;
            const float x = plot.getX() + ph * plot.getWidth() + offset * depthX;
            const float y = plot.getCentreY() - v * plot.getHeight() * 0.45f - offset * depthY;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    const int step = juce::jmax (1, frames / 24);
    for (int f = frames - 1; f >= 0; f -= step)
    {
        const float fp = frames > 1 ? (float) f / (float) (frames - 1) : 0.0f;
        g.setColour (Theme::blue.withAlpha (0.10f + 0.08f * (1.0f - std::abs (fp - pos))));
        g.strokePath (pathFor (fp, fp), juce::PathStrokeType (1.0f));
    }
    auto cur = pathFor (pos, pos, true);
    g.setColour (juce::Colour (0xffff1e3c).withAlpha (0.45f));
    g.strokePath (cur, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (-1.5f, 0.0f));
    g.setColour (Theme::blue.withAlpha (0.45f));
    g.strokePath (cur, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (1.5f, 0.4f));
    g.setColour (Theme::accent.withAlpha (0.3f));
    g.strokePath (cur, juce::PathStrokeType (5.0f));
    g.setColour (Theme::accent.brighter (0.3f));
    g.strokePath (cur, juce::PathStrokeType (1.8f));

    g.setFont (Theme::mono (9.5f));
    g.setColour (Theme::textDim);
    g.drawText (juce::String (frames) + " FRAMES  " + juce::String (juce::roundToInt (pos * 100.0f)) + " %", r.reduced (6.0f, 3.0f), juce::Justification::topRight);

    // mini-spectre : position des harmoniques (étirées / décalées par l'inharmonicité) et leur niveau
    const float stretch = proc.getValue (oscId (osc, "stretch")), shift = proc.getValue (oscId (osc, "shift"));
    const bool inharm = std::abs (stretch) > 0.002f || std::abs (shift) > 0.002f;
    auto sp = juce::Rectangle<float> (r.getRight() - r.getWidth() * 0.42f - 4.0f, r.getBottom() - r.getHeight() * 0.26f - 4.0f, r.getWidth() * 0.42f, r.getHeight() * 0.26f);
    g.setColour (Theme::bg.withAlpha (0.85f));
    g.fillRect (sp);
    g.setColour (Theme::line);
    g.drawRect (sp, 1.0f);
    const float fp = pos * (float) (frames - 1);
    const int f0 = (int) fp, f1 = juce::jmin (f0 + 1, frames - 1);
    const float fx = fp - (float) f0;
    constexpr int bars = 32;
    float ampMax = 1.0e-6f;
    for (int k = 1; k <= bars; ++k) ampMax = juce::jmax (ampMax, wt.partialAmp (f0, k));
    const float expo = 1.0f + stretch * 0.5f;
    for (int k = 1; k <= bars; ++k)
    {
        const float ratio = std::pow ((float) k, expo) + shift;
        if (ratio <= 0.0f || ratio > (float) bars + 1.0f) continue;
        const float a = wt.partialAmp (f0, k) + (wt.partialAmp (f1, k) - wt.partialAmp (f0, k)) * fx;
        const float h = std::sqrt (a / ampMax) * (sp.getHeight() - 6.0f);
        const float x = sp.getX() + 3.0f + (ratio / ((float) bars + 1.0f)) * (sp.getWidth() - 6.0f);
        g.setColour ((inharm ? Theme::accent : Theme::blue).withAlpha (0.85f));
        g.fillRect (x - 0.75f, sp.getBottom() - 3.0f - h, 1.5f, h);
    }
    g.setFont (Theme::mono (8.0f));
    g.setColour (Theme::textDim);
    g.drawText (inharm ? IDG_T ("SPECTRE · INHARMONIQUE") : "SPECTRE", sp.reduced (3.0f, 1.0f), juce::Justification::topLeft);
}

void OscDisplay::paintSample (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto* set = proc.currentSamples (osc);
    auto plot = r.reduced (6.0f, 18.0f);
    if (set == nullptr || set->zones.empty())
    {
        g.setFont (Theme::mono (11.0f));
        g.setColour (Theme::textDim.withAlpha (0.8f));
        g.drawText (lastMode == ModeMulti ? IDG_T ("Glisse plusieurs fichiers ou un dossier ici (ou bouton CHARGER)")
                                          : IDG_T ("Glisse un fichier audio ici (ou bouton CHARGER)"),
                    plot, juce::Justification::centred);
        return;
    }
    // aperçu : on affiche la zone de la note C3 (ou la première)
    const int zi = juce::jmax (0, set->zoneFor (60));
    const auto& z = *set->zones[(size_t) zi];
    const int cols = juce::jmax (8, (int) plot.getWidth());
    if ((int) overview.size() != cols)
    {
        overview.assign ((size_t) cols, 0.0f);
        const int len = z.length();
        for (int c = 0; c < cols; ++c)
        {
            const int a = (int) ((juce::int64) c * len / cols), b = (int) ((juce::int64) (c + 1) * len / cols);
            float pk = 0.0f;
            for (int i = a; i < juce::jmin (b, len); i += juce::jmax (1, (b - a) / 64))
                pk = juce::jmax (pk, std::abs (z.data.getSample (0, i)), std::abs (z.data.getSample (1, i)));
            overview[(size_t) c] = pk;
        }
    }
    float mx = 1.0e-6f;
    for (auto v : overview) mx = juce::jmax (mx, v);
    g.setColour (Theme::accent.withAlpha (0.85f));
    for (int c = 0; c < cols; ++c)
    {
        const float h = overview[(size_t) c] / mx * plot.getHeight() * 0.48f;
        g.fillRect (plot.getX() + (float) c, plot.getCentreY() - h, 1.0f, juce::jmax (1.0f, 2.0f * h));
    }
    // point de départ
    const float x = plot.getX() + juce::jlimit (0.0f, 1.0f, lastPos) * plot.getWidth();
    g.setColour (Theme::blue);
    g.fillRect (x - 1.0f, plot.getY(), 2.0f, plot.getHeight());
    g.setColour (Theme::blue.withAlpha (0.12f));
    g.fillRect (plot.getX(), plot.getY(), x - plot.getX(), plot.getHeight());

    g.setFont (Theme::mono (9.5f));
    g.setColour (Theme::textDim);
    juce::String info = juce::String (z.length() / z.sampleRate, 2) + " s  " + juce::String (juce::roundToInt (z.sampleRate)) + " Hz";
    if (set->zones.size() > 1) info = juce::String ((int) set->zones.size()) + " ZONES  " + info;
    g.drawText (info, r.reduced (6.0f, 3.0f), juce::Justification::topRight);
}

// =============================================================================
// Scope
// =============================================================================
void Scope::tick()
{
    int s1, n1, s2, n2;
    const int avail = proc.scopeFifo.getNumReady();
    proc.scopeFifo.prepareToRead (avail, s1, n1, s2, n2);
    auto push = [this] (float v) { buf[(size_t) writePos] = v; writePos = (writePos + 1) % (int) buf.size(); };
    // décimation simple pour couvrir ~50 ms
    for (int i = 0; i < n1; i += 4) push (proc.scopeData[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; i += 4) push (proc.scopeData[(size_t) (s2 + i)]);
    proc.scopeFifo.finishedRead (n1 + n2);
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg.withAlpha (0.7f));
    g.fillRect (r);
    g.setColour (Theme::line);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    juce::Path p;
    const int n = (int) buf.size();
    for (int i = 0; i < n; ++i)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, buf[(size_t) ((writePos + i) % n)]);
        const float x = r.getX() + (float) i / (float) (n - 1) * r.getWidth();
        const float y = r.getCentreY() - v * r.getHeight() * 0.48f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (Theme::blue.withAlpha (0.35f));
    g.strokePath (p, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (1.2f, 0.0f));
    g.setColour (Theme::accent);
    g.strokePath (p, juce::PathStrokeType (1.2f));
    g.setFont (Theme::mono (9.0f));
    g.setColour (Theme::textDim);
    g.drawText ("VOIX " + juce::String (proc.activeVoices.load()) + "  CPU " + juce::String (juce::roundToInt (proc.cpuLoad.load() * 100.0f)) + "%",
                r.reduced (3.0f, 1.0f), juce::Justification::topLeft);
}
} // namespace idgs::ui
