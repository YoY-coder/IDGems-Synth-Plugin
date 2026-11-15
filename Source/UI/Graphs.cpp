#include "Graphs.h"
#include "../Synth/Envelope.h"
#include "../Synth/NoiseGen.h"
#include <complex>

namespace idgs::ui
{
namespace
{
using cplx = std::complex<double>;
constexpr float minHz = 20.0f, maxHz = 20000.0f, minDb = -36.0f, maxDb = 24.0f;

ParamDrag drag (IDGemsSynthProcessor& p, const juce::String& id) { return ParamDrag { p.apvts.getParameter (id) }; }

void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float intensity = 1.0f)
{
    g.setColour (c.withAlpha (0.18f * intensity));
    g.strokePath (p, juce::PathStrokeType (6.0f));
    g.setColour (c.withAlpha (0.35f * intensity));
    g.strokePath (p, juce::PathStrokeType (3.0f));
    g.setColour (c.interpolatedWith (juce::Colours::white, 0.3f));
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

void node (juce::Graphics& g, juce::Point<float> p, juce::Colour c, bool hot, float size = 9.0f)
{
    if (hot)
    {
        g.setColour (c.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (size * 2.2f, size * 2.2f).withCentre (p));
    }
    g.setColour (Theme::bg);
    g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (p));
    g.setColour (c);
    g.drawEllipse (juce::Rectangle<float> (size, size).withCentre (p), 2.0f);
}
} // namespace

