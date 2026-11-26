// Admiral 1506 - generativer Drumcomputer (VST3 / AU / Standalone)
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Core/DrumEngine.h"
#include "Core/Generator.h"
#include "Core/Params.h"
#include "Core/TrackFx.h"
#include "Core/MasterBus.h"
#include "Core/KitDesigner.h"

class AdmiralProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    AdmiralProcessor();
    ~AdmiralProcessor() override { stopTimer(); }

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Admiral 1506"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void pollParameters() { timerCallback(); }    // sonst 30x pro Sekunde per Timer (Kit-Wechsel, MIDI-CC an den Host)

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Parameter ---
    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter* getParam (int index) const { return params[(size_t) index]; }
    float getValue (int index) const { return raw[(size_t) index]->load(); }

    // --- Pattern (Message-Thread schreibt, Audio-Thread liest lock-frei) ---
    // view: 0 = normaler Durchlauf (Groove + Ending-Variation), 1 = Fill-Durchlauf
    int getLength() const { return ad::patternLength ((int) std::lround (getValue (ad::GP_LENGTH))); }
    ad::Pattern getPattern() const;
    void setPattern (const ad::Pattern& p);
    float getCell (int view, int track, int step) const { return getPattern().value (track, step, getLength(), view == 1); }
    // Schritt setzen: in der Variations-Zone der Ansicht wird die Variation bearbeitet, sonst der Groove
    void setCell (int view, int track, int step, float v);
    void removeVariation (int view, int track, int step);   // Schritt folgt wieder dem Groove
    void clearVariation (int view);                          // ganze Variation (Ending bzw. Fill) entfernen
    void clearPattern (int view);
    void copyMainToFill();
    int getPatternRevision() const { return patternRevision.load(); }    // zählt bei jeder Änderung hoch

    // --- Generator ---
    void generateNew();                 // neues Pattern (gesperrte Spuren bleiben)
    void regenerateFill();              // nur den Fill-Takt neu erzeugen
    bool historyBack();                 // vorheriges erzeugtes Pattern
    bool historyForward();
    int getHistoryPos() const { return histPos; }
    int getHistorySize() const { return (int) history.size(); }
    bool isLocked (int track) const { return locked[(size_t) track]; }                 // Pattern-Sperre
    void setLocked (int track, bool on) { locked[(size_t) track] = on; }
    bool isSoundLocked (int track) const { return soundLocked[(size_t) track]; }       // Sound-Sperre (NEW KIT / MUTATE)
    void setSoundLocked (int track, bool on) { soundLocked[(size_t) track] = on; }

    // --- Kit-Designer ---
    void newKit();                      // neues, regelbasiertes Zufalls-Kit
    void mutateKit();                   // aktuelles Kit leicht variieren
    bool kitHistoryBack();
    bool kitHistoryForward();
    int getKitHistoryPos() const { return kitHistPos; }
    int getKitHistorySize() const { return (int) kitHistory.size(); }
    juce::String getKitCharacter() const;   // Charakter des zuletzt erzeugten Kits ("" = von Hand)
    ad::KitSnapshot captureKit() const;
    int resolvedKit (int track) const;

    // --- Wiedergabe-Anzeige ---
    int getPlayStep() const { return playStep.load(); }        // -1 = steht
    bool isPlayingFill() const { return playFill.load(); }
    long long getPlayCycle() const { return playCycle.load(); }
    double getCurrentBpm() const { return currentBpm.load(); }
    bool isHostSynced() const { return hostPlaying.load(); }
    float takeMeter (int track) { return engine.takeMeter (track); }
    float getGainReductionDb() const { return masterBus.getGainReductionDb(); }
    float takePeakGainReductionDb() { return masterBus.takePeakGainReductionDb(); }
    bool trackHasFx (int track) const { return fxSettings (track).any(); }
    ad::FxSettings fxSettings (int track) const;
    void setAllOuts (bool on);          // alle Spuren auf eigene Ausgänge (bzw. zurück auf MAIN)
    bool allOutsOn() const;
    void previewTrack (int track) { if (juce::isPositiveAndBelow (track, ad::NUM_TRACKS)) preview[(size_t) track].store (true); }

    // --- Samples: eingebaute Kits oder eigene Audiodatei je Spur. Nur im Message-Thread aufrufen. ---
    static constexpr double kMaxSampleSeconds = 10.0;
    bool isSampleFile (const juce::File& f) const;
    juce::String getSampleWildcard() const;
    bool loadSampleFile (int track, const juce::File& file, juce::String& message);
    void resetSample (int track);                        // zurück zum Kit-Klang
    bool hasUserSample (int track) const { return userSample[(size_t) track] != nullptr; }
    juce::String getSampleName (int track) const;

    // --- MIDI-Export (Drag & Drop in FL Studio / Datei speichern) ---
    bool writeMidiFile (const juce::File& dest, int bars) const;
    juce::File createMidiExportFile (int bars) const;   // Datei im Temp-Ordner, für Drag & Drop

    // --- MIDI-Learn / CC-Zuordnung (wie im Diplomat) ---
    void startLearn (int paramIndex)  { learnIndex.store (paramIndex); }
    void cancelLearn()                { learnIndex.store (-1); }
    void setLearnArmed (bool on)      { learnArmed.store (on); if (! on) learnIndex.store (-1); }
    bool isLearnArmed() const         { return learnArmed.load(); }
    int  getLearnIndex() const        { return learnIndex.load(); }
    int  getCcForParam (int paramIndex) const;
    void clearMappingForParam (int paramIndex);
    void clearAllMappings();
    int  getLastCc() const            { return lastCc.load(); }
    int  getLastCcValue() const       { return lastCcValue.load(); }
    int  getLearnEvent() const        { return learnEvent.load(); }
    int  getLearnedCc() const         { return learnedCc.load(); }
    int  getLearnedParam() const      { return learnedParam.load(); }

    static juce::String encodeSample (const ad::SampleData&);
    static std::shared_ptr<ad::SampleData> decodeSample (const juce::String& base64, const juce::String& name);

