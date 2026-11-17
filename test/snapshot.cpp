// Headless checks (run locally and in CI):
//  1) DSP: no kick = untouched, every kick ducks the bass by DEPTH and lets it back, LOWS ONLY keeps the
//     upper bass, FULL ducks it all, SENSITIVITY leaves ghost notes alone, LOOKAHEAD ducks ahead of the hit,
//     hit rate, sidechain detection, LISTEN
//  2) presets, state, link ids
//  3) renders the editor to PNGs
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

static bool writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    if (auto os = f.createOutputStream()) return juce::PNGImageFormat().writeImageToStream (img, *os);
    return false;
}

static void setP (LowBlowProcessor& p, const char* id, float v)
{
    auto* rp = p.apvts.getParameter (id);
    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

constexpr double SR = 48000.0;

// a kick drum: pitch-dropping thump, ~60 ms
static float kickAt (double t, float amp)
{
    if (t < 0 || t > 0.4) return 0.0f;
    const double ph = juce::MathConstants<double>::twoPi * (55.0 * t + 40.0 * (1.0 - std::exp (-t * 40.0)) / 40.0 * 3.0);
    return amp * (float) (std::exp (-t * 22.0) * std::sin (ph));
}

struct Render { std::vector<float> in, out; };
// bass = 55 Hz (+ optional 1 kHz), kicks every `period` s from 0.25 s; ghost kicks (if any) halfway between, `ghostAmp`
static Render render (LowBlowProcessor& p, double seconds, float bass55, float bass1k, double period, float kickAmp, float ghostAmp = 0.0f, bool sidechain = true)
{
    p.prepareToPlay (SR, 512);
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    Render r;
    const int total = (int) (seconds * SR);
    for (int done = 0; done < total; done += 512)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (double) (done + i) / SR;
            const float b = bass55 * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * t) + bass1k * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t);
            float k = 0;
            if (period > 0 && sidechain)
            {
                const double tk = std::fmod (t - 0.25 + 100.0 * period, period);
                k = t >= 0.25 ? kickAt (tk, kickAmp) : 0.0f;
                if (ghostAmp > 0 && t >= 0.25) k += kickAt (std::fmod (t - 0.25 - period * 0.5 + 100.0 * period, period), ghostAmp);
            }
            buf.setSample (0, i, b); buf.setSample (1, i, b);
            buf.setSample (2, i, k); buf.setSample (3, i, k);
            r.in.push_back (b);
        }
        p.processBlock (buf, midi);
        r.out.insert (r.out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
    }
    return r;
}

