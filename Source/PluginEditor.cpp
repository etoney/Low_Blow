#include "PluginEditor.h"
#include <functional>

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "dev"
#endif

//==============================================================================
namespace pal
{
const juce::Colour ironDark  { 0xff1d1815 };
const juce::Colour iron      { 0xff3a322c };
const juce::Colour rustDeep  { 0xff4a1d0c };
const juce::Colour rust      { 0xff9a4518 };
const juce::Colour rustLight { 0xffc0702a };
const juce::Colour paint     { 0xffd6c7a6 };   // dirty cream stencil paint
const juce::Colour paintDim  { 0xffa8997c };
const juce::Colour signal    { 0xffd2452a };   // rusty red for active things
const juce::Colour signalHot { 0xffff6a3d };
const juce::Colour hazard    { 0xffc99a22 };   // worn safety yellow
const juce::Colour screen    { 0xff0c0a08 };
}

//==============================================================================
namespace tex
{
inline float hash (int x, int y, int seed)
{
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float) (h & 0xffffffu) / 16777215.0f;
}
inline float smooth (float t) { return t * t * (3.0f - 2.0f * t); }
inline float smoothstep (float a, float b, float x) { return smooth (juce::jlimit (0.0f, 1.0f, (x - a) / (b - a))); }
inline float vnoise (float x, float y, int seed)
{
    const int xi = (int) std::floor (x), yi = (int) std::floor (y);
    const float fx = x - (float) xi, fy = y - (float) yi;
    const float a = hash (xi, yi, seed), b = hash (xi + 1, yi, seed), c = hash (xi, yi + 1, seed), d = hash (xi + 1, yi + 1, seed);
    const float u = smooth (fx), v = smooth (fy);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
inline float fbm (float x, float y, int seed, int octaves)
{
    float s = 0, amp = 0.5f, f = 1, norm = 0;
    for (int i = 0; i < octaves; ++i) { s += amp * vnoise (x * f, y * f, seed + i * 31); norm += amp; f *= 2.03f; amp *= 0.5f; }
    return s / norm;
}
struct RGB { float r, g, b; };
inline RGB rgb (juce::Colour c) { return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() }; }
inline RGB mix (RGB a, RGB b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
inline RGB mul (RGB a, float k) { return { a.r * k, a.g * k, a.b * k }; }

inline RGB rustAt (float x, float y, float rustBias, int seed)
{
    const float n      = fbm (x / 170.0f, y / 170.0f, seed, 4);
    const float detail = fbm (x / 36.0f, y / 36.0f, seed + 7, 3);
    const float fine   = vnoise (x / 7.0f, y / 7.0f, seed + 13);
    const float mask   = smoothstep (0.45f, 0.62f, n + (detail - 0.5f) * 0.35f + rustBias);
    RGB base = mix (rgb (pal::ironDark), rgb (pal::iron), detail);
    RGB r    = mix (rgb (pal::rustDeep), rgb (pal::rust), fbm (x / 20.0f, y / 20.0f, seed + 3, 2));
    if (fine > 0.72f) r = mix (r, rgb (pal::rustLight), (fine - 0.72f) * 2.5f);
    return mix (base, r, mask);
}

// Paint-chip a rendered image: knock out chunks of alpha with noise, plus grain.
inline void distress (juce::Image& img, float scale, float wear, int seed)
{
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
        {
            auto c = bd.getPixelColour (x, y);
            if (c.getAlpha() == 0) continue;
            const float n = fbm ((float) x / (7.0f * scale), (float) y / (7.0f * scale), seed, 3);
            const float chip = n < wear ? 0.08f : 1.0f;
            const float grain = 0.7f + 0.3f * hash (x, y, seed + 99);
            bd.setPixelColour (x, y, c.withMultipliedAlpha (chip * grain));
        }
}

inline juce::Font stencil (float size) { return juce::Font (juce::FontOptions ("Impact", size, juce::Font::plain)); }
inline juce::Font plainBold (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }

inline juce::Image distressedText (const juce::String& text, int w, int h, juce::Font font, juce::Colour col,
                                   juce::Justification just, float scale, float wear, int seed, float kerning = 0.0f)
{
    juce::Image img (juce::Image::ARGB, juce::roundToInt ((float) w * scale), juce::roundToInt ((float) h * scale), true);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.setColour (col);
        g.setFont (font.withExtraKerningFactor (kerning));
        g.drawFittedText (text, 0, 0, w, h, just, 1, 0.55f);   // squeeze rather than clip if the font runs wide
    }
    distress (img, scale, wear, seed);
    return img;
}
} // namespace tex

//==============================================================================
// The Castle Danger crest: a castle silhouette under a blood moon, on a riveted iron medallion.
static void drawCastleCrest (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = r.getCentre();
    const float R = std::min (r.getWidth(), r.getHeight()) * 0.5f;

    // iron ring
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (r.translated (1.5f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a4c), c.x - R * 0.6f, c.y - R * 0.8f, juce::Colour (0xff1a1410), c.x + R, c.y + R, true));
    g.fillEllipse (r);
    g.setColour (pal::rust.withAlpha (0.55f)); g.drawEllipse (r.reduced (1.0f), 1.6f);
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i / 8.0f * juce::MathConstants<float>::twoPi + 0.39f;
        const float rx = c.x + std::cos (a) * R * 0.88f, ry = c.y + std::sin (a) * R * 0.88f;
        g.setColour (juce::Colour (0xff8a7a68)); g.fillEllipse (rx - 1.8f, ry - 1.8f, 3.6f, 3.6f);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawEllipse (rx - 1.8f, ry - 1.8f, 3.6f, 3.6f, 0.6f);
    }

    // night sky + blood moon
    const auto inner = r.reduced (R * 0.2f);
    const float ir = inner.getWidth() * 0.5f;
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addEllipse (inner);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a0c08), c.x, inner.getY(), juce::Colour (0xff080505), c.x, inner.getBottom(), false));
    g.fillEllipse (inner);
    const juce::Rectangle<float> moon (c.x - ir * 0.55f, inner.getY() + ir * 0.12f, ir * 1.1f, ir * 1.1f);
    juce::DropShadow (pal::signalHot.withAlpha (0.55f), (int) (ir * 0.45f), {}).drawForPath (g, [&] { juce::Path p; p.addEllipse (moon); return p; }());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff7a4a), moon.getX() + moon.getWidth() * 0.35f, moon.getY() + moon.getHeight() * 0.3f,
                                             juce::Colour (0xff8a1408), moon.getRight(), moon.getBottom(), true));
    g.fillEllipse (moon);
    g.setColour (juce::Colour (0x40400000));
    g.fillEllipse (moon.getX() + moon.getWidth() * 0.55f, moon.getY() + moon.getHeight() * 0.25f, moon.getWidth() * 0.16f, moon.getWidth() * 0.16f);
    g.fillEllipse (moon.getX() + moon.getWidth() * 0.25f, moon.getY() + moon.getHeight() * 0.55f, moon.getWidth() * 0.12f, moon.getWidth() * 0.12f);

    // castle silhouette
    const float x0 = inner.getX(), y0 = inner.getY(), w = inner.getWidth(), h = inner.getHeight();
    auto X = [&] (float f) { return x0 + f * w; };
    auto Y = [&] (float f) { return y0 + f * h; };
    juce::Path castle;
    auto tower = [&] (float l, float rgt, float top, int merlons)
    {
        castle.addRectangle (X (l), Y (top), X (rgt) - X (l), Y (1.05f) - Y (top));
        const float mw = (rgt - l) / (float) (merlons * 2 - 1);
        for (int i = 0; i < merlons; ++i)
            castle.addRectangle (X (l + (float) i * 2.0f * mw), Y (top - 0.06f), X (l + mw) - X (l), Y (0.06f) - Y (0.0f) + 0.5f);
    };
    tower (0.08f, 0.30f, 0.50f, 3);           // left tower
    tower (0.70f, 0.92f, 0.50f, 3);           // right tower
    tower (0.38f, 0.62f, 0.36f, 3);           // keep
    tower (0.30f, 0.70f, 0.64f, 5);           // curtain wall
    castle.addTriangle (X (0.08f), Y (0.44f), X (0.19f), Y (0.27f), X (0.30f), Y (0.44f));   // spires
    castle.addTriangle (X (0.70f), Y (0.44f), X (0.81f), Y (0.27f), X (0.92f), Y (0.44f));
    castle.setUsingNonZeroWinding (true);
    g.setColour (juce::Colour (0xff070505)); g.fillPath (castle);
    // gate + lit windows
    juce::Path gate;
    gate.addRectangle (X (0.44f), Y (0.84f), X (0.56f) - X (0.44f), Y (0.2f) - Y (0.0f));
    gate.addEllipse (X (0.44f), Y (0.79f), X (0.56f) - X (0.44f), Y (0.1f) - Y (0.0f));
    g.setColour (juce::Colour (0xff2a0c06)); g.fillPath (gate);
    g.setColour (pal::signalHot.withAlpha (0.9f));
    for (auto p : { juce::Point<float> (0.19f, 0.62f), { 0.81f, 0.62f }, { 0.5f, 0.5f } })
        g.fillRect (X (p.x) - w * 0.018f, Y (p.y), w * 0.036f, h * 0.07f);
}

