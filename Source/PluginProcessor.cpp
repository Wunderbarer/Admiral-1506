#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include <cstring>

using namespace ad;

namespace
{
juce::NormalisableRange<float> makeRange (const ParamSpec& s)
{
    juce::NormalisableRange<float> r (s.min, s.max, s.step);
    if (s.centre > s.min && s.centre < s.max)
        r.setSkewForCentre (s.centre);
    return r;
}

int indexOf (const juce::String& id)
{
    for (int i = 0; i < NUM_PARAMS; ++i)
        if (id == juce::String (specs()[(size_t) i].id)) return i;
    return -1;
}

// Sanfte Begrenzung oberhalb von 0.9 (schützt vor Übersteuerung, wenn viele Spuren zusammenfallen)
inline float softLimit (float x)
{
    const float a = std::abs (x);
    if (a <= 0.9f) return x;
    const float y = 0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f);
    return x < 0.f ? -y : y;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AdmiralProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& s : specs())
    {
        const juce::ParameterID pid { s.id, 1 };
        switch (s.kind)
        {
            case Kind::Float:
            {
                const ParamSpec copy = s;
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, juce::String (s.name), makeRange (s), s.def,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([copy] (float v, int) { return juce::String (formatValue (copy, v)); })
                        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); })));
                break;
            }
            case Kind::Choice:
            {
                juce::StringArray items;
                for (auto& c : s.choices) items.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, juce::String (s.name), items, (int) s.def));
                break;
            }
            case Kind::Toggle:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, juce::String (s.name), s.def > 0.5f));
                break;
        }
    }
    return layout;
}

juce::AudioProcessor::BusesProperties AdmiralProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    // Einzelausgänge je Spur (im Host zuschaltbar, z. B. FL Studio: eigene Mixer-Spuren)
    for (int t = 0; t < NUM_TRACKS; ++t)
        b = b.withOutput (juce::String (tracks()[(size_t) t].file), juce::AudioChannelSet::stereo(), true);   // aktiv: FL Studio aktiviert inaktive VST3-Ausgänge nicht zuverlässig
    return b;
}

AdmiralProcessor::AdmiralProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "ADMIRAL", createLayout())
{
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        const auto& id = specs()[(size_t) i].id;
        params[(size_t) i] = apvts.getParameter (id);
        raw[(size_t) i] = apvts.getRawParameterValue (id);
        ccValue[(size_t) i].store (0.f);
        ccDirty[(size_t) i].store (false);
        ccIdle[(size_t) i] = -1;
    }
    for (auto& e : ccMap) e.store (-1);
    for (auto& v : pat) v.store (0.f);
    for (auto& p : preview) p.store (false);
    formats.registerBasicFormats();

    // Eingebaute Kits aus den eingebetteten WAV-Dateien lesen
    for (int k = 0; k < NUM_KITS; ++k)
        for (int t = 0; t < NUM_TRACKS; ++t)
        {
            const juce::String res = juce::String (kitNames()[(size_t) k]) + "_" + tracks()[(size_t) t].file + "_wav";
            int size = 0;
            if (const char* data = BinaryData::getNamedResource (res.toRawUTF8(), size))
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (new juce::MemoryInputStream (data, (size_t) size, false), true));
                if (r != nullptr && r->lengthInSamples > 1)
                {
                    auto s = std::make_shared<SampleData>();
                    s->sampleRate = r->sampleRate;
                    s->name = juce::String (kitNames()[(size_t) k]).toUpperCase().toStdString() + " KIT";
                    juce::AudioBuffer<float> buf (1, (int) r->lengthInSamples);
                    r->read (&buf, 0, (int) r->lengthInSamples, 0, true, false);
                    s->data.assign (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
                    kits[(size_t) k][(size_t) t] = s;
                }
            }
        }
    updateSlots();

    // Start-Pattern
    generateNew();
    startTimerHz (30);
}

bool AdmiralProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& s = layouts.outputBuses.getReference (i);
        if (! s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    return layouts.inputBuses.isEmpty();
}

void AdmiralProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    for (auto& f : fx) f.prepare ((float) sampleRate);
    masterBus.prepare (sampleRate);
    trackBuf.setSize (2 * NUM_TRACKS, kChunk, false, true, false);
    seq.reset();
    events.reserve (512);
    for (auto& p : pendingOff) p.note = -1;
}

//==============================================================================
void AdmiralProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    // Einstellungen der Spuren
    std::array<TrackSettings, NUM_TRACKS> ts;
    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        auto& s = ts[(size_t) t];
        s.level = getValue (tp (t, TP_LEVEL));
        s.pan   = getValue (tp (t, TP_PAN));
        s.tune  = getValue (tp (t, TP_TUNE));
        s.decay = getValue (tp (t, TP_DECAY));
        s.mute  = getValue (tp (t, TP_MUTE)) > 0.5f;
    }
    const bool midiOut = getValue (GP_MIDIOUT) > 0.5f;
    juce::MidiBuffer outMidi;

    auto sendNote = [&] (int track, float vel, int offset)
    {
        if (! midiOut) return;
        const int note = tracks()[(size_t) track].gmNote;
        const int pos = juce::jlimit (0, juce::jmax (0, n - 1), offset);
        outMidi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (vel * 127.f))), pos);
        for (auto& p : pendingOff)
            if (p.note < 0) { p.note = note; p.samplesLeft = pos + (int) (0.06 * sr); break; }
    };

    // Eingehendes MIDI: Noten spielen die Spuren (Pads des Keyboards), CCs steuern Parameter
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int t = trackForNote (m.getNoteNumber());
            if (t >= 0) engine.trigger (t, m.getFloatVelocity(), meta.samplePosition, ts[(size_t) t]);
        }
        else if (m.isController())
            handleController (m.getControllerNumber(), m.getControllerValue());
        else if (m.isAllSoundOff())
            engine.allSoundOff();
    }

    // Vorhören (Klick auf die Spur in der Oberfläche)
    for (int t = 0; t < NUM_TRACKS; ++t)
        if (preview[(size_t) t].exchange (false))
        {
            auto s = ts[(size_t) t]; s.mute = false;
            engine.trigger (t, 0.9f, 0, s);
        }

    // Position: Host-Transport oder interner Lauf
    double bpm = getValue (GP_TEMPO), ppq = 0.0;
    bool hostPlay = false, haveHostBpm = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { bpm = *b; haveHostBpm = true; }
            if (auto q = pos->getPpqPosition()) { ppq = *q; hostPlay = pos->getIsPlaying(); }
        }
    if (! haveHostBpm) bpm = getValue (GP_TEMPO);
    bpm = juce::jlimit (20.0, 400.0, bpm);
    currentBpm.store (bpm);
    hostPlaying.store (hostPlay);

    const bool run = getValue (GP_RUN) > 0.5f;
    bool sequencing = false;
    if (hostPlay) { sequencing = true; wasRunning = false; }
    else if (run)
    {
        if (! wasRunning) { internalPpq = 0.0; wasRunning = true; seq.reset(); }
        ppq = internalPpq;
        sequencing = true;
    }
    else wasRunning = false;

    if (sequencing)
    {
        Pattern mainP, fillP;
        for (int t = 0; t < NUM_TRACKS; ++t)
            for (int k = 0; k < NUM_STEPS; ++k)
            {
                mainP.v[(size_t) t][(size_t) k] = pat[idx (0, t, k)].load (std::memory_order_relaxed);
                fillP.v[(size_t) t][(size_t) k] = pat[idx (1, t, k)].load (std::memory_order_relaxed);
            }
        Sequencer::Block b;
        b.ppqStart = ppq; b.bpm = bpm; b.sampleRate = sr; b.numSamples = n;
        b.swing = getValue (GP_SWING); b.humanize = getValue (GP_HUMANIZE);
        b.fillEvery = fillEveryBars ((int) std::lround (getValue (GP_FILL)));
        events.clear();
        seq.collect (b, mainP, fillP, events);
        for (const auto& e : events)
        {
            engine.trigger (e.track, e.velocity, e.offset, ts[(size_t) e.track]);
            if (! ts[(size_t) e.track].mute) sendNote (e.track, e.velocity, e.offset);
        }
        playStep.store (seq.lastStep());
        playFill.store (seq.lastWasFill());
        playBar.store ((long long) std::floor (ppq / 4.0));
        if (! hostPlay) internalPpq += (double) n * bpm / (60.0 * sr);
    }
    else
    {
        playStep.store (-1);
        seq.reset();
    }

    // Note-Offs der MIDI-Ausgabe (Zeit relativ zum Blockanfang)
    for (auto& p : pendingOff)
    {
        if (p.note < 0) continue;
        if (p.samplesLeft < n)
        {
            outMidi.addEvent (juce::MidiMessage::noteOff (10, p.note), juce::jmax (0, p.samplesLeft));
            p.note = -1;
        }
        else p.samplesLeft -= n;
    }

    // Ziele der Spuren: Haupt-Ausgang oder (falls zugeschaltet) eigener Ausgang
    auto mainBus = getBusBuffer (buffer, false, 0);
    float* mainL = mainBus.getWritePointer (0);
    float* mainR = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer (1) : mainL;
    float* dl[NUM_TRACKS]; float* dr[NUM_TRACKS];
    bool usesAux[NUM_TRACKS] {};
    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        dl[t] = mainL; dr[t] = mainR;
        if (getValue (tp (t, TP_OUT)) > 0.5f && t + 1 < getBusCount (false))
        {
            auto* bus = getBus (false, t + 1);
            if (bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2)
            {
                auto aux = getBusBuffer (buffer, false, t + 1);
                dl[t] = aux.getWritePointer (0);
                dr[t] = aux.getWritePointer (1);
                usesAux[t] = true;
            }
        }
    }
    // Spuren einzeln berechnen (je Spur eigene Effektkette), dann auf ihr Ziel addieren
    std::array<FxSettings, NUM_TRACKS> fxs;
    for (int t = 0; t < NUM_TRACKS; ++t) fxs[(size_t) t] = fxSettings (t);
    for (int pos = 0; pos < n; pos += kChunk)
    {
        const int c = juce::jmin (kChunk, n - pos);
        float* tl[NUM_TRACKS]; float* tr[NUM_TRACKS];
        for (int t = 0; t < NUM_TRACKS; ++t)
        {
            tl[t] = trackBuf.getWritePointer (2 * t);
            tr[t] = trackBuf.getWritePointer (2 * t + 1);
            juce::FloatVectorOperations::clear (tl[t], c);
            juce::FloatVectorOperations::clear (tr[t], c);
        }
        engine.render (tl, tr, c);
        for (int t = 0; t < NUM_TRACKS; ++t)
        {
            fx[(size_t) t].process (tl[t], tr[t], c, fxs[(size_t) t], bpm);
            juce::FloatVectorOperations::add (dl[t] + pos, tl[t], c);
            juce::FloatVectorOperations::add (dr[t] + pos, tr[t], c);
        }
    }

    // Master (geglättet) und sanfte Begrenzung
    const float master = getValue (GP_MASTER);
    const float m0 = lastMaster, m1 = master * master * 1.25f;
    lastMaster = m1;
    auto applyMaster = [&] (float* p)
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = m0 + (m1 - m0) * (float) i / (float) n;
            p[i] = softLimit (p[i] * g);
        }
    };
    // Haupt-Summe: Master-Lautstärke, dann GLUE (Bus-Kompressor) und TAPE, zuletzt sanfte Begrenzung
    for (int i = 0; i < n; ++i)
    {
        const float g = m0 + (m1 - m0) * (float) i / (float) n;
        mainL[i] *= g;
        if (mainR != mainL) mainR[i] *= g;
    }
    masterBus.process (mainL, mainR, n, getValue (GP_GLUE), getValue (GP_TAPE));
    for (int i = 0; i < n; ++i)
    {
        mainL[i] = softLimit (mainL[i]);
        if (mainR != mainL) mainR[i] = softLimit (mainR[i]);
    }
    for (int t = 0; t < NUM_TRACKS; ++t)
        if (usesAux[t])
        {
            bool seen = false;
            for (int u = 0; u < t; ++u) if (usesAux[u] && dl[u] == dl[t]) seen = true;
            if (! seen) { applyMaster (dl[t]); applyMaster (dr[t]); }
        }

    midi.swapWith (outMidi);
}

