/*
    IconMenu.cpp - Light Host
*/

#include "IconMenu.hpp"
#include "PluginWindow.h"

#include <ctime>
#include <limits.h>

//==============================================================================
namespace
{
    constexpr AudioProcessorGraph::NodeID inputNodeId  { 1000000 };
    constexpr AudioProcessorGraph::NodeID outputNodeId { 1000001 };

    void connectChannels (AudioProcessorGraph& graph,
                          AudioProcessorGraph::NodeID srcNode, int srcChannel,
                          AudioProcessorGraph::NodeID dstNode, int dstChannel)
    {
        graph.addConnection ({ { srcNode, srcChannel },
                               { dstNode, dstChannel } });
    }
}

//==============================================================================
class IconMenu::PluginListWindow  : public DocumentWindow
{
public:
    PluginListWindow (IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
        : DocumentWindow ("Available Plugins", Colours::white,
                          DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          owner (owner_)
    {
        const File deadMansPedalFile (getUserSettings().getFile().getSiblingFile ("RecentlyCrashedPluginsList"));

        setContentOwned (new PluginListComponent (pluginFormatManager,
                                                  owner.knownPluginList,
                                                  deadMansPedalFile,
                                                  &getUserSettings()), true);

        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (300, 400, 800, 1500);
        setTopLeftPosition (60, 60);

        restoreWindowStateFromString (getUserSettings().getValue ("listWindowPos"));
        setVisible (true);
    }

    ~PluginListWindow() override
    {
        getUserSettings().setValue ("listWindowPos", getWindowStateAsString());
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        owner.removePluginsLackingInputOutput();

        // Deleting the window here would delete `this` and then return through the
        // destroyed object. Ask the owner to do it from the message queue instead.
        Component::SafePointer<IconMenu> safeOwner (&owner);

        MessageManager::getInstance()->callAsync ([safeOwner]
        {
            if (safeOwner != nullptr)
                safeOwner->closePluginListWindow();
        });
    }

private:
    IconMenu& owner;
};

//==============================================================================
IconMenu::IconMenu()
{
    // JUCE ships two headers that look interchangeable but are not: VSTPluginFormat is
    // guarded by JUCE_INTERNAL_HAS_VST (VST2, compiled out above) and VST3PluginFormat
    // by JUCE_INTERNAL_HAS_VST3. A VST2 SDK is no longer obtainable, so VST3 is the only
    // format this host can offer.
    auto vst3 = std::make_unique<VST3PluginFormat>();
    auto& vst3Format = *vst3;                 // scanner keeps a reference, manager owns it
    formatManager.addFormat (std::move (vst3));

    const File deadMansPedalFile (getUserSettings().getFile().getSiblingFile ("RecentlyCrashedPluginsList"));

    scanner = std::make_unique<PluginDirectoryScanner> (knownPluginList,
                                                         vst3Format,
                                                         FileSearchPath(),
                                                         true,   // searchRecursively
                                                         deadMansPedalFile);

    // Audio device
    auto savedAudioState = getUserSettings().getXmlValue ("audioDeviceState");
    deviceManager.initialise (256, 256, savedAudioState.get(), true);
    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&player);

    // Plugins - all
    if (auto savedPluginList = getUserSettings().getXmlValue ("pluginList"))
        knownPluginList.recreateFromXml (*savedPluginList);

    knownPluginList.addChangeListener (this);

    // Plugins - active
    if (auto savedPluginListActive = getUserSettings().getXmlValue ("pluginListActive"))
        activePluginList.recreateFromXml (*savedPluginListActive);

    loadActivePlugins();
    activePluginList.addChangeListener (this);

    setIcon();
    setIconTooltip (JUCEApplication::getInstance()->getApplicationName());
}

IconMenu::~IconMenu()
{
    // Idempotent, and normally already done from PluginHostApp::shutdown(). Calling it
    // again here means the audio thread is guaranteed to be stopped before
    // savePluginStates() runs, even if shutdown() never reached us.
    prepareToDie();

    savePluginStates();

    knownPluginList.removeChangeListener (this);
    activePluginList.removeChangeListener (this);
}

void IconMenu::prepareToDie()
{
    // 1. Nothing that could re-enter this object.
    stopTimer();
    PopupMenu::dismissAllActiveMenus();

    // 2. The tray icon. On a Windows session end JUCE answers WM_QUERYENDSESSION
    //    immediately, so we are already being torn down by the OS at this point.
    //    Dropping the icon now destroys JUCE's tray Pimpl while its window handle is
    //    still valid, instead of from ~SystemTrayIconComponent later on, when the
    //    handle may have been destroyed or recycled by user32.
    setIconImage (Image(), Image());

    // 3. Plugin editors hold raw Node* into the graph. They are real top-level windows,
    //    so they have to be gone before ~AudioProcessorGraph deletes those nodes -
    //    otherwise moved() writes through freed memory while JUCE destroys windows.
    PluginWindow::closeAllCurrentlyOpenWindows();

    if (pluginListWindow != nullptr)
        pluginListWindow.reset();

    // 4. The audio thread. deviceManager keeps &player in its callback list, so player
    //    and graph must be unreachable from it before they are destroyed - and they are
    //    destroyed after this function returns. This also stops the device, which is
    //    what makes the savePluginStates() in ~IconMenu race-free.
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
    deviceManager.closeAudioDevice();
}

//==============================================================================
void IconMenu::setIcon()
{
    String defaultColor = "white";

    if (! getUserSettings().containsKey ("icon"))
        getUserSettings().setValue ("icon", defaultColor);

    const String color = getUserSettings().getValue ("icon");

    Image icon;

    if (color.equalsIgnoreCase ("white"))
        icon = ImageFileFormat::loadFrom (BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
    else if (color.equalsIgnoreCase ("black"))
        icon = ImageFileFormat::loadFrom (BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);

    // JUCE 8 wants a colour image plus a separate macOS template image.
    setIconImage (icon, Image());
}

//==============================================================================
void IconMenu::loadActivePlugins()
{
    constexpr int channelOne = 0;
    constexpr int channelTwo = 1;

    PluginWindow::closeAllCurrentlyOpenWindows();
    graph.clear();

    inputNode = graph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                                   AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), inputNodeId);

