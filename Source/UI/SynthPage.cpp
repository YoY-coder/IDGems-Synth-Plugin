#include "SynthPage.h"

namespace idgs::ui
{
SynthPage::SynthPage (IDGemsSynthProcessor& p)
    : proc (p),
      oscOn (p, P::oscAOn, "ON"),
      table (p, P::oscATable),
      wtView (p),
      pos (p, P::oscAPos, "WT Pos"), oct (p, P::oscAOct, "Oct"), semi (p, P::oscASemi, "Semi"), fine (p, P::oscAFine, "Fine"),
      level (p, P::oscALevel, "Level"), pan (p, P::oscAPan, "Pan"), phase (p, P::oscAPhase, "Phase"), phRand (p, P::oscAPhRand, "Rand"),
      envView (p),
      delay (p, P::env1Delay, "Delay"), attack (p, P::env1Attack, "Attack"), hold (p, P::env1Hold, "Hold"),
      decay (p, P::env1Decay, "Decay"), sustain (p, P::env1Sustain, "Sustain"), release (p, P::env1Release, "Release"),
      aCurve (p, P::env1ACurve, "A curve", Theme::blue), dCurve (p, P::env1DCurve, "D curve", Theme::blue), rCurve (p, P::env1RCurve, "R curve", Theme::blue),
      voices (p, P::voices, "Voix", Theme::blue), velSens (p, P::velSens, IDG_T ("VÉLOCITÉ"), Theme::blue), bendRange (p, P::bendRange, "Bend", Theme::blue)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &oscOn, &table, &wtView, &pos, &oct, &semi, &fine, &level, &pan, &phase, &phRand,
             &envView, &delay, &attack, &hold, &decay, &sustain, &release, &aCurve, &dCurve, &rCurve,
             &voices, &velSens, &bendRange })
        addAndMakeVisible (c);
}

void SynthPage::tick()
{
    wtView.tick();
    envView.tick();
}

void SynthPage::paint (juce::Graphics& g)
{
    auto frame = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour c, float alpha = 1.0f)
    {
        Theme::panelFrame (g, r.toFloat(), c, alpha);
        Theme::sectionTitle (g, title, r.toFloat().reduced (10.0f, 7.0f), c);
    };
    frame (oscArea, "Osc A  //  wavetable", Theme::accent);
    frame (envArea, IDG_T ("Env 1  //  volume des voix"), Theme::accent);
    frame (voiceArea, "Voix", Theme::blue);

    // emplacements des modules à venir
    auto future = [&] (juce::Rectangle<int> r, const juce::String& title, const juce::StringArray& lines)
    {
        frame (r, title, Theme::textDim, 0.45f);
        g.setFont (Theme::mono (11.0f));
        g.setColour (Theme::textDim.withAlpha (0.7f));
        auto t = r.reduced (16, 34);
        for (auto& l : lines) { g.drawText (l, t.removeFromTop (18), juce::Justification::left); }
    };
    future (futureOsc, "Osc B / Texture / Sub / Noise",
            { IDG_T ("phase 2 : Osc B, Sub, Noise, cross-mod"),
              IDG_T ("phase 2 : warps, unison, inharmonicité"),
              IDG_T ("phase 5 : Osc Texture (granulaire,"),
              IDG_T ("          spectral, chaos, résonateur...)") });
    future (futureFilter, "Filtres",
            { IDG_T ("phase 2 : 2 filtres par voix"),
              IDG_T ("série / parallèle, routage par osc"),
              IDG_T ("LP · HP · BP · notch · ladder · comb"),
              IDG_T ("formant · destructeur") });
}

void SynthPage::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto top = r.removeFromTop (r.getHeight() * 52 / 100);
    r.removeFromTop (10);
    auto bottom = r;

    oscArea = top.removeFromLeft (660);
    top.removeFromLeft (10);
    futureOsc = top;

    envArea = bottom.removeFromLeft (660);
    bottom.removeFromLeft (10);
    voiceArea = bottom.removeFromLeft (230);
    bottom.removeFromLeft (10);
    futureFilter = bottom;

    // ---- Osc A
    {
        auto a = oscArea.reduced (12, 8);
        auto head = a.removeFromTop (22);
        oscOn.setBounds (head.removeFromRight (44).reduced (0, 1));
        head.removeFromRight (8);
        table.setBounds (head.removeFromRight (170).reduced (0, 1));
        a.removeFromTop (6);
        auto knobs = a.removeFromBottom (70);
        wtView.setBounds (a);
        const int kw = knobs.getWidth() / 8;
        for (auto* k : { &pos, &level, &pan, &oct, &semi, &fine, &phase, &phRand })
            k->setBounds (knobs.removeFromLeft (kw).reduced (3, 2));
    }
    // ---- Env 1
    {
        auto a = envArea.reduced (12, 8);
        a.removeFromTop (22);
        auto knobs = a.removeFromBottom (70);
        auto curves = a.removeFromRight (78);
        envView.setBounds (a);
        const int kw = knobs.getWidth() / 6;
        for (auto* k : { &delay, &attack, &hold, &decay, &sustain, &release })
            k->setBounds (knobs.removeFromLeft (kw).reduced (3, 2));
        const int ch = curves.getHeight() / 3;
        for (auto* k : { &aCurve, &dCurve, &rCurve })
            k->setBounds (curves.removeFromTop (ch).reduced (4, 0));
    }
    // ---- Voix
    {
        auto a = voiceArea.reduced (12, 8);
        a.removeFromTop (26);
        const int kh = a.getHeight() / 2;
        auto row = a.removeFromTop (kh);
        voices.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (6, 4));
        velSens.setBounds (row.reduced (6, 4));
        bendRange.setBounds (a.removeFromLeft (a.getWidth() / 2).reduced (6, 4));
    }
}
} // namespace idgs::ui
