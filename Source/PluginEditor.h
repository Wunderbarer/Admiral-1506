#pragma once
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Look.h"

//==============================================================================
// Kleiner Leuchttaster (waagerecht, LED + Text). Für LOCK, MUTE, OUT, RUN ...
class LedPill : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LedPill (juce::String t, juce::Colour onColour = col::cyan) : text (std::move (t)), colOn (onColour) {}
    void setOn (bool b) { if (b != on) { on = b; repaint(); } }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    bool isOn() const { return on; }
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu() && onClick) onClick(); }

protected:
    void paintLearn (juce::Graphics& g, bool learn);
    juce::String text;
    juce::Colour colOn;
    bool on = false;
};

// Leuchttaster für einen Ein/Aus-Parameter, mit MIDI-Learn
class ParamPill : public LedPill, public LearnWidget
{
public:
    ParamPill (AdmiralProcessor& p, int idx, juce::String t, juce::Colour onColour = col::cyan);
    void mouseDown (const juce::MouseEvent& e) override;
    void paint (juce::Graphics& g) override { LedPill::paint (g); paintLearn (g, shownLearn); }

private:
    juce::ParameterAttachment attach;
};

//==============================================================================
// Schrittraster: 10 Spuren x die ganze Pattern-Länge (16 / 24 / 32 / 48) auf einen Blick.
// Die Zellbreite passt sich der Länge an; Takte sind durch breitere Lücken getrennt.
class StepMatrix : public juce::Component
{
public:
    static constexpr int kHeader = 20, kRowH = 42, kWidth = 690;
    static int width() { return kWidth; }
    static int height() { return kHeader + ad::NUM_TRACKS * kRowH; }

    explicit StepMatrix (AdmiralProcessor& p) : proc (p) { cache = proc.getPattern(); layout(); }
    void setEditTarget (int which) { if (which != target) { target = which; repaint(); } }
    int getEditTarget() const { return target; }
    void update();                     // aus dem Timer: Wiedergabe-Position / Änderungen

    const ad::Pattern& cached() const { return cache; }
    int length() const { return len; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void layout();                     // Spalten für die aktuelle Länge berechnen
    bool cellAt (juce::Point<int> p, int& track, int& step) const;
    void showVelocityMenu (int track, int step);
    float cell (int t, int k) const { return cache.value (t, k, len, target == 1); }

    AdmiralProcessor& proc;
    ad::Pattern cache;
    int len = ad::MAX_STEPS;
    std::array<float, ad::MAX_STEPS> colX {};
    float cellW = 40.f;
    int target = 0;                    // 0 = normaler Durchlauf, 1 = Fill-Durchlauf
    int playing = -1;
    bool playingFill = false;
    int seenRevision = -1, muteMask = -1;
    float paintValue = 0.f;
    int lastPaintT = -1, lastPaintK = -1;
};

//==============================================================================
// Waagerechte Auswahltaster für einen Auswahl-Parameter (z. B. LENGTH 16 / 24 / 32 / 48)
class HChoice : public juce::Component
{
public:
    HChoice (AdmiralProcessor& p, int paramIdx, juce::StringArray items);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray items;
    int current = 0;
    juce::ParameterAttachment attach;
};

//==============================================================================
// Kopf einer Spur: Aktivitäts-LED, Name, Sample-Name, LOCK und MUTE
class TrackHeader : public juce::Component
{
public:
    TrackHeader (AdmiralProcessor& p, int track, std::function<void (int)> onLoadRequest);
    std::function<void (int)> onSelect;
    void setSelected (bool b) { if (b != selected) { selected = b; repaint(); } }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void update();                     // aus dem Timer
    ParamPill& getMute() { return mute; }
    void setDropHighlight (bool b) { if (b != dropHi) { dropHi = b; repaint(); } }

private:
    AdmiralProcessor& proc;
    const int track;
    std::function<void (int)> requestLoad;
    LedPill lock { "LOCK", col::yellow };
    ParamPill mute;
    float led = 0.f;
    juce::String sampleName;
    bool dropHi = false, shownMute = false, selected = false, shownFx = false;
};

//==============================================================================
// Spurwahl im Bereich TRACK FX: Rahmen = gewählt, LED = Spur hat aktive Effekte
class TrackSelectButton : public juce::Component
{
public:
    TrackSelectButton (int t, std::function<void (int)> cb) : track (t), onClick (std::move (cb)) {}
    void set (bool sel, bool fxOn) { if (sel != selected || fxOn != fx) { selected = sel; fx = fxOn; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick (track); }

private:
    int track;
    std::function<void (int)> onClick;
    bool selected = false, fx = false;
};

//==============================================================================
// Ziehfläche: erzeugt eine MIDI-Datei des Patterns und zieht sie in den Host (FL Studio: Playlist / Piano Roll)
class MidiDragTile : public juce::Component
{
public:
    explicit MidiDragTile (AdmiralProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onNotice;

private:
    AdmiralProcessor& proc;
    bool hover = false, dragging = false;
};

//==============================================================================
class MainPanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    static constexpr int W = 1320, H = 936;

