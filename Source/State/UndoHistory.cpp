#include "UndoHistory.h"
#include "../PluginProcessor.h"

namespace idgs
{
UndoHistory::UndoHistory (IDGemsSynthProcessor& p) : proc (p)
{
    for (auto* prm : proc.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm);
        isMacro.push_back (rp != nullptr && rp->getParameterID().startsWith ("macro"));
    }
    current = proc.captureState();
    sig = signature();
    startTimerHz (4);
}

std::vector<float> UndoHistory::signature() const
{
    const auto& params = proc.getParameters();
    std::vector<float> s;
    s.reserve ((size_t) params.size() + 4);
    // les macros rejouées par le motion recording ne comptent pas comme des modifications
    const bool motionPlaying = proc.isMotionPlaying();
    for (int i = 0; i < params.size(); ++i)
    {
        s.push_back (motionPlaying && isMacro[(size_t) i] ? 0.0f : params[i]->getValue());
    }
    s.push_back ((float) proc.modShared.version.load());
    s.push_back ((float) proc.noteSeq.version.load());
    s.push_back ((float) proc.assetsVersion.load());
    return s;
}

void UndoHistory::timerCallback()
{
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) return;
    auto s = signature();
    if (s == sig) return;
    sig = std::move (s);
    undoStack.push_back (current);
    if ((int) undoStack.size() > maxSteps) undoStack.erase (undoStack.begin());
    redoStack.clear();
    current = proc.captureState();
    if (onChange) onChange();
}

void UndoHistory::commit() { timerCallback(); }

void UndoHistory::restore (const juce::ValueTree& t)
{
    proc.restoreState (t, true);
    current = t;
    sig = signature();
    if (onChange) onChange();
}

void UndoHistory::undo()
{
    commit();
    if (undoStack.empty()) return;
    redoStack.push_back (current);
    auto t = undoStack.back();
    undoStack.pop_back();
    restore (t);
}

void UndoHistory::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (current);
    auto t = redoStack.back();
    redoStack.pop_back();
    restore (t);
}

void UndoHistory::selectSlot (int s)
{
    s = juce::jlimit (0, 1, s);
    if (s == slot) return;
    commit();
    slots[slot] = proc.captureState();
    slot = s;
    if (slots[slot].isValid()) restore (slots[slot]);
    else slots[slot] = slots[1 - slot];
    if (onChange) onChange();
}

void UndoHistory::copyActiveToOther()
{
    slots[1 - slot] = proc.captureState();
    if (onChange) onChange();
}
} // namespace idgs
