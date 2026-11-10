// Renders the WaveSurfer editor to a PNG after feeding it a synthetic mix.
// Usage: WaveSurferSnapshot out.png [scale]
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "wavesurfer.png");
    const float scale = argc > 2 ? (float) std::atof(argv[2]) : 2.0f;

    WaveSurferProcessor proc;
    const double fs = 48000.0; const int bs = 512;
    proc.setPlayConfigDetails(2, 2, fs, bs);
    proc.prepareToPlay(fs, bs);

    auto set = [&](const char* id, float v) {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    set("recover", 0.85f); set("tame", 0.78f); set("bias", 0.30f); set("brighten", 0.25f);
    set("amount", 0.90f);  set("boost", 0.0f); set("lowLimit", 42.0f); set("highLimit", 16000.0f);
    set("gainMatch", 1.0f);

    // synthetic mix: kick, bass, pink bed, boxy 450 Hz + harsh 2.6 kHz resonances, hats
    juce::Random rng(7);
    float pk[2][7] {}; double t = 0.0;
    auto mixSample = [&](int ch) {
        const double tw = juce::MathConstants<double>::twoPi;
        const double beat = std::fmod(t, 0.5), hat = std::fmod(t + 0.0625, 0.125);
        float kick = (float) (0.75 * std::exp(-beat * 14.0) * std::sin(tw * (50.0 + 90.0 * std::exp(-beat * 30.0)) * beat));
        float bass = 0.0f; for (int h = 1; h <= 6; ++h) bass += (float) (0.10 / h * std::sin(tw * 82.4 * h * t));
        float w = rng.nextFloat() * 2.0f - 1.0f; auto& b = pk[ch];
        b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f; b[2] = 0.96900f * b[2] + w * 0.1538520f;
        b[3] = 0.86650f * b[3] + w * 0.3104856f; b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
        float pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f) * 0.11f; b[6] = w * 0.115926f;
        float res = (float) (0.07 * std::sin(tw * 450.0 * t) + 0.05 * std::sin(tw * 2600.0 * t + ch));
        float hats = (float) ((rng.nextFloat() * 2.0f - 1.0f) * 0.12 * std::exp(-hat * 60.0));
        return kick + bass + 0.22f * pink + res + hats;
    };

    juce::AudioBuffer<float> buf(2, bs); juce::MidiBuffer midi;
    auto runBlocks = [&](int n) {
        for (int k = 0; k < n; ++k) {
            for (int i = 0; i < bs; ++i) { t += 1.0 / fs; buf.setSample(0, i, mixSample(0)); buf.setSample(1, i, mixSample(1)); }
            proc.processBlock(buf, midi);
        }
    };
    runBlocks((int) (fs * 4.0 / bs));

    if (argc > 1 && juce::String(argv[1]) == "--editor-stress") {
        // open real editor windows x5 while an audio thread runs and the GUI thread hammers params
        std::atomic<bool> run { true }, fin { true };
        std::thread audio([&] {
            juce::AudioBuffer<float> b(2, bs); juce::MidiBuffer m; juce::Random r(3);
            while (run.load()) {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) b.setSample(c, i, (r.nextFloat() - 0.5f) * (r.nextInt(40) == 0 ? 40.0f : 1.0f));
                proc.processBlock(b, m);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) if (! std::isfinite(b.getSample(c, i))) fin = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
        juce::Random r(5); auto& ps = proc.getParameters(); int opened = 0;
        for (int k = 0; k < 5; ++k) {
            std::unique_ptr<juce::AudioProcessorEditor> e(proc.createEditor());
            e->setSize(WaveSurferEditor::baseW * (k % 2 ? 13 : 8) / 10, WaveSurferEditor::baseH * (k % 2 ? 13 : 8) / 10);
            e->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
            e->setVisible(true); ++opened;
            for (int t = 0; t < 40; ++t) {
                for (auto* p : ps) p->setValueNotifyingHost(r.nextFloat());
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            }
            e->removeFromDesktop();
        }
        run = false; audio.join();
        std::printf("editor stress: %d/5 windows opened+closed, audio finite: %s\n", opened, fin.load() ? "yes" : "NO");
        return (opened == 5 && fin.load()) ? 0 : 1;
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(WaveSurferEditor::baseW, WaveSurferEditor::baseH);
    auto* wsEd = dynamic_cast<WaveSurferEditor*>(ed.get());
    for (int f = 0; f < 60; ++f) { runBlocks(3); if (wsEd) wsEd->refresh(); }

    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
    outFile.deleteFile();
    juce::FileOutputStream out(outFile);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream(img, out);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", outFile.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return ok ? 0 : 1;
}