// =============================================================================
// FilterView
// =============================================================================
FilterView::FilterView (IDGemsSynthProcessor& p, int i)
    : LiveGraph (p, { filtId (i, "on"), filtId (i, "type"), filtId (i, "cutoff"), filtId (i, "reso"), filtId (i, "drive"), filtId (i, "mix") }),
      index (i), cutoff (drag (p, filtId (i, "cutoff"))), reso (drag (p, filtId (i, "reso"))), drive (drag (p, filtId (i, "drive")))
{
    setTooltip (IDG_T ("Glisser le point : horizontal = coupure, vertical = résonance. Molette = drive. Double-clic = valeurs par défaut."));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

float FilterView::magnitude (int type, float fc, float r, float f)
{
    const double w = f / fc;
    const cplx s (0.0, w);
    auto svf = [&] (double q, int kind)
    {
        const cplx d = s * s + s / q + 1.0;
        switch (kind)
        {
            case 0: return 1.0 / d;
            case 1: return s * s / d;
            case 2: return (s / q) / d;
            default: return (s * s + 1.0) / d;
        }
    };
    const double q12 = 0.5 + r * r * 24.0, q24 = 0.6 + r * r * 18.0;
    cplx h (1.0, 0.0);
    switch (type)
    {
        case FiltLP12: h = svf (q12, 0); break;
        case FiltHP12: h = svf (q12, 1); break;
        case FiltBP12: h = svf (q12, 2) * (0.5 + r); break;
        case FiltNotch12: h = svf (q12, 3); break;
        case FiltLP24: h = svf (0.54, 0) * svf (q24, 0); break;
        case FiltHP24: h = svf (0.54, 1) * svf (q24, 1); break;
        case FiltBP24: h = svf (0.54, 2) * svf (q24, 2) * (1.0 + r); break;
        case FiltNotch24: h = svf (0.54, 3) * svf (q24, 3); break;
        case FiltLadder:
        {
            const double k = r * 4.1;
            h = (1.0 + k * 0.6) / (std::pow (1.0 + s, 4.0) + k);
            break;
        }
        case FiltCombPlus: case FiltCombMinus:
        {
            const double fb = r * 0.97 * (type == FiltCombMinus ? -1.0 : 1.0);
            const double ph = 2.0 * juce::MathConstants<double>::pi * f / fc;
            h = (1.0 - std::abs (fb) * 0.5) / (1.0 - fb * cplx (std::cos (ph), -std::sin (ph)));
            break;
        }
        case FiltFormant:
        {
            static const float vowels[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const float t = juce::jlimit (0.0f, 1.0f, std::log (fc / 20.0f) / std::log (1000.0f)) * 4.0f;
            const int a = juce::jmin (3, (int) t);
            const float x = t - (float) a;
            const double q = 4.0 + r * 20.0;
            const double gains[3] { 1.0, 0.6, 0.35 };
            h = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                const double ff = vowels[a][k] + (vowels[a + 1][k] - vowels[a][k]) * x;
                const cplx sk (0.0, f / ff);
                h += gains[k] * (sk / q) / (sk * sk + sk / q + 1.0);
            }
            h *= 1.6;
            break;
        }
        case FiltDestroy: h = 1.0; break;
        default: break;
    }
    return (float) std::abs (h);
}

float FilterView::xOf (float hz) const
{
    auto r = plot();
    return r.getX() + r.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float FilterView::hzOf (float x) const
{
    auto r = plot();
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

float FilterView::yOfDb (float db) const
{
    auto r = plot();
    return r.getBottom() - (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb) * r.getHeight();
}

juce::Point<float> FilterView::nodePos() const
{
    auto r = plot();
    const float zero = -minDb / (maxDb - minDb); // hauteur de la ligne 0 dB : résonance nulle = coude de la courbe
    return { xOf (cutoff.get()), r.getBottom() - (zero + (0.97f - zero) * reso.get()) * r.getHeight() };
}

void FilterView::paint (juce::Graphics& g)
{
    auto r = plot();
    paintBackground (g, r);
    const bool on = proc.getValue (filtId (index, "on")) > 0.5f;
    const int type = (int) proc.getValue (filtId (index, "type"));
    const float fc = cutoff.get(), rs = reso.get(), dr = drive.get(), mix = proc.getValue (filtId (index, "mix"));
    const auto col = on ? Theme::blue : Theme::textDim;

    // repères de fréquence et ligne 0 dB
    g.setFont (Theme::mono (8.5f));
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        g.setColour (Theme::line.withAlpha (0.5f));
        g.drawVerticalLine ((int) xOf (hz), r.getY(), r.getBottom());
        g.setColour (Theme::textDim.withAlpha (0.6f));
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz), juce::Rectangle<float> (xOf (hz) + 2.0f, r.getBottom() - 11.0f, 30.0f, 10.0f), juce::Justification::left);
    }
    g.setColour (Theme::line.withAlpha (0.8f));
    g.drawHorizontalLine ((int) yOfDb (0.0f), r.getX(), r.getRight());

    // courbe de réponse (mix inclus : le signal non filtré « remonte » la courbe)
    juce::Path curve;
    const int pts = juce::jmax (40, (int) r.getWidth() / 2);
    for (int i = 0; i <= pts; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) pts;
        const float hz = hzOf (x);
        float mag = magnitude (type, fc, rs, hz);
        mag = (1.0f - mix) + mix * mag;
        const float y = yOfDb (20.0f * std::log10 (juce::jmax (1.0e-5f, mag)));
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    if (type == FiltDestroy)
    {
        // zone repliée (aliasing) au-dessus de la fréquence simulée : hachures
        g.setColour (Theme::red.withAlpha (on ? 0.25f : 0.1f));
        for (float x = xOf (fc); x < r.getRight(); x += 6.0f)
            g.drawLine (x, r.getBottom(), juce::jmin (r.getRight(), x + 10.0f), r.getY(), 1.0f);
    }
    juce::Path fill (curve);
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.25f + 0.2f * dr), 0.0f, r.getY(), col.withAlpha (0.0f), 0.0f, r.getBottom(), false));
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    g.fillPath (fill);
    glowPath (g, curve, col, on ? 0.6f + dr : 0.4f);
    g.restoreState();

    // point de contrôle
    node (g, nodePos(), on ? Theme::accent : Theme::textDim, hoverNode || dragging);
    g.setFont (Theme::mono (9.0f, true));
    g.setColour (Theme::textDim);
    auto txt = cutoff.p != nullptr ? cutoff.p->getCurrentValueAsText() : juce::String();
    g.drawText (txt + "  Q " + juce::String (juce::roundToInt (rs * 100.0f)) + "%" + (dr > 0.01f ? "  DRV " + juce::String (juce::roundToInt (dr * 100.0f)) + "%" : juce::String()),
                r.reduced (4.0f, 2.0f), juce::Justification::topRight);
    if (! on)
    {
        g.setColour (Theme::textDim.withAlpha (0.8f));
        g.drawText (IDG_T ("BYPASS"), r.reduced (4.0f, 2.0f), juce::Justification::topLeft);
    }
}