//==============================================================================
void AdmiralProcessor::handleController (int cc, int value)
{
    lastCc.store (cc);
    lastCcValue.store (value);
    if (cc == 0 || cc == 32 || cc >= 120) return;

    const int learn = learnIndex.load();
    if (learn >= 0)
    {
        for (auto& e : ccMap)
            if (e.load() == learn) e.store (-1);
        ccMap[(size_t) cc].store (learn);
        learnedCc.store (cc);
        learnedParam.store (learn);
        learnEvent.fetch_add (1);
        learnIndex.store (-1);
    }
    const int target = ccMap[(size_t) cc].load();
    if (target >= 0) applyNormalised (target, (float) value / 127.f);
}

void AdmiralProcessor::applyNormalised (int paramIndex, float normalised)
{
    if (auto* p = params[(size_t) paramIndex])
    {
        p->setValue (normalised);
        ccValue[(size_t) paramIndex].store (normalised);
        ccDirty[(size_t) paramIndex].store (true);
    }
}

void AdmiralProcessor::flushMidiParamChanges()
{
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        auto* p = params[(size_t) i];
        if (p == nullptr) continue;
        if (ccDirty[(size_t) i].exchange (false))
        {
            if (ccIdle[(size_t) i] < 0) p->beginChangeGesture();
            ccIdle[(size_t) i] = 0;
            p->setValueNotifyingHost (ccValue[(size_t) i].load());
        }
        else if (ccIdle[(size_t) i] >= 0 && ++ccIdle[(size_t) i] > 6)
        {
            p->endChangeGesture();
            ccIdle[(size_t) i] = -1;
        }
    }
}

