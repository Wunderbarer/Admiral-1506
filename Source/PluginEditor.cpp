#include "PluginEditor.h"
#if JUCE_WINDOWS
 #include "NativeDrag.h"
#endif

using namespace ad;
using juce::Rectangle;

namespace
{
// Layout (Entwurfskoordinaten 1320 x 784, wird als Ganzes skaliert)
constexpr int kPatX = 20, kPatY = 92, kPatW = 1132, kPatH = 504;          // Bereich PATTERN
constexpr int kHeadX = 32, kHeadW = 152;                                   // Spurköpfe
constexpr int kGridX = 192, kGridY = 148;                                  // Schrittraster (inkl. Kopfzeile)
constexpr int kRowsY = kGridY + StepMatrix::kHeader;                       // erste Spur
constexpr int kKnobX = 894, kKnobW = 48, kKnobSize = 40;                   // Regler je Spur: TUNE DECAY PAN LEVEL
constexpr int kOutX = kKnobX + 4 * kKnobW + 6;                             // OUT-Taster
constexpr int kMasterX = 1162, kMasterW = 138;                             // Bereich MASTER
constexpr int kFxY = 606, kFxH = 142;                                      // TRACK FX
constexpr int kGenY = 758, kGenH = 142;                                    // GENERATOR / GROOVE
constexpr int kFootY = 906;

// ---- Zeichenhilfen wie im Diplomat ----
void drawSide (juce::Graphics& g, Rectangle<int> r, bool leftSide)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff12142b), (float) r.getX(), 0.f,
                                             juce::Colour (0xff05060d), (float) r.getRight(), 0.f, false));
    g.fillRect (r);
    const int ex = leftSide ? r.getRight() - 2 : r.getX();
    g.setColour (col::cyan.withAlpha (0.18f)); g.fillRect (ex - (leftSide ? 2 : -2), r.getY(), 4, r.getHeight());
    g.setColour (col::cyan.withAlpha (0.9f));  g.fillRect (ex, r.getY(), 2, r.getHeight());
    g.setColour (col::magenta.withAlpha (0.8f));
    for (int y = 40; y < r.getHeight(); y += 140) g.fillRect (r.getX() + 4, y, r.getWidth() - 8, 2);
}

void drawPanel (juce::Graphics& g, Rectangle<int> r, const juce::String& title)
{
    const auto rf = r.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff10132a), rf.getX(), rf.getY(),
                                             juce::Colour (0xff080a17), rf.getX(), rf.getBottom(), false));
    g.fillRoundedRectangle (rf, 6.f);
    g.setColour (col::edge);
    g.drawRoundedRectangle (rf.reduced (0.5f), 6.f, 1.f);
    g.setColour (col::cyan.withAlpha (0.75f));
    g.fillRect (rf.getRight() - 40.f, rf.getY() + 0.5f, 38.f, 1.5f);
    g.fillRect (rf.getRight() - 1.5f, rf.getY() + 2.f, 1.f, 14.f);

    if (title.isNotEmpty())
    {
        const auto bar = Rectangle<float> (rf.getX() + 1.f, rf.getY() + 1.f, rf.getWidth() - 2.f, 24.f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1f45), bar.getX(), 0.f, juce::Colour (0xff0a0b1a), bar.getRight(), 0.f, false));
        g.fillRoundedRectangle (bar, 5.f);
        g.setColour (col::cyan.withAlpha (0.25f)); g.fillRect (bar.getX() + 10.f, bar.getBottom() - 4.f, 34.f, 5.f);
        g.setColour (col::cyan);    g.fillRect (bar.getX() + 10.f, bar.getBottom() - 3.f, 34.f, 2.f);
        g.setColour (col::magenta); g.fillRect (bar.getX() + 46.f, bar.getBottom() - 3.f, 20.f, 2.f);
        g.setColour (col::violet);  g.fillRect (bar.getX() + 68.f, bar.getBottom() - 3.f, 10.f, 2.f);
        g.setColour (col::ice);
        g.setFont (CyberLook::labelFont (13.f));
        if (bar.getWidth() < 200.f)   // schmaler Bereich: Titel rechtsbündig, damit er nicht abgeschnitten wird
            g.drawText (title, (int) bar.getX() + 10, (int) bar.getY(), (int) bar.getWidth() - 18, 22, juce::Justification::centredRight, false);
        else
            g.drawText (title, (int) bar.getX() + 90, (int) bar.getY(), (int) bar.getWidth() - 100, 22, juce::Justification::centredLeft, false);
    }
}

void drawGroupLabel (juce::Graphics& g, int x, int y, int w, const juce::String& text)
{
    g.setColour (col::ice);
    g.setFont (CyberLook::labelFont (10.f));
    g.drawText (text, x, y, w, 14, juce::Justification::centredLeft, false);
    g.setColour (col::cyan);    g.fillRect (x, y + 16, 22, 2);
    g.setColour (col::magenta); g.fillRect (x + 24, y + 16, 12, 2);
    g.setColour (col::violet);  g.fillRect (x + 38, y + 16, 6, 2);
}

void drawColumnLabel (juce::Graphics& g, int x, int y, int w, const juce::String& text)
{
    g.setColour (col::ice);
    g.setFont (CyberLook::labelFont (10.f));
    g.drawText (text, x, y, w, 14, juce::Justification::centred, false);
    const int cx = x + w / 2;
    g.setColour (col::cyan);    g.fillRect (cx - 12, y + 15, 12, 2);
    g.setColour (col::magenta); g.fillRect (cx + 1, y + 15, 7, 2);
    g.setColour (col::violet);  g.fillRect (cx + 9, y + 15, 3, 2);
}
} // namespace

//==============================================================================
void LedPill::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    if (on) glowRect (g, r, colOn, 3.f, 0.6f);
    g.setGradientFill (juce::ColourGradient (on ? colOn.withAlpha (0.28f).overlaidWith (juce::Colour (0x40101830)) : juce::Colour (0xff1a1e3c),
                                             r.getX(), r.getY(), juce::Colour (0xff090a18), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.f);
    g.setColour (on ? colOn : col::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.f, on ? 1.3f : 1.f);

    const float d = juce::jmin (7.f, r.getHeight() - 8.f);
    const auto led = Rectangle<float> (r.getX() + 6.f, r.getCentreY() - d * 0.5f, d, d);
    if (on) glowEllipse (g, led, colOn);
    g.setColour (on ? colOn.brighter (0.4f) : juce::Colour (0xff1a2744));
    g.fillEllipse (led);

    g.setColour (on ? col::ice : col::iceDim);
    g.setFont (CyberLook::labelFont (9.5f));
    g.drawText (text, (int) led.getRight() + 3, (int) r.getY(), (int) (r.getRight() - led.getRight()) - 5, (int) r.getHeight(),
                juce::Justification::centred, false);
}

void LedPill::paintLearn (juce::Graphics& g, bool learn)
{
    if (! learn) return;
    g.setColour (col::yellow);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.f, 2.f);
}

ParamPill::ParamPill (AdmiralProcessor& p, int idx, juce::String t, juce::Colour onColour)
    : LedPill (std::move (t), onColour), LearnWidget (p, idx, *this),
      attach (*p.getParam (idx), [this] (float v) { setOn (v > 0.5f); }, nullptr)
{
    attach.sendInitialUpdate();
}

void ParamPill::mouseDown (const juce::MouseEvent& e)
{
    if (handleLearnClick (e)) return;
    attach.setValueAsCompleteGesture (on ? 0.f : 1.f);
}

//==============================================================================
void StepMatrix::layout()
{
    // Lücken: kleine zwischen den Zählzeiten (4 Schritte), größere zwischen den Takten (16 Schritte)
    const float beatGap = len <= 16 ? 6.f : len <= 24 ? 4.f : 3.f;
    const float barGap = len > 16 ? 10.f : 0.f;
    float gaps = 0.f;
    for (int k = 1; k < len; ++k) gaps += k % 16 == 0 ? barGap : k % 4 == 0 ? beatGap : 0.f;
    const float pitch = ((float) kWidth - 2.f - gaps) / (float) len;
    cellW = pitch - (len <= 16 ? 2.f : 1.5f);
    float x = 0.f;
    for (int k = 0; k < len; ++k)
    {
        if (k > 0) x += k % 16 == 0 ? barGap : k % 4 == 0 ? beatGap : 0.f;
        colX[(size_t) k] = x;
        x += pitch;
    }
}

