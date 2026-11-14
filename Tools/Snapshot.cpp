// Renders the WaveSurfer editor to a PNG after feeding it a synthetic mix.
// Usage: WaveSurferSnapshot out.png [scale]
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <functional>

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
    set("amount", 0.90f);  set("boost", 6.0f); set("lowLimit", 42.0f); set("highLimit", 16000.0f);
    set("gainMatch", 1.0f); set("lookahead", 1.0f);
    if (argc > 3 && juce::String(argv[3]) == "pink") set("reference", 1.0f);
    if (argc > 3 && juce::String(argv[3]) == "track") {
        // write a warm reference (pink, +6 dB below 150 Hz, -6 dB above 5 kHz), load it like the user would
        const double rfs = 44100.0; const int n = (int) (rfs * 12);
        juce::AudioBuffer<float> rb(2, n); juce::Random rr(11); float st[2][7] {};
        double z[2][4] {};
        for (int i = 0; i < n; ++i) for (int c = 0; c < 2; ++c) {
            float w = rr.nextFloat() * 2.0f - 1.0f; auto& b = st[c];
            b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f; b[2] = 0.96900f * b[2] + w * 0.1538520f;
            b[3] = 0.86650f * b[3] + w * 0.3104856f; b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
            float pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f) * 0.11f; b[6] = w * 0.115926f;
            const double lp = std::exp(-2 * 3.14159265 * 150.0 / rfs), hp = std::exp(-2 * 3.14159265 * 5000.0 / rfs);
            z[c][0] = lp * z[c][0] + (1 - lp) * pink;                     // lows
            z[c][1] = hp * z[c][1] + (1 - hp) * pink;                     // everything below 5k
            const double highs = pink - z[c][1];
            rb.setSample(c, i, (float) (0.4 * (pink + 1.0 * z[c][0] - 0.5 * highs)));
        }
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wavesurfer_ref_snapshot.wav");
        tmp.deleteFile();
        { juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(tmp);
          auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions{}.withSampleRate(rfs).withNumChannels(2).withBitsPerSample(24));
          if (w) w->writeFromAudioSampleBuffer(rb, 0, n); }
        proc.loadReferenceFile(tmp);
        for (int k = 0; k < 400 && proc.getRefStatus() != WaveSurferProcessor::RefStatus::Ready
                                && proc.getRefStatus() != WaveSurferProcessor::RefStatus::Failed; ++k)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        std::printf("reference: %s (%s)\n", proc.getRefStatus() == WaveSurferProcessor::RefStatus::Ready ? "ready" : "NOT READY",
                    proc.getRefName().toRawUTF8());
        set("reference", 2.0f);
        if (argc > 4 && juce::String(argv[4]) == "--ref-test") {
            int fails = 0; auto expect = [&](bool c, const char* m) { if (! c) { ++fails; std::printf("  FAIL: %s\n", m); } };
            expect(proc.getRefStatus() == WaveSurferProcessor::RefStatus::Ready, "reference analysed and ready");
            expect(proc.getEngine().hasReference(), "engine has the reference curve");
            juce::MemoryBlock st; proc.getStateInformation(st);
            WaveSurferProcessor p2; p2.setPlayConfigDetails(2, 2, 96000.0, 256); p2.prepareToPlay(96000.0, 256);
            p2.setStateInformation(st.getData(), (int) st.getSize());
            expect(p2.getRefName() == proc.getRefName(), "reference name restored");
            expect(p2.getEngine().hasReference(), "reference curve restored into the engine");
            float worst = 0; const auto& a = proc.getEngine().getSnapshot(); const auto& b = p2.getEngine().getSnapshot();
            for (int k = 0; k < 32; ++k) worst = std::max(worst, std::abs(a.refTargetDb[(size_t) k].load() - b.refTargetDb[(size_t) k].load()));
            std::printf("  restored at 96 kHz: max curve difference %.2f dB\n", worst);
            expect(worst < 1.0f, "restored curve matches (48 k vs 96 k mapping)");
            juce::AudioBuffer<float> bb(2, 256); juce::MidiBuffer mm; bool fin = true; juce::Random r2(4);
            for (auto* prm : p2.getParameters()) if (prm->getName(32) == "Recover" || prm->getName(32) == "Tame") prm->setValue(1.0f);
            for (int k = 0; k < 2000; ++k) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) bb.setSample(c, i, (r2.nextFloat() - 0.5f) * 0.6f);
                p2.processBlock(bb, mm); for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) fin &= std::isfinite(bb.getSample(c, i)); }
            expect(fin, "Ref mode output finite");
            proc.clearReference();
            expect(! proc.getEngine().hasReference() && proc.getRefStatus() == WaveSurferProcessor::RefStatus::None, "clear reference");
            proc.loadReferenceFile(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("does_not_exist.wav"));
            for (int k = 0; k < 200 && proc.getRefStatus() == WaveSurferProcessor::RefStatus::Analysing; ++k)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            expect(proc.getRefStatus() == WaveSurferProcessor::RefStatus::Failed, "missing file reports failure");
            proc.loadReferenceFile(tmp); proc.loadReferenceFile(tmp);   // re-entrant loads cancel the previous one
            for (int k = 0; k < 400 && proc.getRefStatus() == WaveSurferProcessor::RefStatus::Analysing; ++k)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            expect(proc.getRefStatus() == WaveSurferProcessor::RefStatus::Ready, "back-to-back loads end ready");
            std::printf("ref test: %s\n", fails == 0 ? "ALL PASSED" : "FAILED");
            return fails == 0 ? 0 : 1;
        }
    }

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

    if (argc > 1 && juce::String(argv[1]) == "--paint-bench") {
        // GUI cost per frame: refresh() (curve maths) + repainting the display region, at 1x and 2x (Retina)
        std::function<juce::Component*(juce::Component*)> findDisplay = [&](juce::Component* c) -> juce::Component* {
            if (dynamic_cast<wsui::Display*>(c) != nullptr) return c;
            for (auto* ch : c->getChildren()) if (auto* r = findDisplay(ch)) return r;
            return nullptr; };
        auto* disp = findDisplay(ed.get());
        const auto area = ed->getLocalArea(disp, disp->getLocalBounds());
        auto best = [](auto fn) { double b = 1e30; for (int r = 0; r < 5; ++r) { double s = 1e30;
            for (int k = 0; k < 20; ++k) { const auto t0 = juce::Time::getHighResolutionTicks(); fn();
                s = std::min(s, juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0)); }
            b = std::min(b, s); } return b * 1000.0; };
        const double tRefresh = best([&] { runBlocks(1); if (wsEd) wsEd->refresh(); });
        const double tAudio   = best([&] { runBlocks(1); });
        for (float sc : { 1.0f, 2.0f }) {
            const double tDisp = best([&] { auto im = ed->createComponentSnapshot(area, true, sc); juce::ignoreUnused(im); });
            const double tFull = best([&] { auto im = ed->createComponentSnapshot(ed->getLocalBounds(), true, sc); juce::ignoreUnused(im); });
            std::printf("scale %.0fx: display repaint %.2f ms, full editor %.2f ms\n", sc, tDisp, tFull);
            std::printf("           at 30 fps the display costs %.1f%% of one core\n", 30.0 * (tDisp + std::max(0.0, tRefresh - tAudio)) / 10.0);
        }
        std::printf("refresh() maths: %.3f ms per frame\n", std::max(0.0, tRefresh - tAudio));
        return 0;
    }

    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
    outFile.deleteFile();
    juce::FileOutputStream out(outFile);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream(img, out);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", outFile.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return ok ? 0 : 1;
}