static double toneDb (const std::vector<float>& v, double f, double t0, double t1)
{
    const int a = (int) (t0 * SR), b = std::min ((int) v.size(), (int) (t1 * SR));
    double re = 0, im = 0, ws = 0;
    for (int i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) (i - a) / (double) (b - a - 1));
        re += v[(size_t) i] * w * std::cos (juce::MathConstants<double>::twoPi * f * (double) i / SR);
        im -= v[(size_t) i] * w * std::sin (juce::MathConstants<double>::twoPi * f * (double) i / SR);
        ws += w;
    }
    return 20.0 * std::log10 (std::max (1e-12, 2.0 * std::sqrt (re * re + im * im) / ws));
}
// gain of a steady tone in a window, comparing output with input (shifted by the latency)
static double gainAt (const Render& r, double f, double t0, double t1, int latency = 0)
{
    return toneDb (r.out, f, t0 + latency / SR, t1 + latency / SR) - toneDb (r.in, f, t0, t1);
}
// RMS ratio in dB, output vs input, for a single-tone bass
static double rmsGain (const Render& r, double t0, double t1, int latency = 0)
{
    double a = 0, b = 0;
    for (int i = (int) (t0 * SR); i < (int) (t1 * SR); ++i) { a += (double) r.out[(size_t) (i + latency)] * r.out[(size_t) (i + latency)]; b += (double) r.in[(size_t) i] * r.in[(size_t) i]; }
    return 10.0 * std::log10 (std::max (1e-20, a) / std::max (1e-20, b));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory());
    dir.createDirectory();
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (! ok) ++failures; };
    auto fmt = [] (double v) { return juce::String (v, 2); };

    {
        LowBlowProcessor p;
        check (p.getBusCount (true) == 2 && p.sidechainBusEnabled(), "has a sidechain input, on by default");
        p.setRateAndBufferSizeDetails (SR, 512);   // (setPlayConfigDetails would switch the sidechain off)
        setP (p, "lookahead", 0.0f);

        // no kick: the bass passes untouched
        auto r = render (p, 1.0, 0.3f, 0.1f, 0.0, 0.0f);
        const double g55 = gainAt (r, 55.0, 0.5, 1.0), g1k = gainAt (r, 1000.0, 0.5, 1.0), g120 = 0;
        juce::ignoreUnused (g120);
        auto rx = render (p, 1.0, 0.0f, 0.0f, 0.0, 0.0f);
        double sweepMax = 0;   // the crossover sums flat: check with a 120 Hz tone, right at the split
        {
            p.prepareToPlay (SR, 512);
            juce::AudioBuffer<float> b (4, 512); juce::MidiBuffer m; std::vector<float> in, out;
            for (int k = 0; k < 94; ++k) { for (int i = 0; i < 512; ++i) { const float v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 120.0 * (k * 512 + i) / SR); b.setSample (0, i, v); b.setSample (1, i, v); b.setSample (2, i, 0); b.setSample (3, i, 0); in.push_back (v); } p.processBlock (b, m); out.insert (out.end(), b.getReadPointer (0), b.getReadPointer (0) + 512); }
            sweepMax = std::abs (toneDb (out, 120.0, 0.5, 1.0) - toneDb (in, 120.0, 0.5, 1.0));
        }
        check (std::abs (g55) < 0.05 && std::abs (g1k) < 0.05 && sweepMax < 0.05,
               "no kick on the sidechain: bass level untouched (55 Hz " + fmt (g55) + ", 120 Hz " + juce::String (sweepMax, 3) + ", 1 kHz " + fmt (g1k) + " dB)");
        check (! p.kickArriving(), "knows nothing is arriving on the sidechain");

        // kicks at 120 bpm, DEPTH 6, LOWS ONLY
        r = render (p, 6.0, 0.3f, 0.0f, 0.5, 0.8f);
        const double during = rmsGain (r, 4.255, 4.28), between = rmsGain (r, 4.55, 4.7);
        check (during < -5.0 && during > -7.0, "each kick ducks the bass by DEPTH (" + fmt (during) + " dB, depth 6)");
        check (std::abs (between) < 0.5, "the bass comes back between kicks (" + fmt (between) + " dB)");
        check (p.kickArriving(), "knows the kick is arriving");
        const float hpm = p.getEngine().hitsPerMinute(), gap = p.getEngine().medianGapMs();
        check (std::abs (hpm - 120.0f) < 4.0f && std::abs (gap - 500.0f) < 15.0f, "counts the hits: " + juce::String (hpm, 1) + " a minute, " + juce::String (gap, 0) + " ms apart");

        // LOWS ONLY keeps the upper bass, FULL ducks it too
        r = render (p, 6.0, 0.3f, 0.1f, 0.5, 0.8f);
        const double lo = gainAt (r, 55.0, 4.252, 4.29), hi = gainAt (r, 1000.0, 4.252, 4.29);
        check (lo < -4.5 && std::abs (hi) < 0.5, "LOWS ONLY: 55 Hz ducked (" + fmt (lo) + " dB), 1 kHz kept (" + fmt (hi) + " dB)");
        setP (p, "mode", 1.0f);
        r = render (p, 6.0, 0.3f, 0.1f, 0.5, 0.8f);
        const double hiFull = gainAt (r, 1000.0, 4.252, 4.29);
        check (hiFull < -4.5, "FULL: 1 kHz ducked too (" + fmt (hiFull) + " dB)");
        setP (p, "mode", 0.0f);

        // SENSITIVITY: ghost kicks 20 dB down between the main ones
        setP (p, "sensitivity", 20.0f);
        render (p, 8.0, 0.3f, 0.0f, 0.5, 0.8f, 0.08f);
        const float hpmLow = p.getEngine().hitsPerMinute();
        setP (p, "sensitivity", 100.0f);
        render (p, 8.0, 0.3f, 0.0f, 0.5, 0.8f, 0.08f);
        const float hpmHigh = p.getEngine().hitsPerMinute();
        check (std::abs (hpmLow - 120.0f) < 5.0f && std::abs (hpmHigh - 240.0f) < 8.0f,
               "SENSITIVITY: ghost notes ignored at 20% (" + juce::String (hpmLow, 0) + "/min), caught at 100% (" + juce::String (hpmHigh, 0) + "/min)");
        setP (p, "sensitivity", 60.0f);

        // LOOKAHEAD: reported as latency, and the duck starts before the kick lands
        setP (p, "lookahead", 3.0f);
        r = render (p, 6.0, 0.3f, 0.0f, 0.5, 0.8f);
        const int lat = p.getLatencySamples();
        const double atOnset = rmsGain (r, 4.2502, 4.2522, lat);
        check (lat == 144, "LOOKAHEAD 3 ms = " + juce::String (lat) + " samples of latency");
        setP (p, "lookahead", 0.0f);
        r = render (p, 6.0, 0.3f, 0.0f, 0.5, 0.8f);
        const double atOnset0 = rmsGain (r, 4.2502, 4.2522, 0);
        check (atOnset < atOnset0 - 1.5, "with LOOKAHEAD the bass is already ducking as the kick lands (" + fmt (atOnset) + " dB vs " + fmt (atOnset0) + " without)");

        // RELEASE: longer release = slower recovery
        setP (p, "release", 30.0f);
        r = render (p, 6.0, 0.3f, 0.0f, 0.5, 0.8f);
        const double fast = rmsGain (r, 4.46, 4.48);
        setP (p, "release", 300.0f);
        r = render (p, 6.0, 0.3f, 0.0f, 0.5, 0.8f);
        const double slow = rmsGain (r, 4.46, 4.48);
        check (slow < fast - 1.5 && fast > -1.0, "RELEASE: 30 ms is back 210 ms after the hit (" + fmt (fast) + " dB), 300 ms isn't (" + fmt (slow) + " dB)");
        setP (p, "release", 90.0f);

        // LISTEN: only what's taken out
        setP (p, "listen", 1.0f);
        r = render (p, 2.0, 0.3f, 0.0f, 0.0, 0.0f);
        float mx = 0; for (size_t i = 4800; i < r.out.size(); ++i) mx = std::max (mx, std::abs (r.out[i]));
        check (mx < 1e-5f, "LISTEN with no kicks: silence (nothing taken out)");
        setP (p, "listen", 0.0f);

        // sidechain bus switched off by the host
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
        check (p.setBusesLayout (layout) && ! p.sidechainBusEnabled(), "copes with the sidechain switched off");
        p.prepareToPlay (SR, 512);
        juce::AudioBuffer<float> b2 (2, 512);
        juce::MidiBuffer mid;
        std::vector<float> in2, out2;
        for (int k = 0; k < 94; ++k)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * (k * 512 + i) / SR); b2.setSample (0, i, v); b2.setSample (1, i, v); in2.push_back (v); }
            p.processBlock (b2, mid);
            out2.insert (out2.end(), b2.getReadPointer (0), b2.getReadPointer (0) + 512);
        }
        check (std::abs (toneDb (out2, 55.0, 0.5, 1.0) - toneDb (in2, 55.0, 0.5, 1.0)) < 0.05, "...and passes the bass through at full level");
    }

    // ---------------- presets, state, link
    {
        LowBlowProcessor p;
        bool allLoad = true;
        for (int i = 0; i < p.getNumPrograms(); ++i) { p.setCurrentProgram (i); allLoad &= p.getCurrentPresetName() == p.getProgramName (i); }
        check (allLoad && p.getNumPrograms() >= 8, juce::String (p.getNumPrograms()) + " factory presets load");
        for (auto& pr : lowblow::factoryPresets())
            for (auto& [id, v] : pr.values)
                if (p.apvts.getParameter (id) == nullptr) check (false, "preset " + pr.name + " uses unknown id " + id);
        bool idsOk = true;
        for (auto* id : { "depth", "attack", "hold", "release", "sensitivity", "mode", "crossover", "lookahead", "output", "listen" }) idsOk &= p.apvts.getParameter (id) != nullptr;
        check (idsOk, "every linked control exists");

        p.setCurrentProgram (1);
        setP (p, "depth", 7.7f);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        LowBlowProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (q.apvts.getRawParameterValue ("depth")->load() - 7.7f) < 0.01f && q.getCurrentPresetName() == p.getCurrentPresetName(), "state round trip");

        const auto name = juce::String ("zz test ") + juce::String (juce::Random::getSystemRandom().nextInt (100000));
        setP (p, "crossover", 99.0f); setP (p, "listen", 1.0f);
        const bool saved = p.saveUserPreset (name);
        p.setCurrentProgram (0);
        const bool loaded = p.loadUserPreset (LowBlowProcessor::fileForPresetName (name));
        check (saved && loaded && std::abs (p.apvts.getRawParameterValue ("crossover")->load() - 99.0f) < 0.6f && p.apvts.getRawParameterValue ("listen")->load() < 0.5f,
               "user preset round trip (and never saves LISTEN)");
        LowBlowProcessor::fileForPresetName (name).deleteFile();
    }

    // ---------------- GUI
    {
        LowBlowProcessor p;
        p.setRateAndBufferSizeDetails (SR, 512);   // (setPlayConfigDetails would switch the sidechain off)
        p.setCurrentProgram (2);   // Rock: Punchy
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* le = dynamic_cast<LowBlowEditor*> (ed.get());
        // play kicks + bass, pumping the editor as if 30 fps
        p.prepareToPlay (SR, 512);
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        const int blocksPerFrame = (int) (SR / 30.0 / 512.0) + 1;
        int64_t n = 0;
        for (int frame = 0; frame < 150; ++frame)
        {
            for (int b = 0; b < blocksPerFrame; ++b)
            {
                for (int i = 0; i < 512; ++i, ++n)
                {
                    const double t = (double) n / SR;
                    const float bass = 0.35f * (float) (std::sin (juce::MathConstants<double>::twoPi * 55.0 * t) + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)) * (0.7f + 0.3f * (float) std::sin (t * 3.0));
                    const float k = kickAt (std::fmod (t, 0.469), 0.8f) + kickAt (std::fmod (t + 0.234, 0.938), 0.5f);
                    buf.setSample (0, i, bass); buf.setSample (1, i, bass); buf.setSample (2, i, k); buf.setSample (3, i, k);
                }
                p.processBlock (buf, midi);
            }
            le->pumpForSnapshot();
        }
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), dir.getChildFile ("gui.png")),
               "rendered gui.png (" + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) + ")");
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f), dir.getChildFile ("gui@2x.png")), "rendered gui@2x.png");

        LowBlowProcessor p2;
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (p2.createEditor());
        dynamic_cast<LowBlowEditor*> (ed2.get())->pumpForSnapshot();
        check (writePng (ed2->createComponentSnapshot (ed2->getLocalBounds(), true, 1.0f), dir.getChildFile ("gui_nokick.png")), "rendered gui_nokick.png (no sidechain)");
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