int AdmiralProcessor::getCcForParam (int paramIndex) const
{
    for (int cc = 0; cc < 128; ++cc)
        if (ccMap[(size_t) cc].load() == paramIndex) return cc;
    return -1;
}

void AdmiralProcessor::clearMappingForParam (int paramIndex)
{
    for (auto& e : ccMap)
        if (e.load() == paramIndex) e.store (-1);
}

void AdmiralProcessor::clearAllMappings()
{
    for (auto& e : ccMap) e.store (-1);
}

void AdmiralProcessor::timerCallback()
{
    flushMidiParamChanges();
    const int kit = (int) std::lround (getValue (GP_KIT));
    if (kit != shownKit) updateSlots();
}

//==============================================================================
void AdmiralProcessor::updateSlots()
{
    shownKit = juce::jlimit (0, NUM_KITS - 1, (int) std::lround (getValue (GP_KIT)));
    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        auto s = userSample[(size_t) t] != nullptr ? userSample[(size_t) t] : kits[(size_t) shownKit][(size_t) t];
        engine.setSample (t, s.get());
    }
}

juce::String AdmiralProcessor::getSampleName (int track) const
{
    if (auto& u = userSample[(size_t) track]) return juce::String (u->name);
    return juce::String (kitNames()[(size_t) juce::jlimit (0, NUM_KITS - 1, shownKit)]).toUpperCase() + " KIT";
}