void StepMatrix::update()
{
    const int ps = proc.getPlayStep();
    const bool pf = proc.isPlayingFill();
    const int rev = proc.getPatternRevision();
    const int l = proc.getLength();
    int mask = 0;
    for (int t = 0; t < NUM_TRACKS; ++t) if (proc.getValue (tp (t, TP_MUTE)) > 0.5f) mask |= 1 << t;
    bool changed = ps != playing || pf != playingFill || mask != muteMask;
    if (rev != seenRevision || l != len)
    {
        cache = proc.getPattern();
        if (l != len) { len = l; layout(); }
        seenRevision = rev;
        changed = true;
    }
    playing = ps; playingFill = pf; muteMask = mask;
    if (changed) repaint();
}

void StepMatrix::paint (juce::Graphics& g)
{
    const bool fillView = target == 1;
    auto colourFor = [&] (float v) { return fillView ? col::accent (v) : col::value (v); };
    const int zoneStart = len - ad::Pattern::zoneFor (len, fillView);
    const bool narrow = cellW < 24.f;
    const float rad = narrow ? 2.5f : 4.f;

    // Kopfzeile: Schrittnummern (bei schmalen Zellen nur die Zählzeiten), Taktanfänge hervorgehoben
    for (int k = 0; k < len; ++k)
    {
        const bool beat = k % 4 == 0, bar = k % 16 == 0;
        if (narrow && ! beat) continue;
        const bool now = k == playing;
        g.setColour (now ? col::yellow : k >= zoneStart ? col::magenta.brighter (0.2f) : bar ? col::cyan : beat ? col::ice : col::iceDim);
        g.setFont (CyberLook::labelFont (bar ? 11.f : beat ? 10.5f : 10.f));
        const int w = narrow ? 30 : (int) cellW;
        g.drawText (juce::String (k + 1), juce::jmax (0, (int) colX[(size_t) k] - (narrow ? 1 : 0)), 0, w, kHeader - 4,
                    narrow ? juce::Justification::centredLeft : juce::Justification::centred, false);
    }
    // Takt-Trenner
    for (int k = 16; k < len; k += 16)
    {
        const float x = colX[(size_t) k] - 5.5f;
        g.setColour (col::cyan.withAlpha (0.35f));
        g.fillRect (x, (float) kHeader, 1.5f, (float) (getHeight() - kHeader));
    }

    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        const bool muted = proc.getValue (tp (t, TP_MUTE)) > 0.5f;
        for (int k = 0; k < len; ++k)
        {
            const auto r = Rectangle<float> (colX[(size_t) k] + 1.f, (float) (kHeader + t * kRowH) + 3.f, cellW, (float) kRowH - 6.f);
            const float v = cell (t, k);
            const bool now = k == playing;
            const bool group = (k / 4) % 2 == 0;
            const bool zone = k >= zoneStart;
            const float dim = (fillView && ! zone) ? 0.45f : 1.f;     // Fill-Ansicht: Groove gedämpft

            g.setColour (zone ? (group ? juce::Colour (0xff1a0f26) : juce::Colour (0xff1e112c)) : (group ? juce::Colour (0xff0b0d1e) : juce::Colour (0xff0e1126)));
            g.fillRoundedRectangle (r, rad);

            if (v > 0.f)
            {
                const auto c = colourFor (0.2f + 0.8f * v).withMultipliedAlpha ((muted ? 0.35f : 1.f) * dim);
                const auto inner = r.reduced (narrow ? 1.5f : 2.f);
                if (! narrow) glowRect (g, inner, c, 3.f, ((muted ? 0.2f : 0.5f) + 0.5f * v) * dim);
                g.setGradientFill (juce::ColourGradient (c, r.getX(), r.getY(), c.darker (0.6f), r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (inner, rad - 1.f);
                const float h = (r.getHeight() - 8.f) * (1.f - v);
                if (h > 0.5f)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.45f));
                    const float ins = narrow ? 2.5f : 4.f;
                    g.fillRoundedRectangle (r.getX() + ins, r.getY() + 4.f, r.getWidth() - 2.f * ins, h, 1.5f);
                }
            }
            // Schritt weicht vom Groove ab (Variation / Fill): kleines Dreieck in der Ecke
            if (cache.isVaried (t, k, len, fillView))
            {
                const float ts = juce::jmin (7.f, cellW * 0.45f);
                juce::Path tri;
                tri.addTriangle (r.getRight() - ts - 2.f, r.getY() + 2.f, r.getRight() - 2.f, r.getY() + 2.f, r.getRight() - 2.f, r.getY() + ts + 2.f);
                g.setColour (col::magenta);
                g.fillPath (tri);
            }
            g.setColour (now ? col::yellow : zone ? col::magenta.withAlpha (0.55f) : (k % 4 == 0 ? col::edge.brighter (0.4f) : col::edge.withAlpha (0.8f)));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, now ? 2.f : 1.f);
        }
    }
}

bool StepMatrix::cellAt (juce::Point<int> p, int& track, int& step) const
{
    if (p.y < kHeader) return false;
    track = (p.y - kHeader) / kRowH;
    if (! juce::isPositiveAndBelow (track, NUM_TRACKS)) return false;
    // nächstliegende Spalte (Klicks in die Lücken zählen zur Zelle links davon)
    for (int k = len - 1; k >= 0; --k)
        if ((float) p.x >= colX[(size_t) k])
        {
            step = k;
            return (float) p.x <= colX[(size_t) k] + cellW + 6.f;
        }
    return false;
}

void StepMatrix::mouseDown (const juce::MouseEvent& e)
{
    int t = 0, k = 0;
    if (! cellAt (e.getPosition(), t, k)) return;
    if (e.mods.isPopupMenu()) { showVelocityMenu (t, k); return; }
    const float v = cell (t, k);
    if (e.mods.isShiftDown()) paintValue = v >= 0.99f ? 0.8f : 1.f;       // Shift: Betonung umschalten
    else paintValue = v > 0.f ? 0.f : 0.8f;
    proc.setCell (target, t, k, paintValue);
    if (paintValue > 0.f) proc.previewTrack (t);
    lastPaintT = t; lastPaintK = k;
}

void StepMatrix::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    int t = 0, k = 0;
    if (! cellAt (e.getPosition(), t, k) || (t == lastPaintT && k == lastPaintK)) return;
    lastPaintT = t; lastPaintK = k;
    proc.setCell (target, t, k, paintValue);
}

void StepMatrix::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int t = 0, k = 0;
    if (! cellAt (e.getPosition(), t, k)) return;
    const float v = cell (t, k);
    if (v <= 0.f) return;
    proc.setCell (target, t, k, juce::jlimit (0.05f, 1.f, v + (w.deltaY > 0 ? 0.05f : -0.05f)));
}

void StepMatrix::showVelocityMenu (int t, int k)
{
    const bool fillView = target == 1;
    const bool inZone = k >= len - ad::Pattern::zoneFor (len, fillView);
    juce::PopupMenu m;
    m.addSectionHeader (juce::String (tracks()[(size_t) t].name) + "  STEP " + juce::String (k + 1)
                        + (inZone ? (fillView ? "  (FILL)" : "  (VARIATION)") : ""));
    m.addItem (1, "Accent (100 %)");
    m.addItem (2, "Normal (80 %)");
    m.addItem (3, "Soft (55 %)");
    m.addItem (4, "Ghost (35 %)");
    m.addItem (5, "Off");
    if (inZone)
    {
        m.addSeparator();
        m.addItem (6, "Follow the groove here (remove variation)", cache.isVaried (t, k, len, fillView));
        m.addItem (7, fillView ? "Remove the whole fill" : "Remove the whole variation");
    }
    juce::Component::SafePointer<StepMatrix> sp (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [sp, t, k] (int r)
    {
        if (sp == nullptr || r == 0) return;
        if (r == 6) { sp->proc.removeVariation (sp->target, t, k); return; }
        if (r == 7) { sp->proc.clearVariation (sp->target); return; }
        static const float vals[] = { 0.f, 1.f, 0.8f, 0.55f, 0.35f, 0.f };
        sp->proc.setCell (sp->target, t, k, vals[r]);
    });
}

