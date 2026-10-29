#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const int v = 1;
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mute", v }, "Mute", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", v }, "Mode", StringArray { "Mono", "Poly", "Strobe" }, 0));   // MONO par défaut
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "ref", v }, "Reference A4", 430, 450, 440,
                                                   AudioParameterIntAttributes().withLabel ("Hz")));
    StringArray chans { "Omni" };
    for (int c = 1; c <= 16; ++c) chans.add (String (c));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "midichan", v }, "MIDI Channel", chans, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "ccmomentary", v }, "MIDI footswitch momentary", false));
    return l;
}

//==============================================================================
const TunaProcessor::LearnDef& TunaProcessor::learnDef (int i)
{
    static const LearnDef defs[numLearn] =
    {
        { "ccMute", "Mute footswitch", 80 },   // à accrochage ou momentané (option du menu MIDI)
        { "ccMode", "Mode",            81 },   // 0-42 MONO, 43-85 POLY, 86-127 STROBE
        { "ccNext", "Next tuning",     82 },   // chaque valeur >= 64 = accordage suivant
        { "ccPrev", "Previous tuning", 83 },   // chaque valeur >= 64 = accordage précédent
        { "ccRef",  "Reference A4",    84 },   // 0-127 -> 430-450 Hz
    };
    return defs[juce::jlimit (0, numLearn - 1, i)];
}

void TunaProcessor::resetMidiMap()
{
    for (int i = 0; i < numLearn; ++i) ccMap[i].store (learnDef (i).defaultCC);
}

TunaProcessor::TunaProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TunaState", createLayout())
{
    pMute = apvts.getRawParameterValue ("mute");
    pMode = apvts.getRawParameterValue ("mode");
    pRef = apvts.getRawParameterValue ("ref");
    pMidiChan = apvts.getRawParameterValue ("midichan");
    pMomentary = apvts.getRawParameterValue ("ccmomentary");
    for (int i = 0; i < numQueued; ++i) { pendingValue[i] = 0.0f; pendingSet[i] = false; }
    resetMidiMap();
    engine.prepare (48000.0);
    refreshEngineTuning();
    startTimerHz (60);
}

TunaProcessor::~TunaProcessor()
{
    stopTimer();
    analysis.stopThread (2000);
}

bool TunaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

void TunaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analysis.stopThread (2000);
    engine.prepare (sampleRate);
    engine.setReference ((double) pRef->load());
    refreshEngineTuning();
    fifo.reset();
    monoTmp.assign ((size_t) juce::jmax (512, samplesPerBlock), 0.0f);
    gain = pMute->load() > 0.5f ? 0.0f : 1.0f;
    analysis.startThread (juce::Thread::Priority::low);
}

void TunaProcessor::releaseResources()
{
    analysis.stopThread (2000);
}

//==============================================================================
void TunaProcessor::AnalysisThread::run()
{
    std::vector<float> tmp (8192);
    while (! threadShouldExit())
    {
        owner.engine.setReference ((double) owner.pRef->load());
        int ready = owner.fifo.getNumReady();
        if (ready == 0) { wait (5); continue; }
        while (ready > 0 && ! threadShouldExit())
        {
            const int n = juce::jmin (ready, (int) tmp.size());
            const auto scope = owner.fifo.read (n);
            if (scope.blockSize1 > 0) std::copy_n (owner.fifoBuf.data() + scope.startIndex1, scope.blockSize1, tmp.data());
            if (scope.blockSize2 > 0) std::copy_n (owner.fifoBuf.data() + scope.startIndex2, scope.blockSize2, tmp.data() + scope.blockSize1);
            owner.engine.push (tmp.data(), n);
            ready -= n;
        }
    }
}

//==============================================================================
// Anti-gel v2 (Studio Wiga) : jamais d'appel à l'hôte (setValueNotifyingHost, triggerAsyncUpdate) ni
// d'allocation depuis le thread audio. Le MIDI met la valeur en file ; le timer du processeur (60 Hz,
// thread message) la transmet au DAW et à la fenêtre, et traite les changements d'accordage demandés.
const char* TunaProcessor::queuedId (int i)
{
    static const char* ids[numQueued] = { "mute", "mode", "ref" };
    return ids[juce::jlimit (0, numQueued - 1, i)];
}

void TunaProcessor::setParam01 (const char* id, float v01)
{
    for (int i = 0; i < numQueued; ++i)
        if (std::strcmp (id, queuedId (i)) == 0)
        {
            pendingValue[i].store (juce::jlimit (0.0f, 1.0f, v01), std::memory_order_relaxed);
            pendingSet[i].store (true, std::memory_order_release);
            return;
        }
}

void TunaProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    const int chanSel = (int) pMidiChan->load();
    const bool momentary = pMomentary->load (std::memory_order_relaxed) > 0.5f;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (chanSel != 0 && m.getChannel() != chanSel) continue;

        if (m.isProgramChange())
        {
            pendingProgram.store (m.getProgramChangeNumber());
            continue;
        }
        if (! m.isController()) continue;
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        lastMidiCc.store (cc); lastMidiValue.store (val); lastMidiStamp.fetch_add (1);

        // MIDI Learn : le premier CC reçu est affecté à la commande visée (il n'agit pas sur l'accordeur)
        const int learn = learnTarget.load();
        if (learn >= 0 && learn < numLearn && cc < 120)
        {
            for (int j = 0; j < numLearn; ++j)
                if (j != learn && ccMap[j].load() == cc) ccMap[j].store (-1);   // numéro déjà pris : l'autre commande est désassignée
            ccMap[learn].store (cc);
            if (learn == 0) muteCcDown = val >= 64;
            lastLearned.store (learn);
            learnTarget.store (-1);
            continue;
        }

        if (cc == ccMap[0].load())
        {
            const bool down = val >= 64;
            if (momentary)
            {
                if (down && ! muteCcDown)
                {
                    // bascule : part de la valeur en file si elle existe, sinon de la valeur courante
                    const float cur = pendingSet[0].load() ? pendingValue[0].load() : pMute->load();
                    setParam01 ("mute", cur > 0.5f ? 0.0f : 1.0f);
                }
            }
            else setParam01 ("mute", down ? 1.0f : 0.0f);
            muteCcDown = down;
        }
        if (cc == ccMap[1].load()) setParam01 ("mode", (float) juce::jmin (2, val / 43) / 2.0f);
        if (cc == ccMap[4].load()) setParam01 ("ref", (float) val / 127.0f);
        if (cc == ccMap[2].load() && val >= 64) pendingStep.fetch_add (1);
        if (cc == ccMap[3].load() && val >= 64) pendingStep.fetch_sub (1);
    }
}

void TunaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);

    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, n);
    if (n == 0) return;

    // 1) vers l'analyse : somme mono de l'entrée (avant le mute)
    if ((int) monoTmp.size() < n) monoTmp.resize ((size_t) n);   // garde-fou (hôte qui dépasse la taille annoncée)
    const int nIn = juce::jmin (nch, getTotalNumInputChannels());
    for (int i = 0; i < n; ++i)
    {
        float s = 0;
        for (int ch = 0; ch < nIn; ++ch) s += buffer.getSample (ch, i);
        monoTmp[(size_t) i] = nIn > 0 ? s / (float) nIn : 0.0f;
    }
    const int free = fifo.getFreeSpace();
    if (free >= n)
    {
        const auto scope = fifo.write (n);
        if (scope.blockSize1 > 0) std::copy_n (monoTmp.data(), scope.blockSize1, fifoBuf.data() + scope.startIndex1);
        if (scope.blockSize2 > 0) std::copy_n (monoTmp.data() + scope.blockSize1, scope.blockSize2, fifoBuf.data() + scope.startIndex2);
    }

    // 2) sortie : passage direct, coupée en douceur quand MUTE est actif
    const float target = pMute->load() > 0.5f ? 0.0f : 1.0f;
    if (target > 0.5f && gain >= 1.0f) return;   // passage direct, rien à faire
    const float step = 1.0f / (float) (0.008 * getSampleRate());
    for (int i = 0; i < n; ++i)
    {
        gain = target > gain ? juce::jmin (target, gain + step) : juce::jmax (target, gain - step);
        for (int ch = 0; ch < nch; ++ch) buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
    }
}

//==============================================================================
void TunaProcessor::timerCallback()
{
    for (int i = 0; i < numQueued; ++i)
        if (pendingSet[i].exchange (false, std::memory_order_acquire))
            if (auto* prm = apvts.getParameter (queuedId (i)))
                prm->setValueNotifyingHost (pendingValue[i].load (std::memory_order_relaxed));

    const int prog = pendingProgram.exchange (-1);
    if (prog >= 0)
    {
        const auto fav = library.favouriteIndices();
        if (! fav.empty()) selectTuning (fav[(size_t) (prog % (int) fav.size())]);
        else selectTuning (prog % library.size());
    }
    const int steps = pendingStep.exchange (0);
    if (steps != 0) stepTuning (steps);
}

void TunaProcessor::selectTuning (int index)
{
    tuningIndex = juce::jlimit (0, library.size() - 1, index);
    refreshEngineTuning();
}

void TunaProcessor::stepTuning (int delta)
{
    const int n = library.size();
    selectTuning (((tuningIndex + delta) % n + n) % n);
}

void TunaProcessor::refreshEngineTuning()
{
    tuningIndex = juce::jlimit (0, library.size() - 1, tuningIndex);
    engine.setTuning (library.get (tuningIndex).notes);
}

juce::String TunaProcessor::getTuningName() const
{
    return juce::String::fromUTF8 (library.get (tuningIndex).name.c_str());
}

//==============================================================================
void TunaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("tuningKey", library.keyAt (tuningIndex), nullptr);
    state.setProperty ("tuningNotes", juce::String (tuna::notesToString (library.get (tuningIndex).notes)), nullptr);
    state.setProperty ("tuningName", getTuningName(), nullptr);
    state.setProperty ("uiHeight", uiHeight.load(), nullptr);
    for (int i = 0; i < numLearn; ++i)
        state.setProperty (learnDef (i).key, ccMap[i].load(), nullptr);     // -1 = non assigné
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TunaProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);
    uiHeight.store (juce::jlimit (tunaui::heightDefault, tunaui::heightMax, (int) state.getProperty ("uiHeight", tunaui::heightDefault)));
    for (int i = 0; i < numLearn; ++i)
        ccMap[i].store (juce::jlimit (-1, 127, (int) state.getProperty (learnDef (i).key, learnDef (i).defaultCC)));

    const juce::String key = state.getProperty ("tuningKey").toString();
    int idx = library.indexOfKey (key);
    if (idx < 0)
    {
        // accordage perso absent de cette machine : on le recrée à partir des notes sauvegardées dans le projet
        const auto notes = tuna::parseNotes (state.getProperty ("tuningNotes").toString().toStdString());
        if (! notes.empty()) idx = library.addUser (state.getProperty ("tuningName", "Projet").toString(), notes);
    }
    tuningIndex = juce::jmax (0, idx);
    refreshEngineTuning();
}

juce::AudioProcessorEditor* TunaProcessor::createEditor() { return new TunaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TunaProcessor(); }