FxSettings AdmiralProcessor::fxSettings (int t) const
{
    FxSettings f;
    f.dsType  = (int) std::lround (getValue (tp (t, TP_DS_TYPE)));
    f.dsDrive = getValue (tp (t, TP_DS_DRIVE));
    f.dsTone  = getValue (tp (t, TP_DS_TONE));
    f.dsMix   = getValue (tp (t, TP_DS_MIX));
    f.dlSync  = getValue (tp (t, TP_DL_SYNC)) > 0.5f;
    f.dlTimeMs = getValue (tp (t, TP_DL_TIME));
    f.dlDiv   = (int) std::lround (getValue (tp (t, TP_DL_DIV)));
    f.dlFb    = getValue (tp (t, TP_DL_FB));
    f.dlMix   = getValue (tp (t, TP_DL_MIX));
    f.dlPing  = getValue (tp (t, TP_DL_PING)) > 0.5f;
    f.rvSize  = getValue (tp (t, TP_RV_SIZE));
    f.rvDamp  = getValue (tp (t, TP_RV_DAMP));
    f.rvMix   = getValue (tp (t, TP_RV_MIX));
    f.hp      = getValue (tp (t, TP_FLT_HP));
    f.lp      = getValue (tp (t, TP_FLT_LP));
    f.res     = getValue (tp (t, TP_FLT_RES));
    return f;
}

void AdmiralProcessor::setAllOuts (bool on)
{
    for (int t = 0; t < NUM_TRACKS; ++t)
        if (auto* p = params[(size_t) tp (t, TP_OUT)])
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (on ? 1.f : 0.f);
            p->endChangeGesture();
        }
}

bool AdmiralProcessor::allOutsOn() const
{
    for (int t = 0; t < NUM_TRACKS; ++t)
        if (getValue (tp (t, TP_OUT)) < 0.5f) return false;
    return true;
}

bool AdmiralProcessor::isSampleFile (const juce::File& f) const
{
    return formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

juce::String AdmiralProcessor::getSampleWildcard() const
{
    return formats.getWildcardForAllFormats();
}

bool AdmiralProcessor::loadSampleFile (int track, const juce::File& file, juce::String& message)
{
    if (! juce::isPositiveAndBelow (track, NUM_TRACKS)) return false;
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    if (r == nullptr || r->lengthInSamples < 2)
    {
        message = "CANNOT READ " + file.getFileName().toUpperCase();
        return false;
    }
    const auto len = (int) juce::jmin (r->lengthInSamples, (juce::int64) (r->sampleRate * kMaxSampleSeconds));
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, r->numChannels), len);
    r->read (&buf, 0, len, 0, true, true);

    auto s = std::make_shared<SampleData>();
    s->sampleRate = r->sampleRate;
    s->name = file.getFileNameWithoutExtension().toStdString();
    s->data.resize ((size_t) len);
    const int ch = buf.getNumChannels();
    for (int i = 0; i < len; ++i)
    {
        float v = 0.f;
        for (int c = 0; c < ch; ++c) v += buf.getSample (c, i);
        s->data[(size_t) i] = v / (float) ch;
    }
    userSample[(size_t) track] = s;
    keepAlive.push_back (s);
    updateSlots();
    message = juce::String (tracks()[(size_t) track].name) + ": " + file.getFileName().toUpperCase()
            + (r->lengthInSamples > len ? "  (CUT TO 10 S)" : "");
    return true;
}

void AdmiralProcessor::resetSample (int track)
{
    if (! juce::isPositiveAndBelow (track, NUM_TRACKS)) return;
    userSample[(size_t) track] = nullptr;      // keepAlive hält das alte Sample, bis der Processor endet
    updateSlots();
}

//==============================================================================
void AdmiralProcessor::setStep (int which, int track, int step, float v)
{
    if (! juce::isPositiveAndBelow (which, 2) || ! juce::isPositiveAndBelow (track, NUM_TRACKS) || ! juce::isPositiveAndBelow (step, NUM_STEPS)) return;
    pat[idx (which, track, step)].store (juce::jlimit (0.f, 1.f, v));
    patternRevision.fetch_add (1);
}

Pattern AdmiralProcessor::getPattern (int which) const
{
    Pattern p;
    for (int t = 0; t < NUM_TRACKS; ++t)
        for (int k = 0; k < NUM_STEPS; ++k)
            p.v[(size_t) t][(size_t) k] = pat[idx (which, t, k)].load();
    return p;
}

