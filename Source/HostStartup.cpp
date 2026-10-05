/*
    HostStartup.cpp - Light Host
*/

#include "LightHostJuceHeader.h"
#include "IconMenu.hpp"

// Mirrors PluginHostApp::appProperties, and outlives shutdown() on purpose. Declared
// up here so the application class below can see it.
static ApplicationProperties* appPropertiesPtr = nullptr;

class PluginHostApp  : public JUCEApplication
{
public:
    PluginHostApp() = default;

    ~PluginHostApp() override
    {
        // Last point at which anything in the app can legally run, so this is the only
        // place where the settings pointer may be cleared.
        appPropertiesPtr = nullptr;
    }

    void initialise (const String&) override
    {
        PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";

        checkArguments (&options);

        appProperties = std::make_unique<ApplicationProperties>();
        appProperties->setStorageParameters (options);
        appPropertiesPtr = appProperties.get();

        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        mainWindow = std::make_unique<IconMenu>();
    }

    void shutdown() override
    {
        // Quiesce first: stop the tray timer, close the plugin editor windows, detach and
        // close the audio device. Everything below this point runs against a stopped
        // graph, which is what makes the settings flush below safe.
        if (mainWindow != nullptr)
        {
            mainWindow->prepareToDie();
            mainWindow.reset();          // ~IconMenu saves plugin states against an idle graph
        }

        // Flush but deliberately do NOT delete. ApplicationProperties was previously
        // deleted here, while JUCE still had window and plugin teardown to run - and any
        // of that reaching getAppProperties() dereferenced a null pointer. Letting the
        // member destructor handle it keeps the object alive for the whole process.
        if (appProperties != nullptr)
            appProperties->saveIfNeeded();

        LookAndFeel::setDefaultLookAndFeel (nullptr);   // still last: every Component is gone
    }

    void systemRequestedQuit() override
    {
        // JUCE routes WM_QUERYENDSESSION here and answers "yes, end the session"
        // immediately, so the rest of shutdown() happens while Windows is already
        // tearing the session down. Take the tray icon away right now, while its
        // window is definitely still valid, rather than letting it be destroyed later
        // against an HWND the OS may already have freed.
        quitRequested = true;

        if (mainWindow != nullptr)
            mainWindow->prepareToDie();

        JUCEApplicationBase::quit();
    }

    const String getApplicationName() override         { return "Light Host"; }

    // JUCE normally injects this via the generated JuceHeader, which this project does
    // not use, so CMake passes the version in as a define instead.
    const String getApplicationVersion() override      { return LIGHT_HOST_VERSION; }

    bool moreThanOneInstanceAllowed() override
    {
        StringArray multiInstance = getParameter ("-multi-instance");
        return multiInstance.size() == 2;
    }

    /** True once a quit has been requested, including a Windows session end. Used to
        stop the tray from arming its timer or showing menus during teardown.
    */
    bool isQuitting() const noexcept                   { return quitRequested; }

private:
    StringArray getParameter (String lookFor) const
    {
        StringArray parameters = getCommandLineParameterArray();
        StringArray found;

        for (int i = 0; i < parameters.size(); ++i)
        {
            String param = parameters[i];

            if (param.contains (lookFor))
            {
                found.add (lookFor);
                int delimiter = param.indexOf (0, "=") + 1;
                found.add (param.substring (delimiter));
                return found;
            }
        }

        return found;
    }

    void checkArguments (PropertiesFile::Options* options) const
    {
        StringArray multiInstance = getParameter ("-multi-instance");

        if (multiInstance.size() == 2)
            options->filenameSuffix = multiInstance[1] + "." + options->filenameSuffix;
    }

    bool quitRequested = false;

    // Declaration order matters here too: mainWindow must be destroyed before the
    // look-and-feel it uses, and both before appProperties is flushed.
    std::unique_ptr<ApplicationProperties> appProperties;
    LookAndFeel_V4 lookAndFeel;
    std::unique_ptr<IconMenu> mainWindow;
};

//==============================================================================
PropertiesFile& getUserSettings()
{
    // Deliberately not routed through JUCEApplication::getInstance(): that can be null
    // while JUCE tears itself down, which turned every late settings access into a null
    // dereference.
    jassert (appPropertiesPtr != nullptr);
    return *appPropertiesPtr->getUserSettings();
}

ApplicationProperties& getAppProperties()
{
    jassert (appPropertiesPtr != nullptr);
    return *appPropertiesPtr;
}

bool isAppQuitting() noexcept
{
    if (auto* app = dynamic_cast<PluginHostApp*> (JUCEApplication::getInstance()))
        return app->isQuitting();

    return true;
}

void requestApplicationQuit()
{
    // Goes through systemRequestedQuit() rather than quit() so that the early tray and
    // audio teardown happens for menu-initiated quits as well as session ends.
    if (auto* app = dynamic_cast<PluginHostApp*> (JUCEApplication::getInstance()))
    {
        app->systemRequestedQuit();
        return;
    }

    if (auto* anyApp = JUCEApplication::getInstance())
        anyApp->quit();
}

START_JUCE_APPLICATION (PluginHostApp)