//==============================================================================
LowBlowLookAndFeel::LowBlowLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, pal::paintDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, pal::rust.withAlpha (0.6f));
    setColour (juce::Label::textColourId, pal::paintDim);
    setColour (juce::ComboBox::textColourId, pal::paint);
    setColour (juce::ComboBox::arrowColourId, pal::signal);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1411));
    setColour (juce::PopupMenu::textColourId, pal::paint);
    setColour (juce::PopupMenu::headerTextColourId, pal::rustLight);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::rust);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::textColourOffId, pal::paint);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0f0b09));
    setColour (juce::TextEditor::textColourId, pal::paint);
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff5a3420));
    setColour (juce::TextEditor::focusedOutlineColourId, pal::rustLight);
    setColour (juce::TextEditor::highlightColourId, pal::rust.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, pal::signalHot);

    rustDisc = juce::Image (juce::Image::ARGB, 128, 128, true);
    juce::Image::BitmapData bd (rustDisc, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
        {
            const float n = tex::fbm ((float) x / 22.0f, (float) y / 22.0f, 501, 4);
            const float a = tex::smoothstep (0.5f, 0.7f, n);
            const auto c = tex::rustAt ((float) x * 2.0f, (float) y * 2.0f, 0.3f, 777);
            bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (c.r, c.g, c.b, a * 0.85f));
        }
}

juce::Font LowBlowLookAndFeel::getComboBoxFont (juce::ComboBox&) { return tex::plainBold (13.0f); }
juce::Font LowBlowLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return tex::stencil (juce::jmin (15.0f, (float) h * 0.62f)).withExtraKerningFactor (0.08f); }

void LowBlowLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.translated (1.0f, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (down ? pal::rustDeep : juce::Colour (0xff3a2a20), 0, r.getY(),
                                             down ? juce::Colour (0xff2a120a) : juce::Colour (0xff17110e), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (hi ? pal::rustLight : juce::Colour (0xff6a3a20)); g.drawRoundedRectangle (r, 3.0f, 1.2f);
}

void LowBlowLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff0f0b09)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.isMouseOver (true) ? pal::rustLight : juce::Colour (0xff6a3a20)); g.drawRoundedRectangle (r, 3.0f, 1.2f);
    juce::Path arrow; const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
    g.setColour (pal::signal); g.fillPath (arrow);
}

void LowBlowLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = juce::jmax (2.5f, r * 0.11f);
    const float alpha = s.isEnabled() ? 1.0f : 0.3f;

    // tick marks stamped round the dial
    g.setColour (pal::paintDim.withAlpha (0.45f * alpha));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle) - juce::MathConstants<float>::halfPi;
        const float r1 = r + 1.0f, r0 = r - (i % 5 == 0 ? 5.0f : 3.0f);
        g.drawLine (c.x + std::cos (a) * r0, c.y + std::sin (a) * r0, c.x + std::cos (a) * r1, c.y + std::sin (a) * r1, i % 5 == 0 ? 1.4f : 0.9f);
    }

    const float ar = r - thick - 4.0f;
    juce::Path track; track.addCentredArc (c.x, c.y, ar, ar, 0, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff120e0c).withAlpha (alpha)); g.strokePath (track, juce::PathStrokeType (thick + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff3a2418).withAlpha (alpha)); g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // bipolar knobs (GAIN, DEPTH) sweep out from zero
    float from = startAngle;
    if (s.getProperties().contains ("bip")) from = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);
    juce::Path val; val.addCentredArc (c.x, c.y, ar, ar, 0, std::min (from, angle), std::max (from, angle), true);
    const auto base = s.getProperties().contains ("col") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["col"]) : pal::signal;
    const auto arcCol = (s.isMouseOverOrDragging() ? base.brighter (0.3f) : base).withAlpha (alpha);
    g.setColour (arcCol.withAlpha (0.25f * alpha));
    g.strokePath (val, juce::PathStrokeType (thick + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (arcCol);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float br = ar - thick * 1.4f;
    if (br < 4.0f) return;
    const juce::Rectangle<float> body (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillEllipse (body.translated (1.5f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a4c).withAlpha (alpha), c.x - br * 0.5f, c.y - br * 0.7f,
                                             juce::Colour (0xff17120f), c.x + br * 0.6f, c.y + br, true));
    g.fillEllipse (body);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addEllipse (body);
        g.reduceClipRegion (clip);
        const float off = (float) (((juce::uint64) (juce::pointer_sized_int) &s >> 4) % 64);
        g.setOpacity (0.9f * alpha);
        g.drawImage (rustDisc, body.expanded (br * 0.4f).translated (-off * 0.3f, off * 0.2f), juce::RectanglePlacement::stretchToFit);
    }
    g.setColour (juce::Colour (0xff0f0b09).withAlpha (0.8f));
    for (int i = 0; i < 32; ++i)
    {
        const float a = (float) i / 32.0f * juce::MathConstants<float>::twoPi;
        g.drawLine (c.x + std::cos (a) * br * 0.87f, c.y + std::sin (a) * br * 0.87f,
                    c.x + std::cos (a) * br, c.y + std::sin (a) * br, 1.0f);
    }
    g.setColour (juce::Colour (0xff0b0807)); g.drawEllipse (body, 1.2f);

    juce::Path p; p.addRoundedRectangle (-1.8f, -br * 0.92f, 3.6f, br * 0.6f, 1.2f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x + 0.8f, c.y + 1.0f));
    g.setColour (pal::paint.withAlpha (alpha));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

// toggles: an indicator lamp + stencilled label on a rusty plate
void LowBlowLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const bool warn = b.getName() == "listen";
    const auto lit = warn ? pal::hazard : pal::signalHot;
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.translated (1.0f, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xff3a1a10) : juce::Colour (0xff2a221d), 0, r.getY(),
                                             juce::Colour (0xff120d0b), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? lit.withAlpha (0.8f) : (hi ? pal::rustLight : juce::Colour (0xff5a3420))); g.drawRoundedRectangle (r, 3.0f, 1.2f);

    const float d = r.getHeight() * 0.42f;
    auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + r.getHeight() * 0.55f, r.getCentreY() });
    if (on)
    {
        juce::Path lp; lp.addEllipse (led);
        juce::DropShadow (lit.withAlpha (0.9f), 10, {}).drawForPath (g, lp);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0c0), led.getCentreX(), led.getCentreY(), lit, led.getX(), led.getY(), true));
    }
    else g.setColour (juce::Colour (0xff2a1c16));
    g.fillEllipse (led);
    g.setColour (juce::Colours::black.withAlpha (0.7f)); g.drawEllipse (led, 1.0f);

    g.setColour (on ? pal::paint : pal::paintDim);
    g.setFont (tex::stencil (15.0f).withExtraKerningFactor (0.1f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (r.getHeight() * 1.0f).withTrimmedRight (6), juce::Justification::centredLeft);
}