void AdmiralProcessor::setPattern (int which, const Pattern& p)
{
    for (int t = 0; t < NUM_TRACKS; ++t)
        for (int k = 0; k < NUM_STEPS; ++k)
            pat[idx (which, t, k)].store (p.v[(size_t) t][(size_t) k]);
    patternRevision.fetch_add (1);
}

void AdmiralProcessor::clearPattern (int which)
{
    Pattern p; p.clear();
    setPattern (which, p);
}

void AdmiralProcessor::copyMainToFill()
{
    setPattern (1, getPattern (0));
}

GenSettings AdmiralProcessor::currentGenSettings() const
{
    GenSettings s;
    s.genre = (int) std::lround (getValue (GP_GENRE));
    s.density = getValue (GP_DENSITY);
    s.variation = getValue (GP_VARIATION);
    return s;
}

void AdmiralProcessor::applyGen (const GenResult& g)
{
    setPattern (0, g.main);
    setPattern (1, g.fill);
}

void AdmiralProcessor::generateNew()
{
    const uint32_t seed = (uint32_t) juce::Time::getMillisecondCounter() * 2654435761u + (++seedCounter) * 40503u;
    const auto g = Generator::generate (currentGenSettings(), seed, locked, currentGen());
    // Verlauf: alles hinter der aktuellen Position verwerfen, dann anhängen
    if (histPos >= 0 && histPos < (int) history.size() - 1)
        history.erase (history.begin() + histPos + 1, history.end());
    history.push_back (g);
    if (history.size() > 32) history.erase (history.begin());
    histPos = (int) history.size() - 1;
    applyGen (g);
}

void AdmiralProcessor::regenerateFill()
{
    auto main = getPattern (0);
    const uint32_t seed = (uint32_t) juce::Time::getMillisecondCounter() + (++seedCounter) * 7919u;
    auto fill = Generator::makeFill (main, currentGenSettings(), seed);
    auto prev = getPattern (1);
    for (int t = 0; t < NUM_TRACKS; ++t)
        if (locked[(size_t) t]) fill.v[(size_t) t] = prev.v[(size_t) t];
    setPattern (1, fill);
}

bool AdmiralProcessor::historyBack()
{
    if (histPos <= 0) return false;
    applyGen (history[(size_t) --histPos]);
    return true;
}

bool AdmiralProcessor::historyForward()
{
    if (histPos < 0 || histPos >= (int) history.size() - 1) return false;
    applyGen (history[(size_t) ++histPos]);
    return true;
}

//==============================================================================
bool AdmiralProcessor::writeMidiFile (const juce::File& dest, int bars) const
{
    constexpr int ppqTicks = 960;
    const auto mainP = getPattern (0), fillP = getPattern (1);
    const int fillEvery = fillEveryBars ((int) std::lround (getValue (GP_FILL)));
    const float swing = getValue (GP_SWING);
    std::array<bool, NUM_TRACKS> muted {};
    for (int t = 0; t < NUM_TRACKS; ++t) muted[(size_t) t] = getValue (tp (t, TP_MUTE)) > 0.5f;

    juce::MidiMessageSequence seqOut;
    const double bpm = currentBpm.load();
    seqOut.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0.0);
    seqOut.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    auto name = juce::MidiMessage::textMetaEvent (3, "Admiral " + juce::String (genreNames()[(size_t) juce::jlimit (0, 6, (int) std::lround (getValue (GP_GENRE)))]));
    seqOut.addEvent (name, 0.0);

    for (int bar = 0; bar < bars; ++bar)
    {
        const bool isFill = isFillBar (bar, fillEvery);
        const Pattern& p = isFill ? fillP : mainP;
        for (int k = 0; k < NUM_STEPS; ++k)
        {
            const double tick = ((double) bar * 4.0 + stepOffsetPpq (k, swing)) * ppqTicks;
            for (int t = 0; t < NUM_TRACKS; ++t)
            {
                if (muted[(size_t) t]) continue;
                float v = p.v[(size_t) t][(size_t) k];
                if (t == T_CRASH && k == 0 && crashAfterFill (bar, fillEvery)) v = juce::jmax (v, 0.9f);
                if (v <= 0.f) continue;
                const int note = tracks()[(size_t) t].gmNote;
                const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (v * 127.f));
                seqOut.addEvent (juce::MidiMessage::noteOn (10, note, vel), tick);
                seqOut.addEvent (juce::MidiMessage::noteOff (10, note), tick + ppqTicks / 8.0);
            }
        }
    }
    seqOut.addEvent (juce::MidiMessage::endOfTrack(), (double) bars * 4.0 * ppqTicks);
    seqOut.sort();
    seqOut.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppqTicks);
    mf.addTrack (seqOut);
    dest.deleteFile();
    juce::FileOutputStream out (dest);
    if (! out.openedOk()) return false;
    return mf.writeTo (out, 1);
}

