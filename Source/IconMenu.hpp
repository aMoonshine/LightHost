/*
    IconMenu.hpp - Light Host

    The tray icon is the app's entire user interface: there is no main window.
*/

#pragma once

#include "LightHostJuceHeader.h"

class IconMenu : public SystemTrayIconComponent,
                 private Timer,
                 public ChangeListener
{
public:
    IconMenu();
    ~IconMenu() override;

    /** Stops everything that can still reach into this object from another thread or
        from JUCE's own teardown: the tray timer, any open popup, the plugin editor
        windows (which hold raw pointers to graph nodes) and the audio device (which
        holds &player as a registered callback).

        Idempotent, and called both from PluginHostApp::shutdown() and from the
        destructor, so that no caller can forget it.
    */
    void prepareToDie();

    void mouseDown (const MouseEvent&) override;
    static void menuInvocationCallback (int id, IconMenu* im);
    void changeListenerCallback (ChangeBroadcaster* changed) override;
    static String getKey (String type, const PluginDescription& plugin);

    static constexpr int INDEX_EDIT     = 1000000;
    static constexpr int INDEX_BYPASS   = 2000000;
    static constexpr int INDEX_DELETE   = 3000000;
    static constexpr int INDEX_MOVE_UP  = 4000000;
    static constexpr int INDEX_MOVE_DOWN = 5000000;

private:
    void timerCallback() override;
    void reloadPlugins();
    void closePluginListWindow();
    void showAudioSettings();
    void loadActivePlugins();
    void savePluginStates();
    void deletePluginStates();
    void movePlugin (int index, int direction);
    PluginDescription getNextPluginOlderThanTime (int& time) const;
    AudioProcessorGraph::NodeID getNodeIdFor (const PluginDescription& plugin) const;
    void removePluginsLackingInputOutput();
    std::vector<PluginDescription> getTimeSortedList() const;
    void setIcon();

    class PluginListWindow;

    // Member declaration order is load bearing. C++ destroys members in reverse
    // declaration order, so deviceManager is declared LAST so that it is destroyed
    // FIRST: it owns the audio thread and keeps &player in its callback list, so both
    // player and graph have to be gone before it closes the device. Declaring it
    // first (as this class used to) meant the device outlived the callback it was
    // still calling, on every single shutdown.
    //
    // formatManager is declared before graph because the AudioPluginInstance objects
    // inside the graph hold a pointer back to the format that created them.
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    KnownPluginList activePluginList;
    KnownPluginList::SortMethod pluginSortMethod { KnownPluginList::sortByManufacturer };
    std::unique_ptr<PluginDirectoryScanner> scanner;
    PopupMenu menu;

    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
    AudioDeviceManager deviceManager;   // destroyed first - see above

    AudioProcessorGraph::Node* inputNode  = nullptr;
    AudioProcessorGraph::Node* outputNode = nullptr;

    // Successfully instantiated plugins in graph order; index + 1 is the node id.
    std::vector<PluginDescription> nodeDescriptions;
    bool menuIconLeftClicked = false;
    int x = 0, y = 0;   // cached cursor position for the tray popup on Windows

    std::unique_ptr<PluginListWindow> pluginListWindow;
};
