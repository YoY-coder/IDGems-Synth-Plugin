#pragma once
#include "Widgets.h"

namespace idgs::ui
{
/** Modification d'un paramètre depuis un graphique (avec geste d'automation pour l'hôte). */
struct ParamDrag
{
    juce::RangedAudioParameter* p = nullptr;
    void begin() { if (p) p->beginChangeGesture(); }
    void end() { if (p) p->endChangeGesture(); }
    void set (float real) { if (p) p->setValueNotifyingHost (p->convertTo0to1 (real)); }
    float get() const { return p ? p->convertFrom0to1 (p->getValue()) : 0.0f; }
    void reset() { if (p) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); } }
};

/** Base des graphiques : se redessine quand l'un des paramètres observés change. */
class LiveGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    LiveGraph (IDGemsSynthProcessor& p, juce::StringArray watchedIds) : proc (p), ids (std::move (watchedIds)) {}
    virtual void tick()
    {
        float h = 0.0f;
        int k = 1;
        for (auto& id : ids) h += proc.getValue (id) * (float) (k++ * 37 % 101 + 1);
        h += (float) proc.assetsVersion.load() * 0.123f;
        if (std::abs (h - hash) > 1.0e-6f) { hash = h; repaint(); }
    }

protected:
    void paintBackground (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Theme::bg.withAlpha (0.65f));
        g.fillRect (r);
        g.setColour (Theme::line.withAlpha (0.35f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
    }
    IDGemsSynthProcessor& proc;
    juce::StringArray ids;
    float hash = -1.0f;
};

/** Filtre façon Vital : courbe de réponse en fréquence, point à glisser (X = coupure, Y = résonance),
    molette = drive, double-clic = valeurs par défaut. */
class FilterView : public LiveGraph, public juce::DragAndDropTarget
{
public:
    FilterView (IDGemsSynthProcessor& p, int index);
    void paint (juce::Graphics&) override;
    void tick() override
    {
        LiveGraph::tick();
        const int f = proc.modTargets.fieldForParam (proc.paramIndex (filtId (index, "cutoff")));
        const float m = f >= 0 ? proc.modShared.fieldValue01[f].load() : -1.0f;
        if (std::abs (m - lastMod) > 0.002f) { lastMod = m; repaint(); }
    }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent& e) override { const bool h = nodePos().getDistanceFrom (e.position) < 14.0f; if (h != hoverNode) { hoverNode = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hoverNode = false; repaint(); }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("mod:"); }
    void itemDragEnter (const SourceDetails&) override { modHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { modHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    /** Gain (linéaire) de la réponse du filtre à la fréquence f, pour l'affichage. */
    static float magnitude (int type, float cutoff, float reso, float freq);

private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (3.0f); }
    float xOf (float hz) const;
    float hzOf (float x) const;
    float yOfDb (float db) const;
    juce::Point<float> nodePos() const;
    int index;
    ParamDrag cutoff, reso, drive;
    bool dragging = false, hoverNode = false, modHover = false;
    float lastMod = -2.0f;
};

/** Enveloppe éditable : points à glisser (temps, sustain) + poignées de courbe au milieu des segments. */
class EnvelopeEditor : public LiveGraph
{
public:
    EnvelopeEditor (IDGemsSynthProcessor& p, int envIndex);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    enum Handle { HDelay, HAttack, HHold, HDecay, HRelease, HCurveA, HCurveD, HCurveR, NumHandles };
    struct Layout { float x0, xDelay, xAttack, xHold, xDecay, xSustainEnd, xRelease, px; };
    Layout layout (bool useFrozen) const;
    juce::Point<float> handlePos (int h, const Layout& l) const;
    int hitTest (juce::Point<float> p) const;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 12.0f); }
    float yOf (float level) const { auto r = plot(); return r.getBottom() - level * r.getHeight(); }

    ParamDrag delay, attack, hold, decay, sustain, release, aCurve, dCurve, rCurve;
    int active = -1, hover = -1;
    Layout frozen {};
    bool isFrozen = false;
    float dragStartValue = 0.0f;
    juce::Point<float> dragStart;
};

/** Unison : chaque voix est un trait placé selon son panoramique, hauteur = niveau, décalage = désaccord. */
class UnisonView : public LiveGraph
{
public:
    UnisonView (IDGemsSynthProcessor& p, int osc);
    void paint (juce::Graphics&) override;

private:
    int osc;
};

/** Aperçu du sub (forme et octave) et du bruit (extrait + tonalité). */
class SubView : public LiveGraph
{
public:
    explicit SubView (IDGemsSynthProcessor& p);
    void paint (juce::Graphics&) override;
};

/** Choix du type de bruit : une rangée de petits logos cliquables (un par bruit). */
class NoiseTypeSelector : public juce::Component, public juce::TooltipClient
{
public:
    explicit NoiseTypeSelector (IDGemsSynthProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { const int c = cellAt (e.position.x); if (c != hover) { hover = c; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    juce::String getTooltip() override { return hover >= 0 ? noiseTypeNames()[hover] : IDG_T ("Type de bruit"); }
    void tick();

    /** Logo d'un type de bruit dans un rectangle. */
    static void drawIcon (juce::Graphics&, int type, juce::Rectangle<float>, juce::Colour);

private:
    int cellAt (float x) const { return juce::jlimit (0, NumNoiseTypes - 1, (int) (x / ((float) getWidth() / (float) NumNoiseTypes))); }
    IDGemsSynthProcessor& proc;
    ParamDrag type;
    int hover = -1, last = -1;
    bool lastOn = false;
};

} // namespace idgs::ui