juce::File AdmiralProcessor::createMidiExportFile (int bars) const
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Admiral 1506");
    dir.createDirectory();
    const juce::String genre = genreNames()[(size_t) juce::jlimit (0, 6, (int) std::lround (getValue (GP_GENRE)))];
    const auto f = dir.getChildFile (juce::File::createLegalFileName ("Admiral " + genre.replace ("/", "-") + " "
                                     + juce::String (juce::roundToInt (currentBpm.load())) + "bpm " + juce::String (bars) + "bars.mid"));
    return writeMidiFile (f, bars) ? f : juce::File();
}

//==============================================================================
void AdmiralProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();

    juce::ValueTree pt ("PATTERN");
    for (int w = 0; w < 2; ++w)
    {
        juce::String s;
        for (int t = 0; t < NUM_TRACKS; ++t)
            for (int k = 0; k < NUM_STEPS; ++k)
                s << juce::String (pat[idx (w, t, k)].load(), 3) << " ";
        pt.setProperty (w == 0 ? "main" : "fill", s.trim(), nullptr);
    }
    juce::String lk;
    for (bool b : locked) lk << (b ? "1" : "0");
    pt.setProperty ("locks", lk, nullptr);
    state.appendChild (pt, nullptr);

    juce::ValueTree map ("MIDIMAP");
    for (int cc = 0; cc < 128; ++cc)
    {
        const int i = ccMap[(size_t) cc].load();
        if (i < 0) continue;
        juce::ValueTree e ("MAP");
        e.setProperty ("cc", cc, nullptr);
        e.setProperty ("param", juce::String (specs()[(size_t) i].id), nullptr);
        map.appendChild (e, nullptr);
    }
    state.appendChild (map, nullptr);

    for (int t = 0; t < NUM_TRACKS; ++t)
        if (auto& s = userSample[(size_t) t])
        {
            juce::ValueTree smp ("SAMPLE");
            smp.setProperty ("track", t, nullptr);
            smp.setProperty ("name", juce::String (s->name), nullptr);
            smp.setProperty ("data", encodeSample (*s), nullptr);
            state.appendChild (smp, nullptr);
        }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void AdmiralProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);

    if (auto pt = tree.getChildWithName ("PATTERN"); pt.isValid())
    {
        for (int w = 0; w < 2; ++w)
        {
            juce::StringArray tok;
            tok.addTokens (pt.getProperty (w == 0 ? "main" : "fill").toString(), " ", "");
            tok.removeEmptyStrings();
            if (tok.size() == NUM_TRACKS * NUM_STEPS)
                for (int i = 0; i < tok.size(); ++i)
                    pat[(size_t) (w * NUM_TRACKS * NUM_STEPS + i)].store (juce::jlimit (0.f, 1.f, tok[i].getFloatValue()));
        }
        const auto lk = pt.getProperty ("locks").toString();
        for (int t = 0; t < NUM_TRACKS; ++t) locked[(size_t) t] = lk.length() > t && lk[t] == '1';
        patternRevision.fetch_add (1);
        history.clear();
        history.push_back (currentGen());
        histPos = 0;
    }

    if (auto map = tree.getChildWithName ("MIDIMAP"); map.isValid())
    {
        clearAllMappings();
        for (auto e : map)
        {
            const int cc = (int) e.getProperty ("cc", -1);
            const int i = indexOf (e.getProperty ("param").toString());
            if (cc >= 0 && cc < 128 && i >= 0) ccMap[(size_t) cc].store (i);
        }
    }

    for (auto& u : userSample) u = nullptr;
    for (auto c : tree)
        if (c.hasType ("SAMPLE"))
        {
            const int t = (int) c.getProperty ("track", -1);
            if (! juce::isPositiveAndBelow (t, NUM_TRACKS)) continue;
            if (auto s = decodeSample (c.getProperty ("data").toString(), c.getProperty ("name").toString()))
            {
                userSample[(size_t) t] = s;
                keepAlive.push_back (s);
            }
        }

    // Hilfsknoten entfernen, bevor der Parameterzustand übernommen wird
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        const auto type = tree.getChild (i).getType().toString();
        if (type == "PATTERN" || type == "MIDIMAP" || type == "SAMPLE") tree.removeChild (i, nullptr);
    }
    apvts.replaceState (tree);
    updateSlots();
}