void FilterView::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    cutoff.begin();
    reso.begin();
    mouseDrag (e);
}

void FilterView::mouseDrag (const juce::MouseEvent& e)
{
    auto r = plot();
    cutoff.set (hzOf (e.position.x));
    const float zero = -minDb / (maxDb - minDb);
    reso.set (juce::jlimit (0.0f, 1.0f, ((r.getBottom() - e.position.y) / r.getHeight() - zero) / (0.97f - zero)));
}

void FilterView::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    cutoff.end();
    reso.end();
    repaint();
}

void FilterView::mouseDoubleClick (const juce::MouseEvent&)
{
    cutoff.reset();
    reso.reset();
}

void FilterView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    drive.begin();
    drive.set (juce::jlimit (0.0f, 1.0f, drive.get() + w.deltaY * 0.15f));
    drive.end();
}

// =============================================================================
// EnvelopeEditor
// =============================================================================
EnvelopeEditor::EnvelopeEditor (IDGemsSynthProcessor& p)
    : LiveGraph (p, { P::env1Delay, P::env1Attack, P::env1Hold, P::env1Decay, P::env1Sustain, P::env1Release, P::env1ACurve, P::env1DCurve, P::env1RCurve }),
      delay (drag (p, P::env1Delay)), attack (drag (p, P::env1Attack)), hold (drag (p, P::env1Hold)), decay (drag (p, P::env1Decay)),
      sustain (drag (p, P::env1Sustain)), release (drag (p, P::env1Release)), aCurve (drag (p, P::env1ACurve)),
      dCurve (drag (p, P::env1DCurve)), rCurve (drag (p, P::env1RCurve))
{
    setTooltip (IDG_T ("Glisser les points : temps (horizontal) et sustain (vertical).\n"
                       "Glisser les petits losanges au milieu des segments : courbes.\nDouble-clic sur un point : valeur par défaut."));
}

EnvelopeEditor::Layout EnvelopeEditor::layout (bool useFrozen) const
{
    if (useFrozen && isFrozen) return frozen;
    // échelle temporelle compressée (racine) : les temps courts restent lisibles
    auto w = [] (float ms) { return std::sqrt (juce::jmax (0.0f, ms)); };
    const float d = delay.get(), a = juce::jmax (Envelope::minAttackMs, attack.get()), h = hold.get(),
                dc = juce::jmax (Envelope::minAttackMs, decay.get()), rl = juce::jmax (Envelope::minReleaseMs, release.get());
    const float sustainW = juce::jmax (w (a) + w (dc), 20.0f) * 0.35f;
    const float total = w (d) + w (a) + w (h) + w (dc) + sustainW + w (rl);
    auto r = plot();
    Layout l;
    l.px = r.getWidth() / juce::jmax (1.0f, total);
    l.x0 = r.getX();
    l.xDelay = l.x0 + w (d) * l.px;
    l.xAttack = l.xDelay + w (a) * l.px;
    l.xHold = l.xAttack + w (h) * l.px;
    l.xDecay = l.xHold + w (dc) * l.px;
    l.xSustainEnd = l.xDecay + sustainW * l.px;
    l.xRelease = l.xSustainEnd + w (rl) * l.px;
    return l;
}

