#include "DrumEngine.h"
#include <algorithm>
#include <cmath>

namespace ad
{
//==============================================================================
void Sequencer::collect (const Block& b, const Pattern& main, const Pattern& fill, std::vector<NoteEvent>& out)
{
    if (b.numSamples <= 0 || b.bpm <= 0.0) return;
    const double samplesPerPpq = b.sampleRate * 60.0 / b.bpm;
    const double p0 = b.ppqStart;
    const double p1 = p0 + (double) b.numSamples / samplesPerPpq;

    // alle Sechzehntel, deren (verschwungene) Position in [p0, p1) liegt
    const long long nFirst = std::max (0LL, (long long) std::floor (p0 * 4.0) - 1);
    const long long nLast = (long long) std::floor (p1 * 4.0) + 1;
    for (long long n = nFirst; n <= nLast; ++n)
    {
        const long long bar = n / NUM_STEPS;
        const int step = (int) (n % NUM_STEPS);
        const double t = (double) bar * 4.0 + stepOffsetPpq (step, b.swing);
        if (t < p0 || t >= p1) continue;

        const bool isFill = isFillBar (bar, b.fillEvery);
        const Pattern& pat = isFill ? fill : main;
        const int baseOffset = (int) std::floor ((t - p0) * samplesPerPpq);
        lastStep_ = step;
        lastFill_ = isFill;

        for (int tr = 0; tr < NUM_TRACKS; ++tr)
        {
            float vel = pat.v[(size_t) tr][(size_t) step];
            if (tr == T_CRASH && step == 0 && crashAfterFill (bar, b.fillEvery))
                vel = std::max (vel, 0.9f);
            if (vel <= 0.f) continue;

            int off = baseOffset;
            if (b.humanize > 0.f)
            {
                // leicht verspätet (bis 12 ms) und Anschlag gestreut - nie die betonte Eins verschieben
                if (! (step == 0 && vel >= 0.99f))
                    off += (int) (rand01() * b.humanize * 0.012 * b.sampleRate);
                vel = std::clamp (vel + (rand01() - 0.5f) * 0.3f * b.humanize, 0.05f, 1.f);
            }
            out.push_back ({ tr, vel, off });
        }
    }
}

//==============================================================================
void DrumEngine::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    fadeSamples = std::max (16, (int) (0.003 * sr));
    allSoundOff();
}

void DrumEngine::allSoundOff()
{
    for (auto& v : voices) v = Voice();
}

bool DrumEngine::active() const
{
    for (auto& v : voices) if (v.track >= 0) return true;
    return false;
}

void DrumEngine::startFade (Voice& v, int afterSamples)
{
    if (v.fadeDelay >= 0 && v.fadeDelay <= afterSamples) return;   // blendet schon früher aus
    if (v.fadeLeft > 0) return;
    v.fadeDelay = std::max (0, afterSamples);
}

void DrumEngine::trigger (int track, float velocity, int offset, const TrackSettings& ts)
{
    if (track < 0 || track >= NUM_TRACKS || ts.mute) return;
    const SampleData* s = slot[(size_t) track].load();
    if (s == nullptr || s->data.size() < 2) return;

    // Gleiche Spur neu angeschlagen: vorige Stimme kurz ausblenden (kein Knacken).
    // Geschlossene Hi-Hat würgt die offene ab (wie bei echten Drumcomputern).
    for (auto& v : voices)
    {
        if (v.track < 0) continue;
        if (v.track == track || (track == T_CHAT && v.track == T_OHAT))
            startFade (v, offset);
    }

    // freie Stimme suchen, sonst die älteste übernehmen
    int best = -1;
    for (int i = 0; i < kVoices; ++i) if (voices[(size_t) i].track < 0) { best = i; break; }
    if (best < 0)
    {
        uint32_t oldest = UINT32_MAX;
        for (int i = 0; i < kVoices; ++i)
            if (started[(size_t) i] < oldest) { oldest = started[(size_t) i]; best = i; }
    }

    Voice v;
    v.track = track;
    v.s = s;
    v.rate = std::pow (2.0, (double) ts.tune / 12.0) * s->sampleRate / sr;
    const float vel = std::clamp (velocity, 0.f, 1.f);
    const float g = std::pow (vel, 1.4f) * ts.level * ts.level * 0.8f;
    const float pan = std::clamp (ts.pan, -1.f, 1.f);
    const float a = (pan + 1.f) * 0.25f * 3.14159265f;
    v.gl = g * std::cos (a) * 1.41421356f;
    v.gr = g * std::sin (a) * 1.41421356f;
    if (ts.decay < 0.995f)
    {
        const double tau = 0.015 + 1.6 * (double) ts.decay * (double) ts.decay;
        v.envMul = (float) std::exp (-1.0 / (tau * sr));
    }
    v.delay = std::max (0, offset);
    voices[(size_t) best] = v;
    started[(size_t) best] = ++age;
}

void DrumEngine::render (float* const* outL, float* const* outR, int numSamples)
{
    for (auto& v : voices)
    {
        if (v.track < 0) continue;
        const int trk = v.track;
        float* L = outL[v.track];
        float* R = outR[v.track];
        const auto& d = v.s->data;
        const double last = (double) d.size() - 1.0;
        float peak = 0.f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.fadeDelay >= 0)
            {
                if (v.fadeDelay == 0)
                {
                    v.fadeDelay = -1;
                    v.fadeLeft = fadeSamples;
                    v.fadeStep = v.fadeGain / (float) fadeSamples;
                }
                else --v.fadeDelay;
            }
            if (v.delay > 0) { --v.delay; continue; }

            const int i0 = (int) v.pos;
            const float fr = (float) (v.pos - (double) i0);
            const float x = d[(size_t) i0] + (d[(size_t) i0 + 1] - d[(size_t) i0]) * fr;
            const float y = x * v.env * v.fadeGain;
            L[i] += y * v.gl;
            R[i] += y * v.gr;
            peak = std::max (peak, std::abs (y) * std::max (v.gl, v.gr));

            v.env *= v.envMul;
            v.pos += v.rate;
            if (v.fadeLeft > 0)
            {
                v.fadeGain -= v.fadeStep;
                if (--v.fadeLeft == 0 || v.fadeGain <= 0.f) { v.track = -1; break; }
            }
            if (v.pos >= last || v.env < 1.0e-4f) { v.track = -1; break; }
        }
        if (peak > 0.f)
        {
            auto& m = meter[(size_t) trk];
            float cur = m.load();
            while (peak > cur && ! m.compare_exchange_weak (cur, peak)) {}
        }
    }
}
} // namespace ad
