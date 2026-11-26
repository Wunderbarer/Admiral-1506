// Admiral 1506 - Test des kompletten Plugins (Processor + Oberfläche) ohne DAW:
// spielt 8 Takte mit simuliertem Host-Transport ab, prüft Audio, MIDI-Ausgabe, Einzelausgänge,
// Zustand speichern/laden und MIDI-Export und speichert ein Bild der Oberfläche.
// Aufruf: plugin_test <Ausgabe-Ordner>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "PluginProcessor.h"
#include "PluginEditor.h"


namespace
{
struct FakeHost : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 124.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (bpm);
        i.setPpqPosition (ppq);
        i.setIsPlaying (playing);
        return i;
    }
};

int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "OK    " : "FEHLER", what.toRawUTF8());
    if (! ok) ++failures;
}

struct RunResult { double rms = 0; float peak = 0; bool nan = false; int noteOns = 0; double auxRms = 0; };

RunResult run (AdmiralProcessor& p, int bars, int auxBus = -1)
{
    const double sr = 48000.0;
    const int block = 480;
    FakeHost host;
    p.setPlayHead (&host);
    p.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    RunResult r;
    const int blocks = (int) (bars * 4.0 * 60.0 / host.bpm * sr / block);
    double sum = 0, auxSum = 0; long long cnt = 0;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        p.processBlock (buf, midi);
        for (const auto m : midi) if (m.getMessage().isNoteOn()) ++r.noteOns;
        for (int i = 0; i < block; ++i)
        {
            const float l = buf.getSample (0, i), rr = buf.getSample (1, i);
            if (! std::isfinite (l) || ! std::isfinite (rr)) r.nan = true;
            r.peak = std::max ({ r.peak, std::abs (l), std::abs (rr) });
            sum += (double) l * l; ++cnt;
            if (auxBus > 0)
            {
                auto aux = p.getBusBuffer (buf, false, auxBus);
                auxSum += (double) aux.getSample (0, i) * aux.getSample (0, i);
            }
        }
        host.ppq += block / (sr * 60.0 / host.bpm);
    }
    r.rms = std::sqrt (sum / (double) cnt);
    r.auxRms = std::sqrt (auxSum / (double) cnt);
    p.setPlayHead (nullptr);
    return r;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory());
    out.createDirectory();

    // Hörbeispiel: plugin_test <Ordner> --demo  (rendert mehrere Genres mit Fills, GLUE und TAPE in eine WAV-Datei)
    const bool kitDemo = argc > 2 && juce::String (argv[2]) == "--kits";
    if (argc > 2 && (juce::String (argv[2]) == "--demo" || kitDemo))
    {
        std::unique_ptr<AdmiralProcessor> d (dynamic_cast<AdmiralProcessor*> (createPluginFilter()));
        const double sr = 48000.0; const int block = 512;
        std::vector<float> L, R;
        auto setP = [&] (int idx, float plain) { auto* q = d->getParam (idx); q->setValueNotifyingHost (q->convertTo0to1 (plain)); };
        const int genres[] = { 0, 1, 3, 4, 5 };
        if (kitDemo)
        {
            // ein Pattern (Synthwave), nacheinander mit sechs gewürfelten Kits
            setP (ad::GP_GENRE, 0.f); setP (ad::GP_FILL, 0.f);
            d->generateNew();
            for (int i = 0; i < 6; ++i)
            {
                d->newKit();
                d->pollParameters();
                std::printf ("Kit %d: %s  (Kits: ", i + 1, d->getKitCharacter().toRawUTF8());
                for (int t = 0; t < ad::NUM_TRACKS; ++t) std::printf ("%c", ad::kitNames()[(size_t) d->resolvedKit (t)][0]);
                std::printf (")\n");
                FakeHost host; host.bpm = 100.0;
                d->setPlayHead (&host);
                d->prepareToPlay (sr, block);
                juce::AudioBuffer<float> buf (d->getTotalNumOutputChannels(), block);
                juce::MidiBuffer midi;
                const int blocks = (int) (2 * 4.0 * 60.0 / host.bpm * sr / block);
                for (int b = 0; b < blocks; ++b)
                {
                    midi.clear();
                    d->processBlock (buf, midi);
                    for (int j = 0; j < block; ++j) { L.push_back (buf.getSample (0, j)); R.push_back (buf.getSample (1, j)); }
                    host.ppq += block / (sr * 60.0 / host.bpm);
                }
                d->setPlayHead (nullptr);
                for (int j = 0; j < (int) (0.6 * sr); ++j) { L.push_back (0.f); R.push_back (0.f); }   // Pause zwischen den Kits
            }
        }
        const int kitsUsed[] = { 2, 1, 2, 0, 1 };
        for (int gi = 0; gi < (kitDemo ? 0 : 5); ++gi)
        {
            const int g = genres[gi];
            setP (ad::GP_GENRE, (float) g); setP (ad::GP_KIT, (float) kitsUsed[gi]);
            setP (ad::GP_FILL, 1.f); setP (ad::GP_SWING, g == 3 ? 0.35f : 0.f);
            setP (ad::tp (ad::T_SNARE, ad::TP_RV_MIX), 0.25f);
            setP (ad::tp (ad::T_CLAP, ad::TP_RV_MIX), 0.3f);
            setP (ad::tp (ad::T_RIM, ad::TP_DL_MIX), 0.35f);
            d->generateNew();
            d->pollParameters();
            FakeHost host; host.bpm = ad::Generator::suggestedBpm (g);
            d->setPlayHead (&host);
            d->prepareToPlay (sr, block);
            juce::AudioBuffer<float> buf (d->getTotalNumOutputChannels(), block);
            juce::MidiBuffer midi;
            const int blocks = (int) (4 * 4.0 * 60.0 / host.bpm * sr / block);
            for (int b = 0; b < blocks; ++b)
            {
                midi.clear();
                d->processBlock (buf, midi);
                for (int i = 0; i < block; ++i) { L.push_back (buf.getSample (0, i)); R.push_back (buf.getSample (1, i)); }
                host.ppq += block / (sr * 60.0 / host.bpm);
            }
            d->setPlayHead (nullptr);
        }
        juce::MemoryOutputStream o;
        const auto nS = (juce::uint32) L.size();
        o.write ("RIFF", 4); o.writeInt ((int) (36 + nS * 4)); o.write ("WAVEfmt ", 8); o.writeInt (16); o.writeShort (1); o.writeShort (2);
        o.writeInt (48000); o.writeInt (48000 * 4); o.writeShort (4); o.writeShort (16); o.write ("data", 4); o.writeInt ((int) (nS * 4));
        for (size_t i = 0; i < L.size(); ++i)
        {
            o.writeShort ((short) juce::jlimit (-32767, 32767, juce::roundToInt (L[i] * 32767.f)));
            o.writeShort ((short) juce::jlimit (-32767, 32767, juce::roundToInt (R[i] * 32767.f)));
        }
        out.getChildFile ("admiral_demo.wav").replaceWithData (o.getData(), o.getDataSize());
        std::printf ("Demo geschrieben: %s\n", out.getChildFile ("admiral_demo.wav").getFullPathName().toRawUTF8());
        return 0;
    }

    std::unique_ptr<AdmiralProcessor> p (dynamic_cast<AdmiralProcessor*> (createPluginFilter()));
    check (p != nullptr, "Plugin erzeugt");
    check (p->getPattern (0).count() > 5, "Start-Pattern vorhanden (" + juce::String (p->getPattern (0).count()) + " Schläge)");
    check (p->getSampleName (0) == "ANALOG KIT", "Kit-Samples eingebettet (" + p->getSampleName (0) + ")");

    auto r = run (*p, 8);
    check (! r.nan, "kein NaN");
    check (r.rms > 0.02, "Ausgang hörbar (RMS " + juce::String (r.rms, 3) + ")");
    check (r.peak <= 1.0f, "keine Übersteuerung (Spitze " + juce::String (r.peak, 2) + ")");
    check (r.noteOns > 40, "MIDI-Ausgabe (" + juce::String (r.noteOns) + " Noten)");

    // alle Genres und beide Kits
    for (int g = 0; g < (int) ad::genreNames().size(); ++g)
    {
        p->getParam (ad::GP_GENRE)->setValueNotifyingHost (p->getParam (ad::GP_GENRE)->convertTo0to1 ((float) g));
        p->getParam (ad::GP_KIT)->setValueNotifyingHost (p->getParam (ad::GP_KIT)->convertTo0to1 ((float) (g % ad::NUM_KITS)));
        p->generateNew();
        p->pollParameters();   // sonst per Timer: Kit-Wechsel übernehmen
        auto rg = run (*p, 4);
        check (! rg.nan && rg.rms > 0.02 && rg.peak <= 1.f, juce::String (ad::genreNames()[(size_t) g]) + " / Kit " + juce::String (ad::kitNames()[(size_t) (g % ad::NUM_KITS)])
               + ": RMS " + juce::String (rg.rms, 3) + ", Spitze " + juce::String (rg.peak, 2));
    }

    // Verlauf
    const auto before = p->getPattern (0);
    p->generateNew();
    p->historyBack();
    const auto after = p->getPattern (0);
    check (std::memcmp (&before, &after, sizeof (ad::Pattern)) == 0, "Verlauf zurück");

    // Einzelausgang: Kick auf eigenen Ausgang
    {
        auto layout = p->getBusesLayout();
        check (layout.outputBuses.size() == 11 && layout.outputBuses.getReference (1) == juce::AudioChannelSet::stereo(),
               "11 Stereo-Ausgänge standardmäßig aktiv (Main + 10 Spuren)");
        check (p->setBusesLayout (layout), "Einzelausgang KICK zuschaltbar");
        p->getParam (ad::tp (ad::T_KICK, ad::TP_OUT))->setValueNotifyingHost (1.f);
        auto ra = run (*p, 2, 1);
        check (ra.auxRms > 0.01, "Kick klingt auf eigenem Ausgang (RMS " + juce::String (ra.auxRms, 3) + ")");
        p->getParam (ad::tp (ad::T_KICK, ad::TP_OUT))->setValueNotifyingHost (0.f);
    }

    // Effekte je Spur: Snare mit Hall + Delay wird lauter/länger, alle Effekte auf allen Spuren: Rechenzeit
    {
        auto setP = [&] (int idx, float plain) { auto* q = p->getParam (idx); q->setValueNotifyingHost (q->convertTo0to1 (plain)); };
        setP (ad::tp (ad::T_SNARE, ad::TP_OUT), 1.f);              // Snare einzeln messen (Ausgang 3)
        auto dry = run (*p, 4, 2);
        setP (ad::tp (ad::T_SNARE, ad::TP_RV_MIX), 0.6f);
        setP (ad::tp (ad::T_SNARE, ad::TP_DL_MIX), 0.5f);
        check (p->trackHasFx (ad::T_SNARE) && ! p->trackHasFx (ad::T_KICK), "FX-Anzeige je Spur");
        auto wet = run (*p, 4, 2);
        check (! wet.nan && wet.auxRms > dry.auxRms * 1.15, "Hall/Delay auf der Snare hörbar (RMS Snare-Ausgang " + juce::String (dry.auxRms, 3) + " -> " + juce::String (wet.auxRms, 3) + ")");
        setP (ad::tp (ad::T_SNARE, ad::TP_RV_MIX), 0.f);
        setP (ad::tp (ad::T_SNARE, ad::TP_DL_MIX), 0.f);
        setP (ad::tp (ad::T_SNARE, ad::TP_FLT_LP), 400.f);
        auto filt = run (*p, 4, 2);
        check (filt.auxRms < dry.auxRms * 0.8 && filt.auxRms > 0.0005, "Tiefpass 400 Hz dämpft die Snare (RMS " + juce::String (dry.auxRms, 3) + " -> " + juce::String (filt.auxRms, 3) + ")");
        setP (ad::tp (ad::T_SNARE, ad::TP_FLT_LP), 20000.f);
        setP (ad::tp (ad::T_SNARE, ad::TP_OUT), 0.f);

        // Master-Bus: GLUE komprimiert hörbar, TAPE bleibt pegelneutral
        setP (ad::GP_GLUE, 1.f);
        p->takePeakGainReductionDb();
        run (*p, 2);
        const float grDb = p->takePeakGainReductionDb();
        check (grDb > 1.f, "GLUE-Kompressor arbeitet (bis " + juce::String (grDb, 1) + " dB Pegelreduktion)");
        setP (ad::GP_GLUE, 0.f); setP (ad::GP_TAPE, 0.f);
        auto clean = run (*p, 4);
        setP (ad::GP_TAPE, 1.f);
        auto taped = run (*p, 4);
        check (! taped.nan && taped.rms > clean.rms * 0.6 && taped.rms < clean.rms * 1.4,
               "TAPE ohne Pegelsprung (RMS " + juce::String (clean.rms, 3) + " -> " + juce::String (taped.rms, 3) + ")");
        setP (ad::GP_GLUE, 0.3f); setP (ad::GP_TAPE, 0.25f);

        for (int t = 0; t < ad::NUM_TRACKS; ++t)
        {
            setP (ad::tp (t, ad::TP_DS_MIX), 1.f); setP (ad::tp (t, ad::TP_DS_DRIVE), 1.f); setP (ad::tp (t, ad::TP_DS_TYPE), 2.f);
            setP (ad::tp (t, ad::TP_DL_MIX), 1.f); setP (ad::tp (t, ad::TP_DL_FB), 0.9f); setP (ad::tp (t, ad::TP_DL_DIV), 8.f);
            setP (ad::tp (t, ad::TP_RV_MIX), 1.f); setP (ad::tp (t, ad::TP_RV_SIZE), 1.f);
        }
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        auto full = run (*p, 8);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double audioMs = 8 * 4 * 60000.0 / 124.0;
        check (! full.nan && full.peak <= 1.0f, "alle Effekte auf allen Spuren voll aufgedreht: stabil (Spitze " + juce::String (full.peak, 2) + ")");
        check (ms < audioMs, "Rechenzeit mit 30 Effekten: " + juce::String (100.0 * ms / audioMs, 1) + " % eines CPU-Kerns");
        for (int t = 0; t < ad::NUM_TRACKS; ++t)
            for (int q = ad::TP_DS_TYPE; q <= ad::TP_RV_MIX; ++q)
                p->getParam (ad::tp (t, q))->setValueNotifyingHost (p->getParam (ad::tp (t, q))->getDefaultValue());
        setP (ad::tp (ad::T_SNARE, ad::TP_RV_MIX), 0.3f);
        p->setAllOuts (true);
        check (p->allOutsOn(), "ALL OUTS schaltet alle Spuren um");
        p->setAllOuts (false);
    }

    // Kit-Designer: NEW KIT, MUTATE, Sound-Sperre, Verlauf
    {
        const auto kitStart = p->captureKit();
        p->setSoundLocked (ad::T_SNARE, true);
        int worst = 0;
        for (int i = 0; i < 12; ++i)
        {
            p->newKit();
            auto rk = run (*p, 1);
            if (rk.nan || rk.rms < 0.02 || rk.peak > 1.f) ++worst;
        }
        check (worst == 0, "12 Zufalls-Kits: alle hörbar, kein NaN, keine Übersteuerung");
        const auto kitEnd = p->captureKit();
        check (std::memcmp (&kitStart.t[ad::T_SNARE], &kitEnd.t[ad::T_SNARE], sizeof (ad::TrackSound)) == 0, "Sound-Sperre: Snare bleibt beim Würfeln gleich");
        check (p->getKitCharacter().isNotEmpty(), "Charakter des Kits: " + p->getKitCharacter());
        const auto k1 = p->captureKit();
        p->mutateKit();
        const auto k2 = p->captureKit();
        float maxTune = 0.f;
        for (int t = 0; t < ad::NUM_TRACKS; ++t) maxTune = std::max (maxTune, std::abs (k1.t[(size_t) t].tune - k2.t[(size_t) t].tune));
        check (maxTune > 0.f && maxTune <= 0.55f, "MUTATE variiert leicht (max. " + juce::String (maxTune, 2) + " Halbtöne)");
        p->kitHistoryBack();
        const auto k3 = p->captureKit();
        check (std::abs (k3.t[ad::T_KICK].tune - k1.t[ad::T_KICK].tune) < 0.06f, "Kit-Verlauf zurück");
        for (int i = 0; i < 20; ++i) p->kitHistoryBack();
        const auto k0 = p->captureKit();
        check (std::abs (k0.t[ad::T_KICK].level - kitStart.t[ad::T_KICK].level) < 0.01f && k0.globalKit == kitStart.globalKit, "Verlauf führt zum Ausgangskit zurück");
        p->kitHistoryForward();
    }

    // Zustand speichern / laden (inkl. eigenem Sample und Sperre)
    {
        p->setLocked (3, true);
        p->setStep (1, 2, 5, 0.42f);
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        std::unique_ptr<AdmiralProcessor> q (dynamic_cast<AdmiralProcessor*> (createPluginFilter()));
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int w = 0; w < 2; ++w)
            for (int t = 0; t < ad::NUM_TRACKS; ++t)
                for (int k = 0; k < ad::NUM_STEPS; ++k)
                    if (std::abs (p->getStep (w, t, k) - q->getStep (w, t, k)) > 0.002f) same = false;
        check (same, "Pattern nach Laden identisch");
        check (q->isLocked (3) && ! q->isLocked (2) && q->isSoundLocked (ad::T_SNARE) && ! q->isSoundLocked (ad::T_KICK), "Sperren (Pattern + Sound) nach Laden erhalten");
        bool srcSame = true;
        for (int t = 0; t < ad::NUM_TRACKS; ++t) if (q->resolvedKit (t) != p->resolvedKit (t)) srcSame = false;
        check (srcSame, "Klangquelle je Spur nach Laden erhalten");
        check (std::abs (q->getValue (ad::GP_GENRE) - p->getValue (ad::GP_GENRE)) < 0.01f, "Parameter nach Laden erhalten");
        check (std::abs (q->getValue (ad::tp (ad::T_SNARE, ad::TP_RV_MIX)) - 0.3f) < 0.01f, "Spur-Effekte nach Laden erhalten");
    }

    // MIDI-Export
    {
        const auto f = out.getChildFile ("export_test.mid");
        check (p->writeMidiFile (f, 8), "MIDI-Datei geschrieben");
        juce::FileInputStream in (f);
        juce::MidiFile mf;
        check (mf.readFrom (in), "MIDI-Datei lesbar");
        int notes = 0;
        if (mf.getNumTracks() > 0)
            for (auto* e : *mf.getTrack (0)) if (e->message.isNoteOn()) ++notes;
        const auto m = p->getPattern (0).count(), fl = p->getPattern (1).count();
        check (notes >= m * 6 + fl * 2, "MIDI-Export: " + juce::String (notes) + " Noten in 8 Takten");
    }

    // Oberfläche rendern (mit --no-gui überspringen, z. B. auf Build-Servern ohne Bildschirm)
    if (! (argc > 2 && juce::String (argv[2]) == "--no-gui"))
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (MainPanel::W, MainPanel::H);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const auto f = out.getChildFile ("admiral_ui.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        check (png.writeImageToStream (img, os), "Oberfläche gerendert: " + f.getFullPathName());
    }

    std::printf ("\n%s (%d Fehler)\n", failures == 0 ? "ALLE PLUGIN-TESTS BESTANDEN" : "PLUGIN-TESTS FEHLGESCHLAGEN", failures);
    return failures == 0 ? 0 : 1;
}