juce::Point<float> EnvelopeEditor::handlePos (int h, const Layout& l) const
{
    const float s = sustain.get();
    auto mid = [&] (float x0, float x1, float y0, float y1, float c, bool falling)
    {
        const float v = falling ? 1.0f - Envelope::shape (0.5f, -c) : Envelope::shape (0.5f, c);
        return juce::Point<float> ((x0 + x1) * 0.5f, y0 + (y1 - y0) * v);
    };
    switch (h)
    {
        case HDelay:   return { l.xDelay, yOf (0.0f) };
        case HAttack:  return { l.xAttack, yOf (1.0f) };
        case HHold:    return { l.xHold, yOf (1.0f) };
        case HDecay:   return { l.xDecay, yOf (s) };
        case HRelease: return { l.xRelease, yOf (0.0f) };
        case HCurveA:  return mid (l.xDelay, l.xAttack, yOf (0.0f), yOf (1.0f), aCurve.get(), false);
        case HCurveD:  return mid (l.xHold, l.xDecay, yOf (1.0f), yOf (s), dCurve.get(), true);
        case HCurveR:  return mid (l.xSustainEnd, l.xRelease, yOf (s), yOf (0.0f), rCurve.get(), true);
        default: return {};
    }
}

int EnvelopeEditor::hitTest (juce::Point<float> p) const
{
    const auto l = layout (false);
    int best = -1;
    float bestD = 12.0f;
    for (int h = NumHandles - 1; h >= 0; --h)
    {
        const float d = handlePos (h, l).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = h; }
    }
    return best;
}

void EnvelopeEditor::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat().reduced (2.0f);
    paintBackground (g, full);
    const auto l = layout (true);
    const float s = sustain.get();
    const float ac = aCurve.get(), dcv = dCurve.get(), rc = rCurve.get();

    juce::Path p;
    p.startNewSubPath (l.x0, yOf (0));
    p.lineTo (l.xDelay, yOf (0));
    for (int i = 1; i <= 32; ++i) { const float t = (float) i / 32.0f; p.lineTo (l.xDelay + t * (l.xAttack - l.xDelay), yOf (Envelope::shape (t, ac))); }
    p.lineTo (l.xHold, yOf (1));
    for (int i = 1; i <= 32; ++i) { const float t = (float) i / 32.0f; p.lineTo (l.xHold + t * (l.xDecay - l.xHold), yOf (1.0f + (s - 1.0f) * (1.0f - Envelope::shape (1.0f - t, -dcv)))); }
    p.lineTo (l.xSustainEnd, yOf (s));
    for (int i = 1; i <= 32; ++i) { const float t = (float) i / 32.0f; p.lineTo (l.xSustainEnd + t * (l.xRelease - l.xSustainEnd), yOf (s * Envelope::shape (1.0f - t, -rc))); }

    juce::Path fill (p);
    fill.lineTo (l.xRelease, yOf (0));
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Theme::accent.withAlpha (0.28f), 0.0f, plot().getY(), Theme::accent.withAlpha (0.0f), 0.0f, plot().getBottom(), false));
    g.fillPath (fill);
    glowPath (g, p, Theme::accent);

    // relâchement de la note
    g.setColour (Theme::blue.withAlpha (0.6f));
    g.drawVerticalLine ((int) l.xSustainEnd, plot().getY(), plot().getBottom());
    g.setFont (Theme::mono (9.0f));
    g.drawText ("NOTE OFF", juce::Rectangle<float> (l.xSustainEnd + 3.0f, plot().getY() - 10.0f, 60.0f, 10.0f), juce::Justification::left);

    // points
    for (int h = 0; h < NumHandles; ++h)
    {
        const auto pos = handlePos (h, l);
        const bool hot = h == hover || h == active;
        if (h >= HCurveA)
        {
            juce::Path dia;
            dia.addQuadrilateral (pos.x, pos.y - 5, pos.x + 5, pos.y, pos.x, pos.y + 5, pos.x - 5, pos.y);
            g.setColour (hot ? Theme::blue : Theme::blue.withAlpha (0.6f));
            g.fillPath (dia);
        }
        else node (g, pos, Theme::accent, hot, h == HDelay || h == HHold ? 7.0f : 9.0f);
    }

    // valeur du point actif
    if (active >= 0 || hover >= 0)
    {
        const int h = active >= 0 ? active : hover;
        ParamDrag* const params[] { &delay, &attack, &hold, &decay, &release, &aCurve, &dCurve, &rCurve };
        juce::String t = params[h]->p ? params[h]->p->getName (20) + " : " + params[h]->p->getCurrentValueAsText() : juce::String();
        if (h == HDecay && sustain.p) t += "  /  Sustain : " + sustain.p->getCurrentValueAsText();
        g.setFont (Theme::mono (10.0f, true));
        g.setColour (Theme::text);
        g.drawText (t, full.reduced (6.0f, 3.0f), juce::Justification::topRight);
    }
}