//==============================================================================
//==============================================================================
Selector::Selector (juce::StringArray l) : labels (std::move (l)) {}

void Selector::bind (juce::RangedAudioParameter& p)
{
    att = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { index = juce::roundToInt (v); repaint(); if (onChange) onChange(); });
    att->sendInitialUpdate();
}

int Selector::segmentAt (juce::Point<int> p) const
{
    if (labels.isEmpty()) return -1;
    return juce::jlimit (0, labels.size() - 1, p.x * labels.size() / juce::jmax (1, getWidth()));
}

void Selector::mouseDown (const juce::MouseEvent& e) { if (att) att->setValueAsCompleteGesture ((float) segmentAt (e.getPosition())); }
void Selector::mouseMove (const juce::MouseEvent& e) { const int h = segmentAt (e.getPosition()); if (h != hover) { hover = h; repaint(); } }

void Selector::paint (juce::Graphics& g)
{
    const int n = labels.size();
    const float sw = (float) getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * sw, 0, sw, (float) getHeight()).reduced (3.0f, 2.0f);
        const bool on = i == index, hi = i == hover;
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (r.translated (1.5f, 2.0f), 3.0f);
        if (on)
        {
            juce::Path pth; pth.addRoundedRectangle (r, 3.0f);
            juce::DropShadow (pal::signalHot.withAlpha (0.4f), 10, {}).drawForPath (g, pth);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a2a14), r.getCentreX(), r.getY(), juce::Colour (0xff3a0e06), r.getCentreX(), r.getBottom(), false));
        }
        else
            g.setGradientFill (juce::ColourGradient (hi ? juce::Colour (0xff43332a) : juce::Colour (0xff332822), r.getCentreX(), r.getY(),
                                                     juce::Colour (0xff15100d), r.getCentreX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? pal::signalHot : (hi ? pal::rustLight : juce::Colour (0xff5a3420)));
        g.drawRoundedRectangle (r, 3.0f, on ? 1.6f : 1.1f);
        g.setColour (on ? juce::Colour (0xfffff0d8) : pal::paint.withAlpha (0.85f));
        g.setFont (tex::stencil (15.0f).withExtraKerningFactor (0.08f));
        g.drawFittedText (labels[i], r.reduced (6, 2).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
}


//==============================================================================
void Glove::advance (bool hit, float dt)
{
    t += dt;
    if (hit) punch = 1.0f;
    else punch = std::max (0.0f, punch - dt * 6.0f);
}

void Glove::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto c = r.getCentre();
    const float R = std::min (r.getWidth(), r.getHeight()) * 0.5f;

    // iron medallion
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (r.translated (1.5f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a4c), c.x - R * 0.6f, c.y - R * 0.8f, juce::Colour (0xff1a1410), c.x + R, c.y + R, true));
    g.fillEllipse (r);
    g.setColour (pal::rust.withAlpha (0.55f)); g.drawEllipse (r.reduced (1.0f), 1.6f);
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i / 8.0f * juce::MathConstants<float>::twoPi + 0.39f;
        g.setColour (juce::Colour (0xff8a7a68)); g.fillEllipse (c.x + std::cos (a) * R * 0.88f - 1.8f, c.y + std::sin (a) * R * 0.88f - 1.8f, 3.6f, 3.6f);
    }
    const auto inner = r.reduced (R * 0.2f);
    juce::Graphics::ScopedSaveState ss (g);
    {
        juce::Path clip; clip.addEllipse (inner);
        g.reduceClipRegion (clip);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a0c08), c.x, inner.getY(), juce::Colour (0xff080505), c.x, inner.getBottom(), false));
    g.fillEllipse (inner);

    // impact flash
    if (punch > 0.05f)
    {
        g.setColour (pal::signalHot.withAlpha (0.5f * punch));
        juce::Path star;
        star.addStar ({ inner.getRight() - inner.getWidth() * 0.12f, c.y + R * 0.15f }, 8, R * 0.12f, R * (0.3f + 0.25f * punch));
        g.fillPath (star);
    }

    // the glove: swings in from the left, low (it's a low blow)
    const float u = R / 30.0f;
    const float dx = -R * 0.35f + punch * R * 0.45f;
    auto T = juce::AffineTransform::rotation (-0.35f + punch * 0.25f).translated (c.x + dx, c.y + R * 0.12f);
    juce::Path glove;
    glove.addRoundedRectangle (-14 * u, -11 * u, 24 * u, 22 * u, 10 * u);                 // fist
    glove.addEllipse (-6 * u, -15 * u, 12 * u, 10 * u);                                     // thumb
    juce::Path cuff; cuff.addRoundedRectangle (-26 * u, -8 * u, 14 * u, 16 * u, 3 * u);      // cuff
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (glove, T.translated (1.5f, 2.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe05a3a), c.x + dx - 6 * u, c.y - 8 * u, juce::Colour (0xff5a0e06), c.x + dx + 12 * u, c.y + 16 * u, true));
    g.fillPath (glove, T);
    g.setColour (pal::paint); g.fillPath (cuff, T);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (glove, juce::PathStrokeType (1.2f), T);
    g.strokePath (cuff, juce::PathStrokeType (1.0f), T);
    // laces + knuckle crease
    juce::Path lace; lace.startNewSubPath (-24 * u, -3 * u); lace.lineTo (-14 * u, -3 * u); lace.startNewSubPath (-24 * u, 3 * u); lace.lineTo (-14 * u, 3 * u);
    g.setColour (pal::rustDeep); g.strokePath (lace, juce::PathStrokeType (1.2f), T);
    juce::Path crease; crease.startNewSubPath (2 * u, -9 * u); crease.quadraticTo (6 * u, 0, 2 * u, 9 * u);
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.strokePath (crease, juce::PathStrokeType (1.2f), T);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (-8 * u, -8 * u, 8 * u, 5 * u).transformedBy (T));
}

//==============================================================================
void RingScope::pull()
{
    auto& e = proc.getEngine();
    const int w = e.scopeWrite.load();
    if (w - readPos > lowblow::Engine::kScope) readPos = w - lowblow::Engine::kScope;
    hitSinceLastPull = false;
    while (readPos < w)
    {
        const auto sp = e.scope[(size_t) (readPos % lowblow::Engine::kScope)];
        pts.erase (pts.begin());
        pts.push_back (sp);
        hitSinceLastPull = hitSinceLastPull || sp.hit;
        ++readPos;
    }
}