//==============================================================================
HChoice::HChoice (AdmiralProcessor& p, int paramIdx, juce::StringArray it)
    : items (std::move (it)),
      attach (*p.getParam (paramIdx), [this] (float v) { current = juce::roundToInt (v); repaint(); }, nullptr)
{
    attach.sendInitialUpdate();
}

void HChoice::paint (juce::Graphics& g)
{
    const float w = (float) getWidth() / (float) items.size();
    for (int i = 0; i < items.size(); ++i)
    {
        const auto r = Rectangle<float> ((float) i * w + 1.5f, 1.5f, w - 3.f, (float) getHeight() - 3.f);
        const bool on = i == current;
        if (on) glowRect (g, r, col::cyan, 3.f, 0.6f);
        g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xff1f3a5e) : juce::Colour (0xff1a1e3c), r.getX(), r.getY(),
                                                 juce::Colour (0xff090a18), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.f);
        g.setColour (on ? col::cyan : col::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.f, on ? 1.4f : 1.f);
        g.setColour (on ? col::ice : col::iceDim);
        g.setFont (CyberLook::labelFont (11.f));
        g.drawText (items[i], r.toNearestInt(), juce::Justification::centred, false);
    }
}

void HChoice::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, items.size() - 1, (int) ((float) e.x / ((float) getWidth() / (float) items.size())));
    attach.setValueAsCompleteGesture ((float) i);
}

//==============================================================================
TrackHeader::TrackHeader (AdmiralProcessor& p, int t, std::function<void (int)> onLoadRequest)
    : proc (p), track (t), requestLoad (std::move (onLoadRequest)),
      mute (p, tp (t, TP_MUTE), "MUTE", col::magenta)
{
    lock.onClick = [this]
    {
        // LOCK schützt Pattern und Sound; getrennt einstellbar per Rechtsklick
        const bool any = proc.isLocked (track) || proc.isSoundLocked (track);
        proc.setLocked (track, ! any);
        proc.setSoundLocked (track, ! any);
        update();
    };
    addAndMakeVisible (lock);
    addAndMakeVisible (mute);
    sampleName = proc.getSampleName (track);
}

void TrackHeader::resized()
{
    lock.setBounds (getWidth() - 52, 2, 52, 19);
    mute.setBounds (getWidth() - 52, 21, 52, 19);
}

void TrackHeader::update()
{
    const float m = proc.takeMeter (track);
    const float nl = juce::jmax (m > 0.001f ? 1.f : 0.f, led * 0.82f);
    if (std::abs (nl - led) > 0.01f) { led = nl < 0.02f ? 0.f : nl; repaint (0, 0, 24, getHeight()); }
    {
        const bool p = proc.isLocked (track), s = proc.isSoundLocked (track);
        lock.setOn (p || s);
        lock.setText (p && s ? "LOCK" : p ? "LOCK P" : s ? "LOCK S" : "LOCK");
    }
    const auto sn = proc.getSampleName (track);
    const bool mu = proc.getValue (tp (track, TP_MUTE)) > 0.5f;
    const bool fxOn = proc.trackHasFx (track);
    if (sn != sampleName || mu != shownMute || fxOn != shownFx) { sampleName = sn; shownMute = mu; shownFx = fxOn; repaint(); }
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f, 3.f);
    g.setColour (dropHi ? col::cyan.withAlpha (0.18f) : selected ? juce::Colour (0xff15204a) : juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (r, 4.f);
    if (selected) glowRect (g, r, col::cyan, 4.f, 0.35f);
    g.setColour (dropHi || selected ? col::cyan : col::edge.withAlpha (0.7f));
    g.drawRoundedRectangle (r, 4.f, dropHi || selected ? 1.6f : 1.f);

    // Aktivitäts-LED (leuchtet beim Anschlag auf)
    const auto ledR = Rectangle<float> (8.f, 10.f, 8.f, 8.f);
    if (led > 0.f) glowEllipse (g, ledR, col::cyan, led);
    g.setColour (col::mix (juce::Colour (0xff1a2744), col::cyan.brighter (0.4f), led));
    g.fillEllipse (ledR);

    const bool muted = proc.getValue (tp (track, TP_MUTE)) > 0.5f;
    g.setColour (muted ? col::iceDim : col::ice);
    g.setFont (CyberLook::labelFont (12.5f));
    g.drawText (tracks()[(size_t) track].name, 21, 4, getWidth() - 100, 18, juce::Justification::centredLeft, false);

    if (shownFx)   // Spur hat aktive Effekte
    {
        const Rectangle<float> b ((float) getWidth() - 78.f, 7.f, 20.f, 12.f);
        g.setColour (col::magenta.withAlpha (0.25f));
        g.fillRoundedRectangle (b, 2.f);
        g.setColour (col::magenta);
        g.drawRoundedRectangle (b, 2.f, 1.f);
        g.setFont (CyberLook::labelFont (8.f));
        g.drawText ("FX", b.toNearestInt(), juce::Justification::centred, false);
    }

    g.setColour (proc.hasUserSample (track) ? col::magenta : col::iceDim);
    g.setFont (CyberLook::labelFont (8.5f, false));
    g.drawFittedText (sampleName.toUpperCase(), 21, 22, getWidth() - 78, 14, juce::Justification::centredLeft, 1, 0.75f);
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader (tracks()[(size_t) track].name);
        m.addItem (1, "Preview");
        m.addItem (2, "Load sample ...");
        m.addItem (3, "Reset to kit sound", proc.hasUserSample (track));
        m.addSeparator();
        m.addItem (4, "Lock pattern  (keep with GENERATE)", true, proc.isLocked (track));
        m.addItem (5, "Lock sound  (keep with NEW KIT / MUTATE)", true, proc.isSoundLocked (track));
        m.addSeparator();
        juce::PopupMenu src;
        const int cur = (int) std::lround (proc.getValue (tp (track, TP_SRC)));
        src.addItem (10, "Global kit (KIT buttons)", true, cur == 0);
        for (int k = 0; k < NUM_KITS; ++k)
            src.addItem (11 + k, juce::String (kitNames()[(size_t) k]) + " kit", true, cur == k + 1);
        m.addSubMenu ("Sound source", src, ! proc.hasUserSample (track));
        juce::Component::SafePointer<TrackHeader> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [sp] (int r)
        {
            if (sp == nullptr) return;
            auto& pr = sp->proc;
            const int t = sp->track;
            if (r == 1) pr.previewTrack (t);
            else if (r == 2 && sp->requestLoad) sp->requestLoad (t);
            else if (r == 3) pr.resetSample (t);
            else if (r == 4) pr.setLocked (t, ! pr.isLocked (t));
            else if (r == 5) pr.setSoundLocked (t, ! pr.isSoundLocked (t));
            else if (r >= 10 && r < 11 + NUM_KITS)
            {
                auto* p = pr.getParam (tp (t, TP_SRC));
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 10)));
                p->endChangeGesture();
                pr.pollParameters();
                pr.previewTrack (t);
            }
            sp->update();
        });
        return;
    }
    if (onSelect) onSelect (track);
    if (e.getNumberOfClicks() > 1) { if (requestLoad) requestLoad (track); return; }
    proc.previewTrack (track);
}

//==============================================================================
void TrackSelectButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    if (selected) glowRect (g, r, col::cyan, 3.f, 0.6f);
    g.setGradientFill (juce::ColourGradient (selected ? juce::Colour (0xff1f3a5e) : juce::Colour (0xff1a1e3c), r.getX(), r.getY(),
                                             juce::Colour (0xff090a18), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.f);
    g.setColour (selected ? col::cyan : col::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.f, selected ? 1.4f : 1.f);

    const auto led = Rectangle<float> (r.getX() + 5.f, r.getCentreY() - 3.f, 6.f, 6.f);
    if (fx) glowEllipse (g, led, col::magenta);
    g.setColour (fx ? col::magenta.brighter (0.3f) : juce::Colour (0xff1a2744));
    g.fillEllipse (led);

    g.setColour (selected ? col::ice : col::iceDim);
    g.setFont (CyberLook::labelFont (8.5f));
    g.drawFittedText (tracks()[(size_t) track].name, (int) led.getRight() + 2, (int) r.getY(), (int) (r.getRight() - led.getRight()) - 3,
                      (int) r.getHeight(), juce::Justification::centred, 1, 0.7f);
}