void EnvelopeEditor::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    active = hitTest (e.position);
    if (active < 0) return;
    frozen = layout (false); // l'échelle reste figée pendant le glissement (pas de « fuite » du point)
    isFrozen = true;
    dragStart = e.position;
    ParamDrag* const params[] { &delay, &attack, &hold, &decay, &release, &aCurve, &dCurve, &rCurve };
    dragStartValue = params[active]->get();
    params[active]->begin();
    if (active == HDecay) sustain.begin();
}

void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (active < 0) return;
    const auto& l = frozen;
    auto msFrom = [&] (float x, float segStart) { const float w = juce::jmax (0.0f, (x - segStart) / l.px); return w * w; };
    switch (active)
    {
        case HDelay:   delay.set (msFrom (e.position.x, l.x0)); break;
        case HAttack:  attack.set (msFrom (e.position.x, l.xDelay)); break;
        case HHold:    hold.set (msFrom (e.position.x, l.xAttack)); break;
        case HDecay:
            decay.set (msFrom (e.position.x, l.xHold));
            sustain.set (juce::jlimit (0.0f, 1.0f, (plot().getBottom() - e.position.y) / plot().getHeight()));
            break;
        case HRelease: release.set (msFrom (e.position.x, l.xSustainEnd)); break;
        case HCurveA: case HCurveD: case HCurveR:
        {
            // montée : vers le haut = démarrage rapide ; descentes : vers le bas = chute rapide
            const float dy = (e.position.y - dragStart.y) / 60.0f;
            const float v = active == HCurveA ? dragStartValue + dy : dragStartValue - dy;
            (active == HCurveA ? aCurve : active == HCurveD ? dCurve : rCurve).set (juce::jlimit (-1.0f, 1.0f, v));
            break;
        }
        default: break;
    }
}

void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    if (active >= 0)
    {
        ParamDrag* const params[] { &delay, &attack, &hold, &decay, &release, &aCurve, &dCurve, &rCurve };
        params[active]->end();
        if (active == HDecay) sustain.end();
    }
    active = -1;
    isFrozen = false;
    repaint();
}

void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    ParamDrag* const params[] { &delay, &attack, &hold, &decay, &release, &aCurve, &dCurve, &rCurve };
    if (h >= 0) params[h]->reset();
    if (h == HDecay) sustain.reset();
}

// =============================================================================
// UnisonView
// =============================================================================
UnisonView::UnisonView (IDGemsSynthProcessor& p, int o)
    : LiveGraph (p, { oscId (o, "unison"), oscId (o, "detune"), oscId (o, "blend"), oscId (o, "spread"), oscId (o, "pan"), oscId (o, "on") }), osc (o)
{
    setTooltip (IDG_T ("Unison : chaque trait est une voix. Position = panoramique, hauteur = niveau, inclinaison = désaccord."));
}

