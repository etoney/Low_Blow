#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CDLink.h"
#include "LowBlowEngine.h"
#include "Presets.h"

class LowBlowProcessor : public juce::AudioProcessor
{
public:
    LowBlowProcessor();
    ~LowBlowProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Low Blow"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;
    lowblow::Params currentParams() const;
    lowblow::Engine& getEngine() { return engine; }

    // is a sidechain bus there at all (the host may have disabled it), and is anything arriving on it
    bool sidechainBusEnabled() const { return getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled(); }
    bool kickArriving() const { return engine.sidechainActive(); }

    juce::String getTrackName() const { const juce::ScopedLock sl (nameLock); return trackName; }
    void updateTrackProperties (const TrackProperties& props) override { const juce::ScopedLock sl (nameLock); trackName = props.name.value_or (juce::String()); }

    // Castle Danger Link: The Master can read and set Low Blow's controls
    using Link = cdlink::PluginLink<juce::AudioProcessorValueTreeState>;
    Link& getLink() { return *link; }

    // ---- user presets (files shared by every project and DAW)
    static juce::File getUserPresetFolder();
    static juce::Array<juce::File> getUserPresets();
    static juce::File fileForPresetName (const juce::String& name);
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String getCurrentPresetName() const { return currentPresetName; }
    std::atomic<int> presetVersion { 0 };

private:
    std::unique_ptr<Link> link;
    void resetToDefaults();
    lowblow::Engine engine;
    int currentProgram = 0, reportedLatency = -1;
    juce::String currentPresetName { "Init (Rock)" };
    juce::CriticalSection nameLock;
    juce::String trackName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LowBlowProcessor)
};