    outputNode = graph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                                    AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), outputNodeId);

    if (activePluginList.getNumTypes() == 0)
    {
        connectChannels (graph, inputNodeId,  channelOne, outputNodeId, channelOne);
        connectChannels (graph, inputNodeId,  channelTwo, outputNodeId, channelTwo);
    }

    int pluginTime = 0;
    AudioProcessorGraph::NodeID lastId { 0 };
    bool hasInputConnected = false;

    // NOTE: Node ids cannot begin at 0.
    for (int i = 1; i <= activePluginList.getNumTypes(); ++i)
    {
        const PluginDescription plugin = getNextPluginOlderThanTime (pluginTime);
        String errorMessage;

        auto instance = formatManager.createPluginInstance (plugin,
                                                             graph.getSampleRate(),
                                                             graph.getBlockSize(),
                                                             errorMessage);

        if (instance == nullptr)
        {
            // A plugin can fail to instantiate (missing file, licence check, wrong
            // format). Skip it rather than taking the whole chain down with it.
            DBG ("Light Host: could not create plugin " + plugin.name + " - " + errorMessage);
            continue;
        }

        MemoryBlock savedPluginBinary;
        savedPluginBinary.fromBase64Encoding (getUserSettings().getValue (getKey ("state", plugin)));

        if (savedPluginBinary.getSize() > 0)
            instance->setStateInformation (savedPluginBinary.getData(),
                                          (int) savedPluginBinary.getSize());

        const AudioProcessorGraph::NodeID thisId { (uint32) i };
        graph.addNode (std::move (instance), thisId);

        const bool bypass = getUserSettings().getBoolValue (getKey ("bypass", plugin), false);

        if (! bypass)
        {
            if (! hasInputConnected)
            {
                connectChannels (graph, inputNodeId, channelOne, thisId, channelOne);
                connectChannels (graph, inputNodeId, channelTwo, thisId, channelTwo);
                hasInputConnected = true;
            }
            else
            {
                connectChannels (graph, lastId, channelOne, thisId, channelOne);
                connectChannels (graph, lastId, channelTwo, thisId, channelTwo);
            }

            lastId = thisId;
        }
    }

    if (lastId.uid != 0)
    {
        connectChannels (graph, lastId, channelOne, outputNodeId, channelOne);
        connectChannels (graph, lastId, channelTwo, outputNodeId, channelTwo);
    }
}