//==============================================================================
void MidiDragTile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.f);
    const auto c = dragging ? col::yellow : col::magenta;
    glowRect (g, r, c, 5.f, hover ? 0.9f : 0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a1440), r.getX(), r.getY(), juce::Colour (0xff0a0816), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 5.f);
    g.setColour (c);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.5f);

    // kleines Noten-Symbol: drei Balken
    const float x0 = r.getX() + 12.f, cy = r.getCentreY();
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == 1 ? col::cyan : col::magenta.brighter (0.3f));
        g.fillRoundedRectangle (x0 + (float) i * 7.f, cy - 6.f + (float) (i % 2) * 4.f, 5.f, 8.f - (float) (i % 2) * 4.f + 4.f, 1.5f);
    }
    g.setColour (col::ice);
    g.setFont (CyberLook::labelFont (12.f));
    g.drawText ("DRAG MIDI TO FL STUDIO", (int) x0 + 26, (int) r.getY(), (int) r.getWidth() - 44, (int) r.getHeight(), juce::Justification::centredLeft, false);
}

void MidiDragTile::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    dragDone = false;
    const int bars = exportBars ((int) std::lround (proc.getValue (GP_BARS)));
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Save MIDI file ...");
        m.addItem (2, "Show MIDI file in folder (drag it from there)");
        juce::Component::SafePointer<MidiDragTile> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [sp, bars] (int r)
        {
            if (sp == nullptr) return;
            if (r == 1 && sp->onSaveRequest) sp->onSaveRequest();
            if (r == 2)
            {
                const auto f = sp->proc.createMidiExportFile (bars);
                if (f.existsAsFile()) f.revealToUser();
            }
        });
        return;
    }
    // Datei schon beim Anklicken schreiben, damit das Ziehen ohne Verzögerung startet
    prepared = proc.createMidiExportFile (bars);
    preparedBars = bars;
}

void MidiDragTile::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || dragging || dragDone || e.getDistanceFromDragStart() < 4) return;
    if (! prepared.existsAsFile())
    {
        if (onNotice) onNotice ("MIDI EXPORT FAILED");
        return;
    }
    dragging = true;
    repaint();
    if (onNotice) onNotice ("MIDI: " + juce::String (preparedBars) + (preparedBars == 1 ? " BAR" : " BARS") + "  >  DROP INTO PIANO ROLL OR PLAYLIST");
#if JUCE_WINDOWS
    // Windows: im Fenster-Thread ziehen (zuverlässig in FL Studio), siehe NativeDrag.h
    if (adwin::dragFile (prepared.getFullPathName().toWideCharPointer()))
    {
        dragging = false;
        dragDone = true;               // erst nach dem nächsten Klick wieder ziehen
        repaint();
        return;
    }
#endif
    juce::Component::SafePointer<MidiDragTile> sp (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ prepared.getFullPathName() }, false, this, [sp]
    {
        if (sp == nullptr) return;
        sp->dragging = false;
        sp->dragDone = true;
        sp->repaint();
    });
}

//==============================================================================
KnobCell* MainPanel::knob (int paramIdx, const juce::String& label, Rectangle<int> b, int knobSize, int labelH)
{
    auto* k = add<KnobCell> (proc, paramIdx, label, knobSize, labelH);
    k->setBounds (b);
    learners.push_back (&k->getSlider());
    return k;
}

