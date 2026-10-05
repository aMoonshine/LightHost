#pragma once

// JUCE 8 has no generated JuceHeader.h, so this file plays that role: it pulls in
// every module Light Host uses and puts the framework namespace in scope, which keeps
// the rest of the sources close to how they were written against JUCE 4.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "BinaryData.h"

using namespace juce;

class IconMenu;
class PluginWindow;

/** Settings access for the whole app.
    Backed by a plain pointer that is cleared only in PluginHostApp's destructor, i.e.
    after JUCE has finished tearing itself down. Routing this through
    JUCEApplication::getInstance() instead meant a null dereference for anything JUCE
    touched after ApplicationProperties had been deleted inside shutdown().
*/
PropertiesFile& getUserSettings();
ApplicationProperties& getAppProperties();

/** True once a quit has been requested, including a Windows session end. */
bool isAppQuitting() noexcept;

/** Quits via PluginHostApp::systemRequestedQuit() so the early teardown runs. */
void requestApplicationQuit();
