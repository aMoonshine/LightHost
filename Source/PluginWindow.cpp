/*
    PluginWindow.cpp - Light Host
*/

#include "PluginWindow.h"

static Array<PluginWindow*> activePluginWindows;

PluginWindow::PluginWindow (Component* const pluginEditor,
                            AudioProcessorGraph::Node* const o,
                            WindowFormatType t)
    : DocumentWindow (pluginEditor->getName(), Colours::lightgrey,
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner (o),
      type (t)
{
    setSize (400, 300);
    setUsingNativeTitleBar (true);
    setContentOwned (pluginEditor, true);

    setTopLeftPosition (owner->properties.getWithDefault (getLastXProp (type), Random::getSystemRandom().nextInt (500)),
                        owner->properties.getWithDefault (getLastYProp (type), Random::getSystemRandom().nextInt (500)));

    owner->properties.set (getOpenProp (type), true);

    setVisible (true);
    activePluginWindows.add (this);
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    // The editor windows are heavyweight top-level HWNDs; deleting them
    // synchronously destroys their peers, so no nested event loop is needed.
    if (activePluginWindows.size() > 0)
    {
        for (int i = activePluginWindows.size(); --i >= 0;)
            delete activePluginWindows.getUnchecked (i);
    }
}

//==============================================================================
class ProcessorProgramPropertyComp  : public PropertyComponent,
                                       private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
        : PropertyComponent (name),
          owner (p),
          index (index_)
    {
        owner.addListener (this);
    }

    ~ProcessorProgramPropertyComp() override
    {
        owner.removeListener (this);
    }

    void refresh() override {}
    void audioProcessorChanged (AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChanged (AudioProcessor*, int, float) override {}

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor  : public AudioProcessorEditor
{
public:
    explicit ProgramAudioProcessorEditor (AudioProcessor* const p)
        : AudioProcessorEditor (p)
    {
        jassert (p != nullptr);
        setOpaque (true);

        addAndMakeVisible (panel);

        Array<PropertyComponent*> programs;
        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        for (int i = 0; i < numPrograms; ++i)
        {
            String name (p->getProgramName (i).trim());

            if (name.isEmpty())
                name = "Unnamed";

            auto* const pc = new ProcessorProgramPropertyComp (name, *p, i);
            programs.add (pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties (programs);
        setSize (400, jlimit (25, 400, totalHeight));
    }

    void paint (Graphics& g) override        { g.fillAll (Colours::grey); }
    void resized() override                 { panel.setBounds (getLocalBounds()); }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgramAudioProcessorEditor)
};

//==============================================================================
PluginWindow* PluginWindow::getWindowFor (AudioProcessorGraph::Node* const node,
                                          WindowFormatType type)
{
    jassert (node != nullptr);

    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked (i)->owner == node
             && activePluginWindows.getUnchecked (i)->type == type)
            return activePluginWindows.getUnchecked (i);

    AudioProcessor* processor = node->getProcessor();

    if (processor == nullptr)
        return nullptr;

    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        ui = processor->createEditorAndMakeActive();

        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui = new GenericAudioProcessorEditor (*processor);
        else if (type == Programs)
            ui = new ProgramAudioProcessorEditor (processor);
    }

    if (ui != nullptr)
    {
        if (auto* const plugin = dynamic_cast<AudioPluginInstance*> (processor))
            ui->setName (plugin->getName());

        return new PluginWindow (ui, node, type);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    activePluginWindows.removeFirstMatchingValue (this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set (getLastXProp (type), getX());
    owner->properties.set (getLastYProp (type), getY());
}

void PluginWindow::closeButtonPressed()
{
    owner->properties.set (getOpenProp (type), false);
    delete this;
}