void UnisonView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    paintBackground (g, r);
    const int n = (int) proc.getValue (oscId (osc, "unison"));
    const float det = proc.getValue (oscId (osc, "detune")), blend = proc.getValue (oscId (osc, "blend")),
                spread = proc.getValue (oscId (osc, "spread")), pan = proc.getValue (oscId (osc, "pan"));
    const bool on = proc.getValue (oscId (osc, "on")) > 0.5f;
    g.setFont (Theme::mono (8.5f));
    g.setColour (Theme::textDim.withAlpha (0.7f));
    g.drawText ("G", r.reduced (3.0f, 1.0f), juce::Justification::bottomLeft);
    g.drawText ("D", r.reduced (3.0f, 1.0f), juce::Justification::bottomRight);
    g.drawVerticalLine ((int) r.getCentreX(), r.getY() + 4.0f, r.getBottom() - 4.0f);
    for (int u = 0; u < n; ++u)
    {
        const float x = n == 1 ? 0.0f : 2.0f * (float) u / (float) (n - 1) - 1.0f;
        const bool centre = n == 1 || std::abs (x) <= 1.0f / (float) (n - 1) + 1.0e-4f;
        const float gain = centre ? 1.0f : blend;
        const float p = juce::jlimit (-1.0f, 1.0f, pan + x * spread * (n > 1 ? 1.0f : 0.0f));
        const float px = r.getCentreX() + p * r.getWidth() * 0.44f;
        const float h = (r.getHeight() - 8.0f) * (0.15f + 0.85f * gain);
        const float tilt = x * det * 10.0f;
        const auto c = (on ? (centre ? Theme::accent : Theme::blue) : Theme::textDim).withAlpha (0.5f + 0.5f * gain);
        g.setColour (c.withAlpha (0.25f));
        g.drawLine (px - tilt, r.getBottom() - 4.0f - h, px + tilt, r.getBottom() - 4.0f, 5.0f);
        g.setColour (c);
        g.drawLine (px - tilt, r.getBottom() - 4.0f - h, px + tilt, r.getBottom() - 4.0f, 1.8f);
    }
    g.setColour (Theme::textDim);
    g.drawText (juce::String (n) + (n > 1 ? " VOIX" : " VOIX (UNISON OFF)"), r.reduced (4.0f, 2.0f), juce::Justification::topLeft);
}

// =============================================================================
// SubView / NoiseView
// =============================================================================
SubView::SubView (IDGemsSynthProcessor& p) : LiveGraph (p, { P::subOn, P::subShape, P::subOct, P::subLevel }) {}

void SubView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    paintBackground (g, r);
    const int shape = (int) proc.getValue (P::subShape), oct = (int) proc.getValue (P::subOct);
    const bool on = proc.getValue (P::subOn) > 0.5f;
    const float lvl = proc.getValue (P::subLevel);
    const auto& basic = WavetableBank::get()[0];
    const float pos = shape == 0 ? 0.0f : (shape == 1 ? 1.0f / 3.0f : 1.0f);
    // référence : la note jouée (fin, en bleu) ; le sub : 2 ou 4 fois plus lent
    juce::Path ref, sub;
    const int pts = 160;
    const float cycles = 4.0f;
    for (int i = 0; i <= pts; ++i)
    {
        const float t = (float) i / (float) pts;
        const float x = r.getX() + t * r.getWidth();
        const float vr = std::sin (juce::MathConstants<float>::twoPi * t * cycles);
        const float ph = t * cycles / (oct == 0 ? 2.0f : 4.0f);
        const float vs = basic.readMorph (pos, 4, ph - std::floor (ph)) * (0.2f + 0.8f * lvl);
        const float yr = r.getCentreY() - vr * r.getHeight() * 0.18f, ys = r.getCentreY() - vs * r.getHeight() * 0.42f;
        if (i == 0) { ref.startNewSubPath (x, yr); sub.startNewSubPath (x, ys); }
        else { ref.lineTo (x, yr); sub.lineTo (x, ys); }
    }
    g.setColour (Theme::blue.withAlpha (0.25f));
    g.strokePath (ref, juce::PathStrokeType (1.0f));
    glowPath (g, sub, on ? Theme::accent : Theme::textDim, on ? 1.0f : 0.3f);
}

NoiseView::NoiseView (IDGemsSynthProcessor& p) : LiveGraph (p, { P::noiseOn, P::noiseType, P::noiseTone, P::noiseLevel }) {}