MainPanel::MainPanel (AdmiralProcessor& p) : proc (p)
{
    setLookAndFeel (&look);

    // ---- Bereich PATTERN ----
    groups.push_back ({ "PATTERN", { kPatX, kPatY, kPatW, kPatH } });
    matrix = add<StepMatrix> (proc);
    matrix->setBounds (kGridX, kGridY, StepMatrix::width(), StepMatrix::height());

    // Leiste über dem Raster: Länge, Seiten mit Mini-Vorschau, FOLLOW
    {
        auto* lenSel = add<HChoice> (proc, GP_LENGTH, juce::StringArray { "16", "24", "32", "48" });
        lenSel->setBounds (90, kPatY + 29, 196, 24);
    }

    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        const int y = kRowsY + t * StepMatrix::kRowH;
        auto* h = add<TrackHeader> (proc, t, [this] (int tr) { startLoadSample (tr); });
        h->onSelect = [this] (int tr) { selectTrack (tr); };
        h->setBounds (kHeadX, y, kHeadW, StepMatrix::kRowH);
        learners.push_back (&h->getMute());
        headers[(size_t) t] = h;

        const int ky = y + (StepMatrix::kRowH - kKnobSize) / 2;
        const int ids[4] = { TP_TUNE, TP_DECAY, TP_PAN, TP_LEVEL };
        for (int i = 0; i < 4; ++i)
        {
            auto* k = knob (tp (t, ids[i]), "", { kKnobX + i * kKnobW, ky, kKnobW, kKnobSize }, kKnobSize, 0);
            if (ids[i] == TP_LEVEL) k->getSlider().getProperties().set ("accent", true);
            if (ids[i] == TP_TUNE || ids[i] == TP_PAN) k->getSlider().setDoubleClickReturnValue (true, 0.0);
        }
        auto* out = add<ParamPill> (proc, tp (t, TP_OUT), "OUT");
        out->setBounds (kOutX, y + 10, 52, 22);
        learners.push_back (out);
    }

    // ---- Bereich MASTER ----
    groups.push_back ({ "MASTER", { kMasterX, kPatY, kMasterW, kPatH } });
    {
        juce::StringArray kitItems;
        for (auto& k : kitNames()) kitItems.add (juce::String (k));
        auto* kit = add<ChoiceButtons> (proc, GP_KIT, kitItems, "KIT");
        kit->setBounds (kMasterX + 12, kPatY + 34, kMasterW - 24, kit->preferredHeight());
        learners.push_back (kit);

        knob (GP_GLUE, "GLUE", { kMasterX + 6, kPatY + 120, 63, 64 }, 44);
        knob (GP_TAPE, "TAPE", { kMasterX + 69, kPatY + 120, 63, 64 }, 44);
        auto* pp = add<ParamPill> (proc, GP_PATPLAY, "PATTERN PLAY", col::yellow);
        pp->setBounds (kMasterX + 16, kPatY + 212, kMasterW - 32, 24);
        pp->setTooltip ("Off: Admiral only plays incoming MIDI notes (e.g. the exported pattern in FL's piano roll)");
        learners.push_back (pp);

        auto* mo = add<ParamPill> (proc, GP_MIDIOUT, "MIDI OUT");
        mo->setBounds (kMasterX + 16, kPatY + 240, kMasterW - 32, 24);
        learners.push_back (mo);

        allOuts = add<LedPill> ("ALL OUTS", col::magenta);
        allOuts->setBounds (kMasterX + 16, kPatY + 268, kMasterW - 32, 24);
        allOuts->onClick = [this]
        {
            const bool on = ! proc.allOutsOn();
            proc.setAllOuts (on);
            showNotice (on ? "ALL TRACKS ON SEPARATE OUTPUTS (KICK, SNARE ...)" : "ALL TRACKS ON MAIN OUTPUT");
        };

        auto* f = add<FaderCell> (proc, GP_MASTER, "MASTER", true);
        f->setBounds (kMasterX + (kMasterW - 70) / 2, kPatY + 298, 70, kPatH - 306);
        learners.push_back (&f->getSlider());
    }

    // ---- Bereich TRACK FX ----
    buildTrackFx();

    // ---- Bereich GENERATOR ----
    groups.push_back ({ "GENERATOR", { kPatX, kGenY, 830, kGenH } });
    {
        for (int i = 0; i < (int) genreNames().size(); ++i) genreBox.addItem (juce::String (genreNames()[(size_t) i]).toUpperCase(), i + 1);
        genreAtt = std::make_unique<juce::ComboBoxParameterAttachment> (*proc.getParam (GP_GENRE), genreBox);
        addAndMakeVisible (genreBox);
        genreBox.setBounds (36, kGenY + 34, 214, 30);

        generateBtn.getProperties().set ("fontSize", 20.0);
        generateBtn.setClickingTogglesState (false);
        generateBtn.setColour (juce::TextButton::buttonColourId, col::panelHi);
        generateBtn.onClick = [this] { proc.generateNew(); showNotice ("NEW PATTERN: " + genreBox.getText()); };
        addAndMakeVisible (generateBtn);
        generateBtn.setBounds (36, kGenY + 72, 214, 58);

        histBack.onClick = [this] { proc.historyBack(); };
        histFwd.onClick  = [this] { proc.historyForward(); };
        histBack.setTooltip ("Previous generated pattern");
        histFwd.setTooltip ("Next generated pattern");
        addAndMakeVisible (histBack);
        addAndMakeVisible (histFwd);
        histBack.setBounds (262, kGenY + 34, 40, 26);
        histFwd.setBounds (306, kGenY + 34, 40, 26);

        newFillBtn.onClick = [this] { proc.regenerateFill(); setEditTarget (1); showNotice ("NEW FILL"); };
        addAndMakeVisible (newFillBtn);
        newFillBtn.setBounds (262, kGenY + 80, 84, 24);
        // FILL NOW: Fill am Ende des laufenden (bzw. nächsten) Durchlaufs, unabhängig von der FILL-Einstellung
        fillNowBtn.onClick = [this] { proc.triggerFill(); showNotice ("FILL AT THE END OF THIS CYCLE"); };
        fillNowBtn.setTooltip ("Play the fill at the end of the current cycle");
        addAndMakeVisible (fillNowBtn);
        fillNowBtn.setBounds (262, kGenY + 108, 84, 24);

        knob (GP_DENSITY, "DENSITY", { 360, kGenY + 30, 96, 104 }, 66);
        knob (GP_VARIATION, "VARIATION", { 462, kGenY + 30, 96, 104 }, 66);

        auto* fill = add<ChoiceButtons> (proc, GP_FILL, juce::StringArray { "Off", "Every 2", "Every 4", "Every 8" }, "FILL");
        fill->setBounds (576, kGenY + 32, 110, fill->preferredHeight());
        learners.push_back (fill);

        // Kit-Designer
        newKitBtn.getProperties().set ("fontSize", 13.0);
        newKitBtn.onClick = [this] { proc.newKit(); showNotice ("NEW KIT: " + proc.getKitCharacter().toUpperCase()); };
        mutateBtn.onClick = [this] { proc.mutateKit(); showNotice ("KIT MUTATED"); };
        kitBack.onClick = [this] { proc.kitHistoryBack(); };
        kitFwd.onClick  = [this] { proc.kitHistoryForward(); };
        newKitBtn.setTooltip ("New random kit (rule-based, follows the genre). Sound-locked tracks stay.");
        mutateBtn.setTooltip ("Slightly vary the current kit");
        for (auto* b : { &newKitBtn, &mutateBtn, &kitBack, &kitFwd }) addAndMakeVisible (*b);
        newKitBtn.setBounds (702, kGenY + 48, 67, 28);
        mutateBtn.setBounds (773, kGenY + 48, 67, 28);
        newKitBtn.getProperties().set ("fontSize", 11.0);
        saveKitBtn.onClick = [this] { saveKit(); };
        loadKitBtn.onClick = [this] { loadKit(); };
        saveKitBtn.setTooltip ("Save the kit: all track settings, effects, GLUE/TAPE and your samples");
        loadKitBtn.setTooltip ("Load a saved kit (sound-locked tracks stay unchanged)");
        addAndMakeVisible (saveKitBtn);
        addAndMakeVisible (loadKitBtn);
        saveKitBtn.setBounds (702, kGenY + 80, 67, 26);
        loadKitBtn.setBounds (773, kGenY + 80, 67, 26);
        kitBack.setBounds (702, kGenY + 112, 34, 22);
        kitFwd.setBounds (806, kGenY + 112, 34, 22);
    }

    // ---- Bereich GROOVE ----
    groups.push_back ({ "GROOVE", { 862, kGenY, 438, kGenH } });
    knob (GP_SWING, "SWING", { 872, kGenY + 30, 84, 104 }, 60);
    knob (GP_HUMANIZE, "HUMANIZE", { 956, kGenY + 30, 84, 104 }, 60);
    knob (GP_TEMPO, "INT. TEMPO", { 1040, kGenY + 30, 84, 104 }, 60);
    {
        auto* run = add<ParamPill> (proc, GP_RUN, "INT. RUN");
        run->setBounds (1132, kGenY + 104, 154, 26);
        learners.push_back (run);
    }

    // ---- Kopfzeile rechts ----
    for (auto* b : { &tabMain, &tabFill })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    tabMain.onClick = [this] { setEditTarget (0); };
    tabFill.onClick = [this] { setEditTarget (1); };
    copyBtn.onClick = [this] { proc.copyMainToFill(); showNotice ("MAIN COPIED INTO THE FILL ZONE"); };
    clearBtn.onClick = [this] { proc.clearPattern (matrix->getEditTarget()); showNotice (matrix->getEditTarget() == 0 ? "PATTERN CLEARED" : "FILL REMOVED"); };
    addAndMakeVisible (copyBtn);
    addAndMakeVisible (clearBtn);

    dragTile = add<MidiDragTile> (proc);
    dragTile->onNotice = [this] (const juce::String& t) { showNotice (t); };
    dragTile->onSaveRequest = [this] { saveMidiBtn.triggerClick(); };

    for (int i = 0; i < NUM_EXPORT_BARS; ++i)
        barsBox.addItem (juce::String (exportBars (i)) + (i == 0 ? " BAR" : " BARS"), i + 1);
    barsAtt = std::make_unique<juce::ComboBoxParameterAttachment> (*proc.getParam (GP_BARS), barsBox);
    addAndMakeVisible (barsBox);

    saveMidiBtn.onClick = [this]
    {
        const int bars = exportBars ((int) std::lround (proc.getValue (GP_BARS)));
        chooser = std::make_unique<juce::FileChooser> ("Save MIDI file",
            proc.getLastFolder ("lastMidiFolder", juce::File::userDocumentsDirectory).getChildFile ("Admiral Pattern.mid"), "*.mid");
        juce::Component::SafePointer<MainPanel> sp (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [sp, bars] (const juce::FileChooser& fc)
        {
            if (sp == nullptr) return;
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (! f.hasFileExtension ("mid")) f = f.withFileExtension ("mid");
            const bool ok = sp->proc.writeMidiFile (f, bars);
            if (ok) sp->proc.setLastFolder ("lastMidiFolder", f.getParentDirectory());
            sp->showNotice (ok ? "SAVED: " + f.getFileName().toUpperCase() : juce::String ("SAVING FAILED"));
        });
    };
    addAndMakeVisible (saveMidiBtn);

    learnBtn.setClickingTogglesState (true);
    learnBtn.onClick = [this]
    {
        proc.setLearnArmed (learnBtn.getToggleState());
        learnBtn.setButtonText (learnBtn.getToggleState() ? "MIDI LEARN: ON" : "MIDI LEARN: OFF");
    };
    learnBtn.setToggleState (proc.isLearnArmed(), juce::dontSendNotification);
    learnBtn.setButtonText (proc.isLearnArmed() ? "MIDI LEARN: ON" : "MIDI LEARN: OFF");
    addAndMakeVisible (learnBtn);

    setEditTarget (0);
    selectTrack (0);
    setSize (W, H);              // erst jetzt: resized() braucht alle Bedienelemente
    updateCaption();
    startTimerHz (30);
}

