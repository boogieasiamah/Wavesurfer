// Loads the built WaveSurfer.vst3 like a DAW would and abuses it.
// Usage: WaveSurferHostTest path/to/WaveSurfer.vst3
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_MAJOR_VERSION >= 9
 #define WS_CREATE_EDITOR(p) (p)->createEditorAndMakeActive()
#else
 #define WS_CREATE_EDITOR(p) (p)->createEditorIfNeeded()
#endif

static int passes = 0, fails = 0;
#define EXPECT(c, msg) do { if (c) ++passes; else { ++fails; std::printf("  FAIL: %s\n", msg); } } while (0)

static bool finite(const juce::AudioBuffer<float>& b) {
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite(b.getSample(c, i))) return false;
    return true;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::puts("usage: WaveSurferHostTest WaveSurfer.vst3"); return 2; }

    juce::AudioPluginFormatManager fm;
   #if JUCE_MAJOR_VERSION >= 9
    fm.addFormat(std::make_unique<juce::VST3PluginFormat>());
   #else
    fm.addFormat(new juce::VST3PluginFormat());
   #endif
    juce::OwnedArray<juce::PluginDescription> found;
    juce::KnownPluginList list;
    list.scanAndAddFile(argv[1], true, found, *fm.getFormat(0));
    EXPECT(found.size() == 1, "VST3 scanned");
    if (found.isEmpty()) return 1;
    std::printf("Loaded: %s by %s (%s)\n", found[0]->name.toRawUTF8(), found[0]->manufacturerName.toRawUTF8(), found[0]->category.toRawUTF8());

    juce::String err;
    auto plugin = fm.createPluginInstance(*found[0], 48000.0, 512, err);
    EXPECT(plugin != nullptr, "instantiate");
    if (! plugin) { std::printf("  %s\n", err.toRawUTF8()); return 1; }

    auto& params = plugin->getParameters();
    std::printf("Parameters: %d, latency %d, tail %.2fs\n", params.size(), plugin->getLatencySamples(), plugin->getTailLengthSeconds());
    EXPECT(params.size() >= 12, "12 parameters exposed");
    EXPECT(plugin->getBypassParameter() != nullptr, "host bypass parameter");
    EXPECT(plugin->getLatencySamples() == 0, "zero latency");

    juce::Random rng(1234);
    juce::MidiBuffer midi;

    // 1) every rate, random block sizes, every parameter randomly at extremes each block
    std::puts("[H1] rates x random block sizes x parameter extremes");
    bool allFinite = true;
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 }) {
        plugin->releaseResources();
        plugin->setRateAndBufferSizeDetails(fs, 2048);
        plugin->prepareToPlay(fs, 2048);
        juce::AudioBuffer<float> buf(2, 2048);
        for (int blk = 0; blk < 400; ++blk) {
            const int n = 1 + rng.nextInt(2048);
            for (auto* p : params) {
                const float r = rng.nextFloat();
                p->setValue(r < 0.4f ? 0.0f : r < 0.8f ? 1.0f : rng.nextFloat());
            }
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    buf.setSample(c, i, (rng.nextFloat() * 2.0f - 1.0f) * (blk % 50 == 7 ? 30.0f : 0.5f));
            juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, n);
            plugin->processBlock(view, midi);
            allFinite &= finite(view);
        }
    }
    EXPECT(allFinite, "finite output under random extreme automation");

    // 2) state round trip (VST3 delivers parameter changes on the next process call)
    std::puts("[H2] state save / restore");
    auto pump = [&] { juce::AudioBuffer<float> b(2, 64); b.clear(); plugin->processBlock(b, midi); };
    auto randomise = [&] { for (auto* p : params) { const float r = rng.nextFloat(); p->setValue(p->getNumSteps() == 2 ? std::round(r) : r); } pump(); };
    plugin->prepareToPlay(48000, 512);
    randomise();
    juce::MemoryBlock state; plugin->getStateInformation(state);
    std::vector<float> saved; for (auto* p : params) saved.push_back(p->getValue());
    randomise();
    plugin->setStateInformation(state.getData(), (int) state.getSize());
    pump();
    float maxErr = 0; for (int i = 0; i < params.size(); ++i) maxErr = std::max(maxErr, std::abs(params[i]->getValue() - saved[(size_t) i]));
    std::printf("  max parameter error after restore: %g\n", maxErr);
    EXPECT(maxErr < 1e-4f, "state restores every parameter");
    plugin->setStateInformation("garbage", 7);   // must not crash
    pump();
    EXPECT(true, "survives garbage state");

    // 3) mono layout
    std::puts("[H3] mono bus layout");
    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add(juce::AudioChannelSet::mono()); mono.outputBuses.add(juce::AudioChannelSet::mono());
    plugin->releaseResources();                     // layouts change only while inactive
    const bool monoOk = plugin->setBusesLayout(mono);
    std::printf("  mono accepted: %s\n", monoOk ? "yes" : "no");
    if (monoOk) {
        plugin->prepareToPlay(48000, 512);
        juce::AudioBuffer<float> b(1, 512); bool ok = true;
        for (int k = 0; k < 200; ++k) { for (int i = 0; i < 512; ++i) b.setSample(0, i, rng.nextFloat() - 0.5f); plugin->processBlock(b, midi); ok &= finite(b); }
        EXPECT(ok, "mono processing finite");
        juce::AudioProcessor::BusesLayout st;
        st.inputBuses.add(juce::AudioChannelSet::stereo()); st.outputBuses.add(juce::AudioChannelSet::stereo());
        plugin->releaseResources();
        plugin->setBusesLayout(st);
    }
    EXPECT(monoOk, "mono layout accepted");

    // 4) host-side editor lifecycle while audio runs (window embedding is tested in-process,
    //    because XEmbed needs a real window manager)
    std::puts("[H4] editor wrapper create/resize/destroy x5 with audio + automation running");
    plugin->prepareToPlay(48000, 256);
    std::atomic<bool> run { true }, audioFinite { true };
    std::thread audio([&] {
        juce::AudioBuffer<float> b(2, 256); juce::MidiBuffer m; juce::Random r(9);
        while (run.load()) {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) b.setSample(c, i, r.nextFloat() - 0.5f);
            plugin->processBlock(b, m);
            if (! finite(b)) audioFinite = false;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    bool editorsOk = true;
    for (int k = 0; k < 5; ++k) {
        std::unique_ptr<juce::AudioProcessorEditor> ed(WS_CREATE_EDITOR(plugin));
        if (! ed) { editorsOk = false; break; }
        std::printf("  editor %d: %dx%d\n", k, ed->getWidth(), ed->getHeight());
        for (int t = 0; t < 20; ++t) {
            for (auto* p : params) p->setValueNotifyingHost(rng.nextFloat());
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
    }
    run = false; audio.join();
    EXPECT(editorsOk, "editor created every time");
    EXPECT(audioFinite.load(), "audio stayed finite while parameters were hammered");

    // 5) lookahead: the DAW must be told about the 20 ms latency, and back to 0
    std::puts("[H5] lookahead latency reported to the host");
    {
        juce::AudioProcessorParameter* look = nullptr;
        for (auto* p : params) if (p->getName(64) == "Lookahead") look = p;
        EXPECT(look != nullptr, "Lookahead parameter exposed");
        if (look) {
            plugin->prepareToPlay(48000, 512);
            auto settle = [&](float v) {
                look->setValueNotifyingHost(v);
                for (int t = 0; t < 30; ++t) {
                    juce::AudioBuffer<float> b(2, 512); b.clear(); plugin->processBlock(b, midi);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
                }
                return plugin->getLatencySamples();
            };
            const int on = settle(1.0f), off = settle(0.0f);
            std::printf("  latency with lookahead: %d samples, without: %d\n", on, off);
            EXPECT(on == 960 && off == 0, "host sees lookahead latency changes");
        }
    }

   #if JUCE_MAC
    // 6) what a DAW does when you open the plug-in: editor embedded in a real window
    std::puts("[H6] editor in a real macOS window, audio + automation running");
    {
        plugin->prepareToPlay(48000, 512);
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            std::unique_ptr<juce::AudioProcessorEditor> ed(WS_CREATE_EDITOR(plugin));
            if (! ed) { ok = false; break; }
            ed->addToDesktop(juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowIsResizable);
            ed->setVisible(true);
            for (int t = 0; t < 60; ++t) {
                for (auto* p : params) if (rng.nextInt(10) == 0) p->setValueNotifyingHost(rng.nextFloat());
                juce::AudioBuffer<float> b(2, 512);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample(c, i, rng.nextFloat() - 0.5f);
                plugin->processBlock(b, midi);
                ok &= finite(b);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(16);
            }
            std::printf("  window %d: %dx%d ok\n", k, ed->getWidth(), ed->getHeight());
            ed->removeFromDesktop();
        }
        EXPECT(ok, "editor survives a real window with audio running");
    }
   #endif

    plugin->releaseResources();
    plugin.reset();
    std::printf("\nHost test: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