void RingScope::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    g.setColour (pal::screen); g.fillRoundedRectangle (full, 4.0f);
    auto a = full.reduced (34.0f, 10.0f).withTrimmedBottom (12.0f);
    const float depth = proc.apvts.getRawParameterValue ("depth")->load();

    // grid: level (dB) on the left, duck (dB) hanging from the top
    g.setFont (tex::plainBold (9.0f));
    auto yLevel = [&] (float db) { return a.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * a.getHeight(); };
    for (int db : { 0, -12, -24, -36, -48 })
    {
        const float y = yLevel ((float) db);
        g.setColour (pal::paintDim.withAlpha (0.08f)); g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        g.setColour (pal::paintDim.withAlpha (0.55f));
        g.drawText (juce::String (db), (int) full.getX() + 2, (int) y - 6, 28, 12, juce::Justification::centredRight);
    }
    const float duckH = a.getHeight() * 0.45f;
    const float span = depth <= 9.0f ? 12.0f : 24.0f;   // zoom the duck scale to the depth in use
    auto yDuck = [&] (float gr) { return a.getY() + juce::jlimit (0.0f, 1.0f, gr / span) * duckH; };
    g.setColour (pal::hazard.withAlpha (0.55f));
    const int step = span < 13.0f ? 3 : 6;
    for (int db = step; db <= (int) span; db += step)
        g.drawText ("-" + juce::String (db), (int) a.getRight() + 2, (int) yDuck ((float) db) - 6, 30, 12, juce::Justification::centredLeft);
    // the DEPTH line
    g.setColour (pal::hazard.withAlpha (0.35f));
    const float dl[] = { 4.0f, 4.0f };
    g.drawDashedLine ({ a.getX(), yDuck (depth), a.getRight(), yDuck (depth) }, dl, 2, 1.0f);
    for (int s = 1; s < 4; ++s)
    {
        const float x = a.getX() + a.getWidth() * (float) s / 4.0f;
        g.setColour (pal::paintDim.withAlpha (0.06f)); g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
    }
    g.setColour (pal::paintDim.withAlpha (0.5f));
    g.drawText ("-4 s", (int) a.getX() - 10, (int) a.getBottom() + 1, 40, 11, juce::Justification::centredLeft);
    g.drawText ("NOW", (int) a.getRight() - 30, (int) a.getBottom() + 1, 30, 11, juce::Justification::centredRight);

    const int n = (int) pts.size();
    auto xAt = [&] (int i) { return a.getX() + a.getWidth() * (float) i / (float) (n - 1); };

    // the bass
    {
        juce::Path p; p.startNewSubPath (a.getX(), a.getBottom());
        for (int i = 0; i < n; ++i) p.lineTo (xAt (i), yLevel (pts[(size_t) i].bassDb));
        p.lineTo (a.getRight(), a.getBottom()); p.closeSubPath();
        g.setGradientFill (juce::ColourGradient (pal::paint.withAlpha (0.28f), 0, a.getY(), pal::paint.withAlpha (0.05f), 0, a.getBottom(), false));
        g.fillPath (p);
    }
    // the kick
    {
        juce::Path p;
        for (int i = 0; i < n; ++i) { const float y = yLevel (pts[(size_t) i].kickDb + 6.0f); if (i == 0) p.startNewSubPath (xAt (i), y); else p.lineTo (xAt (i), y); }
        g.setColour (pal::signal.withAlpha (0.8f)); g.strokePath (p, juce::PathStrokeType (1.3f));
    }
    // the duck, hanging from the top
    {
        juce::Path p; p.startNewSubPath (a.getX(), a.getY());
        for (int i = 0; i < n; ++i) p.lineTo (xAt (i), yDuck (pts[(size_t) i].grDb));
        p.lineTo (a.getRight(), a.getY()); p.closeSubPath();
        g.setColour (pal::hazard.withAlpha (0.35f)); g.fillPath (p);
        juce::Path line;
        for (int i = 0; i < n; ++i) { const float y = yDuck (pts[(size_t) i].grDb); if (i == 0) line.startNewSubPath (xAt (i), y); else line.lineTo (xAt (i), y); }
        g.setColour (pal::hazard); g.strokePath (line, juce::PathStrokeType (1.6f));
    }
    // hits
    for (int i = 0; i < n; ++i)
        if (pts[(size_t) i].hit)
        {
            g.setColour (pal::signalHot);
            g.fillRect (xAt (i) - 1.0f, a.getBottom() - 6.0f, 2.0f, 6.0f);
        }

    // legend
    g.setFont (tex::plainBold (9.0f).withExtraKerningFactor (0.08f));
    auto leg = a.withTrimmedTop (a.getHeight() - 14.0f).withTrimmedLeft (8.0f).toNearestInt();
    juce::ignoreUnused (leg);
    auto key = [&] (float x, juce::Colour col, const juce::String& s)
    {
        g.setColour (col); g.fillRect (x, a.getY() + 6.0f, 10.0f, 4.0f);
        g.setColour (pal::paintDim); g.drawText (s, (int) x + 14, (int) a.getY() + 1, 120, 14, juce::Justification::centredLeft);
    };
    key (a.getRight() - 300, pal::paint.withAlpha (0.5f), "BASS (OUT)");
    key (a.getRight() - 216, pal::signal, "KICK (SIDECHAIN)");
    key (a.getRight() - 110, pal::hazard, "BASS DUCKED");
}

//==============================================================================
void MeterPair::paint (juce::Graphics& g)
{
    auto m = getLocalBounds().toFloat();
    g.setColour (pal::screen); g.fillRoundedRectangle (m, 3.0f);
    auto bars = m.reduced (10.0f, 8.0f);
    auto labels = bars.removeFromBottom (30.0f);
    const float bw = 26.0f, gap = bars.getWidth() - bw * 2.0f;
    auto barK = bars.removeFromLeft (bw); bars.removeFromLeft (gap);
    auto barD = bars.removeFromLeft (bw);

    // KICK level
    {
        g.setColour (juce::Colour (0xff171210)); g.fillRect (barK);
        const float db = juce::Decibels::gainToDecibels (kick, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
        auto inner = barK.reduced (2.0f);
        const int segs = 20; const float sh = inner.getHeight() / (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            const float t = (float) (i + 1) / (float) segs;
            auto seg = juce::Rectangle<float> (inner.getX(), inner.getBottom() - (float) (i + 1) * sh + 1.0f, inner.getWidth(), sh - 1.5f);
            g.setColour (t <= frac ? pal::signal : pal::signal.withAlpha (0.07f)); g.fillRect (seg);
        }
        auto lab = labels.withX (barK.getX() - 14).withWidth (bw + 28);
        g.setColour (hitLamp ? pal::signalHot : pal::paint); g.setFont (tex::stencil (13.0f));
        g.drawText ("KICK", lab.removeFromTop (15), juce::Justification::centred);
        g.setColour (pal::paintDim); g.setFont (tex::plainBold (9.5f));
        g.drawText (kick > 0.00002f ? juce::String (db, 1) : juce::String ("-inf"), lab, juce::Justification::centred);
    }
    // DUCK: hangs from the top, 0..24 dB
    {
        g.setColour (juce::Colour (0xff171210)); g.fillRect (barD);
        auto inner = barD.reduced (2.0f);
        const int segs = 24; const float sh = inner.getHeight() / (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            auto seg = juce::Rectangle<float> (inner.getX(), inner.getY() + (float) i * sh + 0.5f, inner.getWidth(), sh - 1.5f);
            g.setColour (duck > (float) i + 0.05f ? pal::hazard : pal::hazard.withAlpha (0.08f)); g.fillRect (seg);
        }
        const float dy = inner.getY() + juce::jlimit (0.0f, 24.0f, depth) / 24.0f * inner.getHeight();
        g.setColour (pal::paintDim); g.drawLine (inner.getX() - 4, dy, inner.getRight() + 4, dy, 1.0f);
        if (duckHold > 0.05f)
        {
            const float hy = inner.getY() + std::min (duckHold, 24.0f) / 24.0f * inner.getHeight();
            g.setColour (juce::Colour (0xfffff0d8)); g.fillRect (inner.getX(), hy - 1.0f, inner.getWidth(), 2.0f);
        }
        auto lab = labels.withX (barD.getX() - 14).withWidth (bw + 28);
        g.setColour (pal::hazard); g.setFont (tex::stencil (13.0f));
        g.drawText ("DUCK", lab.removeFromTop (15), juce::Justification::centred);
        g.setColour (pal::paintDim); g.setFont (tex::plainBold (9.5f));
        g.drawText (duckHold > 0.05f ? "-" + juce::String (duckHold, 1) : juce::String ("0.0"), lab, juce::Justification::centred);
    }
}

//==============================================================================
void SideStatus::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r, 4.0f);
    const auto col = ok ? juce::Colour (0xff9aa86a) : pal::hazard;
    auto lamp = juce::Rectangle<float> (10, 10).withCentre ({ r.getX() + 14, r.getY() + 14 });
    { juce::Path lp; lp.addEllipse (lamp); juce::DropShadow (col.withAlpha (0.8f), 8, {}).drawForPath (g, lp); }
    g.setColour (col); g.fillEllipse (lamp);
    auto text = r.withTrimmedLeft (28).reduced (0, 4);
    g.setFont (tex::stencil (13.5f).withExtraKerningFactor (0.06f));
    g.drawText (ok ? "KICK ON THE SIDECHAIN" : "NO KICK ON THE SIDECHAIN", text.removeFromTop (16).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (pal::paint.withAlpha (0.85f));
    g.setFont (tex::plainBold (10.5f));
    juce::String s;
    if (ok) s = hitsPerMin > 0 ? juce::String (juce::roundToInt (hitsPerMin)) + " hits a minute. Every one pulls the bass down." : "Listening for hits...";
    else if (busOff) s = "The sidechain input is switched off. Turn it on in your DAW, then send the kick track to it.";
    else s = "Send your kick track to Low Blow's sidechain input. Until then the bass passes through untouched.";
    g.drawFittedText (s, text.toNearestInt(), juce::Justification::topLeft, 2, 0.85f);
}