//==============================================================================
juce::String AdmiralProcessor::encodeSample (const SampleData& s)
{
    const juce::uint32 n = (juce::uint32) s.data.size();
    const juce::uint32 dataBytes = n * 2, rate = (juce::uint32) juce::roundToInt (s.sampleRate);
    juce::MemoryOutputStream out;
    out.write ("RIFF", 4);                 out.writeInt ((int) (36 + dataBytes));
    out.write ("WAVEfmt ", 8);             out.writeInt (16);
    out.writeShort (1);                    out.writeShort (1);
    out.writeInt ((int) rate);             out.writeInt ((int) (rate * 2));
    out.writeShort (2);                    out.writeShort (16);
    out.write ("data", 4);                 out.writeInt ((int) dataBytes);
    for (float v : s.data)
        out.writeShort ((short) juce::jlimit (-32767, 32767, juce::roundToInt (v * 32767.f)));
    return juce::Base64::toBase64 (out.getData(), out.getDataSize());
}

std::shared_ptr<SampleData> AdmiralProcessor::decodeSample (const juce::String& base64, const juce::String& name)
{
    if (base64.isEmpty()) return nullptr;
    juce::MemoryOutputStream decoded;
    if (! juce::Base64::convertFromBase64 (decoded, base64)) return nullptr;

    const auto* p = static_cast<const juce::uint8*> (decoded.getData());
    const size_t size = decoded.getDataSize();
    if (size < 46 || std::memcmp (p, "RIFF", 4) != 0 || std::memcmp (p + 8, "WAVEfmt ", 8) != 0) return nullptr;

    auto rd32 = [&] (size_t o) { return (juce::uint32) p[o] | ((juce::uint32) p[o + 1] << 8) | ((juce::uint32) p[o + 2] << 16) | ((juce::uint32) p[o + 3] << 24); };
    const juce::uint32 rate = rd32 (24);
    const juce::uint32 dataBytes = rd32 (40);
    if (rate < 1000 || rate > 400000 || 44 + (size_t) dataBytes > size || dataBytes < 4) return nullptr;

    auto s = std::make_shared<SampleData>();
    s->sampleRate = (double) rate;
    s->name = name.toStdString();
    const size_t n = std::min ((size_t) (dataBytes / 2), (size_t) (rate * kMaxSampleSeconds) + 16);
    s->data.resize (n);
    for (size_t i = 0; i < n; ++i)
        s->data[i] = (float) (short) (p[44 + 2 * i] | (p[45 + 2 * i] << 8)) * (1.f / 32767.f);
    return s;
}

juce::AudioProcessorEditor* AdmiralProcessor::createEditor() { return new AdmiralEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AdmiralProcessor(); }
