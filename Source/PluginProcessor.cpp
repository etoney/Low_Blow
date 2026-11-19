#include "PluginProcessor.h"
#include "Rebel.h"
#include "PluginEditor.h"

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "dev"
#endif

LowBlowProcessor::LowBlowProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain (Kick)", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LowBlow", createLayout())
{
    link = std::make_unique<Link> ("lowblow", "Low Blow", JucePlugin_VersionString, apvts,
        juce::StringArray { "depth", "attack", "hold", "release", "sensitivity", "mode", "crossover", "lookahead", "output", "listen" });
    link->onSetByMaster = [this] (const juce::StringArray&) { currentPresetName = "Set by The Master"; ++presetVersion; };
    link->onOtherMessage = [this] (const juce::var& m)
    {
        if (m.getProperty ("type", "").toString() == "measure-reset") engine.resetStats();   // The Master's RESET: count hits afresh
    };
    link->extraStatus = [this]
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("track", getTrackName());
        o->setProperty ("sidechain", kickArriving());
        o->setProperty ("kickSeconds", engine.kickSeconds());
        o->setProperty ("seconds", engine.bassSeconds());
        o->setProperty ("hitsPerMin", (double) engine.hitsPerMinute());
        o->setProperty ("gapMs", (double) engine.medianGapMs());
        return juce::var (o);
    };
}

juce::AudioProcessorValueTreeState::ParameterLayout LowBlowProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB")
        .withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });
    auto depthAttr = AudioParameterFloatAttributes().withLabel ("dB")
        .withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? String ("0 dB") : "-" + String (v, 1) + " dB"; });
    auto msAttr = AudioParameterFloatAttributes().withLabel ("ms")
        .withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; });
    auto pctAttr = AudioParameterFloatAttributes().withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; });
    auto hzAttr = AudioParameterFloatAttributes().withLabel ("Hz")
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; });

    auto skewed = [] (float lo, float hi, float step, float centre) { NormalisableRange<float> r (lo, hi, step); r.setSkewForCentre (centre); return r; };
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "depth", 1 }, "Depth", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f, depthAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack", skewed (0.1f, 20.0f, 0.1f, 2.0f), 1.0f, msAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "hold", 1 }, "Hold", skewed (0.0f, 150.0f, 1.0f, 30.0f), 20.0f, msAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", skewed (10.0f, 500.0f, 1.0f, 100.0f), 90.0f, msAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "sensitivity", 1 }, "Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, pctAttr));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Lows Only", "Full" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "crossover", 1 }, "Crossover", skewed (40.0f, 300.0f, 1.0f, 120.0f), 120.0f, hzAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "lookahead", 1 }, "Lookahead", NormalisableRange<float> (0.0f, 5.0f, 0.1f), 1.0f, msAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "listen", 1 }, "Listen (What's Taken Out)", false));
    p.push_back (cdrebel::makeParam());
    return { p.begin(), p.end() };
}

lowblow::Params LowBlowProcessor::currentParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    lowblow::Params p;
    p.depthDb = v ("depth"); p.attackMs = v ("attack"); p.holdMs = v ("hold"); p.releaseMs = v ("release");
    p.sensitivity = v ("sensitivity") * 0.01f; p.mode = (int) v ("mode"); p.crossoverHz = v ("crossover");
    p.lookaheadMs = v ("lookahead"); p.outputDb = v ("output"); p.listen = v ("listen") > 0.5f;
    return p;
}

bool LowBlowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    const auto in = layouts.getMainInputChannelSet();
    if (! (in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo()))) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void LowBlowProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.setParams (currentParams());
    reportedLatency = engine.getLatency();
    setLatencySamples (reportedLatency);
}

void LowBlowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    const int outCh = juce::jmin (2, mainOut.getNumChannels());
    if (outCh <= 0) return;
    // copy the sidechain first: its channels can share the buffer with the main output
    juce::AudioBuffer<float> side;
    int sideCh = 0;
    if (sidechainBusEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        sideCh = juce::jmin (2, sc.getNumChannels());
        if (sideCh > 0) { side.setSize (sideCh, n, false, false, true); for (int c = 0; c < sideCh; ++c) side.copyFrom (c, 0, sc, c, 0, n); }
    }
    if (mainIn.getNumChannels() == 1 && outCh > 1) mainOut.copyFrom (1, 0, mainOut, 0, 0, n);   // mono in -> stereo out

    double inPow = 0;
    for (int c = 0; c < outCh; ++c) { auto* d = mainOut.getReadPointer (c); for (int i = 0; i < n; ++i) inPow += (double) d[i] * d[i]; }
    engine.setParams (currentParams());
    engine.process (mainOut.getArrayOfWritePointers(), outCh, sideCh > 0 ? side.getArrayOfReadPointers() : nullptr, sideCh, n);
    double outPow = 0;
    for (int c = 0; c < outCh; ++c) { auto* d = mainOut.getReadPointer (c); for (int i = 0; i < n; ++i) outPow += (double) d[i] * d[i]; }
    link->env.addPowers (inPow / juce::jmax (1, outCh * n), outPow / juce::jmax (1, outCh * n), n);
    if (engine.getLatency() != reportedLatency) { reportedLatency = engine.getLatency(); setLatencySamples (reportedLatency); }
    for (int ch = outCh; ch < mainOut.getNumChannels(); ++ch) mainOut.clear (ch, 0, n);
}

//==============================================================================
int LowBlowProcessor::getNumPrograms() { return (int) lowblow::factoryPresets().size(); }

const juce::String LowBlowProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? lowblow::factoryPresets()[(size_t) index].name : "";
}

void LowBlowProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != "rebel") rp->setValueNotifyingHost (rp->getDefaultValue());   // a preset never changes REBEL
}

void LowBlowProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    currentPresetName = lowblow::factoryPresets()[(size_t) index].name;
    resetToDefaults();
    for (auto& [id, v] : lowblow::factoryPresets()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    ++presetVersion;
}

juce::File LowBlowProcessor::getUserPresetFolder()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory); // ~/Library on macOS, %APPDATA% on Windows
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("Castle Danger").getChildFile ("Low Blow").getChildFile ("Presets");
}

juce::Array<juce::File> LowBlowProcessor::getUserPresets()
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.lowblow");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    return files;
}

juce::File LowBlowProcessor::fileForPresetName (const juce::String& name)
{
    return getUserPresetFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".lowblow");
}

bool LowBlowProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty()) return false;
    const auto file = fileForPresetName (name);
    if (! file.getParentDirectory().createDirectory()) return false;

    juce::XmlElement xml ("LowBlowPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("pluginVersion", JucePlugin_VersionString);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->getParameterID() == "listen") continue; // a preset never opens on LISTEN
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->getParameterID());
            e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
        }
    if (! xml.writeTo (file)) return false;
    currentPresetName = name;
    ++presetVersion;
    return true;
}

bool LowBlowProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("LowBlowPreset")) return false;
    resetToDefaults();
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        if (auto* rp = apvts.getParameter (e->getStringAttribute ("id")))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) e->getDoubleAttribute ("value")));
    currentPresetName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    ++presetVersion;
    return true;
}

void LowBlowProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void LowBlowProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = vt.getProperty ("program", 0);
            currentPresetName = vt.getProperty ("presetName", getProgramName (currentProgram)).toString();
            apvts.replaceState (vt);
            ++presetVersion;
        }
}

juce::AudioProcessorEditor* LowBlowProcessor::createEditor() { return new LowBlowEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LowBlowProcessor(); }