//==============================================================================
LowBlowEditor::LowBlowEditor (LowBlowProcessor& p)
    : AudioProcessorEditor (&p), proc (p), scope (p)
{
    setLookAndFeel (&lnf);

    depthKnob   = &makeKnob ("depth", "DEPTH", "How far the bass is pulled down on each kick. 4-8 dB is typical; more for a pumping effect.");
    attackKnob  = &makeKnob ("attack", "ATTACK", "How fast the bass gets out of the way. Keep it short so the kick's punch is clear.");
    holdKnob    = &makeKnob ("hold", "HOLD", "How long the bass stays down after the kick fades under the trigger level.");
    releaseKnob = &makeKnob ("release", "RELEASE", "How fast the bass comes back. Shorter for fast double kicks, longer for a smooth or pumping feel.");
    sensKnob    = &makeKnob ("sensitivity", "SENSITIVITY", "Which hits count. The trigger follows the loudest recent kicks: higher catches softer hits (ghost notes), lower only the big ones.");
    xoKnob      = &makeKnob ("crossover", "CROSSOVER", "LOWS ONLY ducks the bass below this frequency, so its growl and pick attack stay. Set it just above where the kick's thump sits.");
    laKnob      = &makeKnob ("lookahead", "LOOKAHEAD", "Starts the duck slightly before the kick lands. Adds the same amount of latency (your DAW compensates).");
    outKnob     = &makeKnob ("output", "OUTPUT", "Output level.");

    addAndMakeVisible (modeSel);
    modeSel.bind (*p.apvts.getParameter ("mode"));
    modeSel.setTooltip ("LOWS ONLY ducks just the bass's low end (below CROSSOVER). FULL ducks the whole bass.");
    addAndMakeVisible (listenButton);
    listenButton.setName ("listen");
    listenButton.setTooltip ("Hear only what Low Blow takes out of the bass. Turn it off before you bounce!");
    listenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "listen", listenButton);

    addAndMakeVisible (glove);  glove.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (scope);  scope.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (meters); meters.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (status);
    status.setTooltip ("Fender Studio Pro: open Low Blow's window, click the sidechain button in its header and pick your kick track "
                       "(or add a send on the kick channel to Low Blow). Other DAWs: route the kick to Low Blow's sidechain / key input.");

    addAndMakeVisible (presetBox);
    presetBox.setJustificationType (juce::Justification::centredLeft);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0) return;
        if (id >= kUserIdBase)
        {
            if (id - kUserIdBase < userFiles.size()) proc.loadUserPreset (userFiles[id - kUserIdBase]);
        }
        else if (id - 1 != proc.getCurrentProgram() || proc.getCurrentPresetName() != proc.getProgramName (id - 1))
            proc.setCurrentProgram (id - 1);
    };
    for (auto* b : { &prevButton, &nextButton, &saveButton, &deleteButton }) addAndMakeVisible (*b);
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    saveButton.onClick = [this] { promptSave(); };
    deleteButton.onClick = [this] { confirmDelete(); };

    refreshPresetBox();
    addAndMakeVisible (rebel);
    setSize (940, 600);
    startTimerHz (30);
}

LowBlowEditor::~LowBlowEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

LowBlowEditor::Knob& LowBlowEditor::makeKnob (const juce::String& id, const juce::String& text, const juce::String& tip)
{
    auto k = std::make_unique<Knob>();
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (tex::stencil (14.0f).withExtraKerningFactor (0.08f));
    k->label.setColour (juce::Label::textColourId, pal::paint);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 17);
    k->slider.setColour (juce::Slider::textBoxTextColourId, pal::paint.withAlpha (0.85f));
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3a2418));
    k->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x90000000));
    k->slider.setMouseDragSensitivity (220);
    k->slider.setTooltip (tip);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    auto* prm = proc.apvts.getParameter (id);
    k->slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void LowBlowEditor::placeKnob (Knob& k, juce::Rectangle<int> cell)
{
    k.label.setBounds (cell.removeFromTop (18));
    k.slider.setBounds (cell);
}

void LowBlowEditor::resized()
{
    plates.clear();
    logoArea  = { 18, 12, 440, 92 };
    glove.setBounds (18, 12, 90, 90);
    badgeArea = { 460, 14, 252, 86 };
    crestArea = { 468, 19, 76, 76 };
    presetArea = { 724, 14, 200, 86 };
    presetBox.setBounds (724, 36, 200, 26);
    prevButton.setBounds (724, 70, 28, 24);
    nextButton.setBounds (756, 70, 28, 24);
    saveButton.setBounds (790, 70, 62, 24);
    deleteButton.setBounds (856, 70, 68, 24);
    stripeArea = { 0, 110, getWidth(), 10 };
    linkArea = { 300, 84, 150, 14 };
    rebel.setBounds (linkArea.getX(), linkArea.getCentreY() - 8, 54, 16);

    plates.push_back ({ "THE RING", { 16, 130, 908, 214 } });
    scope.setBounds (28, 154, 884, 180);

    plates.push_back ({ "THE HIT", { 16, 354, 480, 230 }, true });
    const int kx = 24, kw = 93, ky = 376, kh = 132;
    placeKnob (*depthKnob,   { kx,          ky, kw, kh });
    placeKnob (*attackKnob,  { kx + kw,     ky, kw, kh });
    placeKnob (*holdKnob,    { kx + kw * 2, ky, kw, kh });
    placeKnob (*releaseKnob, { kx + kw * 3, ky, kw, kh });
    placeKnob (*sensKnob,    { kx + kw * 4, ky, kw, kh });
    status.setBounds (28, 518, 456, 54);

    plates.push_back ({ "WHERE", { 506, 354, 250, 230 } });
    modeSel.setBounds (514, 378, 234, 34);
    placeKnob (*xoKnob,  { 512, 420, 80, 118 });
    placeKnob (*laKnob,  { 591, 420, 80, 118 });
    placeKnob (*outKnob, { 670, 420, 80, 118 });

    plates.push_back ({ "METERS", { 766, 354, 158, 230 } });
    meters.setBounds (776, 378, 138, 152);
    listenButton.setBounds (776, 540, 138, 32);

    buildBackdrop();
}

