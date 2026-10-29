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
                      private juce::Timer
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

    const juce::String getName() const override { return "Chroma"; }
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
    // v1.3 MIDI Learn : toutes les commandes de la façade (0 footswitch MUTE, 1 MODE, 2 TUNING suivant,
    // 3 TUNING précédent, 4 REF) ; numéros par défaut = anciens CC 80 à 84
    struct LearnDef { const char* key; const char* name; int defaultCC; };
    static constexpr int numLearn = 5;
    static const LearnDef& learnDef (int i);
    std::atomic<int> ccMap[numLearn];         // numéro de CC de chaque commande, -1 = non assigné
    std::atomic<int> learnTarget { -1 };      // commande en attente d'un CC (-1 = pas de learn)
    std::atomic<int> lastLearned { -1 };      // dernière commande apprise (lue et remise à -1 par la fenêtre)
    void resetMidiMap();
    std::atomic<int> lastMidiCc { -1 }, lastMidiValue { 0 }, lastMidiStamp { 0 };

    std::atomic<int> uiHeight { tunaui::heightDefault };
    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override;
    void handleMidi (const juce::MidiBuffer& midi);
    void setParam01 (const char* id, float v01);   // thread audio : met la valeur en file (anti-gel v2)

    // anti-gel v2 : valeurs demandées par le MIDI (thread audio), transmises à l'hôte par le timer (thread message)
    static constexpr int numQueued = 3;
    static const char* queuedId (int i);
    std::atomic<float> pendingValue[numQueued];
    std::atomic<bool>  pendingSet[numQueued];

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

    std::atomic<float>* pMute = nullptr, *pMode = nullptr, *pRef = nullptr, *pMidiChan = nullptr, *pMomentary = nullptr;
    float gain = 1.0f;
    float lastRef = 0.0f;

    int tuningIndex = 0;
    std::atomic<int> pendingStep { 0 };       // pas demandés en MIDI (traités sur le thread message)
    std::atomic<int> pendingProgram { -1 };
    bool muteCcDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunaProcessor)
};