    explicit MainPanel (AdmiralProcessor&);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    int rowAt (int x, int y) const;    // Spur unter der Mausposition (-1 = keine)
    void setDropRow (int r);
    void startLoadSample (int track);
    void showNotice (const juce::String& t) { notice = t; noticeTicks = 90; }
    void setEditTarget (int which);
    void updateCaption();
    void selectTrack (int t);
    void buildTrackFx();

    template <typename T, typename... Args>
    T* add (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c);
        addAndMakeVisible (c);
        return c;
    }
    KnobCell* knob (int paramIdx, const juce::String& label, juce::Rectangle<int> bounds, int knobSize, int labelH = 16);

    AdmiralProcessor& proc;
    CyberLook look;
    juce::OwnedArray<juce::Component> owned;
    std::vector<LearnWidget*> learners;
    StepMatrix* matrix = nullptr;
    std::array<TrackHeader*, ad::NUM_TRACKS> headers {};
    MidiDragTile* dragTile = nullptr;
    int selTrack = 0;
    std::array<TrackSelectButton*, ad::NUM_TRACKS> selButtons {};
    std::array<std::vector<juce::Component*>, ad::NUM_TRACKS> fxComps;     // Regler der Spur-Effekte (nur die gewählte Spur sichtbar)
    std::array<KnobCell*, ad::NUM_TRACKS> dlTimeKnob {}, dlDivKnob {};
    LedPill* allOuts = nullptr;
    size_t fxGroupIndex = 0;
    float shownGr = 0.f;
    juce::TextButton resetFxBtn { "RESET FX" };
    juce::TextButton newKitBtn { "NEW KIT" }, mutateBtn { "MUTATE" }, kitBack { "<" }, kitFwd { ">" };
    juce::String kitHistText;
    juce::TooltipWindow tooltips { this, 700 };

    juce::ComboBox genreBox, barsBox;
    std::unique_ptr<juce::ComboBoxParameterAttachment> genreAtt, barsAtt;
    juce::TextButton generateBtn { "GENERATE" }, histBack { "<" }, histFwd { ">" }, newFillBtn { "NEW FILL" };
    juce::TextButton tabMain { "MAIN" }, tabFill { "FILL" }, copyBtn { "MAIN > FILL" }, clearBtn { "CLEAR" };
    juce::TextButton saveMidiBtn { "SAVE MIDI ..." }, learnBtn { "MIDI LEARN: OFF" };

    std::unique_ptr<juce::FileChooser> chooser;
    int dropRow = -1;
    bool dropElsewhere = false;
    juce::String caption, notice, midiText = "NO MIDI CC RECEIVED YET", posText, histText;
    int noticeTicks = 0, seenLearnEvent = 0;
    int titleRight = 330;

    struct Group { juce::String title; juce::Rectangle<int> r; };
    std::vector<Group> groups;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

class AdmiralEditor : public juce::AudioProcessorEditor
{
public:
    explicit AdmiralEditor (AdmiralProcessor&);
    void resized() override;

private:
    MainPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdmiralEditor)
};
