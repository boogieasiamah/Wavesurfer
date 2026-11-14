#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "BinaryData.h"
#include <map>
#include "../PluginProcessor.h"

// ==============================================================================
// WAVESURFER UI — dark navy glass, teal/blue accents, glowing knobs
// References: Gullfoss (curve display, red limit zones, meter column),
//             LANDR (navy gradient, teal spectrum fill, pill toggles),
//             audio-ui kit (soft halo knobs, big centre knob).
// ==============================================================================
namespace wsui {
using namespace juce;

namespace Pal {
    const Colour bg0     { 0xFF070C17 };
    const Colour bg1     { 0xFF0E1830 };
    const Colour panel   { 0xFF0A1122 };
    const Colour panelHi { 0xFF101A30 };
    const Colour border  { 0xFF1C2844 };
    const Colour grid    { 0xFF15203A };
    const Colour gridHi  { 0xFF22304F };
    const Colour text    { 0xFFDDE5F3 };
    const Colour dim     { 0xFF7381A0 };
    const Colour faint   { 0xFF3B4868 };
    const Colour teal    { 0xFF4FE3D0 };
    const Colour blue    { 0xFF4C7DFF };
    const Colour coral   { 0xFFFF5C7A };
    const Colour amber   { 0xFFFFC266 };
    const Colour pink    { 0xFFFF8CC6 };
}

// ------------------------------------------------------------------ theme ----
struct Theme {
    Typeface::Ptr regular, medium, semibold;
    Theme() {
        regular  = Typeface::createSystemTypefaceFor(BinaryData::InterRegular_otf,  BinaryData::InterRegular_otfSize);
        medium   = Typeface::createSystemTypefaceFor(BinaryData::InterMedium_otf,   BinaryData::InterMedium_otfSize);
        semibold = Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
    }
    Font font(const Typeface::Ptr& t, float h, float tracking = 0.0f) const {
        return Font(FontOptions(t).withHeight(h).withKerningFactor(tracking));
    }
    Font caps(float h)  const { return font(medium, h, 0.16f); }
    Font value(float h) const { return font(semibold, h, 0.02f); }
};

inline Point<float> polar(Point<float> c, float r, float ang) {
    return { c.x + r * std::sin(ang), c.y - r * std::cos(ang) };
}

// ----------------------------------------------------------- look & feel ----
class LnF : public LookAndFeel_V4 {
public:
    explicit LnF(Theme& t) : theme(t) {
        setColour(TooltipWindow::backgroundColourId, Pal::panelHi);
        setColour(TooltipWindow::textColourId, Pal::text);
        setColour(TooltipWindow::outlineColourId, Pal::border);
    }

    Typeface::Ptr getTypefaceForFont(const Font&) override { return theme.regular; }