//==============================================================================
void LowBlowEditor::buildBackdrop()
{
    const float scale = 2.0f;
    const int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0) return;
    const int PW = (int) ((float) W * scale), PH = (int) ((float) H * scale);
    juce::Image base (juce::Image::RGB, W, H, false);

    std::vector<juce::Rectangle<float>> pr;
    for (auto& s : plates) pr.push_back (s.bounds.toFloat());
    pr.push_back (badgeArea.toFloat());
    const auto stripe = stripeArea.toFloat();

    {
        juce::Image::BitmapData bd (base, juce::Image::BitmapData::writeOnly);
        for (int py = 0; py < H; ++py)
        {
            const float y = (float) py;
            for (int px = 0; px < W; ++px)
            {
                const float x = (float) px;
                tex::RGB col;
                int inside = -1; float edge = 1e9f; bool shadow = false;
                for (int i = 0; i < (int) pr.size(); ++i)
                {
                    const auto& p = pr[(size_t) i];
                    if (p.contains (x, y))
                    {
                        inside = i;
                        edge = std::min ({ x - p.getX(), p.getRight() - x, y - p.getY(), p.getBottom() - y });
                        break;
                    }
                    if (p.translated (3.0f, 4.0f).contains (x, y)) shadow = true;
                }
                if (stripe.contains (x, y))
                {
                    // worn hazard stripes
                    const bool yellow = std::fmod (x + y * 1.0f + 1000.0f, 28.0f) < 14.0f;
                    const float wear = tex::fbm (x / 12.0f, y / 4.0f, 2024, 3);
                    col = yellow ? tex::rgb (pal::hazard) : tex::rgb (juce::Colour (0xff141010));
                    col = tex::mix (col, tex::rustAt (x, y, 0.3f, 99), tex::smoothstep (0.5f, 0.68f, wear));
                    col = tex::mul (col, 0.85f + 0.15f * tex::hash (px, py, 3));
                }
                else if (inside >= 0)
                {
                    // dark diamond-plate steel, rust creeping in from the edges
                    const float d = tex::fbm (x / 30.0f, y / 30.0f, 211, 3);
                    tex::RGB steel = tex::mix (tex::rgb (juce::Colour (0xff1e1a17)), tex::rgb (juce::Colour (0xff332c27)), d);
                    // diamond tread
                    const float u = std::fmod (x + y + 2000.0f, 16.0f) / 16.0f, v = std::fmod (x - y + 2000.0f, 16.0f) / 16.0f;
                    const float tread = (std::abs (u - 0.5f) < 0.08f && std::abs (v - 0.25f) < 0.18f) || (std::abs (v - 0.5f) < 0.08f && std::abs (u - 0.75f) < 0.18f) ? 1.0f : 0.0f;
                    steel = tex::mix (steel, tex::rgb (juce::Colour (0xff4a4038)), tread * 0.55f);
                    const float edgeRust = (1.0f - juce::jlimit (0.0f, 1.0f, edge / 14.0f)) * tex::smoothstep (0.35f, 0.6f, tex::vnoise (x / 6.0f, y / 6.0f, 17));
                    const float bloom = tex::smoothstep (0.6f, 0.75f, tex::fbm (x / 55.0f, y / 55.0f, 404, 3)) * 0.7f;
                    col = tex::mix (steel, tex::rustAt (x, y, 0.15f, 1337), juce::jlimit (0.0f, 1.0f, edgeRust + bloom));
                    col = tex::mul (col, 0.88f);
                }
                else
                {
                    col = tex::rustAt (x, y, 0.1f, 42);
                    if (shadow) col = tex::mul (col, 0.45f);
                }

                const float streak = tex::fbm (x / 9.0f, y / 240.0f, 909, 3);
                const float drip = tex::smoothstep (0.58f, 0.78f, streak) * (0.25f + 0.5f * y / (float) H);
                col = tex::mul (col, 1.0f - drip * 0.6f);
                const float runoff = tex::smoothstep (0.66f, 0.82f, tex::fbm (x / 5.0f, y / 120.0f, 31337, 2));
                col = tex::mix (col, tex::rgb (pal::rustDeep), runoff * 0.35f);
                // oil stains
                const float oil = tex::smoothstep (0.7f, 0.8f, tex::fbm (x / 70.0f, y / 50.0f, 555, 3));
                col = tex::mul (col, 1.0f - oil * 0.45f);
                if (tex::hash (px, py, 5) > 0.988f) col = tex::mul (col, 0.45f);
                const float grime = 0.72f + 0.28f * tex::fbm (x / 260.0f, y / 260.0f, 77, 2);
                const float dx = (x / (float) W - 0.5f) * 2.0f, dy = (y / (float) H - 0.5f) * 2.0f;
                const float vig = 1.0f - 0.45f * std::min (1.0f, (dx * dx + dy * dy) * 0.6f);
                col = tex::mul (col, grime * vig);
                bd.setPixelColour (px, py, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, col.r), juce::jlimit (0.0f, 1.0f, col.g),
                                                                        juce::jlimit (0.0f, 1.0f, col.b), 1.0f));
            }
        }
    }

    backdrop = juce::Image (juce::Image::RGB, PW, PH, false);
    juce::Graphics g (backdrop);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (base, 0, 0, PW, PH, 0, 0, W, H);
    g.addTransform (juce::AffineTransform::scale (scale));
    juce::Random rnd (1991);

    auto rivet = [&] (float cx, float cy, float rad)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (cx - rad + 1, cy - rad + 1.5f, rad * 2, rad * 2);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a7a68), cx - rad * 0.4f, cy - rad * 0.5f, juce::Colour (0xff2a201a), cx + rad, cy + rad, true));
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
        g.setColour (pal::rust.withAlpha (0.55f)); g.drawEllipse (cx - rad, cy - rad, rad * 2, rad * 2, 1.2f);
        g.setColour (pal::rustDeep.withAlpha (0.35f)); g.fillRect (cx - 0.8f, cy + rad, 1.6f, 3.0f + rnd.nextFloat() * 9.0f);
    };
    auto plateFrame = [&] (juce::Rectangle<float> p, bool hot)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawLine (p.getX() + 2, p.getY() + 1, p.getRight() - 2, p.getY() + 1, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.drawRect (p, 1.0f);
        if (hot) { g.setColour (pal::signal.withAlpha (0.45f)); g.drawRect (p.reduced (2.0f), 1.5f); }
        rivet (p.getX() + 6, p.getY() + 6, 3.0f); rivet (p.getRight() - 6, p.getY() + 6, 3.0f);
        rivet (p.getX() + 6, p.getBottom() - 6, 3.0f); rivet (p.getRight() - 6, p.getBottom() - 6, 3.0f);
    };

    // scratches + weld spatter
    for (int i = 0; i < 300; ++i)
    {
        const float x = rnd.nextFloat() * (float) W, y = rnd.nextFloat() * (float) H;
        const float len = 6.0f + rnd.nextFloat() * 70.0f, ang = rnd.nextFloat() * juce::MathConstants<float>::pi;
        g.setColour ((rnd.nextBool() ? juce::Colour (0xffd0c0a8) : juce::Colours::black).withAlpha (0.04f + rnd.nextFloat() * 0.1f));
        g.drawLine (x, y, x + std::cos (ang) * len, y + std::sin (ang) * len, 0.5f + rnd.nextFloat() * 0.6f);
    }

    int seed = 10;
    for (auto& s : plates)
    {
        const auto p = s.bounds.toFloat();
        plateFrame (p, s.hot);
        auto title = tex::distressedText (s.title, s.bounds.getWidth() - 40, 18, tex::stencil (15.0f), s.hot ? pal::signalHot : pal::paint,
                                          juce::Justification::centredLeft, scale, 0.2f, seed++, 0.12f);
        g.drawImage (title, juce::Rectangle<float> (p.getX() + 14, p.getY() + 3, (float) s.bounds.getWidth() - 40, 18.0f));
    }
    // hazard stripe edges
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (0, stripe.getY(), (float) W, stripe.getY(), 1.5f);
    g.drawLine (0, stripe.getBottom(), (float) W, stripe.getBottom(), 1.5f);

    // CASTLE DANGER MUSIC COMPANY badge with the castle crest
    const auto badge = badgeArea.toFloat();
    plateFrame (badge, false);
    g.setColour (pal::paint.withAlpha (0.22f)); g.drawRect (badge.reduced (5.0f), 1.0f);
    drawCastleCrest (g, crestArea.toFloat());
    const float tx = (float) crestArea.getRight() + 8.0f, tw = badge.getRight() - tx - 10.0f;
    auto l1 = tex::distressedText ("CASTLE DANGER", (int) tw, 28, tex::stencil (25.0f), juce::Colour (0xffeadfc4), juce::Justification::centredLeft, scale, 0.24f, 4242, 0.08f);
    auto l2 = tex::distressedText ("MUSIC COMPANY", (int) tw, 20, tex::stencil (17.0f), pal::rustLight, juce::Justification::centredLeft, scale, 0.24f, 4343, 0.2f);
    auto l3 = tex::distressedText ("WE MAKE MUSIC", (int) tw, 14, tex::plainBold (9.0f), pal::paintDim, juce::Justification::centredLeft, scale, 0.15f, 4444, 0.3f);
    g.setColour (juce::Colours::black);
    g.setOpacity (0.7f); g.drawImage (l1, juce::Rectangle<float> (tx + 1.5f, badge.getY() + 13.0f, tw, 28.0f));
    g.setOpacity (1.0f); g.drawImage (l1, juce::Rectangle<float> (tx, badge.getY() + 11.0f, tw, 28.0f));
    g.setOpacity (0.7f); g.drawImage (l2, juce::Rectangle<float> (tx + 1.5f, badge.getY() + 40.0f, tw, 20.0f));
    g.setOpacity (1.0f); g.drawImage (l2, juce::Rectangle<float> (tx, badge.getY() + 38.0f, tw, 20.0f));
    g.drawImage (l3, juce::Rectangle<float> (tx + 1.0f, badge.getY() + 60.0f, tw, 14.0f));

    // stamped subtitle under the logo
    auto sub = tex::distressedText ("KICK / BASS DUCKER  //  V" + juce::String (JucePlugin_VersionString),
                                    170, 14, tex::plainBold (10.5f), pal::paintDim, juce::Justification::centredLeft, scale, 0.15f, 99, 0.14f);
    g.drawImage (sub, juce::Rectangle<float> (120.0f, 84.0f, 170.0f, 14.0f));

    // PRESET label
    auto pl = tex::distressedText ("PRESET", 120, 16, tex::stencil (14.0f), pal::paint, juce::Justification::centredLeft, scale, 0.2f, 7, 0.15f);
    g.drawImage (pl, juce::Rectangle<float> ((float) presetArea.getX() + 2, (float) presetArea.getY() + 2, 120.0f, 16.0f));

    // the logo: LOW in cream, BLOW in rusty red, knocked down a little
    {
        const int lw = 330, lh = 70;
        logoImage = juce::Image (juce::Image::ARGB, (int) (lw * scale), (int) (lh * scale), true);
        juce::Graphics lg (logoImage);
        lg.addTransform (juce::AffineTransform::scale (scale));
        auto f = tex::stencil (64.0f).withExtraKerningFactor (0.05f);
        {
            juce::GlyphArrangement all; all.addLineOfText (f, "LOW BLOW", 0, 0);
            const float wAll = all.getBoundingBox (0, -1, true).getWidth();
            if (wAll > (float) lw - 8.0f) f = f.withHeight (64.0f * ((float) lw - 8.0f) / wAll);
        }
        lg.setFont (f);
        lg.setColour (pal::paint);
        lg.drawText ("LOW", 0, -3, lw, lh, juce::Justification::centredLeft, false);
        juce::GlyphArrangement ga; ga.addLineOfText (f, "LOW ", 0, 0);
        const float bx = ga.getBoundingBox (0, -1, true).getWidth();
        {
            juce::Graphics::ScopedSaveState ss (lg);
            lg.addTransform (juce::AffineTransform::rotation (0.06f, bx, (float) lh * 0.5f).translated (0.0f, 5.0f));
            lg.setColour (pal::signal);
            lg.drawText ("BLOW", (int) bx, 0, lw - (int) bx, lh, juce::Justification::centredLeft, false);
        }
        tex::distress (logoImage, scale, 0.3f, 666);
    }
}


