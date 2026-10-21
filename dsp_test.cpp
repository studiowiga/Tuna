// Tuna — banc de test du moteur d'accordage (hors JUCE).
// g++ -O2 -std=c++17 -I. dsp_test.cpp -o dsp_test && ./dsp_test
// Synthèse de cordes réalistes (inharmonicité, partiels qui décroissent, fondamentale faible,
// glissement de hauteur à l'attaque, bruit de médiator, bruit de fond) puis mesure de l'erreur.
#include "TunerEngine.h"
#include "Tunings.h"
#include <cstdio>
#include <random>
#include <string>
#include <chrono>

using namespace tuna;

struct Voice
{
    double f0, start, amp, fundGain, B;
    double phases[16];
};

static std::mt19937 rng (1234);
static double urand (double a, double b) { return std::uniform_real_distribution<double> (a, b) (rng); }

static void render (std::vector<float>& out, double fs, const std::vector<Voice>& voices, double noiseDb)
{
    std::normal_distribution<double> nd (0.0, 1.0);
    const double noise = std::pow (10.0, noiseDb / 20.0);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = i / fs;
        double v = noise * nd (rng);
        for (auto& vo : voices)
        {
            const double tt = t - vo.start;
            if (tt < 0) continue;
            const double glide = 1.0 + 0.004 * std::exp (-tt / 0.05);       // +7 cents à l'attaque
            const double env = std::min (1.0, tt / 0.004);
            for (int k = 1; k <= 12; ++k)
            {
                const double fk = k * vo.f0 * std::sqrt (1.0 + vo.B * k * k) / std::sqrt (1.0 + vo.B) * glide;
                if (fk > 0.45 * fs) break;
                const double a = vo.amp * (k == 1 ? vo.fundGain : 1.0) / std::pow (k, 0.9)
                               * std::exp (-tt * (0.6 + 0.25 * k));
                // phase intégrée approximativement (le glissement est court)
                const double ph = kTwoPi * k * vo.f0 * std::sqrt (1.0 + vo.B * k * k) / std::sqrt (1.0 + vo.B)
                                  * (tt + 0.004 * 0.05 * (1.0 - std::exp (-tt / 0.05))) + vo.phases[k];
                v += env * a * std::sin (ph);
            }
            if (tt < 0.012) v += vo.amp * 0.3 * nd (rng) * (1.0 - tt / 0.012);  // médiator
        }
        out[i] = (float) v;
        (void) 0;
    }
}

static Voice makeVoice (double f0, double start, double amp, double fundGain, double B)
{
    Voice v { f0, start, amp, fundGain, B, {} };
    for (auto& p : v.phases) p = urand (0, kTwoPi);
    return v;
}

static int fails = 0;

static void monoTest (double fs)
{
    std::printf ("\n=== MONO  fs=%.0f Hz ===\n", fs);
    std::printf (" note  | detune |  fund | t=0.4s        | t=0.8s        | t=1.4s\n");
    const int notes[] = { 14, 16, 17, 19, 20, 23, 26, 28, 31, 33, 35, 40, 45, 50, 55, 59, 64, 69, 76, 84, 88 };
    double worst = 0;
    for (int midi : notes)
        for (double fg : { 1.0, 0.1 })
        {
            if (fg < 1.0 && midi > 45) continue;
            TunerEngine e;
            e.prepare (fs);
            e.setReference (440.0);
            e.setTuning (parseNotes ("E2 A2 D3 G3 B3 E4"));
            const double det = urand (-25, 25);
            const double f0 = midiToHz (midi, 440.0) * std::pow (2.0, det / 1200.0);
            const double B = midi < 40 ? 2.0e-4 : 8e-5;
            std::vector<float> sig ((size_t) (fs * 1.5));
            render (sig, fs, { makeVoice (f0, 0.05, 0.25, fg, B) }, -75.0);
            std::string line;
            char cell[64];
            const double probes[3] = { 0.4, 0.8, 1.4 };
            int next = 0;
            for (size_t pos = 0, block = 256; pos < sig.size(); pos += block)
            {
                const int n = (int) std::min (block, sig.size() - pos);
                e.push (sig.data() + pos, n);
                const double t = (pos + n) / fs;
                while (next < 3 && t >= probes[next])
                    {
                        const double probe = probes[next++];
                        const auto s = e.getSnapshot();
                        const bool ok = s.monoActive && s.monoMidi == midi;
                        const double err = s.monoCents - det;
                        std::snprintf (cell, sizeof cell, " %-4s %+6.2f %s |", s.monoActive ? noteName (s.monoMidi).c_str() : "--",
                                       ok ? err : 0.0, ok ? (s.monoPrecise ? "P" : "c") : "X");
                        line += cell;
                        if (probe > 1.0)
                        {
                            if (! ok) ++fails;
                            else worst = std::max (worst, std::abs (err));
                        }
                    }
            }
            std::printf (" %-5s | %+6.1f | %5s |%s\n", noteName (midi).c_str(), det, fg < 1 ? "-20dB" : "0dB", line.c_str());
        }
    std::printf ("  -> pire erreur à 1,4 s (notes correctes) : %.2f cents\n", worst);
}