    void drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos,
                          float start, float end, Slider& s) override {
        const bool bip = s.getProperties()["bipolar"];
        const bool hover = s.isMouseOverOrDragging();
        const auto area = Rectangle<float>((float) x, (float) y, (float) w, (float) h);
        const float d = jmin(area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float r = d * 0.5f * 0.84f;               // arc radius
        const float br = r * 0.74f;                     // body radius
        const float tw = jmax(2.0f, r * 0.075f);
        const float ang = start + pos * (end - start);

        // static layers (halo, track, ticks / shadow, body) are cached per size, scale and hover:
        // the blurred drop shadow alone cost more than the rest of the knob
        const float sc = jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        auto& layers = knobLayers(w, h, sc, bip, hover, start, end);
        const auto toArea = AffineTransform::scale(1.0f / sc).translated((float) x, (float) y);
        g.drawImageTransformed(layers.under, toArea);

        // value arc with glow (live)
        const float from = bip ? 0.5f * (start + end) : start;
        if (std::abs(ang - from) > 0.002f) {
            Path arc; arc.addCentredArc(c.x, c.y, r, r, 0.0f, jmin(from, ang), jmax(from, ang), true);
            const auto accA = bip && ang < from ? Pal::coral : Pal::blue;
            const auto accB = bip && ang < from ? Pal::amber : Pal::teal;
            g.setColour(accB.withAlpha(0.16f));
            g.strokePath(arc, PathStrokeType(tw * 3.2f, PathStrokeType::curved, PathStrokeType::rounded));
            ColourGradient grad(accA, polar(c, r, jmin(from, ang)), accB, polar(c, r, jmax(from, ang)), false);
            g.setGradientFill(grad);
            g.strokePath(arc, PathStrokeType(tw * 1.25f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        g.drawImageTransformed(layers.over, toArea);

        // pointer (live)
        const auto accent = bip && ang < from - 0.002f ? Pal::coral : Pal::teal;
        const auto p1 = polar(c, br * 0.36f, ang), p2 = polar(c, br * 0.80f, ang);
        g.setColour(accent.withAlpha(0.25f));
        g.drawLine(Line<float>(p1, p2), br * 0.16f);
        g.setColour(accent);
        Path ptr; ptr.startNewSubPath(p1); ptr.lineTo(p2);
        g.strokePath(ptr, PathStrokeType(jmax(1.6f, br * 0.07f), PathStrokeType::curved, PathStrokeType::rounded));
    }

    void drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) override {
        const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = b.getToggleState();
        const bool warn = b.getProperties()["warn"];
        const auto accA = warn ? Pal::coral : Pal::blue, accB = warn ? Pal::amber : Pal::teal;
        if (on) {
            g.setColour(accB.withAlpha(0.18f));
            g.fillRoundedRectangle(r.expanded(2.0f), rad + 2.0f);
            ColourGradient grad(accA, r.getTopLeft(), accB, r.getBottomRight(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(r, rad);
        } else {
            g.setColour(Pal::panelHi.brighter(over ? 0.12f : 0.0f).darker(down ? 0.2f : 0.0f));
            g.fillRoundedRectangle(r, rad);
            g.setColour(over ? Pal::faint.brighter(0.3f) : Pal::border.brighter(0.15f));
            g.drawRoundedRectangle(r, rad, 1.0f);
        }
    }

    void drawButtonText(Graphics& g, TextButton& b, bool, bool) override {
        const bool on = b.getToggleState();
        g.setFont(theme.font(theme.semibold, 11.5f, 0.08f));
        g.setColour(on ? Colour(0xFF06101F) : Pal::dim.brighter(b.isMouseOver() ? 0.4f : 0.0f));
        g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(), Justification::centred);
    }

private:
    struct KnobLayers { Image under, over; };

    KnobLayers& knobLayers(int w, int h, float sc, bool bip, bool hover, float start, float end) {
        const auto key = String(w) + "x" + String(h) + "@" + String(sc, 3) + (bip ? "b" : "u") + (hover ? "h" : "n");
        if (auto it = knobCache.find(key); it != knobCache.end()) return it->second;
        if (knobCache.size() > 48) knobCache.clear();             // resizes, Retina/non-Retina moves

        const int iw = jmax(1, (int) std::ceil((float) w * sc)), ih = jmax(1, (int) std::ceil((float) h * sc));
        KnobLayers L { Image(Image::ARGB, iw, ih, true), Image(Image::ARGB, iw, ih, true) };
        const auto area = Rectangle<float>(0.0f, 0.0f, (float) w, (float) h);
        const float d = jmin(area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float r = d * 0.5f * 0.84f, br = r * 0.74f, tw = jmax(2.0f, r * 0.075f);
        {
            Graphics g(L.under); g.addTransform(AffineTransform::scale(sc));
            ColourGradient halo(Colour(0xFFB8D4FF).withAlpha(0.13f + (hover ? 0.05f : 0.0f)), c,
                                Colours::transparentBlack, c.translated(r * 1.22f, 0), true);
            halo.addColour(0.55, Colour(0xFF7FA8FF).withAlpha(0.05f));
            g.setGradientFill(halo);
            g.fillEllipse(Rectangle<float>(r * 2.44f, r * 2.44f).withCentre(c));
            Path track; track.addCentredArc(c.x, c.y, r, r, 0.0f, start, end, true);
            g.setColour(Pal::gridHi.withAlpha(0.9f));
            g.strokePath(track, PathStrokeType(tw, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour(Pal::faint);
            for (float a : { start, end }) g.drawLine(Line<float>(polar(c, r + tw * 1.1f, a), polar(c, r + tw * 2.0f, a)), 1.2f);
            if (bip) g.drawLine(Line<float>(polar(c, r + tw * 1.1f, 0.5f * (start + end)), polar(c, r + tw * 2.2f, 0.5f * (start + end))), 1.4f);
        }
        {
            Graphics g(L.over); g.addTransform(AffineTransform::scale(sc));
            Path body; body.addEllipse(Rectangle<float>(br * 2, br * 2).withCentre(c));
            DropShadow(Colours::black.withAlpha(0.75f), (int) (br * 0.45f), { 0, (int) (br * 0.16f) }).drawForPath(g, body);
            ColourGradient face(Colour(0xFF2A3657), c.translated(-br * 0.35f, -br * 0.55f),
                                Colour(0xFF070B15), c.translated(br * 0.5f, br * 0.95f), true);
            g.setGradientFill(face);
            g.fillPath(body);
            ColourGradient rim(Colours::white.withAlpha(0.22f), c.translated(0, -br),
                               Colours::white.withAlpha(0.0f), c.translated(0, br * 0.4f), false);
            g.setGradientFill(rim);
            g.drawEllipse(Rectangle<float>(br * 2 - 1.2f, br * 2 - 1.2f).withCentre(c), 1.2f);
            const float ir = br * 0.80f;
            ColourGradient dish(Colours::black.withAlpha(0.28f), c.translated(0, -ir),
                                Colours::white.withAlpha(0.045f), c.translated(0, ir), false);
            g.setGradientFill(dish);
            g.fillEllipse(Rectangle<float>(ir * 2, ir * 2).withCentre(c));
        }
        return knobCache.emplace(key, std::move(L)).first->second;
    }

    Theme& theme;
    std::map<String, KnobLayers> knobCache;
};

// ------------------------------------------------------------------ knob ----
class Knob : public Component, public SettableTooltipClient {
public:
    enum class Size { Small, Medium, Big };

    Knob(Theme& t, AudioProcessorValueTreeState& state, const String& paramID, const String& labelText,
         Size sz, bool bipolar, const String& tip)
        : theme(t), label(labelText), size(sz)
    {
        slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters(MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity(sz == Size::Big ? 320 : 240);
        slider.setVelocityBasedMode(false);
        slider.getProperties().set("bipolar", bipolar);
        slider.setTooltip(tip);
        addAndMakeVisible(slider);
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(state, paramID, slider);
        if (auto* p = state.getParameter(paramID))
            slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        slider.onValueChange = [this] { repaint(); };
    }

    void resized() override {
        auto b = getLocalBounds();
        const int textH = size == Size::Big ? 46 : size == Size::Medium ? 40 : 32;
        slider.setBounds(b.removeFromTop(b.getHeight() - textH));
    }

    void paint(Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        const float textH = size == Size::Big ? 46.0f : size == Size::Medium ? 40.0f : 32.0f;
        auto t = b.removeFromBottom(textH);
        const float capH = size == Size::Big ? 11.5f : size == Size::Medium ? 10.5f : 9.5f;
        const float valH = size == Size::Big ? 17.0f : size == Size::Medium ? 14.5f : 12.5f;
        g.setFont(theme.caps(capH));
        g.setColour(Pal::dim);
        g.drawText(label.toUpperCase(), t.removeFromTop(capH + 6.0f), Justification::centredBottom);
        g.setFont(theme.value(valH));
        g.setColour(Pal::text);
        g.drawText(slider.getTextFromValue(slider.getValue()), t.removeFromTop(valH + 6.0f), Justification::centred);
    }

    Slider slider;

private:
    Theme& theme;
    String label;
    Size size;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ------------------------------------------------------------- segmented ----
// LANDR-style segmented switch bound to a choice parameter
class Segmented : public Component, public SettableTooltipClient {
public:
    Segmented(Theme& t, RangedAudioParameter& p, StringArray labels)
        : theme(t), names(std::move(labels)),
          attachment(p, [this](float v) { index = roundToInt(v); repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void paint(Graphics& g) override {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        const float rad = r.getHeight() * 0.5f;
        g.setColour(Pal::panelHi); g.fillRoundedRectangle(r, rad);
        g.setColour(Pal::border.brighter(0.15f)); g.drawRoundedRectangle(r, rad, 1.0f);
        const float w = r.getWidth() / (float) names.size();
        for (int i = 0; i < names.size(); ++i) {
            auto seg = Rectangle<float>(r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced(3.0f);
            const bool on = i == index;
            if (on) {
                g.setColour(Pal::teal.withAlpha(0.16f)); g.fillRoundedRectangle(seg.expanded(2.0f), seg.getHeight() * 0.5f + 2.0f);
                ColourGradient grad(Pal::blue, seg.getTopLeft(), Pal::teal, seg.getBottomRight(), false);
                g.setGradientFill(grad); g.fillRoundedRectangle(seg, seg.getHeight() * 0.5f);
            }
            g.setFont(theme.font(theme.semibold, 11.0f, 0.08f));
            g.setColour(on ? Colour(0xFF06101F) : Pal::dim);
            g.drawText(names[i].toUpperCase(), seg, Justification::centred);
        }
    }

    void mouseDown(const MouseEvent& e) override {
        const int i = jlimit(0, names.size() - 1, (int) ((float) e.x / ((float) getWidth() / (float) names.size())));
        attachment.setValueAsCompleteGesture((float) i);
    }

private:
    Theme& theme;
    StringArray names;
    ParameterAttachment attachment;
    int index = 0;
};

// ---------------------------------------------------------------- display ----
class Display : public Component {
public:
    static constexpr int NB = wavesurfer::WaveSurferEngine::NUM_BANDS;
    static constexpr int NPTS = 240;

    Display(Theme& t, WaveSurferProcessor& p) : theme(t), proc(p) {
        lowParam  = proc.apvts.getParameter("lowLimit");
        highParam = proc.apvts.getParameter("highLimit");
        std::fill(std::begin(spec), std::end(spec), -120.0f);
        std::fill(std::begin(curve), std::end(curve), 0.0f);
        for (auto& m : meter) m = -90.0f;
        for (auto& h : hold) h = -90.0f;
    }

    void setRefName(const String& n, bool analysing) { refName = n; refAnalysing = analysing; shownFlags = -1; }

    // called from the editor's timer (message thread)
    void update() {
        const auto& s = proc.getEngine().getSnapshot();
        for (int b = 0; b < NB; ++b) {
            freqs[b] = s.bandFreq[(size_t) b].load(std::memory_order_relaxed);
            const float target = s.bandLevelDb[(size_t) b].load(std::memory_order_relaxed);
            spec[b] += (target > spec[b] ? 0.55f : 0.18f) * (target - spec[b]);
        }
        for (int i = 0; i < NPTS; ++i) {
            const double f = 20.0 * std::pow(1000.0, i / (double) (NPTS - 1));
            const float target = jlimit(-18.0f, 18.0f, proc.curveDbAt(f));
            curve[i] += 0.45f * (target - curve[i]);
        }
        float in[2], out[2];
        proc.getEngine().takePeaks(in, out);
        const float pk[4] = { in[0], in[1], out[0], out[1] };
        for (int i = 0; i < 4; ++i) {
            const float db = Decibels::gainToDecibels(pk[i], -90.0f);
            meter[i] = db > meter[i] ? db : jmax(db, meter[i] - 1.6f);
            if (db >= hold[i]) { hold[i] = db; holdAge[i] = 0; }
            else if (++holdAge[i] > 45) hold[i] = jmax(meter[i], hold[i] - 1.2f);
        }
        gmDb = s.gainMatchDb.load(std::memory_order_relaxed);
        gmOn = *proc.apvts.getRawParameterValue("gainMatch") > 0.5f;
        deltaOn = *proc.apvts.getRawParameterValue("delta") > 0.5f;
        bypassOn = *proc.apvts.getRawParameterValue("bypass") > 0.5f;
        pinkMode = s.reference.load(std::memory_order_relaxed) == 1;
        trackMode = s.reference.load(std::memory_order_relaxed) == 2;
        refLoaded = s.refLoaded.load(std::memory_order_relaxed);
        for (int b = 0; b < NB; ++b) refDb[b] = s.refTargetDb[(size_t) b].load(std::memory_order_relaxed);
        topHz = s.sampleRate.load(std::memory_order_relaxed) * 0.45f;

        // repaint only when something visible moved (an idle or stopped session costs nothing)
        float moved = 0.0f;
        for (int b = 0; b < NB; ++b) moved = jmax(moved, std::abs(spec[b] - shownSpec[b]) * 0.1f);
        for (int i = 0; i < NPTS; i += 4) moved = jmax(moved, std::abs(curve[i] - shownCurve[i]));
        for (int i = 0; i < 4; ++i) moved = jmax(moved, std::abs(meter[i] - shownMeter[i]) * 0.1f, std::abs(hold[i] - shownHold[i]) * 0.1f);
        const int visFlags = (gmOn ? 1 : 0) | (deltaOn ? 2 : 0) | (bypassOn ? 4 : 0) | (pinkMode ? 8 : 0) | (trackMode ? 16 : 0) | (refLoaded ? 32 : 0);
        const float lo = lowHz(), hi = highHz();
        if (moved > 0.01f || visFlags != shownFlags || std::abs(gmDb - shownGm) > 0.05f
            || std::abs(lo - shownLo) > 0.01f || std::abs(hi - shownHi) > 0.01f) {
            std::copy(std::begin(spec), std::end(spec), std::begin(shownSpec));
            std::copy(std::begin(curve), std::end(curve), std::begin(shownCurve));
            std::copy(std::begin(meter), std::end(meter), std::begin(shownMeter));
            std::copy(std::begin(hold), std::end(hold), std::begin(shownHold));
            shownFlags = visFlags; shownGm = gmDb; shownLo = lo; shownHi = hi;
            repaint();
        }
    }

    void paint(Graphics& g) override {
        // static layer (panel, grid, axis labels, meter tracks) is drawn once per size and scale
        const float sc = jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (staticLayer.isNull() || std::abs(staticScale - sc) > 1.0e-3f || staticSize != getLocalBounds().getBottomRight()) {
            staticScale = sc; staticSize = getLocalBounds().getBottomRight();
            staticLayer = Image(Image::ARGB, jmax(1, (int) std::ceil((float) getWidth() * sc)), jmax(1, (int) std::ceil((float) getHeight() * sc)), true);
            Graphics sg(staticLayer); sg.addTransform(AffineTransform::scale(sc));
            drawStatic(sg);
        }
        g.drawImageTransformed(staticLayer, AffineTransform::scale(1.0f / sc));

        const auto plot = plotArea();
        {
            Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(plot.toNearestInt().expanded(0, 2));
            drawSpectrum(g, plot);
            if (pinkMode) drawPinkTarget(g, plot);
            if (trackMode && refLoaded) drawRefTarget(g, plot);
            drawLimits(g, plot);
            drawCurve(g, plot);
        }

        drawLimitReadouts(g, plot);
        drawBadges(g, plot);
        drawMeters(g);
    }

    void drawStatic(Graphics& g) {
        const auto bounds = getLocalBounds().toFloat();
        ColourGradient bg(Colour(0xFF0C1630), bounds.getTopLeft(), Colour(0xFF070D1A), bounds.getBottomLeft(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(bounds, 18.0f);
        ColourGradient sheen(Pal::blue.withAlpha(0.07f), bounds.getCentre().withY(bounds.getY()),
                             Colours::transparentBlack, bounds.getCentre().withY(bounds.getY() + bounds.getHeight() * 0.8f), true);
        g.setGradientFill(sheen);
        g.fillRoundedRectangle(bounds, 18.0f);
        g.setColour(Pal::border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 18.0f, 1.0f);
        const auto plot = plotArea();
        {
            Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(plot.toNearestInt().expanded(0, 2));
            drawGrid(g, plot);
        }
        drawAxisLabels(g, plot);
        drawMeterFrame(g);
    }

    // ---- limit handles ------------------------------------------------------
    void mouseMove(const MouseEvent& e) override {
        setMouseCursor(hitHandle(e.position) != 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
    }
    void mouseDown(const MouseEvent& e) override {
        dragging = hitHandle(e.position);
        if (auto* p = dragParam()) p->beginChangeGesture();
        mouseDrag(e);
    }
    void mouseDrag(const MouseEvent& e) override {
        if (auto* p = dragParam()) p->setValueNotifyingHost(p->convertTo0to1((float) freqAtX(e.position.x)));
    }
    void mouseUp(const MouseEvent&) override {
        if (auto* p = dragParam()) p->endChangeGesture();
        dragging = 0;
    }
    void mouseDoubleClick(const MouseEvent& e) override {
        const int h = hitHandle(e.position);
        auto* p = h < 0 ? lowParam : h > 0 ? highParam : nullptr;
        if (p) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture(); }
    }

private:
    Rectangle<float> plotArea() const { return getLocalBounds().toFloat().withTrimmedLeft(46).withTrimmedRight(104).withTrimmedTop(18).withTrimmedBottom(30); }
    float xOf(double f) const { const auto p = plotArea(); return p.getX() + p.getWidth() * (float) (std::log(f / 20.0) / std::log(1000.0)); }
    double freqAtX(float x) const { const auto p = plotArea(); return 20.0 * std::pow(1000.0, jlimit(0.0, 1.0, (double) ((x - p.getX()) / p.getWidth()))); }
    float yEq(float db) const { const auto p = plotArea(); return p.getCentreY() - db / 15.0f * p.getHeight() * 0.5f; }
    float ySpec(float dbfs) const { const auto p = plotArea(); return p.getBottom() - jlimit(0.0f, 1.0f, (dbfs + 90.0f) / 90.0f) * p.getHeight() * 0.94f; }
    float lowHz()  const { return lowParam  ? lowParam->convertFrom0to1(lowParam->getValue())   : 20.0f; }
    float highHz() const { return highParam ? highParam->convertFrom0to1(highParam->getValue()) : 20000.0f; }
    RangedAudioParameter* dragParam() const { return dragging < 0 ? lowParam : dragging > 0 ? highParam : nullptr; }

    int hitHandle(Point<float> pt) const {
        const auto p = plotArea();
        if (! p.expanded(10, 4).contains(pt)) return 0;
        const float lx = xOf(jmin(lowHz(), highHz())), hx = xOf(jmax(lowHz(), highHz()));
        const float dl = std::abs(pt.x - lx), dh = std::abs(pt.x - hx);
        if (dl < 10 && dl <= dh) return -1;
        if (dh < 10) return 1;
        return 0;
    }

    void drawGrid(Graphics& g, Rectangle<float> p) {
        for (double dec : { 10.0, 100.0, 1000.0, 10000.0 })
            for (int m = 1; m <= 9; ++m) {
                const double f = dec * m;
                if (f < 20.0 || f > 20000.0) continue;
                const bool major = m == 1;
                g.setColour(major ? Pal::gridHi : Pal::grid);
                g.drawVerticalLine(roundToInt(xOf(f)), p.getY(), p.getBottom());
            }
        for (int db = -12; db <= 12; db += 6) {
            g.setColour(db == 0 ? Pal::gridHi.brighter(0.25f) : Pal::grid);
            g.drawHorizontalLine(roundToInt(yEq((float) db)), p.getX(), p.getRight());
        }
    }

    Path smoothPath(const std::vector<Point<float>>& pts) const {
        Path path;
        if (pts.size() < 2) return path;
        path.startNewSubPath(pts[0]);
        for (size_t i = 0; i + 1 < pts.size(); ++i) {   // Catmull-Rom -> cubic
            const auto p0 = pts[i == 0 ? 0 : i - 1], p1 = pts[i], p2 = pts[i + 1], p3 = pts[jmin(i + 2, pts.size() - 1)];
            path.cubicTo(p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
        }
        return path;
    }

    void drawSpectrum(Graphics& g, Rectangle<float> p) {
        std::vector<Point<float>> pts;
        pts.push_back({ p.getX(), ySpec(spec[0] - 6.0f) });
        for (int b = 0; b < NB; ++b)
            if (freqs[b] > 0.0f && freqs[b] <= 20000.0f) pts.push_back({ xOf(freqs[b]), ySpec(spec[b]) });
        pts.push_back({ jmin(p.getRight(), xOf(jmin(20000.0f, topHz)) + 4.0f), p.getBottom() });
        Path line = smoothPath(pts);
        Path fill = line;
        fill.lineTo(pts.back().x, p.getBottom()); fill.lineTo(p.getX(), p.getBottom()); fill.closeSubPath();
        ColourGradient grad(Pal::teal.withAlpha(0.30f), 0, p.getY() + p.getHeight() * 0.25f,
                            Pal::blue.withAlpha(0.03f), 0, p.getBottom(), false);
        g.setGradientFill(grad);
        g.fillPath(fill);
        g.setColour(Pal::teal.withAlpha(0.35f));
        g.strokePath(line, PathStrokeType(1.2f));
    }

    // Pink mode: the flat line the long-term balance is pulled toward (mean level inside the limits)
    void drawPinkTarget(Graphics& g, Rectangle<float>) {
        const float lo = jmin(lowHz(), highHz()), hi = jmax(lowHz(), highHz());
        float sum = 0.0f; int n = 0;
        for (int b = 0; b < NB; ++b) if (freqs[b] >= lo && freqs[b] <= hi && spec[b] > -100.0f) { sum += spec[b]; ++n; }
        if (n == 0) return;
        const float y = ySpec(sum / (float) n);
        const float dash[] = { 6.0f, 5.0f };
        g.setColour(Pal::pink.withAlpha(0.75f));
        g.drawDashedLine(Line<float>(xOf(lo), y, xOf(hi), y), dash, 2, 1.4f);
        g.setFont(theme.font(theme.semibold, 9.5f, 0.12f));
        g.drawText("PINK TARGET", Rectangle<float>(xOf(lo) + 10, y - 16, 90, 12), Justification::centredLeft);
    }

    // Ref mode: the reference song's long-term balance, placed at the mix's mean level inside the limits
    void drawRefTarget(Graphics& g, Rectangle<float>) {
        const float lo = jmin(lowHz(), highHz()), hi = jmax(lowHz(), highHz());
        float sum = 0.0f, rs = 0.0f; int n = 0;
        for (int b = 0; b < NB; ++b) if (freqs[b] >= lo && freqs[b] <= hi && spec[b] > -100.0f) { sum += spec[b]; rs += refDb[b]; ++n; }
        if (n == 0) return;
        const float off = (sum - rs) / (float) n;
        std::vector<Point<float>> pts;
        for (int b = 0; b < NB; ++b) if (freqs[b] > 0.0f && freqs[b] <= 20000.0f) pts.push_back({ xOf(freqs[b]), ySpec(off + refDb[b]) });
        Path line = smoothPath(pts), dashed;
        const float dash[] = { 7.0f, 5.0f };
        PathStrokeType(1.6f).createDashedStroke(dashed, line, dash, 2);
        g.setColour(Pal::amber.withAlpha(0.85f)); g.fillPath(dashed);
        g.setFont(theme.font(theme.semibold, 9.5f, 0.12f));
        if (! pts.empty()) g.drawText("REF TARGET", Rectangle<float>(pts[pts.size() / 3].x + 8, pts[pts.size() / 3].y - 18, 90, 12), Justification::centredLeft);
    }

    void drawLimits(Graphics& g, Rectangle<float> p) {
        const float lx = xOf(jmin(lowHz(), highHz())), hx = xOf(jmax(lowHz(), highHz()));
        if (lx > p.getX() + 0.5f) {
            ColourGradient zl(Pal::coral.withAlpha(0.20f), p.getX(), 0, Pal::coral.withAlpha(0.07f), lx, 0, false);
            g.setGradientFill(zl); g.fillRect(Rectangle<float>(p.getX(), p.getY(), lx - p.getX(), p.getHeight()));
        }
        if (hx < p.getRight() - 0.5f) {
            ColourGradient zh(Pal::coral.withAlpha(0.07f), hx, 0, Pal::coral.withAlpha(0.20f), p.getRight(), 0, false);
            g.setGradientFill(zh); g.fillRect(Rectangle<float>(hx, p.getY(), p.getRight() - hx, p.getHeight()));
        }
        for (float x : { lx, hx }) {
            g.setColour(Pal::coral.withAlpha(0.85f));
            g.drawLine(x, p.getY(), x, p.getBottom(), 1.3f);
            auto grip = Rectangle<float>(8.0f, 26.0f).withCentre({ jlimit(p.getX() + 4.0f, p.getRight() - 4.0f, x), p.getY() + 20.0f });
            g.setColour(Pal::coral.withAlpha(0.18f)); g.fillRoundedRectangle(grip.expanded(3.0f), 6.0f);
            g.setColour(Pal::coral); g.fillRoundedRectangle(grip, 4.0f);
            g.setColour(Colour(0xFF3A0F1C));
            for (int k = -1; k <= 1; ++k) g.drawHorizontalLine(roundToInt(grip.getCentreY() + (float) k * 4.0f), grip.getX() + 2.0f, grip.getRight() - 2.0f);
        }
    }

    void drawCurve(Graphics& g, Rectangle<float> p) {
        Path line;
        for (int i = 0; i < NPTS; ++i) {
            const float x = p.getX() + p.getWidth() * (float) i / (float) (NPTS - 1);
            const float y = yEq(curve[i]);
            if (i == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
        }
        const float y0 = yEq(0.0f);
        Path fill = line; fill.lineTo(p.getRight(), y0); fill.lineTo(p.getX(), y0); fill.closeSubPath();
        const float alpha = bypassOn ? 0.35f : 1.0f;
        {
            Graphics::ScopedSaveState s(g);
            g.reduceClipRegion(Rectangle<float>(p.getX(), p.getY(), p.getWidth(), y0 - p.getY()).toNearestInt());
            ColourGradient up(Pal::teal.withAlpha(0.30f * alpha), 0, p.getY(), Pal::teal.withAlpha(0.04f * alpha), 0, y0, false);
            g.setGradientFill(up); g.fillPath(fill);
        }
        {
            Graphics::ScopedSaveState s(g);
            g.reduceClipRegion(Rectangle<float>(p.getX(), y0, p.getWidth(), p.getBottom() - y0).toNearestInt());
            ColourGradient dn(Pal::coral.withAlpha(0.04f * alpha), 0, y0, Pal::coral.withAlpha(0.30f * alpha), 0, p.getBottom(), false);
            g.setGradientFill(dn); g.fillPath(fill);
        }
        Path glow;                                             // the wide glow doesn't need every point
        for (int i = 0; i < NPTS; i += 3) {
            const float x = p.getX() + p.getWidth() * (float) i / (float) (NPTS - 1), y = yEq(curve[i]);
            if (i == 0) glow.startNewSubPath(x, y); else glow.lineTo(x, y);
        }
        glow.lineTo(p.getRight(), yEq(curve[NPTS - 1]));
        g.setColour(Pal::teal.withAlpha(0.10f * alpha)); g.strokePath(glow, PathStrokeType(9.0f, PathStrokeType::beveled, PathStrokeType::rounded));
        g.setColour(Pal::teal.withAlpha(0.22f * alpha)); g.strokePath(glow, PathStrokeType(4.0f, PathStrokeType::beveled, PathStrokeType::rounded));
        ColourGradient core(Colours::white.withAlpha(alpha), p.getX(), 0, Pal::teal.brighter(0.3f).withAlpha(alpha), p.getRight(), 0, false);
        core.addColour(0.5, Colour(0xFFBFF6EE).withAlpha(alpha));
        g.setGradientFill(core); g.strokePath(line, PathStrokeType(1.9f, PathStrokeType::beveled, PathStrokeType::rounded));
    }

    void drawAxisLabels(Graphics& g, Rectangle<float> p) {
        g.setFont(theme.font(theme.medium, 10.0f, 0.04f));
        g.setColour(Pal::dim.withAlpha(0.85f));
        const std::pair<double, const char*> fl[] = { {20,"20"},{50,"50"},{100,"100"},{200,"200"},{500,"500"},{1000,"1k"},{2000,"2k"},{5000,"5k"},{10000,"10k"},{20000,"20k"} };
        for (auto& [f, s] : fl) {
            const float x = xOf(f);
            g.drawText(s, Rectangle<float>(x - 20, p.getBottom() + 7, 40, 14), Justification::centred);
        }
        for (int db = -12; db <= 12; db += 6) {
            const String s = db > 0 ? "+" + String(db) : String(db);
            g.drawText(s, Rectangle<float>(4, yEq((float) db) - 7, 34, 14), Justification::centredRight);
        }
        g.setColour(Pal::faint);
        g.drawText("dB", Rectangle<float>(4, p.getY() - 16, 34, 12), Justification::centredRight);
    }

    void drawLimitReadouts(Graphics& g, Rectangle<float> p) {
        g.setFont(theme.font(theme.semibold, 10.0f, 0.06f));
        g.setColour(Pal::coral);
        auto fmt = [](float hz) { return hz >= 1000.0f ? String(hz / 1000.0f, 1) + " kHz" : String(roundToInt(hz)) + " Hz"; };
        const float lx = xOf(jmin(lowHz(), highHz())), hx = xOf(jmax(lowHz(), highHz()));
        g.drawText(fmt(jmin(lowHz(), highHz())), Rectangle<float>(lx + 9, p.getY() + 8, 70, 14), Justification::centredLeft);
        g.drawText(fmt(jmax(lowHz(), highHz())), Rectangle<float>(hx - 79, p.getY() + 8, 70, 14), Justification::centredRight);
    }

    void drawBadge(Graphics& g, Rectangle<float> r, const String& s, Colour c) {
        g.setColour(c.withAlpha(0.16f)); g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(c.withAlpha(0.8f)); g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
        g.setColour(c); g.drawText(s, r, Justification::centred);
    }

    void drawBadges(Graphics& g, Rectangle<float> p) {
        g.setFont(theme.font(theme.semibold, 9.5f, 0.12f));
        float x = p.getRight() - 14.0f;            // badges sit bottom-right, clear of the low handle
        if (gmOn) {
            const String s = "LOUDNESS " + String(gmDb >= 0 ? "+" : "") + String(gmDb, 1) + " dB";
            x -= 132; drawBadge(g, { x, p.getBottom() - 28, 132, 18 }, s, Pal::teal); x -= 8;
        }
        if (deltaOn) { x -= 64; drawBadge(g, { x, p.getBottom() - 28, 64, 18 }, "DELTA", Pal::amber); x -= 8; }
        if (trackMode) {
            const String s = refAnalysing ? "ANALYSING REFERENCE" : refLoaded ? "REF  " + refName.substring(0, 28).toUpperCase() : "NO REFERENCE - LOAD ONE";
            const float w = jmin(260.0f, 26.0f + 6.6f * (float) s.length());
            x -= w; drawBadge(g, { x, p.getBottom() - 28, w, 18 }, s, Pal::amber);
        }
        if (bypassOn) {
            g.setFont(theme.font(theme.semibold, 13.0f, 0.4f));
            g.setColour(Pal::text.withAlpha(0.55f));
            g.drawText("BYPASSED", p.withSizeKeepingCentre(220, 30).translated(0, -p.getHeight() * 0.28f), Justification::centred);
        }
    }

    Rectangle<float> meterColumn() const {
        const auto b = getLocalBounds().toFloat();
        return Rectangle<float>(b.getRight() - 92, b.getY() + 18, 74, b.getHeight() - 48);
    }

    void drawMeterFrame(Graphics& g) {
        const auto col = meterColumn();
        g.setColour(Pal::border); g.drawVerticalLine(roundToInt(col.getX() - 6), col.getY(), col.getBottom());
        auto bars = col.withTrimmedBottom(26).withTrimmedTop(2);
        auto yOfDb = [&](float db) { return bars.getBottom() - jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f) * bars.getHeight(); };
        const float xs[4] = { col.getX() + 6, col.getX() + 15, col.getX() + 53, col.getX() + 62 };
        for (int i = 0; i < 4; ++i) { g.setColour(Pal::grid); g.fillRoundedRectangle(Rectangle<float>(xs[i], bars.getY(), 6.0f, bars.getHeight()), 3.0f); }
        g.setFont(theme.font(theme.regular, 8.5f));
        for (int db : { 0, -12, -24, -48 }) {
            g.setColour(Pal::faint);
            g.drawText(String(db), Rectangle<float>(col.getX() + 23, yOfDb((float) db) - 6, 28, 12), Justification::centred);
        }
        g.setFont(theme.caps(9.0f)); g.setColour(Pal::dim);
        g.drawText("IN", Rectangle<float>(col.getX() - 4, col.getBottom() - 22, 37, 12), Justification::centred);
        g.drawText("OUT", Rectangle<float>(col.getX() + 43, col.getBottom() - 22, 37, 12), Justification::centred);
    }

    void drawMeters(Graphics& g) {
        const auto col = meterColumn();
        auto bars = col.withTrimmedBottom(26).withTrimmedTop(2);
        auto yOfDb = [&](float db) { return bars.getBottom() - jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f) * bars.getHeight(); };
        const float bw = 6.0f;
        const float xs[4] = { col.getX() + 6, col.getX() + 15, col.getX() + 53, col.getX() + 62 };
        for (int i = 0; i < 4; ++i) {
            auto track = Rectangle<float>(xs[i], bars.getY(), bw, bars.getHeight());
            const float top = yOfDb(meter[i]);
            if (top < bars.getBottom() - 0.5f) {
                // colour stops by level: teal -> light blue (-12) -> amber (-3) -> coral (0 dBFS and up)
                ColourGradient grad(Pal::teal, 0, bars.getBottom(), Pal::coral, 0, bars.getY(), false);
                auto at = [](float db) { return (double) ((db + 60.0f) / 66.0f); };
                grad.addColour(at(-12.0f), Pal::blue.brighter(0.5f));
                grad.addColour(at(-3.0f), Pal::amber);
                grad.addColour(at(0.0f), Pal::coral);
                g.setGradientFill(grad);
                g.fillRoundedRectangle(track.withTop(top), 3.0f);
            }
            const float hy = yOfDb(hold[i]);
            if (hold[i] > -59.0f) { g.setColour(hold[i] > 0.0f ? Pal::coral : Pal::text.withAlpha(0.8f)); g.fillRect(xs[i], hy - 1.0f, bw, 2.0f); }
        }
        g.setFont(theme.value(10.0f)); g.setColour(Pal::text.withAlpha(0.85f));
        auto txt = [](float db) { return db <= -59.5f ? String("-inf") : String(db, 1); };
        g.drawText(txt(jmax(hold[0], hold[1])), Rectangle<float>(col.getX() - 4, col.getBottom() - 10, 37, 12), Justification::centred);
        g.drawText(txt(jmax(hold[2], hold[3])), Rectangle<float>(col.getX() + 43, col.getBottom() - 10, 37, 12), Justification::centred);
    }

    Theme& theme;
    WaveSurferProcessor& proc;
    RangedAudioParameter* lowParam = nullptr;
    RangedAudioParameter* highParam = nullptr;
    float freqs[NB] {}, spec[NB] {}, curve[NPTS] {};
    float meter[4] {}, hold[4] {}; int holdAge[4] {};
    float gmDb = 0.0f, topHz = 20000.0f;
    bool gmOn = false, deltaOn = false, bypassOn = false, pinkMode = false, trackMode = false, refLoaded = false, refAnalysing = false;
    float refDb[NB] {}; String refName;
    int dragging = 0;
    // what is on screen now (repaint only when it changes) and the cached static layer
    float shownSpec[NB] {}, shownCurve[NPTS] {}, shownMeter[4] {}, shownHold[4] {};
    float shownGm = 0.0f, shownLo = -1.0f, shownHi = -1.0f; int shownFlags = -1;
    Image staticLayer; float staticScale = 0.0f; Point<int> staticSize;
};

// ------------------------------------------------------------------- logo ----
inline void drawLogo(Graphics& g, Rectangle<float> r) {
    ColourGradient grad(Pal::blue, r.getTopLeft(), Pal::teal, r.getBottomRight(), false);
    g.setColour(Pal::teal.withAlpha(0.18f)); g.fillRoundedRectangle(r.expanded(3), 11.0f);
    g.setGradientFill(grad); g.fillRoundedRectangle(r, 9.0f);
    // a breaking wave: rising swell that curls over
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    Path wave;
    wave.startNewSubPath(x + w * 0.14f, y + h * 0.72f);
    wave.cubicTo(x + w * 0.34f, y + h * 0.72f, x + w * 0.36f, y + h * 0.28f, x + w * 0.60f, y + h * 0.27f);
    wave.cubicTo(x + w * 0.80f, y + h * 0.26f, x + w * 0.88f, y + h * 0.44f, x + w * 0.78f, y + h * 0.53f);
    wave.cubicTo(x + w * 0.70f, y + h * 0.60f, x + w * 0.58f, y + h * 0.52f, x + w * 0.64f, y + h * 0.44f);
    g.setColour(Colour(0xFF06101F));
    g.strokePath(wave, PathStrokeType(w * 0.095f, PathStrokeType::curved, PathStrokeType::rounded));
    Path base; base.startNewSubPath(x + w * 0.14f, y + h * 0.82f); base.lineTo(x + w * 0.86f, y + h * 0.82f);
    g.strokePath(base, PathStrokeType(w * 0.06f, PathStrokeType::curved, PathStrokeType::rounded));
}

// --------------------------------------------------- centre-zero meter ----
// Gullfoss-style activity meter: left value grows left, right value grows right.
inline void drawCentreMeter(Graphics& g, const Theme& th, Rectangle<float> r, const String& leftLabel, const String& rightLabel,
                            float leftAmount, float rightAmount, Colour leftCol, Colour rightCol) {
    g.setFont(th.caps(8.5f));
    g.setColour(Pal::faint.brighter(0.2f));
    g.drawText(leftLabel,  r.withHeight(11), Justification::centredLeft);
    g.drawText(rightLabel, r.withHeight(11), Justification::centredRight);
    auto track = Rectangle<float>(r.getX(), r.getBottom() - 5.0f, r.getWidth(), 4.0f);
    g.setColour(Pal::grid); g.fillRoundedRectangle(track, 2.0f);
    const float cx = track.getCentreX(), half = track.getWidth() * 0.5f;
    const float l = jlimit(0.0f, 1.0f, leftAmount) * half, rr = jlimit(0.0f, 1.0f, rightAmount) * half;
    if (l > 0.5f)  { g.setColour(leftCol);  g.fillRoundedRectangle(track.withX(cx - l).withWidth(l), 2.0f); }
    if (rr > 0.5f) { g.setColour(rightCol); g.fillRoundedRectangle(track.withX(cx).withWidth(rr), 2.0f); }
    g.setColour(Pal::text.withAlpha(0.6f));
    g.fillRect(cx - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f);
}

} // namespace wsui