PluginDescription IconMenu::getNextPluginOlderThanTime (int& time) const
{
    const int timeStatic = time;
    PluginDescription closest;
    int diff = INT_MAX;

    for (int i = 0; i < activePluginList.getNumTypes(); ++i)
    {
        const PluginDescription plugin = activePluginList.getTypes()[i];
        const String pluginTimeString = getUserSettings().getValue (getKey ("order", plugin));
        const int pluginTime = pluginTimeString.trim().getIntValue();

        if (pluginTime > timeStatic && abs (timeStatic - pluginTime) < diff)
        {
            diff = abs (timeStatic - pluginTime);
            closest = plugin;
            time = pluginTime;
        }
    }

    return closest;
}

void IconMenu::changeListenerCallback (ChangeBroadcaster* changed)
{
    if (isAppQuitting())
        return;

    if (changed == &knownPluginList)
    {
        if (auto savedPluginList = knownPluginList.createXml())
        {
            getUserSettings().setValue ("pluginList", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
    else if (changed == &activePluginList)
    {
        if (auto savedPluginList = activePluginList.createXml())
        {
            getUserSettings().setValue ("pluginListActive", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
}

//==============================================================================
void IconMenu::timerCallback()
{
    stopTimer();

    if (isAppQuitting())
        return;

    menu.clear();
    menu.addSectionHeader (JUCEApplication::getInstance()->getApplicationName());

    if (menuIconLeftClicked)
    {
        menu.addItem (1, "Preferences");
        menu.addItem (2, "Edit Plugins");
        menu.addSeparator();
        menu.addSectionHeader ("Active Plugins");

        const auto timeSorted = getTimeSortedList();

        for (int i = 0; i < activePluginList.getNumTypes(); ++i)
        {
            PopupMenu options;
            options.addItem (INDEX_EDIT + i, "Edit");
            options.addItem (INDEX_BYPASS + i, "Bypass", true,
                             getUserSettings().getBoolValue (getKey ("bypass", timeSorted[i])));
            options.addSeparator();
            options.addItem (INDEX_MOVE_UP + i, "Move Up", i > 0);
            options.addItem (INDEX_MOVE_DOWN + i, "Move Down", i < (int) timeSorted.size() - 1);
            options.addSeparator();
            options.addItem (INDEX_DELETE + i, "Delete");

            menu.addSubMenu (timeSorted[i].name, options);
        }

        menu.addSeparator();
        menu.addSectionHeader ("Available Plugins");
        KnownPluginList::addToMenu (menu, knownPluginList.getTypes(), pluginSortMethod);
    }
    else
    {
        menu.addItem (1, "Quit");
        menu.addSeparator();
        menu.addItem (2, "Delete Plugin States");
        menu.addItem (3, "Invert Icon Color");
    }

    // The tray popup has no component to anchor to on Windows, so it is positioned from
    // the cached cursor location instead.
    if (x == 0 || y == 0)
    {
        const auto mousePos = Desktop::getMousePosition();
        x = mousePos.x;
        y = mousePos.y;
    }

    const Rectangle<int> rect (x, y, 1, 1);

    menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (rect),
                        ModalCallbackFunction::forComponent (menuInvocationCallback, this));
}

void IconMenu::mouseDown (const MouseEvent& e)
{
    Process::makeForegroundProcess();
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    startTimer (50);
}

//==============================================================================
void IconMenu::menuInvocationCallback (int id, IconMenu* im)
{
    if (im == nullptr || isAppQuitting())
        return;

    // Right click
    if (! im->menuIconLeftClicked)
    {
        if (id == 1)
        {
            im->savePluginStates();
            requestApplicationQuit();
            return;
        }

        if (id == 2)
        {
            im->deletePluginStates();
            im->loadActivePlugins();
            return;
        }

        if (id == 3)
        {
            const String color = getUserSettings().getValue ("icon");
            getUserSettings().setValue ("icon", color.equalsIgnoreCase ("black") ? "white" : "black");
            im->setIcon();
            return;
        }
    }

    // Audio settings
    if (id == 1)
    {
        im->showAudioSettings();
        return;
    }

    // Reload
    if (id == 2)
    {
        im->reloadPlugins();
        return;
    }

    // Plugins
    if (id > 2)
    {
        // Delete plugin
        if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        {
            const auto timeSorted = im->getTimeSortedList();
            const int index = id - im->INDEX_DELETE;

            if (index >= (int) timeSorted.size())
                return;

            const String key = getKey ("order", timeSorted[index]);

            for (const auto& current : im->activePluginList.getTypes())
            {
                if (key.equalsIgnoreCase (getKey ("order", current)))
                {
                    getUserSettings().removeValue (getKey ("order", timeSorted[index]));
                    getUserSettings().removeValue (getKey ("bypass", timeSorted[index]));
                    getAppProperties().saveIfNeeded();

                    im->activePluginList.removeType (current);
                    break;
                }
            }

            im->savePluginStates();
            im->loadActivePlugins();
        }
        // Add plugin
        else if (KnownPluginList::getIndexChosenByMenu (im->knownPluginList.getTypes(), id) > -1)
        {
            const auto types = im->knownPluginList.getTypes();
            const PluginDescription plugin = types[KnownPluginList::getIndexChosenByMenu (types, id)];

            getUserSettings().setValue (getKey ("order", plugin), (int) time (nullptr));
            getAppProperties().saveIfNeeded();
            im->activePluginList.addType (plugin);

            im->savePluginStates();
            im->loadActivePlugins();
        }
        // Bypass plugin
        else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
        {
            const auto timeSorted = im->getTimeSortedList();
            const int index = id - im->INDEX_BYPASS;

            if (index >= (int) timeSorted.size())
                return;

            const String key = getKey ("bypass", timeSorted[index]);

            getUserSettings().setValue (key, ! getUserSettings().getBoolValue (key));
            getAppProperties().saveIfNeeded();

            im->savePluginStates();
            im->loadActivePlugins();
        }
        // Show active plugin GUI
        else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        {
            if (auto* f = im->graph.getNodeForId (AudioProcessorGraph::NodeID { (uint32) (id - im->INDEX_EDIT + 1) }))
                if (auto* const w = PluginWindow::getWindowFor (f, PluginWindow::Normal))
                    w->toFront (true);
        }
        // Move plugin up the list
        else if (id >= im->INDEX_MOVE_UP && id < im->INDEX_MOVE_UP + 1000000)
        {
            im->movePlugin (id - im->INDEX_MOVE_UP, -1);
        }
        // Move plugin down the list
        else if (id >= im->INDEX_MOVE_DOWN && id < im->INDEX_MOVE_DOWN + 1000000)
        {
            im->movePlugin (id - im->INDEX_MOVE_DOWN, +1);
        }

        // Update menu
        im->startTimer (50);
    }
}

void IconMenu::movePlugin (int index, int direction)
{
    const auto timeSorted = getTimeSortedList();

    if (index < 0 || index >= (int) timeSorted.size())
        return;

    const int target = index + direction;

    if (target < 0 || target >= (int) timeSorted.size())
        return;

    // Renumber the whole chain so the requested plugin lands on the neighbour's slot.
    // The original code shifted indexes in place and wrote past the end of the array
    // when moving the last item.
    for (int i = 0; i < (int) timeSorted.size(); ++i)
        getUserSettings().setValue (getKey ("order", timeSorted[i]), i);

    const PluginDescription moved = timeSorted[index];
    const PluginDescription swapped = timeSorted[target];

    getUserSettings().setValue (getKey ("order", moved), target);
    getUserSettings().setValue (getKey ("order", swapped), index);
    getAppProperties().saveIfNeeded();

    savePluginStates();
    loadActivePlugins();
}

//==============================================================================
std::vector<PluginDescription> IconMenu::getTimeSortedList() const
{
    int time = 0;
    std::vector<PluginDescription> list;

    for (int i = 0; i < activePluginList.getNumTypes(); ++i)
        list.push_back (getNextPluginOlderThanTime (time));

    return list;
}

String IconMenu::getKey (String type, const PluginDescription& plugin)
{
    return "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
}

void IconMenu::deletePluginStates()
{
    const auto list = getTimeSortedList();

    for (int i = 0; i < (int) list.size(); ++i)
        getUserSettings().removeValue (getKey ("state", list[i]));

    getAppProperties().saveIfNeeded();
}

void IconMenu::savePluginStates()
{
    const auto list = getTimeSortedList();

    for (int i = 0; i < (int) list.size(); ++i)
    {
        // The graph stores plugins at ids 1..n, matching the sorted order used above.
        if (auto* node = graph.getNodeForId (AudioProcessorGraph::NodeID { (uint32) (i + 1) }))
        {
            if (auto* processor = node->getProcessor())
            {
                MemoryBlock savedStateBinary;
                processor->getStateInformation (savedStateBinary);
                getUserSettings().setValue (getKey ("state", list[i]),
                                            savedStateBinary.toBase64Encoding());
            }
        }
    }

    // One flush for the whole loop, instead of one settings-file write per plugin.
    getAppProperties().saveIfNeeded();
}

//==============================================================================
void IconMenu::showAudioSettings()
{
    AudioDeviceSelectorComponent audioSettingsComp (deviceManager, 0, 256, 0, 256, false, false, true, true);
    audioSettingsComp.setSize (500, 450);

    DialogWindow::LaunchOptions o;
    o.content.setNonOwned (&audioSettingsComp);
    o.dialogTitle                   = "Audio Settings";
    o.componentToCentreAround       = this;
    o.dialogBackgroundColour        = Colour::fromRGB (236, 236, 236);
    o.escapeKeyTriggersCloseButton  = true;
    o.useNativeTitleBar             = true;
    o.resizable                     = false;

    o.runModal();

    // The modal loop above pumps the message queue, so a session end can have been
    // processed while it was open. Do not touch settings or the device after that.
    if (isAppQuitting())
        return;

    auto audioState = deviceManager.createStateXml();

    getUserSettings().setValue ("audioDeviceState", audioState.get());
    getAppProperties().saveIfNeeded();
}

void IconMenu::reloadPlugins()
{
    if (pluginListWindow == nullptr)
        pluginListWindow = std::make_unique<PluginListWindow> (*this, formatManager);

    pluginListWindow->toFront (true);
}

void IconMenu::removePluginsLackingInputOutput()
{
    // Collect descriptions, then remove by value. removeType() takes a description in
    // JUCE 8, and iterating with a running index shifts the list under you.
    const auto types = knownPluginList.getTypes();
    Array<PluginDescription> doomed;

    for (const auto& plugin : types)
        if (plugin.numInputChannels < 2 || plugin.numOutputChannels < 2)
            doomed.add (plugin);

    for (const auto& plugin : doomed)
        knownPluginList.removeType (plugin);
}

void IconMenu::closePluginListWindow()
{
    pluginListWindow.reset();
}