void MainPanel::buildTrackFx()
{
    fxGroupIndex = groups.size();
    groups.push_back ({ "TRACK FX", { kPatX, kFxY, W - 2 * kPatX, kFxH } });

    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        auto* b = add<TrackSelectButton> (t, [this] (int tr) { selectTrack (tr); proc.previewTrack (tr); });
        b->setBounds (34 + (t % 5) * 48, kFxY + 34 + (t / 5) * 30, 46, 28);
        selButtons[(size_t) t] = b;

        auto& v = fxComps[(size_t) t];
        auto track = [&] (juce::Component* c) { v.push_back (c); return c; };
        const int ky = kFxY + 44, cw = 58, ks = 46;

        // Filter (erste Stufe)
        track (knob (tp (t, TP_FLT_HP), "HIGHPASS", { 290, ky, cw, 88 }, ks));
        track (knob (tp (t, TP_FLT_LP), "LOWPASS", { 290 + cw, ky, cw, 88 }, ks));
        track (knob (tp (t, TP_FLT_RES), "RESO", { 290 + 2 * cw, ky, cw, 88 }, ks));

        // Distortion
        auto* type = add<ChoiceButtons> (proc, tp (t, TP_DS_TYPE), juce::StringArray { "Soft", "Hard", "Fold" }, "TYPE");
        type->setBounds (488, kFxY + 46, 62, type->preferredHeight());
        learners.push_back (type); track (type);
        track (knob (tp (t, TP_DS_DRIVE), "DRIVE", { 554, ky, cw, 88 }, ks));
        track (knob (tp (t, TP_DS_TONE), "TONE", { 554 + cw, ky, cw, 88 }, ks));
        auto* dm = knob (tp (t, TP_DS_MIX), "MIX", { 554 + 2 * cw, ky, cw, 88 }, ks);
        dm->getSlider().getProperties().set ("accent", true);
        track (dm);

        // Delay (HOST SYNC wie im Diplomat)
        auto* sync = add<ParamPill> (proc, tp (t, TP_DL_SYNC), "HOST SYNC");
        sync->setBounds (752, kFxY + 50, 84, 24);
        learners.push_back (sync); track (sync);
        auto* ping = add<ParamPill> (proc, tp (t, TP_DL_PING), "PING-PONG");
        ping->setBounds (752, kFxY + 80, 84, 24);
        learners.push_back (ping); track (ping);
        dlTimeKnob[(size_t) t] = knob (tp (t, TP_DL_TIME), "TIME", { 842, ky, cw, 88 }, ks);
        dlDivKnob[(size_t) t] = knob (tp (t, TP_DL_DIV), "NOTE", { 842 + cw, ky, cw, 88 }, ks);
        track (dlTimeKnob[(size_t) t]); track (dlDivKnob[(size_t) t]);
        track (knob (tp (t, TP_DL_FB), "FEEDBACK", { 842 + 2 * cw, ky, cw, 88 }, ks));
        auto* lm = knob (tp (t, TP_DL_MIX), "MIX", { 842 + 3 * cw, ky, cw, 88 }, ks);
        lm->getSlider().getProperties().set ("accent", true);
        track (lm);

        // Reverb
        track (knob (tp (t, TP_RV_SIZE), "SIZE", { 1098, ky, cw, 88 }, ks));
        track (knob (tp (t, TP_RV_DAMP), "DAMP", { 1098 + cw, ky, cw, 88 }, ks));
        auto* rm = knob (tp (t, TP_RV_MIX), "MIX", { 1098 + 2 * cw, ky, cw, 88 }, ks);
        rm->getSlider().getProperties().set ("accent", true);
        track (rm);
    }

    resetFxBtn.onClick = [this]
    {
        for (int p = TP_DS_TYPE; p <= TP_RV_MIX; ++p)
            if (auto* prm = proc.getParam (tp (selTrack, p)))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->getDefaultValue());
                prm->endChangeGesture();
            }
        showNotice ("FX RESET: " + juce::String (tracks()[(size_t) selTrack].name));
    };
    addAndMakeVisible (resetFxBtn);
    resetFxBtn.setBounds (36, kFxY + 100, 234, 26);
}

void MainPanel::selectTrack (int t)
{
    selTrack = juce::jlimit (0, NUM_TRACKS - 1, t);
    for (int i = 0; i < NUM_TRACKS; ++i)
    {
        for (auto* c : fxComps[(size_t) i]) c->setVisible (i == selTrack);
        headers[(size_t) i]->setSelected (i == selTrack);
        selButtons[(size_t) i]->set (i == selTrack, proc.trackHasFx (i));
    }
    groups[fxGroupIndex].title = "TRACK FX  -  " + juce::String (tracks()[(size_t) selTrack].name);
    repaint (kPatX, kFxY, W - 2 * kPatX, 26);
}

MainPanel::~MainPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MainPanel::setEditTarget (int which)
{
    matrix->setEditTarget (which);
    tabMain.setToggleState (which == 0, juce::dontSendNotification);
    tabFill.setToggleState (which == 1, juce::dontSendNotification);
    updateCaption();
}

void MainPanel::resized()
{
    tabMain.setBounds (842, 8, 104, 20);
    tabFill.setBounds (950, 8, 104, 20);
    copyBtn.setBounds (1058, 8, 116, 20);
    clearBtn.setBounds (1178, 8, 116, 20);
    dragTile->setBounds (842, 32, 238, 30);
    barsBox.setBounds (1086, 32, 94, 30);
    saveMidiBtn.setBounds (1184, 32, 110, 30);
    learnBtn.setBounds (1150, kFootY + 2, 144, 22);
}