static void polyTest (double fs, const char* label, const char* notes)
{
    TunerEngine e;
    e.prepare (fs);
    e.setReference (440.0);
    const auto tn = parseNotes (notes);
    e.setTuning (tn);
    std::vector<double> det;
    std::vector<Voice> vs;
    double t = 0.05;
    for (size_t i = 0; i < tn.size(); ++i)
    {
        det.push_back (urand (-20, 20));
        const double f0 = midiToHz (tn[i], 440.0) * std::pow (2.0, det.back() / 1200.0);
        vs.push_back (makeVoice (f0, t, urand (0.08, 0.14), tn[i] < 33 ? 0.3 : 1.0, tn[i] < 40 ? 2e-4 : 8e-5));
        t += urand (0.004, 0.012);   // strum
    }
    std::vector<float> sig ((size_t) (fs * 2.0));
    render (sig, fs, vs, -75.0);
    for (size_t pos = 0; pos < sig.size(); pos += 256) e.push (sig.data() + pos, (int) std::min<size_t> (256, sig.size() - pos));
    const auto s = e.getSnapshot();
    std::printf ("\n=== POLY %s (%s) ===\n", label, notes);
    double worst = 0;
    for (int i = s.numStrings - 1; i >= 0; --i)
    {
        const auto& st = s.strings[i];
        const double err = st.cents - det[(size_t) i];
        std::printf ("  %-4s k=%d%s  réel %+6.1f  lu %+6.1f  erreur %+5.2f  %s\n", noteName (st.midi).c_str(), st.partial,
                     st.shared ? "*" : " ", det[(size_t) i], st.cents, err, st.measured ? "" : "PAS MESURÉE");
        if (! st.measured) ++fails;
        else if (! st.shared) worst = std::max (worst, std::abs (err));
    }
    std::printf ("  -> pire erreur (cordes à partiel isolé) : %.2f cents   (* = partiel partagé)\n", worst);
}

int main()
{
    {   // coût CPU : 10 s de signal guitare 6 cordes à 48 kHz
        TunerEngine e; e.prepare (48000.0); e.setTuning (parseNotes ("E2 A2 D3 G3 B3 E4"));
        std::vector<float> sig (48000 * 10);
        render (sig, 48000.0, { makeVoice (110.0, 0.0, 0.2, 1.0, 1e-4), makeVoice (82.4, 0.0, 0.2, 1.0, 1e-4) }, -70.0);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos < sig.size(); pos += 512) e.push (sig.data() + pos, 512);
        const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("Coût CPU : %.1f %% d'un coeur (temps réel x%.0f)\n", 100.0 * el / 10.0, 10.0 / el);
    }
    monoTest (48000.0);
    monoTest (44100.0);
    polyTest (48000.0, "Guitare 6 standard", "E2 A2 D3 G3 B3 E4");
    polyTest (48000.0, "Guitare 6 Drop B", "B1 F#2 B2 E3 G#3 C#4");
    polyTest (48000.0, "Guitare 7 Sol standard", "G1 C2 F2 A#2 D#3 G3 C4");
    polyTest (48000.0, "Open D", "D2 A2 D3 F#3 A3 D4");
    polyTest (48000.0, "DADGAD", "D2 A2 D3 G3 A3 D4");
    polyTest (48000.0, "Guitare 8 Drop E", "E1 B1 E2 A2 D3 G3 B3 E4");
    polyTest (44100.0, "Basse 4 standard", "E1 A1 D2 G2");
    polyTest (44100.0, "Basse 5 G# standard", "G#0 C#1 F#1 B1 E2");
    polyTest (96000.0, "Basse 5 F standard", "F0 A#0 D#1 G#1 C#2");
    polyTest (48000.0, "Basse 5 Mi standard", "E0 A0 D1 G1 C2");
    std::printf ("\nEchecs (note fausse ou corde non mesurée) : %d\n", fails);
    return fails == 0 ? 0 : 1;
}