void LowBlowEditor::paint (juce::Graphics& g)
{
    if (backdrop.isValid()) g.drawImage (backdrop, getLocalBounds().toFloat());
    else g.fillAll (pal::ironDark);

    if (logoImage.isValid())
    {
        const float alpha = juce::jlimit (0.45f, 1.0f, 1.0f - flicker * 0.6f);
        const auto la = juce::Rectangle<float> (116.0f, 10.0f, 330.0f, 70.0f);
        g.setColour (juce::Colours::black);
        g.setOpacity (0.65f); g.drawImage (logoImage, la.translated (2.5f, 3.5f));
        g.setOpacity (alpha); g.drawImage (logoImage, la);
        g.setOpacity (1.0f);
    }

    // Castle Danger Link status
    {
        auto& lk = proc.getLink();
        const bool linked = lk.masterNearby();
        const bool justSet = lk.getLastNoteTime() > 0 && juce::Time::getMillisecondCounter() - lk.getLastNoteTime() < 6000;
        const bool rebelOn = lk.isRebel();
        auto r = linkArea.toFloat().withTrimmedLeft (60);   // REBEL sits first on the line
        const auto col = rebelOn ? pal::signal : (justSet ? pal::signalHot : (linked ? pal::hazard : pal::paintDim.withAlpha (0.35f)));
        if (linked) { juce::Path lp; lp.addEllipse (r.getX(), r.getCentreY() - 3.5f, 7, 7); juce::DropShadow (col.withAlpha (0.8f), 6, {}).drawForPath (g, lp); }
        g.setColour (col); g.fillEllipse (r.getX(), r.getCentreY() - 3.5f, 7, 7);
        g.setFont (tex::plainBold (9.5f).withExtraKerningFactor (0.1f));
        g.drawText (rebelOn ? "HANDS OFF" : (justSet ? "JUST SET" : (linked ? "LINKED" : "LINK")), r.withTrimmedLeft (11).toNearestInt(), juce::Justification::centredLeft);
    }
}

void LowBlowEditor::timerCallback()
{
    if (++linkTick >= 15) { linkTick = 0; repaint (linkArea.expanded (8)); }
    auto& e = proc.getEngine();
    const float k = e.kickNow.exchange (0.0f), gr = e.grNow.exchange (0.0f);
    kickMeter = std::max (k, kickMeter * 0.86f);
    duckMeter = std::max (gr, duckMeter - 0.8f);
    if (gr >= duckHold) { duckHold = gr; holdTimer = 45; }
    else if (--holdTimer <= 0) duckHold = std::max (0.0f, duckHold - 0.5f);
    scope.pull();
    scope.repaint();
    glove.advance (scope.hitSinceLastPull, 1.0f / 30.0f);
    glove.repaint();
    meters.kick = kickMeter; meters.duck = duckMeter; meters.duckHold = duckHold;
    meters.depth = proc.apvts.getRawParameterValue ("depth")->load();
    meters.hitLamp = scope.hitSinceLastPull;
    meters.repaint();
    const bool ok = proc.kickArriving(), busOff = ! proc.sidechainBusEnabled();
    const float hpm = e.hitsPerMinute();
    if (ok != status.ok || busOff != status.busOff || std::abs (hpm - status.hitsPerMin) > 0.5f)
    {
        status.ok = ok; status.busOff = busOff; status.hitsPerMin = hpm;
        status.repaint();
    }

    auto& rnd = juce::Random::getSystemRandom();
    const float before = flicker;
    flicker = (rnd.nextFloat() < 0.015f + (scope.hitSinceLastPull ? 0.15f : 0.0f)) ? rnd.nextFloat() : flicker * 0.5f;
    if (std::abs (flicker - before) > 0.01f) repaint (110, 6, 350, 80);

    if (proc.presetVersion.load() != lastPresetVersion) refreshPresetBox();
}

