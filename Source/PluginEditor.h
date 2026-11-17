#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class LowBlowLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LowBlowLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool hi, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool hi, bool down) override;
    juce::Image rustDisc;
};

// Segmented row of rusty buttons bound to a choice parameter
class Selector : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Selector (juce::StringArray labels);
    void bind (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int getIndex() const { return index; }
    std::function<void()> onChange;
private:
    int segmentAt (juce::Point<int>) const;
    juce::StringArray labels;
    std::unique_ptr<juce::ParameterAttachment> att;
    int index = 0, hover = -1;
};

// A boxing glove on an iron medallion: throws a jab on every kick hit
class Glove : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void advance (bool hit, float dt);
private:
    float punch = 0, t = 0;
};

// THE RING: the last few seconds: the bass level, the kick, and how far the bass was ducked
class RingScope : public juce::Component
{
public:
    explicit RingScope (LowBlowProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void pull();
    bool hitSinceLastPull = false;
private:
    LowBlowProcessor& proc;
    static constexpr int kPoints = 400;   // 4 s at 10 ms
    std::vector<lowblow::ScopePoint> pts = std::vector<lowblow::ScopePoint> (kPoints);
    int readPos = 0;
};

// KICK and DUCK meters
class MeterPair : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    float kick = 0, duck = 0, duckHold = 0, depth = 6;
    bool hitLamp = false;
};

// Is the kick arriving on the sidechain? (and how to get it there if not)
class SideStatus : public juce::Component, public juce::SettableTooltipClient
{
public:
    void paint (juce::Graphics&) override;
    bool ok = false, busOff = false;
    float hitsPerMin = 0;
};

// In-window modal dialog (message, confirm, or text entry), drawn over the editor
class LowBlowDialog : public juce::Component
{
public:
    LowBlowDialog (const juce::String& title, const juce::String& message, bool withTextField,
                   const juce::String& initialText, juce::StringArray buttons,
                   std::function<void (int buttonIndex, const juce::String& text)> onClose);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;
    void mouseDown (const juce::MouseEvent&) override {}
private:
    void finish (int index);
    juce::String title, message;
    juce::TextEditor field;
    bool hasField;
    juce::OwnedArray<juce::TextButton> buttons;
    std::function<void (int, const juce::String&)> onClose;
    juce::Rectangle<int> panel;
};

class LowBlowEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LowBlowEditor (LowBlowProcessor&);
    ~LowBlowEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::TextButton& getSaveButton() { return saveButton; }
    void pumpForSnapshot() { timerCallback(); }   // used by the headless snapshot test

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Plate { juce::String title; juce::Rectangle<int> bounds; bool hot = false; };

    Knob& makeKnob (const juce::String& id, const juce::String& text, const juce::String& tip);
    void placeKnob (Knob&, juce::Rectangle<int>);
    void timerCallback() override;
    void buildBackdrop();

    void refreshPresetBox();
    void stepPreset (int delta);
    void promptSave();
    void confirmDelete();
    void showDialog (const juce::String& title, const juce::String& message, bool withTextField, const juce::String& initialText,
                     juce::StringArray buttons, std::function<void (int, const juce::String&)> onResult);
    void showMessage (const juce::String& title, const juce::String& text);

    LowBlowProcessor& proc;
    LowBlowLookAndFeel lnf;
    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *depthKnob = nullptr, *attackKnob = nullptr, *holdKnob = nullptr, *releaseKnob = nullptr, *sensKnob = nullptr;
    Knob *xoKnob = nullptr, *laKnob = nullptr, *outKnob = nullptr;
    Selector modeSel { { "LOWS ONLY", "FULL" } };
    juce::ToggleButton listenButton { "LISTEN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAtt;
    Glove glove;
    RingScope scope;
    MeterPair meters;
    SideStatus status;
    juce::TooltipWindow tooltips { this, 700 };

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DELETE" };
    juce::Array<juce::File> userFiles;
    int lastPresetVersion = -1;
    std::unique_ptr<LowBlowDialog> dialog;
    static constexpr int kUserIdBase = 1001;

    std::vector<Plate> plates;
    juce::Rectangle<int> logoArea, badgeArea, crestArea, presetArea, stripeArea, linkArea;
    int linkTick = 0;
    juce::Image backdrop, logoImage;
    float kickMeter = 0, duckMeter = 0, duckHold = 0, flicker = 0;
    int holdTimer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LowBlowEditor)
};
