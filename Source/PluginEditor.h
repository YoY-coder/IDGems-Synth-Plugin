#pragma once
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/SynthPage.h"
#include "UI/PresetBrowser.h"
#include "UI/FxPage.h"
#include "UI/TexturePage.h"
#include "UI/PlayPage.h"
#include "UI/RandomUI.h"
#include "UI/ExportPage.h"

/** Interface d'IDGems Synth : barre du haut toujours visible, onglets façon Serum, clavier à l'écran.
    Redimensionnable : tout le contenu est dessiné à une taille de référence puis mis à l'échelle. */
class IDGemsSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int baseW = 1500, baseH = 900;

    explicit IDGemsSynthEditor (IDGemsSynthProcessor&);
    ~IDGemsSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showTab (int index) { selectTab (index); }
    void showRandomPanel();

    struct Content;

private:
    void timerCallback() override;
    void selectTab (int index);

    IDGemsSynthProcessor& proc;
    idg::ui::IDGLookAndFeel lnf;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::TooltipWindow> tooltip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IDGemsSynthEditor)
};