private:
    static constexpr int kBodySize = ad::NUM_TRACKS * ad::MAX_STEPS, kLayerSize = ad::NUM_TRACKS * ad::END_STEPS;
    static size_t bIdx (int t, int k)            { return (size_t) (t * ad::MAX_STEPS + k); }
    static size_t lIdx (int layer, int t, int i) { return (size_t) (kBodySize + layer * kLayerSize + t * ad::END_STEPS + i); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();
    void timerCallback() override;
    void updateSlots();                                  // Kit/eigene Samples an die Engine geben
    void handleController (int cc, int value);
    void applyNormalised (int paramIndex, float normalised);
    void flushMidiParamChanges();
    ad::GenSettings currentGenSettings() const;


    ad::DrumEngine engine;
    static constexpr int kChunk = 256;                    // Blockteilung für die Spur-Effekte
    std::array<ad::TrackFx, ad::NUM_TRACKS> fx;
    ad::MasterBus masterBus;
    juce::AudioBuffer<float> trackBuf;                    // 2 Kanäle je Spur
    ad::Sequencer seq;
    std::vector<ad::NoteEvent> events;
    std::array<juce::RangedAudioParameter*, ad::NUM_PARAMS> params {};
    std::array<std::atomic<float>*, ad::NUM_PARAMS> raw {};

    std::array<std::atomic<float>, kBodySize + 2 * kLayerSize> pat;
    std::atomic<int> patternRevision { 0 };
    std::array<bool, ad::NUM_TRACKS> locked {}, soundLocked {};
    std::vector<ad::KitSnapshot> kitHistory;
    int kitHistPos = -1, kitCharacter = -1;
    ad::KitLoudness kitLoudness {};
    std::array<int, ad::NUM_TRACKS> shownSlots {};
    void applyKit (const ad::KitSnapshot& k);
    void pushKit (const ad::KitSnapshot& k);
    std::vector<ad::Pattern> history;
    int histPos = -1;
    uint32_t seedCounter = 0;

    // Samples
    juce::AudioFormatManager formats;
    std::array<std::array<std::shared_ptr<const ad::SampleData>, ad::NUM_TRACKS>, ad::NUM_KITS> kits;
    std::array<std::shared_ptr<const ad::SampleData>, ad::NUM_TRACKS> userSample;
    std::vector<std::shared_ptr<const ad::SampleData>> keepAlive;     // alle je benutzten Samples (Audio-Thread-Sicherheit)
    int shownKit = -1;

    // Wiedergabe
    double sr = 44100.0;
    double internalPpq = 0.0;
    bool wasRunning = false;
    std::atomic<int> playStep { -1 };
    std::atomic<bool> playFill { false }, hostPlaying { false };
    std::atomic<long long> playCycle { 0 };
    std::atomic<double> currentBpm { 120.0 };
    std::array<std::atomic<bool>, ad::NUM_TRACKS> preview;
    float lastMaster = 0.65f;
    struct PendingOff { int note = -1; int samplesLeft = 0; };
    std::array<PendingOff, 64> pendingOff {};

    // MIDI-Learn
    std::array<std::atomic<int>, 128> ccMap;
    std::atomic<int> learnIndex { -1 };
    std::atomic<bool> learnArmed { false };
    std::atomic<int> lastCc { -1 }, lastCcValue { 0 };
    std::atomic<int> learnEvent { 0 }, learnedCc { -1 }, learnedParam { -1 };
    std::array<std::atomic<float>, ad::NUM_PARAMS> ccValue;
    std::array<std::atomic<bool>, ad::NUM_PARAMS> ccDirty;
    std::array<int, ad::NUM_PARAMS> ccIdle {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdmiralProcessor)
};
