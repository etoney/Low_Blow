#pragma once
// Low Blow: kick/bass ducker. Goes on the bass; the kick comes in on the sidechain.
// Every kick hit pulls the bass down by DEPTH (only its lows, below CROSSOVER, in LOWS ONLY mode),
// holds while the kick sounds, then lets it back up over RELEASE. The kick level that counts as a hit
// follows the kick itself (SENSITIVITY = how far under the loudest recent hits), so ghost notes and
// bleed can be left out without setting a threshold by hand.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace lowblow
{
struct Params
{
    float depthDb = 6.0f;       // 0..24
    float attackMs = 1.0f;      // 0.1..20
    float holdMs = 20.0f;       // 0..150
    float releaseMs = 90.0f;    // 10..500
    float sensitivity = 0.6f;   // 0..1  -> hit threshold 24..3 dB under the loudest recent hits
    int mode = 0;               // 0 lows only, 1 full
    float crossoverHz = 120.0f; // 40..300
    float lookaheadMs = 1.0f;   // 0..5 (adds latency)
    float outputDb = 0.0f;
    bool listen = false;        // hear only what's being taken out
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void lowpass (double fs, double f, double q)
    {
        const double w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, fs * 0.45, f) / fs, cw = std::cos (w), al = std::sin (w) / (2 * q);
        const double a0 = 1 + al;
        b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    void highpass (double fs, double f, double q)
    {
        const double w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, fs * 0.45, f) / fs, cw = std::cos (w), al = std::sin (w) / (2 * q);
        const double a0 = 1 + al;
        b0 = (1 + cw) / 2 / a0; b1 = -(1 + cw) / a0; b2 = b0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
};

// one point of the scope, every ~10 ms
struct ScopePoint { float bassDb = -100, kickDb = -100, grDb = 0; bool hit = false; };

class Engine
{
public:
    static constexpr int kMaxLookahead = 2048;
    static constexpr int kScope = 1024;

    void prepare (double sampleRate, int)
    {
        fs = sampleRate;
        for (auto& d : delay) d.assign ((size_t) kMaxLookahead, 0.0f);
        wpos = 0;
        for (auto& f : kHp) f.reset();
        for (auto& f : kLp) f.reset();
        for (auto& c : xo) for (auto& f : c) f.reset();
        for (auto& c : xoHi) for (auto& f : c) f.reset();
        env = 0; ref = 0; gainDb = 0; holdLeft = 0; armed = true; ducking = false;
        hitTimes.clear(); samplesRun = 0; kickHeardSamples = 0; bassHeardSamples = 0; silentKick = (juce::int64) (3.0 * fs) + 1;
        frameLeft = frameLen(); fBass = 0; fKick = 0; fGr = 0; fHit = false;
        outGain = juce::Decibels::decibelsToGain (p.outputDb);
        primed = false;
        setParams (p);
    }

    void setParams (const Params& np)
    {
        const bool xoChanged = ! primed || np.crossoverHz != p.crossoverHz;
        p = np;
        if (xoChanged)
        {
            for (auto& c : xo) { c[0].lowpass (fs, p.crossoverHz, 0.70710678); c[1].lowpass (fs, p.crossoverHz, 0.70710678); }   // LR4 low
            for (auto& c : xoHi) { c[0].highpass (fs, p.crossoverHz, 0.70710678); c[1].highpass (fs, p.crossoverHz, 0.70710678); }   // LR4 high
            primed = true;
        }
        for (auto& f : kHp) f.highpass (fs, 30.0, 0.7071);   // coefficients only: the filter state carries on
        for (auto& f : kLp) f.lowpass (fs, 180.0, 0.7071);
        lookahead = juce::jlimit (0, kMaxLookahead - 1, (int) std::lround (p.lookaheadMs * 0.001 * fs));
        envAtk = std::exp (-1.0 / (0.0003 * fs));
        envRel = std::exp (-1.0 / (0.030 * fs));
        refRel = std::exp (-1.0 / (4.0 * fs));                 // loudest recent hits fade over ~4 s
        atkStep = p.depthDb / std::max (1.0, p.attackMs * 0.001 * fs);
        relCoef = std::exp (-1.0 / std::max (1.0, p.releaseMs * 0.001 * fs));
    }

    int getLatency() const { return lookahead; }
    double getSampleRate() const { return fs; }

    // main: in-place, 1-2 channels. side: sidechain channels (may be null / 0 channels)
    void process (float* const* main, int numCh, const float* const* side, int sideCh, int n)
    {
        const float targetOut = juce::Decibels::decibelsToGain (p.outputDb);
        const float thrBelow = 3.0f + 21.0f * juce::jlimit (0.0f, 1.0f, p.sensitivity);    // dB under the loudest recent hits: more sensitive = softer hits count
        float grMax = 0;
        for (int i = 0; i < n; ++i)
        {
            // ---- the kick
            double k = 0;
            if (side != nullptr && sideCh > 0)
            {
                for (int c = 0; c < sideCh; ++c) k += side[c][i];
                k /= sideCh;
            }
            const double kf = kLp[1].process (kLp[0].process (kHp[1].process (kHp[0].process (k))));
            const double a = std::abs (kf);
            env = a > env ? a + envAtk * (env - a) : a + envRel * (env - a);
            ref = env > ref ? env : ref * refRel;
            const float envDb = (float) juce::Decibels::gainToDecibels (env, -120.0);
            const float refDb = (float) juce::Decibels::gainToDecibels (ref, -120.0);
            const bool kickThere = refDb > -60.0f;
            const float thr = refDb - thrBelow;
            bool hit = false;
            if (kickThere && armed && envDb > thr) { hit = true; armed = false; ducking = true; holdLeft = (int) (p.holdMs * 0.001 * fs); hitTimes.push_back (samplesRun); }
            if (! armed && envDb < thr - 6.0f) armed = true;
            if (ducking && envDb < thr) { if (--holdLeft <= 0) ducking = false; }
            else if (ducking) holdLeft = (int) (p.holdMs * 0.001 * fs);

            // ---- gain: ramp down over ATTACK while ducking, glide back over RELEASE
            if (ducking) gainDb = std::min (p.depthDb, gainDb + (float) atkStep);
            else gainDb = (float) (gainDb * relCoef);
            if (gainDb < 1e-4f) gainDb = 0.0f;
            grMax = std::max (grMax, gainDb);
            const float g = juce::Decibels::decibelsToGain (-gainDb);

            // ---- the bass (delayed by LOOKAHEAD so the dip starts just before the kick lands)
            float bassAbs = 0, outAbs = 0;
            for (int c = 0; c < 2; ++c)
            {
                const float x = main[std::min (c, numCh - 1)][i];
                auto& d = delay[(size_t) c];
                d[(size_t) wpos] = x;
                const float xd = d[(size_t) ((wpos - lookahead + kMaxLookahead) % kMaxLookahead)];
                // Linkwitz-Riley split (lows + highs sum flat); it always runs so switching modes doesn't jump
                const float lo = (float) xo[(size_t) c][1].process (xo[(size_t) c][0].process (xd));
                const float hi = (float) xoHi[(size_t) c][1].process (xoHi[(size_t) c][0].process (xd));
                float y, removed;
                if (p.mode == 0) { y = g * lo + hi; removed = (1.0f - g) * lo; }
                else             { y = g * (lo + hi); removed = (1.0f - g) * (lo + hi); }
                if (p.listen) y = removed;
                if (c < numCh) main[c][i] = y * outGain;
                bassAbs = std::max (bassAbs, std::abs (xd));
                outAbs = std::max (outAbs, std::abs (y));
            }
            wpos = (wpos + 1) % kMaxLookahead;
            outGain += (targetOut - outGain) * 0.002f;

            // ---- stats + scope (the scope shows the bass as it comes out)
            ++samplesRun;
            if (kickThere && envDb > -60.0f) ++kickHeardSamples;
            if (bassAbs > 0.001f) ++bassHeardSamples;
            silentKick = std::abs (k) > 1e-6 ? 0 : silentKick + 1;
            fBass = std::max (fBass, outAbs); fKick = std::max (fKick, (float) env); fGr = std::max (fGr, gainDb); fHit = fHit || hit;
            if (--frameLeft <= 0)
            {
                ScopePoint sp { (float) juce::Decibels::gainToDecibels (fBass, -100.0f), (float) juce::Decibels::gainToDecibels (fKick, -100.0f), fGr, fHit };
                const int w = scopeWrite.load();
                scope[(size_t) (w % kScope)] = sp;
                scopeWrite.store (w + 1);
                frameLeft = frameLen(); fBass = 0; fKick = 0; fGr = 0; fHit = false;
            }
        }
        grNow.store (std::max (grNow.load(), grMax));
        kickNow.store (std::max (kickNow.load(), (float) env));
        // keep the last ~20 s of hits for the rate
        const auto keepFrom = samplesRun - (juce::int64) (20.0 * fs);
        while (! hitTimes.empty() && hitTimes.front() < keepFrom) hitTimes.erase (hitTimes.begin());
    }

    // ---- what it heard (for the editor and The Master)
    bool sidechainActive() const { return silentKick < (juce::int64) (3.0 * fs); }        // something on the sidechain in the last 3 s
    double kickSeconds() const { return (double) kickHeardSamples / fs; }
    double bassSeconds() const { return (double) bassHeardSamples / fs; }
    float hitsPerMinute() const
    {
        if (hitTimes.size() < 3) return 0.0f;
        const double span = (double) (hitTimes.back() - hitTimes.front()) / fs;
        return span > 0.5 ? (float) ((double) (hitTimes.size() - 1) / span * 60.0) : 0.0f;
    }
    // median gap between hits, ms (0 = not enough hits)
    float medianGapMs() const
    {
        if (hitTimes.size() < 4) return 0.0f;
        std::vector<double> g;
        for (size_t i = 1; i < hitTimes.size(); ++i) g.push_back ((double) (hitTimes[i] - hitTimes[i - 1]) / fs * 1000.0);
        std::nth_element (g.begin(), g.begin() + (long) g.size() / 2, g.end());
        return (float) g[g.size() / 2];
    }
    void resetStats() { hitTimes.clear(); kickHeardSamples = 0; bassHeardSamples = 0; }

    std::atomic<float> grNow { 0 }, kickNow { 0 };
    std::array<ScopePoint, kScope> scope {};
    std::atomic<int> scopeWrite { 0 };

private:
    int frameLen() const { return std::max (1, (int) (0.01 * fs)); }

    Params p;
    double fs = 48000.0;
    std::array<std::vector<float>, 2> delay;
    int wpos = 0, lookahead = 0;
    std::array<Biquad, 2> kHp, kLp;
    std::array<std::array<Biquad, 2>, 2> xo, xoHi;
    double env = 0, ref = 0, envAtk = 0.9, envRel = 0.999, refRel = 0.9999, atkStep = 1, relCoef = 0.999;
    float gainDb = 0, outGain = 1;
    int holdLeft = 0;
    bool armed = true, ducking = false, primed = false;
    std::vector<juce::int64> hitTimes;
    juce::int64 samplesRun = 0, kickHeardSamples = 0, bassHeardSamples = 0, silentKick = 0;
    int frameLeft = 480;
    float fBass = 0, fKick = 0, fGr = 0;
    bool fHit = false;
};
} // namespace lowblow