void NoiseView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    paintBackground (g, r);
    const bool on = proc.getValue (P::noiseOn) > 0.5f;
    // extrait réel du générateur (même code que dans les voix), calculé à l'affichage
    NoiseGen gen;
    gen.prepare (44100.0);
    gen.reset (7);
    const int pts = juce::jmax (64, (int) r.getWidth());
    const int stride = 24;
    std::vector<float> v ((size_t) pts);
    float mx = 1.0e-4f;
    for (int i = 0; i < pts; ++i)
    {
        if (i % 4 == 0) gen.control ((int) proc.getValue (P::noiseType), proc.getValue (P::noiseTone), 220.0f, stride * 4);
        float l = 0, rr = 0, pk = 0;
        for (int k = 0; k < stride; ++k) { gen.tick (l, rr); if (std::abs (l) > std::abs (pk)) pk = l; }
        v[(size_t) i] = pk;
        mx = juce::jmax (mx, std::abs (pk));
    }
    const float lvl = 0.2f + 0.8f * proc.getValue (P::noiseLevel);
    juce::Path p;
    for (int i = 0; i < pts; ++i)
    {
        const float x = r.getX() + (float) i / (float) (pts - 1) * r.getWidth();
        const float y = r.getCentreY() - v[(size_t) i] / mx * r.getHeight() * 0.42f * lvl;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    glowPath (g, p, on ? Theme::accent : Theme::textDim, on ? 0.8f : 0.3f);
    // tonalité : pente indiquée en bleu
    const float tone = proc.getValue (P::noiseTone);
    g.setColour (Theme::blue.withAlpha (0.7f));
    g.drawLine (r.getX() + 4.0f, r.getBottom() - 6.0f - juce::jmax (0.0f, -tone) * 20.0f, r.getRight() - 4.0f, r.getBottom() - 6.0f - juce::jmax (0.0f, tone) * 20.0f, 1.5f);
}

// =============================================================================
// GlideView
// =============================================================================
GlideView::GlideView (IDGemsSynthProcessor& p)
    : LiveGraph (p, { P::glide, P::glideCurve, P::voiceMode, P::glideLegato }), curve (drag (p, P::glideCurve))
{
    setTooltip (IDG_T ("Glide : trajet de hauteur d'une note à la suivante. Glisser verticalement = courbe. Double-clic = défaut."));
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void GlideView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    paintBackground (g, r);
    auto pl = r.reduced (10.0f, 14.0f);
    const float ms = proc.getValue (P::glide), c = curve.get();
    const int mode = (int) proc.getValue (P::voiceMode);
    const float glideW = ms <= 0.5f ? 0.0f : juce::jlimit (0.08f, 0.8f, std::sqrt (ms) / std::sqrt (5000.0f) * 0.8f + 0.08f);
    juce::Path p;
    const float x0 = pl.getX() + pl.getWidth() * 0.12f, x1 = x0 + pl.getWidth() * glideW;
    p.startNewSubPath (pl.getX(), pl.getBottom());
    p.lineTo (x0, pl.getBottom());
    for (int i = 1; i <= 40; ++i)
    {
        const float t = (float) i / 40.0f;
        p.lineTo (x0 + (x1 - x0) * t, pl.getBottom() - Envelope::shape (t, c) * pl.getHeight());
    }
    p.lineTo (pl.getRight(), pl.getY());
    glowPath (g, p, Theme::blue, ms > 0.5f ? 1.0f : 0.4f);
    g.setFont (Theme::mono (9.5f, true));
    g.setColour (Theme::textDim);
    const juce::String modeTxt = mode == VoicePoly ? "POLY" : mode == VoiceMono ? "MONO" : "LEGATO";
    g.drawText (modeTxt + "  //  GLIDE " + (ms <= 0.5f ? juce::String ("OFF") : (proc.apvts.getParameter (P::glide)->getCurrentValueAsText())),
                r.reduced (6.0f, 3.0f), juce::Justification::topLeft);
}
} // namespace idgs::ui
