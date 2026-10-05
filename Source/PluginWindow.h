/*
    PluginWindow.h - Light Host
*/

#pragma once

#include "LightHostJuceHeader.h"

class PluginWindow  : public DocumentWindow
{
public:
    enum WindowFormatType
    {
        Normal = 0,
        Generic,
        Programs,
        Parameters,
        NumTypes
    };

    PluginWindow (Component* pluginEditor, AudioProcessorGraph::Node*, WindowFormatType);
    ~PluginWindow() override;

    static PluginWindow* getWindowFor (AudioProcessorGraph::Node*, WindowFormatType);

    static void closeCurrentlyOpenWindowsFor (uint32 nodeId);
    static void closeAllCurrentlyOpenWindows();
    static bool containsActiveWindows();

    void moved() override;
    void closeButtonPressed() override;

private:
    // Deliberately a raw pointer, as it always was. It stays valid only because
    // closeAllCurrentlyOpenWindows() runs before the graph that owns the node is
    // destroyed - see IconMenu::prepareToDie().
    AudioProcessorGraph::Node* owner;
    WindowFormatType type;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

inline String toString (PluginWindow::WindowFormatType type)
{
    switch (type)
    {
        case PluginWindow::Normal:     return "Normal";
        case PluginWindow::Generic:    return "Generic";
        case PluginWindow::Programs:   return "Programs";
        case PluginWindow::Parameters: return "Parameters";
        default:                       return String();
    }
}

inline String getLastXProp (PluginWindow::WindowFormatType type)    { return "uiLastX_" + toString (type); }
inline String getLastYProp (PluginWindow::WindowFormatType type)    { return "uiLastY_" + toString (type); }
inline String getOpenProp  (PluginWindow::WindowFormatType type)    { return "uiopen_"  + toString (type); }
