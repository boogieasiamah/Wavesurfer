#include "PluginEditor.h"

using namespace juce;
using wsui::Knob;
namespace Pal = wsui::Pal;

WaveSurferEditor::WaveSurferEditor(WaveSurferProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&lnf);
    addAndMakeVisible(canvas);
    canvas.setLookAndFeel(&lnf);
    canvas.addAndMakeVisible(display);

    auto& s = proc.apvts;
    auto make = [&](const char* id, const char* label, Knob::Size sz, bool bip, const char* tip) {
        auto k = std::make_unique<Knob>(theme, s, id, label, sz, bip, tip);
        canvas.addAndMakeVisible(*k);
        return k;
    };
    recover   = make("recover",   "Recover",   Knob::Size::Medium, false, "Lifts frequencies that are masked by their neighbours.");
    tame      = make("tame",      "Tame",      Knob::Size::Medium, false, "Cuts frequencies that dominate and mask the rest (resonances).");
    amount    = make("amount",    "Amount",    Knob::Size::Big,    false, "Scales the whole correction curve. 0% is a bit-exact bypass.");
    bias      = make("bias",      "Bias",      Knob::Size::Medium, true,  "Left favours Tame, right favours Recover.");
    brighten  = make("brighten",  "Brighten",  Knob::Size::Medium, true,  "Perceptual tilt: darker to the left, brighter to the right.");
    boost     = make("boost",     "Boost",     Knob::Size::Small,  true,  "Listening level: shifts the balance the way the ear hears louder (+) or quieter (-) playback. Global.");
    output    = make("output",    "Output",    Knob::Size::Small,  true,  "Output gain, after loudness match.");
    lowLimit  = make("lowLimit",  "Low",       Knob::Size::Small,  false, "Lowest frequency WaveSurfer may touch (or drag the red handle).");
    highLimit = make("highLimit", "High",      Knob::Size::Small,  false, "Highest frequency WaveSurfer may touch (or drag the red handle).");

    auto pill = [&](TextButton& b, const char* id, bool warn, const char* tip) {
        b.setClickingTogglesState(true);
        b.getProperties().set("warn", warn);
        b.setTooltip(tip);
        canvas.addAndMakeVisible(b);
        buttonAttachments.push_back(std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(s, id, b));
    };
    pill(lookaheadBtn, "lookahead", false, "20 ms lookahead: smoother on transients, adds 20 ms latency (reported to the host).");
    pill(gainMatchBtn, "gainMatch", false, "Keeps perceived (K-weighted) loudness equal to the input, for honest A/B.");
    if (auto* refParam = s.getParameter("reference")) {
        reference = std::make_unique<wsui::Segmented>(theme, *refParam, StringArray { "Ear  ISO 226", "Pink Noise" });
        reference->setTooltip("Ear: unmask by how we hear (ISO 226 + masking). Pink: pull the long-term balance toward pink noise.");
        canvas.addAndMakeVisible(*reference);
    }
    pill(deltaBtn,     "delta",     false, "Listen only to what WaveSurfer changes.");
    pill(clipBtn,      "clip",      false, "Transparent safety clipper on the output (unity below -0.9 dBFS).");
    pill(bypassBtn,    "bypass",    true,  "Click-free bypass.");

    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double) baseW / baseH);
    setResizeLimits(baseW * 7 / 10, baseH * 7 / 10, baseW * 3 / 2, baseH * 3 / 2);
    setSize(baseW, baseH);
    startTimerHz(30);
}

WaveSurferEditor::~WaveSurferEditor()
{
    stopTimer();
    canvas.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void WaveSurferEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseW;
    canvas.setBounds(0, 0, baseW, baseH);
    canvas.setTransform(AffineTransform::scale(scale));

    // header: reference switch + pills (right aligned)
    if (reference) reference->setBounds(338, 22, 228, 30);
    int x = baseW - 28;
    for (auto* b : { &bypassBtn, &clipBtn, &deltaBtn, &gainMatchBtn, &lookaheadBtn }) {
        const int w = b == &bypassBtn ? 78 : b == &clipBtn ? 62 : b == &deltaBtn ? 70 : b == &gainMatchBtn ? 96 : 106;
        x -= w;
        b->setBounds(x, 22, w, 30);
        x -= b == &bypassBtn ? 16 : 8;
    }

    display.setBounds(24, 72, baseW - 48, 320);

    // control deck: limits | recover tame | AMOUNT | bias brighten | boost + match
    const int deckY = 406;
    auto place = [](Knob& k, int cx, int top, int w, int h) { k.setBounds(cx - w / 2, top, w, h); };
    place(*lowLimit,  100, deckY + 20,  90, 100);
    place(*highLimit, 100, deckY + 124, 90, 100);
    place(*recover,   262, deckY + 30, 120, 150);
    place(*tame,      392, deckY + 30, 120, 150);
    place(*amount,    530, deckY + 8,  200, 206);
    place(*bias,      668, deckY + 30, 120, 150);
    place(*brighten,  798, deckY + 30, 120, 150);
    place(*boost,     960, deckY + 20,  90, 100);
    place(*output,    960, deckY + 124, 90, 100);
}