//==============================================================================
void MainPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff04050a));
    drawSide (g, { 0, 0, 16, H }, true);
    drawSide (g, { W - 16, 0, 16, H }, false);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b0d20), 0.f, 0.f, col::body, 0.f, (float) H, false));
    g.fillRect (16, 0, W - 32, H);
    g.setColour (col::cyan.withAlpha (0.035f));
    for (int gx = 16; gx < W - 16; gx += 32) g.fillRect (gx, 0, 1, H);
    for (int gy = 0; gy < H; gy += 32)       g.fillRect (16, gy, W - 32, 1);

    // Kopfzeile
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15183a), 0.f, 0.f, juce::Colour (0xff070815), 0.f, 82.f, false));
    g.fillRect (16, 0, W - 32, 82);
    g.setColour (col::cyan.withAlpha (0.25f)); g.fillRect (16, 80, W - 32, 6);
    g.setColour (col::cyan);                  g.fillRect (16, 82, W - 32, 2);
    g.setColour (col::magenta);               g.fillRect (16, 84, W - 32, 1);

    // Name: ADMIRAL (Orbitron) mit Leuchten und Farbverlauf
    {
        const auto f = fonts::title (38.f).withExtraKerningFactor (0.04f);
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, "ADMIRAL", 0.f, 0.f);
        juce::Path p;
        ga.createPath (p);
        const auto bb = p.getBounds();
        p.applyTransform (juce::AffineTransform::translation (46.f - bb.getX(), 52.f));
        const auto pb = p.getBounds();
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (col::cyan.withAlpha (0.05f * (float) (5 - i)));
            g.strokePath (p, juce::PathStrokeType ((float) i * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setGradientFill (juce::ColourGradient (col::cyan.brighter (0.2f), pb.getX(), 0.f, col::magenta, pb.getRight(), 0.f, false));
        g.fillPath (p);
        titleRight = (int) pb.getRight();
    }
    g.setColour (col::iceDim);
    g.setFont (CyberLook::labelFont (10.5f));
    g.drawText ("GENERATIVE DRUM COMPOSER", 48, 55, 330, 14, juce::Justification::centredLeft, false);
    g.setColour (col::iceDim.withAlpha (0.8f));
    g.setFont (CyberLook::labelFont (8.f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xa9")) + " by Christian Saak", 48, 67, 330, 11, juce::Justification::centredLeft, false);

    // Modellnummer im Neon-Rahmen
    const Rectangle<float> badge ((float) titleRight + 18.f, 16.f, 112.f, 46.f);
    glowRect (g, badge, col::magenta, 6.f, 0.9f);
    g.setColour (juce::Colour (0xff070815));
    g.fillRoundedRectangle (badge, 6.f);
    g.setColour (col::magenta);
    g.drawRoundedRectangle (badge.reduced (1.f), 6.f, 2.f);
    g.setColour (col::ice);
    g.setFont (fonts::model (30.f).withExtraKerningFactor (0.06f));
    g.drawText ("1506", badge.toNearestInt(), juce::Justification::centred, false);

    // Neon-Streifen
    const juce::Colour stripe[3] = { col::cyan, col::magenta, col::violet };
    const int sx = titleRight + 150;
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (stripe[i].withAlpha (0.25f));
        g.fillRect (sx, 25 + i * 10, 826 - sx, 8);
        g.setColour (stripe[i]);
        g.fillRect (sx, 26 + i * 10, 826 - sx, 4);
    }

    g.setColour (col::ice);
    g.setFont (CyberLook::labelFont (10.5f));
    g.drawFittedText (caption, 842, 65, 452, 14, juce::Justification::centredLeft, 1, 0.8f);

    for (auto& grp : groups) drawPanel (g, grp.r, grp.title);

    // Leiste über dem Raster
    {
        const int y = kPatY + 29;
        g.setColour (col::iceDim);
        g.setFont (CyberLook::labelFont (10.f));
        g.drawText ("LENGTH", 34, y, 54, 24, juce::Justification::centredLeft, false);
        g.drawText ("STEPS", 292, y, 48, 24, juce::Justification::centredLeft, false);
        const bool fillView = matrix->getEditTarget() == 1;
        const Rectangle<float> sw (370.f, (float) y + 6.f, 14.f, 12.f);
        g.setColour (juce::Colour (0xff1e112c)); g.fillRoundedRectangle (sw, 2.f);
        g.setColour (col::magenta.withAlpha (0.7f)); g.drawRoundedRectangle (sw, 2.f, 1.f);
        g.setColour (col::iceDim);
        g.setFont (CyberLook::labelFont (9.5f));
        g.drawText (fillView ? "FILL ZONE  (PLAYS EVERY N-TH CYCLE)" : "VARIATION ZONE  (LAST QUARTER, MOVES WHEN SHORTENED)",
                    390, y, 400, 24, juce::Justification::centredLeft, false);
    }


    // Spaltenköpfe
    drawColumnLabel (g, kHeadX, kGridY, kHeadW, "TRACK");
    const char* kl[4] = { "TUNE", "DECAY", "PAN", "LEVEL" };
    for (int i = 0; i < 4; ++i) drawColumnLabel (g, kKnobX + i * kKnobW, kGridY, kKnobW, kl[i]);
    drawColumnLabel (g, kOutX, kGridY, 52, "OUT");

    // TRACK FX: Abschnitte
    drawGroupLabel (g, 296, kFxY + 28, 160, "FILTER");
    drawGroupLabel (g, 488, kFxY + 28, 200, "DISTORTION");
    drawGroupLabel (g, 752, kFxY + 28, 200, "DELAY");
    drawGroupLabel (g, 1104, kFxY + 28, 160, "REVERB");
    g.setColour (col::edge.withAlpha (0.8f));
    for (int x : { 282, 476, 740, 1086 }) g.fillRect (x, kFxY + 34, 1, kFxH - 44);

    // Master: Anzeige der Pegelreduktion des GLUE-Kompressors (0 .. 12 dB)
    {
        const Rectangle<float> m ((float) kMasterX + 30.f, (float) kPatY + 194.f, (float) kMasterW - 42.f, 8.f);
        g.setColour (col::iceDim);
        g.setFont (CyberLook::labelFont (8.5f));
        g.drawText ("GR", kMasterX + 10, kPatY + 191, 20, 14, juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff03040a));
        g.fillRoundedRectangle (m, 2.f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (m, 2.f, 1.f);
        const float f = juce::jlimit (0.f, 1.f, shownGr / 12.f);
        if (f > 0.01f)
        {
            const auto bar = m.withX (m.getRight() - m.getWidth() * f).withWidth (m.getWidth() * f).reduced (1.f);
            glowRect (g, bar, col::magenta, 1.f, 0.6f);
            g.setColour (col::magenta);
            g.fillRoundedRectangle (bar, 1.f);
        }
    }

    // Generator: Verlauf und Hinweis
    g.setColour (col::iceDim);
    g.setFont (CyberLook::labelFont (10.f));
    g.drawText ("HISTORY " + histText, 262, kGenY + 60, 84, 20, juce::Justification::centred, false);
    g.setFont (CyberLook::labelFont (9.5f, false));
    drawGroupLabel (g, 702, kGenY + 28, 138, "KIT DESIGNER");
    g.setColour (col::iceDim);
    g.setFont (CyberLook::labelFont (9.5f));
    g.drawText ("KIT " + kitHistText, 738, kGenY + 112, 66, 22, juce::Justification::centred, false);
    g.setColour (col::edge.withAlpha (0.8f));
    g.fillRect (692, kGenY + 34, 1, kGenH - 44);

    // Groove: Positionsanzeige
    {
        const Rectangle<float> d (1132.f, (float) kGenY + 36.f, 154.f, 62.f);
        g.setColour (juce::Colour (0xff04050c));
        g.fillRoundedRectangle (d, 5.f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (d.reduced (0.5f), 5.f, 1.f);
        g.setColour (proc.isPlayingFill() && proc.getPlayStep() >= 0 ? col::magenta : col::cyan);
        g.setFont (fonts::model (20.f));
        g.drawText (posText, d.withTrimmedBottom (24.f).toNearestInt(), juce::Justification::centred, false);
        g.setColour (col::iceDim);
        g.setFont (CyberLook::labelFont (10.f));
        const auto bpm = juce::String (proc.getCurrentBpm(), 1) + " BPM";
        const juce::String src = proc.getValue (GP_PATPLAY) < 0.5f ? "MIDI ONLY" : proc.isHostSynced() ? "HOST SYNC" : (proc.getValue (GP_RUN) > 0.5f ? "INTERNAL RUN" : "STOPPED");
        g.drawText (src + "  |  " + bpm, d.withTrimmedTop (38.f).withTrimmedBottom (6.f).toNearestInt(), juce::Justification::centred, false);
    }

    // Fußzeile
    g.setColour (col::iceDim);
    g.setFont (CyberLook::labelFont (10.5f, false));
    g.drawText ("CLICK TRACK: SELECT  |  RIGHT-CLICK TRACK: SAMPLE, LOCKS, SOURCE  |  STEP: SHIFT = ACCENT, RIGHT = VELOCITY",
                36, kFootY, 640, 26, juce::Justification::centredLeft, false);
    g.setColour (col::magenta);
    g.setFont (CyberLook::labelFont (11.f));
    g.drawText (noticeTicks > 0 ? notice : midiText, 690, kFootY, 450, 26, juce::Justification::centredRight, false);
}

void MainPanel::paintOverChildren (juce::Graphics& g)
{
    if (! dropElsewhere) return;
    const auto r = Rectangle<float> (kPatX, kPatY, kPatW, kPatH).reduced (40.f, 180.f);
    g.setColour (juce::Colour (0xd0070815));
    g.fillRoundedRectangle (r, 8.f);
    g.setColour (col::magenta);
    g.drawRoundedRectangle (r, 8.f, 2.f);
    g.setColour (col::ice);
    g.setFont (CyberLook::labelFont (22.f));
    g.drawText ("DROP THE AUDIO FILE ONTO A TRACK ROW", r.toNearestInt(), juce::Justification::centred, false);
}

//==============================================================================
int MainPanel::rowAt (int x, int y) const
{
    if (x < kPatX || x > kPatX + kPatW || y < kRowsY) return -1;
    const int r = (y - kRowsY) / StepMatrix::kRowH;
    return juce::isPositiveAndBelow (r, NUM_TRACKS) ? r : -1;
}

void MainPanel::setDropRow (int r)
{
    if (r == dropRow) return;
    dropRow = r;
    for (int t = 0; t < NUM_TRACKS; ++t) headers[(size_t) t]->setDropHighlight (t == r);
}

bool MainPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (proc.isSampleFile (juce::File (f))) return true;
    return false;
}

void MainPanel::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int r = rowAt (x, y);
    setDropRow (r);
    const bool e = r < 0;
    if (e != dropElsewhere) { dropElsewhere = e; repaint(); }
}

void MainPanel::fileDragExit (const juce::StringArray&)
{
    setDropRow (-1);
    if (dropElsewhere) { dropElsewhere = false; repaint(); }
}

void MainPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int r = rowAt (x, y);
    fileDragExit (files);
    if (r < 0) { showNotice ("DROP AUDIO FILES ONTO A TRACK ROW"); return; }
    for (auto& path : files)
    {
        const juce::File f (path);
        if (! proc.isSampleFile (f)) continue;
        juce::String msg;
        proc.loadSampleFile (r, f, msg);
        showNotice (msg);
        proc.previewTrack (r);
        break;
    }
}

