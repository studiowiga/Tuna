#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "TunerEngine.h"
#include "TuningLibrary.h"

namespace tunaui
{
// tailles de fenêtre identiques à Freeze Shimmer : 100 % = 986 px de haut, ouverture par défaut = 493 px (= taille minimale)
constexpr int heightDefault = 493, height60 = 592, height75 = 740, height100 = 986, heightMax = 1479;
}

class TunaProcessor : public juce::AudioProcessor,
                      private juce::AsyncUpdater
{
public:
    TunaProcessor();
    ~TunaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tuna"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- accordage (thread message uniquement) ----
    TuningLibrary library;
    int  getTuningIndex() const { return tuningIndex; }
    void selectTuning (int index);
    void stepTuning (int delta);              // potard TUNING / MIDI
    void refreshEngineTuning();               // après édition d'un accordage perso
    juce::String getTuningName() const;

    tuna::Snapshot getSnapshot() const { return engine.getSnapshot(); }

    // ---- MIDI ----
    std::atomic<int> ccMute { 80 }, ccMode { 81 }, ccNext { 82 }, ccPrev { 83 }, ccRef { 84 };
    std::atomic<int> learnTarget { -1 };      // 0 = mute, 1 = mode, 2 = next, 3 = prev, 4 = ref
    std::atomic<int> lastMidiCc { -1 }, lastMidiValue { 0 }, lastMidiStamp { 0 };

    std::atomic<int> uiHeight { tunaui::heightDefault };
    juce::AudioProcessorValueTreeState apvts;

private:
    void handleAsyncUpdate() override;
    void handleMidi (const juce::MidiBuffer& midi);
    void setParam01 (const char* id, float v01);

    // thread d'analyse : vide la FIFO alimentée par le thread audio et fait tourner le moteur
    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread (TunaProcessor& p) : juce::Thread ("Tuna analysis"), owner (p) {}
        void run() override;
    private:
        TunaProcessor& owner;
    };

    tuna::TunerEngine engine;
    AnalysisThread analysis { *this };
    juce::AbstractFifo fifo { 1 << 17 };
    std::vector<float> fifoBuf = std::vector<float> (1 << 17);
    std::vector<float> monoTmp;

    std::atomic<float>* pMute = nullptr, *pMode = nullptr, *pRef = nullptr, *pMidiChan = nullptr;
    float gain = 1.0f;
    float lastRef = 0.0f;

    int tuningIndex = 0;
    std::atomic<int> pendingStep { 0 };       // pas demandés en MIDI (traités sur le thread message)
    std::atomic<int> pendingProgram { -1 };
    bool muteCcDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunaProcessor)
};