//==============================================================================
void LowBlowEditor::refreshPresetBox()
{
    lastPresetVersion = proc.presetVersion.load();
    userFiles = LowBlowProcessor::getUserPresets();
    const auto current = proc.getCurrentPresetName();

    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.addSeparator();
    presetBox.addSectionHeading ("User Presets");
    if (userFiles.isEmpty()) { presetBox.addItem ("(none saved yet)", 999); presetBox.setItemEnabled (999, false); }
    for (int i = 0; i < userFiles.size(); ++i) presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), kUserIdBase + i);

    int sel = 0;
    for (int i = 0; i < userFiles.size(); ++i)
        if (userFiles[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (current)) sel = kUserIdBase + i;
    if (sel == 0 && proc.getProgramName (proc.getCurrentProgram()) == current) sel = proc.getCurrentProgram() + 1;
    if (sel != 0) presetBox.setSelectedId (sel, juce::dontSendNotification);
    else presetBox.setText (current, juce::dontSendNotification);
    deleteButton.setEnabled (sel >= kUserIdBase);
}

void LowBlowEditor::stepPreset (int delta)
{
    std::vector<int> ids;
    for (int i = 0; i < proc.getNumPrograms(); ++i) ids.push_back (i + 1);
    for (int i = 0; i < userFiles.size(); ++i) ids.push_back (kUserIdBase + i);
    auto it = std::find (ids.begin(), ids.end(), presetBox.getSelectedId());
    const int pos = it == ids.end() ? 0 : (int) (it - ids.begin());
    const int n = (int) ids.size();
    presetBox.setSelectedId (ids[(size_t) (((pos + delta) % n + n) % n)], juce::sendNotificationSync);
}

void LowBlowEditor::showDialog (const juce::String& title, const juce::String& message, bool withTextField, const juce::String& initialText,
                                 juce::StringArray buttons, std::function<void (int, const juce::String&)> onResult)
{
    juce::Component::SafePointer<LowBlowEditor> safe (this);
    dialog = std::make_unique<LowBlowDialog> (title, message, withTextField, initialText, buttons,
        [safe, onResult] (int index, const juce::String& text)
        {
            juce::MessageManager::callAsync ([safe, onResult, index, text]
            {
                if (safe == nullptr) return;
                safe->dialog.reset();
                if (onResult) onResult (index, text);
            });
        });
    addAndMakeVisible (*dialog);
    dialog->setBounds (getLocalBounds());
}

void LowBlowEditor::showMessage (const juce::String& title, const juce::String& text)
{
    showDialog (title, text, false, {}, { "OK" }, {});
}

void LowBlowEditor::promptSave()
{
    const bool onUser = presetBox.getSelectedId() >= kUserIdBase;
    juce::Component::SafePointer<LowBlowEditor> safe (this);
    auto doSave = [safe] (const juce::String& name)
    {
        if (safe == nullptr) return;
        if (! safe->proc.saveUserPreset (name))
            safe->showMessage ("COULDN'T SAVE", "Low Blow couldn't write the preset file to:\n" + LowBlowProcessor::getUserPresetFolder().getFullPathName());
        else safe->refreshPresetBox();
    };
    showDialog ("SAVE PRESET", "Name your preset:", true, onUser ? proc.getCurrentPresetName() : juce::String(), { "Save", "Cancel" },
        [safe, doSave] (int button, const juce::String& text)
        {
            if (button != 0 || safe == nullptr) return;
            const auto name = text.trim();
            if (name.isEmpty()) { safe->showMessage ("SAVE PRESET", "Please enter a name for the preset."); return; }
            for (int i = 0; i < safe->proc.getNumPrograms(); ++i)
                if (safe->proc.getProgramName (i).equalsIgnoreCase (name))
                {
                    safe->showMessage ("SAVE PRESET", "\"" + name + "\" is a factory preset name. Please choose another name.");
                    return;
                }
            if (LowBlowProcessor::fileForPresetName (name).existsAsFile())
            {
                safe->showDialog ("REPLACE PRESET?", "A preset named \"" + name + "\" already exists. Replace it with the current settings?",
                                  false, {}, { "Replace", "Cancel" },
                                  [doSave, name] (int b, const juce::String&) { if (b == 0) doSave (name); });
                return;
            }
            doSave (name);
        });
}

void LowBlowEditor::confirmDelete()
{
    const int id = presetBox.getSelectedId();
    if (id < kUserIdBase || id - kUserIdBase >= userFiles.size()) return;
    const auto file = userFiles[id - kUserIdBase];
    juce::Component::SafePointer<LowBlowEditor> safe (this);
    showDialog ("DELETE PRESET?", "Delete \"" + file.getFileNameWithoutExtension() + "\"? This can't be undone.", false, {},
                { "Delete", "Cancel" }, [safe, file] (int b, const juce::String&)
                {
                    if (b != 0 || safe == nullptr) return;
                    file.deleteFile();
                    safe->proc.setCurrentProgram (0);
                    safe->refreshPresetBox();
                });
}

//==============================================================================
LowBlowDialog::LowBlowDialog (const juce::String& t, const juce::String& m, bool withTextField, const juce::String& initialText,
                        juce::StringArray names, std::function<void (int, const juce::String&)> cb)
    : title (t), message (m), hasField (withTextField), onClose (std::move (cb))
{
    setWantsKeyboardFocus (true);
    if (hasField)
    {
        field.setText (initialText, false);
        field.setInputRestrictions (60);
        field.setSelectAllWhenFocused (true);
        field.setFont (tex::plainBold (15.0f));
        field.setIndents (8, 6);
        field.onReturnKey = [this] { finish (0); };
        field.onEscapeKey = [this] { finish ((int) buttons.size() - 1); };
        addAndMakeVisible (field);
    }
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (names[i]));
        b->onClick = [this, i] { finish (i); };
        addAndMakeVisible (b);
    }
}

void LowBlowDialog::parentHierarchyChanged()
{
    if (isShowing())
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<LowBlowDialog> (this)]
        {
            if (sp == nullptr) return;
            if (sp->hasField) sp->field.grabKeyboardFocus(); else sp->grabKeyboardFocus();
        });
}

void LowBlowDialog::finish (int index)
{
    if (onClose == nullptr) return;
    auto cb = std::move (onClose);
    onClose = nullptr;
    cb (index, field.getText());
}

bool LowBlowDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey) { finish (0); return true; }
    if (k == juce::KeyPress::escapeKey) { finish ((int) buttons.size() - 1); return true; }
    return true;
}

void LowBlowDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (400, hasField ? 196 : 166);
    auto r = panel.reduced (22);
    r.removeFromTop (30);
    r.removeFromTop (hasField ? 22 : 50);
    if (hasField) { field.setBounds (r.removeFromTop (32)); r.removeFromTop (16); }
    auto row = r.removeFromBottom (28);
    for (int i = (int) buttons.size() - 1; i >= 0; --i) { buttons[i]->setBounds (row.removeFromRight (96)); row.removeFromRight (8); }
}

void LowBlowDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));
    auto p = panel.toFloat();
    g.setColour (juce::Colour (0xff1a1411)); g.fillRect (p);
    g.setGradientFill (juce::ColourGradient (pal::rustDeep.withAlpha (0.5f), p.getX(), p.getY(), juce::Colours::transparentBlack, p.getX(), p.getY() + p.getHeight() * 0.6f, false));
    g.fillRect (p);
    g.setColour (pal::rust); g.drawRect (p, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawRect (p.reduced (2.0f), 1.0f);
    for (auto pt : { juce::Point<float> (p.getX() + 8, p.getY() + 8), { p.getRight() - 8, p.getY() + 8 }, { p.getX() + 8, p.getBottom() - 8 }, { p.getRight() - 8, p.getBottom() - 8 } })
    {
        g.setColour (juce::Colour (0xff6a5a4c)); g.fillEllipse (pt.x - 3, pt.y - 3, 6, 6);
        g.setColour (pal::rust.withAlpha (0.6f)); g.drawEllipse (pt.x - 3, pt.y - 3, 6, 6, 1.0f);
    }
    auto r = panel.reduced (22);
    g.setColour (pal::signalHot); g.setFont (tex::stencil (19.0f).withExtraKerningFactor (0.1f));
    g.drawText (title, r.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (pal::paint); g.setFont (tex::plainBold (13.0f));
    g.drawFittedText (message, r.removeFromTop (hasField ? 22 : 50), juce::Justification::topLeft, 3);
}