void WaveSurferEditor::refresh()
{
    display.update();
    const auto& snap = proc.getEngine().getSnapshot();
    const float rec = snap.recoverDb.load(std::memory_order_relaxed);
    const float tam = snap.tameDb.load(std::memory_order_relaxed);
    const float bri = snap.brightDelta.load(std::memory_order_relaxed);
    if (std::abs(rec - recShown) > 0.02f || std::abs(tam - tameShown) > 0.02f) {
        recShown = rec; tameShown = tam; canvas.repaint(200, 592, 260, 34);
    }
    if (std::abs(bri - brightShown) > 0.004f) { brightShown = bri; canvas.repaint(600, 592, 260, 34); }
    const float fsNow = snap.sampleRate.load(std::memory_order_relaxed);
    const int ms = roundToInt(1000.0f * (float) snap.latency.load(std::memory_order_relaxed) / jmax(1.0f, fsNow));
    if (ms != latencyMsShown) { latencyMsShown = ms; canvas.repaint(baseW - 460, baseH - 26, 440, 20); }
}

void WaveSurferEditor::Canvas::paint(Graphics& g)
{
    auto& th = ed.theme;
    const auto b = getLocalBounds().toFloat();

    // background: deep navy with a soft top bloom
    ColourGradient bg(Pal::bg1, 0, 0, Pal::bg0, 0, b.getBottom(), false);
    g.setGradientFill(bg);
    g.fillAll();
    ColourGradient bloom(Pal::teal.withAlpha(0.06f), b.getCentreX(), -40.0f,
                         Colours::transparentBlack, b.getCentreX(), 360.0f, true);
    g.setGradientFill(bloom);
    g.fillRect(b);

    // header
    wsui::drawLogo(g, { 28.0f, 21.0f, 32.0f, 32.0f });
    g.setColour(Pal::text);
    g.setFont(th.font(th.semibold, 21.0f, 0.30f));
    g.drawText("WAVESURFER", Rectangle<float>(74, 17, 260, 26), Justification::centredLeft);
    g.setFont(th.caps(9.5f));
    g.setColour(Pal::dim);
    g.drawText("PERCEPTUAL SPECTRAL UNMASKER", Rectangle<float>(75, 41, 300, 14), Justification::centredLeft);

    // control deck panel
    const auto deck = Rectangle<float>(24, 406, (float) baseW - 48, 230);
    ColourGradient dg(Pal::panelHi, deck.getTopLeft(), Pal::panel, deck.getBottomLeft(), false);
    g.setGradientFill(dg);
    g.fillRoundedRectangle(deck, 18.0f);
    g.setColour(Pal::border);
    g.drawRoundedRectangle(deck.reduced(0.5f), 18.0f, 1.0f);
    ColourGradient topLine(Colours::transparentWhite, deck.getX(), 0, Colours::transparentWhite, deck.getRight(), 0, false);
    topLine.addColour(0.5, Colours::white.withAlpha(0.10f));
    g.setGradientFill(topLine);
    g.fillRect(deck.getX() + 20, deck.getY() + 0.5f, deck.getWidth() - 40, 1.0f);

    // group separators + captions
    g.setColour(Pal::border);
    for (float x : { 172.0f, 888.0f }) g.drawVerticalLine((int) x, deck.getY() + 22, deck.getBottom() - 22);
    g.setFont(th.caps(9.0f));
    g.setColour(Pal::faint);
    g.drawText("LIMITS",  Rectangle<float>(40, deck.getY() + 10, 120, 12), Justification::centred);
    g.drawText("UNMASK",  Rectangle<float>(262, deck.getY() + 14, 130, 12), Justification::centred);
    g.drawText("BALANCE", Rectangle<float>(668, deck.getY() + 14, 130, 12), Justification::centred);
    g.drawText("OUTPUT",  Rectangle<float>(900, deck.getY() + 10, 120, 12), Justification::centred);

    // activity meters (Gullfoss-style): Recover vs Tame, perceived brightness shift
    wsui::drawCentreMeter(g, th, { 212, 598, 232, 22 }, "TAME", "RECOVER",
                          -ed.tameShown / 6.0f, ed.recShown / 6.0f, Pal::coral, Pal::teal);
    wsui::drawCentreMeter(g, th, { 618, 598, 232, 22 }, "DARKER", "BRIGHTER",
                          -ed.brightShown / 1.2f, ed.brightShown / 1.2f, Pal::blue.brighter(0.4f), Pal::amber);

    // footer
    g.setFont(th.caps(8.5f));
    g.setColour(Pal::faint);
    g.drawText("QUANTUM RESEARCH AUDIO", Rectangle<float>(30, (float) baseH - 22, 300, 14), Justification::centredLeft);
    const String lat = ed.latencyMsShown > 0 ? "LATENCY " + String(ed.latencyMsShown) + " MS" : String("ZERO LATENCY");
   #ifndef WAVESURFER_VERSION
    #define WAVESURFER_VERSION "dev"
   #endif
    g.drawText("WAVESURFER " + String(WAVESURFER_VERSION) + String(CharPointer_UTF8("  \xc2\xb7  ")) + lat + String(CharPointer_UTF8("  \xc2\xb7  LINKED STEREO")),
               Rectangle<float>((float) baseW - 430, (float) baseH - 22, 400, 14), Justification::centredRight);
}