void MainPanel::saveKit()
{
    const auto start = proc.getLastFolder ("lastKitFolder", juce::File::userDocumentsDirectory);
    const auto dir = proc.getLastFolder ("lastKitFolder", juce::File::userDocumentsDirectory) == juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         ? AdmiralProcessor::defaultKitFolder() : start;
    dir.createDirectory();
    const juce::String suggestion = proc.getKitCharacter().isNotEmpty() ? proc.getKitCharacter() : juce::String ("My Kit");
    chooser = std::make_unique<juce::FileChooser> ("Save kit", dir.getChildFile (juce::File::createLegalFileName (suggestion) + ".admiralkit"), "*.admiralkit");
    juce::Component::SafePointer<MainPanel> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [sp] (const juce::FileChooser& fc)
    {
        if (sp == nullptr) return;
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension ("admiralkit")) f = f.withFileExtension ("admiralkit");
        const bool ok = sp->proc.saveKitFile (f);
        if (ok) sp->proc.setLastFolder ("lastKitFolder", f.getParentDirectory());
        sp->showNotice (ok ? "KIT SAVED: " + f.getFileNameWithoutExtension().toUpperCase() : juce::String ("SAVING THE KIT FAILED"));
    });
}

void MainPanel::loadKit()
{
    auto dir = proc.getLastFolder ("lastKitFolder", juce::File::userDocumentsDirectory);
    if (dir == juce::File::getSpecialLocation (juce::File::userDocumentsDirectory) && AdmiralProcessor::defaultKitFolder().isDirectory())
        dir = AdmiralProcessor::defaultKitFolder();
    chooser = std::make_unique<juce::FileChooser> ("Load kit", dir, "*.admiralkit");
    juce::Component::SafePointer<MainPanel> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [sp] (const juce::FileChooser& fc)
    {
        if (sp == nullptr) return;
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::String msg;
        if (sp->proc.loadKitFile (f, msg)) sp->proc.setLastFolder ("lastKitFolder", f.getParentDirectory());
        sp->showNotice (msg);
    });
}

void MainPanel::startLoadSample (int track)
{
    // startet im zuletzt benutzten Sample-Ordner (beim ersten Mal: Musik-Ordner)
    chooser = std::make_unique<juce::FileChooser> ("Load sample for " + juce::String (tracks()[(size_t) track].name),
                                                   proc.getLastFolder ("lastSampleFolder", juce::File::userMusicDirectory),
                                                   proc.getSampleWildcard());
    juce::Component::SafePointer<MainPanel> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [sp, track] (const juce::FileChooser& fc)
    {
        if (sp == nullptr) return;
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::String msg;
        sp->proc.loadSampleFile (track, f, msg);
        sp->showNotice (msg);
        sp->proc.previewTrack (track);
    });
}

void MainPanel::updateCaption()
{
    const juce::String which = matrix != nullptr && matrix->getEditTarget() == 1 ? "EDIT: FILL" : "EDIT: MAIN";
    const int fe = fillEveryCycles ((int) std::lround (proc.getValue (GP_FILL)));
    const juce::String c = which + "  |  " + juce::String (proc.getLength()) + " STEPS  |  " + (fe > 0 ? "FILL EVERY " + juce::String (fe) : juce::String ("FILL OFF"))
                         + "  |  " + juce::String (genreNames()[(size_t) juce::jlimit (0, 6, (int) std::lround (proc.getValue (GP_GENRE)))]).toUpperCase()
                         + (proc.getKitCharacter().isNotEmpty() ? "  |  KIT: " + proc.getKitCharacter().toUpperCase() : juce::String())
                         + (proc.getValue (GP_PATPLAY) < 0.5f ? "  |  PATTERN PLAY OFF" : "")
                         + (proc.getPlayingLength() > 0 && proc.getPlayingLength() != proc.getLength()
                                ? "  |  " + juce::String (proc.getLength()) + " STEPS FROM NEXT CYCLE" : juce::String());
    if (c != caption) { caption = c; repaint (842, 65, 452, 14); }
}

void MainPanel::timerCallback()
{
    for (auto* l : learners) l->refreshLearn();
    matrix->update();
    for (auto* h : headers) h->update();
    for (int t = 0; t < NUM_TRACKS; ++t) selButtons[(size_t) t]->set (t == selTrack, proc.trackHasFx (t));
    {
        const bool sync = proc.getValue (tp (selTrack, TP_DL_SYNC)) > 0.5f;
        dlTimeKnob[(size_t) selTrack]->setAlpha (sync ? 0.3f : 1.f);
        dlDivKnob[(size_t) selTrack]->setAlpha (sync ? 1.f : 0.3f);
    }
    allOuts->setOn (proc.allOutsOn());
    if (const float gr = proc.getGainReductionDb(); std::abs (gr - shownGr) > 0.05f)
    {
        shownGr = gr;
        repaint (kMasterX + 8, kPatY + 190, kMasterW - 16, 16);
    }
    updateCaption();

    const juce::String ht = juce::String (proc.getHistoryPos() + 1) + " / " + juce::String (proc.getHistorySize());
    if (ht != histText) { histText = ht; repaint (262, kGenY + 60, 84, 20); }
    fillNowBtn.setColour (juce::TextButton::buttonColourId, proc.isFillPending() ? col::magenta.withAlpha (0.45f) : col::panelHi);
    histBack.setEnabled (proc.getHistoryPos() > 0);
    {
        const juce::String kt = proc.getKitHistorySize() == 0 ? juce::String ("- / -")
                              : juce::String (proc.getKitHistoryPos() + 1) + " / " + juce::String (proc.getKitHistorySize());
        if (kt != kitHistText) { kitHistText = kt; repaint (738, kGenY + 112, 66, 22); }
        kitBack.setEnabled (proc.getKitHistoryPos() > 0);
        kitFwd.setEnabled (proc.getKitHistoryPos() < proc.getKitHistorySize() - 1);
    }
    histFwd.setEnabled (proc.getHistoryPos() < proc.getHistorySize() - 1);

    const int st = proc.getPlayStep();
    const juce::String pt = st < 0 ? juce::String ("- - -")
                                   : juce::String (st + 1).paddedLeft ('0', 2) + "/" + juce::String (proc.getLength()) + "   #" + juce::String (proc.getPlayCycle() + 1);
    if (pt != posText) { posText = pt; repaint (1132, kGenY + 36, 154, 62); }
    static int slow = 0;
    if (++slow % 15 == 0) repaint (1132, kGenY + 36, 154, 62);     // Tempo-Anzeige

    if (const int ev = proc.getLearnEvent(); ev != seenLearnEvent)
    {
        seenLearnEvent = ev;
        const int pi = proc.getLearnedParam();
        if (pi >= 0) showNotice ("LEARNED:  CC " + juce::String (proc.getLearnedCc()) + "  >  " + juce::String (specs()[(size_t) pi].name).toUpperCase());
    }

    juce::String t;
    if (proc.getLearnIndex() >= 0) t = "MIDI LEARN: MOVE A CONTROLLER ...";
    else if (proc.getLastCc() >= 0) t = "LAST CC " + juce::String (proc.getLastCc()) + " = " + juce::String (proc.getLastCcValue());
    else t = "NO MIDI CC RECEIVED YET";
    bool rp = false;
    if (noticeTicks > 0) { if (--noticeTicks == 0) rp = true; if (noticeTicks == 89) rp = true; }
    if (t != midiText) { midiText = t; rp = true; }
    if (rp || noticeTicks > 0) repaint (690, kFootY, 450, 26);
}

//==============================================================================
AdmiralEditor::AdmiralEditor (AdmiralProcessor& p) : juce::AudioProcessorEditor (&p), panel (p)
{
    addAndMakeVisible (panel);
    setResizable (true, true);
    setResizeLimits (MainPanel::W / 2, MainPanel::H / 2, MainPanel::W * 2, MainPanel::H * 2);
    getConstrainer()->setFixedAspectRatio ((double) MainPanel::W / (double) MainPanel::H);
    setSize (MainPanel::W, MainPanel::H);
}

void AdmiralEditor::resized()
{
    panel.setBounds (0, 0, MainPanel::W, MainPanel::H);
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) MainPanel::W));
}
