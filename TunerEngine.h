#pragma once
// CHROMA (Studio Wiga) — moteur d'accordage, C++17 pur (aucune dépendance JUCE, testable seul).
//
// Chaîne d'analyse :
//   entrée pleine résolution -> filtre anti-repliement (Butterworth ordre 8) -> décimation vers ~11-12 kHz
//   -> passe-haut 10 Hz -> tampon circulaire.
//
// MONO  : détection grossière McLeod (NSDF) sur deux fenêtres (courte pour la réactivité, longue ~350 ms
//         pour le grave jusqu'à ~15 Hz) + test d'énergie sous-harmonique (anti-erreur d'octave),
//         puis mesure fine par démodulation complexe à la fréquence de la note et régression de la phase
//         dans le temps (principe d'un accordeur stroboscopique) -> précision au dixième de cent.
// POLY  : une démodulation par corde de l'accordage choisi. Pour chaque corde on mesure le partiel
//         (1 à 3) qui n'est recouvert par aucun partiel des autres cordes, puis on mesure la dérive de phase.
//
// Thread : push() est appelé par UN seul thread d'analyse (jamais par le thread audio).
//          Les réglages (setTuning, setReference) et getSnapshot() peuvent venir d'un autre thread.
#include <vector>
#include <complex>
#include <cmath>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdint>

namespace tuna
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr int kMaxStrings = 10;

inline double midiToHz (double midi, double a4) { return a4 * std::pow (2.0, (midi - 69.0) / 12.0); }
inline double hzToMidi (double f, double a4)    { return 69.0 + 12.0 * std::log2 (f / a4); }
inline double centsOf (double f, double ref)    { return 1200.0 * std::log2 (f / ref); }

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double fs, double fc, double q)
    {
        const double w = kTwoPi * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

//==============================================================================
class FFT
{
public:
    void init (int n)
    {
        if (n == size) return;
        size = n; levels = 0;
        while ((1 << levels) < n) ++levels;
        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < levels; ++b) r |= ((i >> b) & 1) << (levels - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0, -kTwoPi * i / n);
    }
    void run (std::vector<std::complex<double>>& a, bool inverse) const
    {
        const int n = size;
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (a[(size_t) i], a[(size_t) rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    std::complex<double> w = tw[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const auto u = a[(size_t) (i + j)], v = a[(size_t) (i + j + half)] * w;
                    a[(size_t) (i + j)] = u + v;
                    a[(size_t) (i + j + half)] = u - v;
                }
        }
        if (inverse) for (auto& x : a) x /= (double) n;
    }
    int size = 0;
private:
    int levels = 0;
    std::vector<int> rev;
    std::vector<std::complex<double>> tw;
};

//==============================================================================
// Démodulateur complexe : ramène la fréquence cible vers 0 Hz puis filtre passe-bas (3 pôles).
// L'argument de la sortie tourne à la vitesse de l'écart de fréquence (comme la roue d'un stroboscope).
struct Demod
{
    double target = 0, alpha = 0, fc = 0;
    std::complex<double> osc { 1.0, 0.0 }, rot { 1.0, 0.0 };
    int renorm = 0;
    std::complex<double> s1, s2, s3;

    void configure (double freq, double cutoff, double fs)
    {
        target = freq; fc = cutoff;
        rot = std::polar (1.0, -kTwoPi * freq / fs);
        alpha = 1.0 - std::exp (-kTwoPi * cutoff / fs);
        reset();
    }
    void reset() { s1 = s2 = s3 = {}; }
    inline std::complex<double> process (double x)
    {
        const std::complex<double> v = x * osc;
        osc *= rot;
        if (++renorm >= 512) { renorm = 0; osc /= std::abs (osc); }
        s1 += alpha * (v - s1);
        s2 += alpha * (s1 - s2);
        s3 += alpha * (s2 - s3);
        return s3;
    }
    double settleTime() const { return fc > 0 ? 3.2 / (kTwoPi * fc) : 0.0; }
};

// Historique des sorties d'un démodulateur, échantillonné à ~400 Hz.
struct PhaseHistory
{
    static constexpr int N = 1024;  // > 2,5 s à 400 Hz
    double t[N] {};
    std::complex<double> z[N] {};
    int64_t count = 0;

    void clear() { count = 0; }
    void push (double time, std::complex<double> v) { const int i = (int) (count % N); t[i] = time; z[i] = v; ++count; }

    // Amplitude moyenne sur les "dur" dernières secondes.
    double meanAmp (double now, double dur) const
    {
        double s = 0; int n = 0;
        for (int64_t k = count - 1; k >= 0 && k >= count - N; --k)
        {
            const int i = (int) (k % N);
            if (now - t[i] > dur) break;
            s += std::abs (z[i]); ++n;
        }
        return n ? s / n : 0.0;
    }

    // Régression pondérée de la phase déroulée sur [t0, now]. slope en rad/s.
    bool slope (double t0, double& slopeOut, double& residOut, double& span) const
    {
        if (count < 8) return false;
        int64_t first = count - 1;
        while (first - 1 >= 0 && first - 1 >= count - N && t[(int) ((first - 1) % N)] >= t0) --first;
        const int64_t n = count - first;
        if (n < 8) return false;

        double prevRaw = std::arg (z[(int) (first % N)]), unwrapped = prevRaw;
        double sw = 0, st = 0, sp = 0;
        static thread_local std::vector<double> ph, tt, ww;
        ph.resize ((size_t) n); tt.resize ((size_t) n); ww.resize ((size_t) n);
        for (int64_t k = first; k < count; ++k)
        {
            const int i = (int) (k % N);
            const double raw = std::arg (z[i]);
            double d = raw - prevRaw;
            while (d > kPi)  d -= kTwoPi;
            while (d < -kPi) d += kTwoPi;
            unwrapped += (k == first ? 0.0 : d);
            prevRaw = raw;
            const double w = std::norm (z[i]);
            const size_t j = (size_t) (k - first);
            ph[j] = unwrapped; tt[j] = t[i]; ww[j] = w;
            sw += w; st += w * t[i]; sp += w * unwrapped;
        }
        if (sw <= 1e-30) return false;
        const double tm = st / sw, pm = sp / sw;
        double num = 0, den = 0;
        for (size_t j = 0; j < (size_t) n; ++j) { const double dt = tt[j] - tm; num += ww[j] * dt * (ph[j] - pm); den += ww[j] * dt * dt; }
        if (den <= 1e-30) return false;
        slopeOut = num / den;
        double r = 0;
        for (size_t j = 0; j < (size_t) n; ++j) { const double e = ph[j] - (pm + slopeOut * (tt[j] - tm)); r += ww[j] * e * e; }
        residOut = std::sqrt (r / sw);
        span = tt[(size_t) n - 1] - tt[0];
        return true;
    }
};

//==============================================================================
struct StringState
{
    int   midi = 0;
    float targetHz = 0;
    float cents = 0;         // écart lissé
    float level = 0;         // amplitude relative (0..1)
    bool  active = false;    // la corde sonne et la mesure est fiable
    bool  measured = false;  // au moins une mesure depuis le dernier changement d'accordage
    float age = 1e9f;        // secondes depuis la dernière mesure fiable
    int   partial = 1;
    bool  shared = false;    // aucun partiel isolé : mesure "mélangée" (moins précise)
};

struct Snapshot
{
    // MONO / STROBE
    bool  monoActive = false;
    int   monoMidi = -1;
    float monoCents = 0;
    float monoHz = 0;
    float monoClarity = 0;
    bool  monoPrecise = false;   // mesure par phase établie (sinon estimation grossière)
    float monoAge = 1e9f;
    bool  monoOffTuning = false; // note seule hors accordage (bascule auto du mode POLY)
    // POLY
    int   numStrings = 0;
    StringState strings[kMaxStrings];
    bool  allInTune = false;
    bool  anyStringActive = false;
    // divers
    float inputDb = -120.0f;
    double time = 0;
};

//==============================================================================
class TunerEngine
{
public:
    static constexpr double kMonoInTune = 1.0;   // cents
    static constexpr double kPolyInTune = 2.0;   // cents

    void prepare (double sampleRate)
    {
        std::lock_guard<std::mutex> lk (cfgMutex);
        fsIn = sampleRate > 0 ? sampleRate : 44100.0;
        decim = std::max (1, (int) std::floor (fsIn / 11000.0));
        fs = fsIn / decim;
        const double qs[4] = { 2.5629154477, 0.8999762231, 0.6013448869, 0.5097955791 };
        for (int i = 0; i < 4; ++i) { aa[i].lowpass (fsIn, std::min (2800.0, 0.45 * fs), qs[i]); aa[i].reset(); }
        hpA = 1.0 / (1.0 + kTwoPi * 10.0 / fs);
        hpX = hpY = 0;
        decPhase = 0;
        ring.assign (kRing, 0.0f);
        written = 0;
        zHop = std::max (1, (int) std::round (fs / 400.0));
        monoHop = std::max (1, (int) std::round (fs * 0.02));
        polyHop = std::max (1, (int) std::round (fs * 0.04));
        fftLong.init (8192); fftShort.init (2048);
        resetMonoLocked();
        cfgDirty = true;
        prepared = true;
    }

    void setReference (double a4)
    {
        std::lock_guard<std::mutex> lk (cfgMutex);
        if (std::abs (a4 - pendingRef) > 1e-9) { pendingRef = a4; cfgDirty = true; }
    }

    void setTuning (const std::vector<int>& midiLowToHigh)
    {
        std::lock_guard<std::mutex> lk (cfgMutex);
        std::vector<int> n (midiLowToHigh.begin(), midiLowToHigh.begin() + (long) std::min<size_t> (midiLowToHigh.size(), kMaxStrings));
        if (n != pendingTuning) { pendingTuning = n; cfgDirty = true; }
    }

    double getDecimatedRate() const { return fs; }

    Snapshot getSnapshot() const
    {
        std::lock_guard<std::mutex> lk (snapMutex);
        return snap;
    }

    // Échantillons mono, pleine résolution. Appelé uniquement par le thread d'analyse.
    void push (const float* x, int n)
    {
        if (! prepared) return;
        applyConfigIfNeeded();
        for (int i = 0; i < n; ++i)
        {
            double v = x[i];
            for (auto& b : aa) v = b.process (v);
            if (++decPhase < decim) continue;
            decPhase = 0;
            // passe-haut 10 Hz
            const double y = hpA * (hpY + v - hpX);
            hpX = v; hpY = y;
            processDecimated (y);
        }
    }

private:
    //--------------------------------------------------------------------------
    static constexpr int kRing = 1 << 15;

    void applyConfigIfNeeded()
    {
        std::lock_guard<std::mutex> lk (cfgMutex);
        if (! cfgDirty) return;
        cfgDirty = false;
        ref = pendingRef;
        tuning = pendingTuning;
        configurePolyLocked();
        resetMonoLocked();
    }

    void configurePolyLocked()
    {
        const int S = (int) tuning.size();
        numStrings = S;
        for (int s = 0; s < S; ++s)
        {
            Str& st = str[s];
            st = Str {};
            st.midi = tuning[(size_t) s];
            st.f0 = midiToHz (st.midi, ref);
        }
        // choix du partiel mesuré : le plus bas qui n'est recouvert par aucun partiel d'une autre corde
        for (int s = 0; s < S; ++s)
        {
            Str& st = str[s];
            int chosen = 0;
            for (int k = 1; k <= 3 && chosen == 0; ++k)
            {
                const double fk = k * st.f0;
                if (fk > 1800.0) break;
                if (k >= 2 && st.f0 > 250.0) break;
                bool clash = false;
                for (int t = 0; t < S && ! clash; ++t)
                {
                    if (t == s) continue;
                    for (int j = 1; j <= 8; ++j)
                    {
                        const double fj = j * str[t].f0;
                        if (std::abs (centsOf (fk, fj)) < 45.0 || std::abs (fk - fj) < 2.0) { clash = true; break; }
                    }
                }
                if (! clash) chosen = k;
            }
            st.shared = (chosen == 0);
            st.partial = chosen == 0 ? 1 : chosen;
            const double fk = st.partial * st.f0;
            // largeur du filtre : 30 % de la distance au partiel voisin le plus proche
            double nearest = st.f0;
            for (int t = 0; t < S; ++t)
                for (int j = 1; j <= 12; ++j)
                {
                    if (t == s && j == st.partial) continue;
                    const double d = std::abs (j * str[t].f0 - fk);
                    if (st.shared && d < 0.05 * fk) continue;   // le partiel en collision est volontairement inclus
                    nearest = std::min (nearest, d);
                }
            const double fc = std::clamp (0.3 * nearest, 1.5, 12.0);
            st.dem.configure (fk, fc, fs);
            st.hist.clear();
            st.configTime = (double) written / fs;
            st.window = std::clamp (20.0 / st.f0, 0.35, 1.0);
        }
        polyCounter = 0;
    }

    void resetMonoLocked()
    {
        monoMidi = -1; candMidi = -1; candCount = 0;
        monoHist.clear();
        monoDem.reset();
        monoLastActive = -1e9;
        coarseHz = 0; coarseClarity = 0;
        monoStableSince = 1e9;
    }

    //--------------------------------------------------------------------------
    void processDecimated (double x)
    {
        ring[(size_t) (written & (kRing - 1))] = (float) x;
        ++written;
        const double now = written / fs;

        // démodulateurs
        const auto zm = monoDem.process (x);
        std::complex<double> zs[kMaxStrings];
        for (int s = 0; s < numStrings; ++s) zs[s] = str[s].dem.process (x);

        if (++zCounter >= zHop)
        {
            zCounter = 0;
            if (monoMidi >= 0) monoHist.push (now, zm);
            for (int s = 0; s < numStrings; ++s) str[s].hist.push (now, zs[s]);
            levelHist[levelIdx] = rmsRecent (std::max (8, (int) (fs * 0.03)));
            levelIdx = (levelIdx + 1) % kLevelHist;
        }

        if (++monoCounter >= monoHop) { monoCounter = 0; analyseMono (now); }
        if (++polyCounter >= polyHop) { polyCounter = 0; analysePoly (now); publish (now); }
    }

    double rmsRecent (int n) const
    {
        n = (int) std::min<int64_t> (n, written);
        double s = 0;
        for (int i = 0; i < n; ++i) { const double v = ring[(size_t) ((written - 1 - i) & (kRing - 1))]; s += v * v; }
        return n ? std::sqrt (s / n) : 0.0;
    }

    void frame (int n, std::vector<double>& out) const
    {
        out.resize ((size_t) n);
        const int64_t start = written - n;
        double mean = 0;
        for (int i = 0; i < n; ++i)
        {
            const int64_t k = start + i;
            out[(size_t) i] = k >= 0 ? ring[(size_t) (k & (kRing - 1))] : 0.0;
            mean += out[(size_t) i];
        }
        mean /= n;
        for (auto& v : out) v -= mean;
    }

    //--------------------------------------------------------------------------
    // McLeod Pitch Method. Retourne fréquence (0 si rien) et clarté (0..1).
    struct Pitch { double hz = 0, clarity = 0, tau = 0; };

    Pitch mpm (const std::vector<double>& x, const FFT& fft, double minHz, double maxHz)
    {
        const int N = (int) x.size();
        const int M = fft.size;
        buf.assign ((size_t) M, {});
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[(size_t) i];
        fft.run (buf, false);
        for (auto& c : buf) c = std::norm (c);
        fft.run (buf, true);

        const int maxLag = std::min (N / 2, (int) (fs / minHz) + 2);
        const int minLag = std::max (2, (int) (fs / maxHz));
        nsdf.assign ((size_t) maxLag + 2, 0.0);
        double m = 0;
        for (int i = 0; i < N; ++i) m += 2.0 * x[(size_t) i] * x[(size_t) i];
        if (m < 1e-14) return {};
        for (int tau = 0; tau <= maxLag + 1 && tau < N; ++tau)
        {
            if (tau > 0) m -= x[(size_t) (tau - 1)] * x[(size_t) (tau - 1)] + x[(size_t) (N - tau)] * x[(size_t) (N - tau)];
            nsdf[(size_t) tau] = m > 1e-14 ? 2.0 * buf[(size_t) tau].real() / m : 0.0;
        }
        // maxima "clés" (un par zone positive après le premier passage négatif)
        keys.clear();
        int t = 1;
        while (t < maxLag && nsdf[(size_t) t] > 0) ++t;              // sortir du lobe central
        while (t < maxLag)
        {
            while (t < maxLag && nsdf[(size_t) t] <= 0) ++t;
            int best = -1; double bv = -1;
            while (t < maxLag && nsdf[(size_t) t] > 0)
            {
                if (nsdf[(size_t) t] > bv) { bv = nsdf[(size_t) t]; best = t; }
                ++t;
            }
            if (best > 0 && best >= minLag) keys.push_back (best);
        }
        if (keys.empty()) return {};
        double nmax = 0;
        for (int k : keys) nmax = std::max (nmax, nsdf[(size_t) k]);
        const double thr = 0.88 * nmax;
        int pick = keys.front();
        for (int k : keys) if (nsdf[(size_t) k] >= thr) { pick = k; break; }
        // interpolation parabolique
        const double a = nsdf[(size_t) pick - 1], b = nsdf[(size_t) pick], c = nsdf[(size_t) pick + 1];
        const double den = a - 2 * b + c;
        const double off = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
        Pitch p;
        p.tau = pick + std::clamp (off, -0.5, 0.5);
        p.hz = fs / p.tau;
        p.clarity = b - 0.25 * (a - c) * off;
        return p;
    }

    // Puissance d'une composante (DFT pondérée Hann) à la fréquence f sur la trame x (oscillateur récursif).
    double bandPower (const std::vector<double>& x, double f)
    {
        const int N = (int) x.size();
        if ((int) hann.size() != N)
        {
            hann.resize ((size_t) N);
            for (int i = 0; i < N; ++i) hann[(size_t) i] = 0.5 - 0.5 * std::cos (kTwoPi * i / (N - 1));
        }
        const std::complex<double> rot = std::polar (1.0, -kTwoPi * f / fs);
        std::complex<double> osc (1.0, 0.0), acc;
        for (int i = 0; i < N; ++i)
        {
            acc += (x[(size_t) i] * hann[(size_t) i]) * osc;
            osc *= rot;
            if ((i & 1023) == 1023) osc /= std::abs (osc);
        }
        return std::norm (acc);
    }

    // Vrai si f/2 est la vraie fondamentale (énergie présente sur les partiels impairs de f/2).
    bool subOctaveIsReal (const std::vector<double>& x, double f)
    {
        const double h = 0.5 * f;
        if (h < 14.0) return false;
        const int N = (int) x.size();
        if (N / fs < 3.5 / h) return false;   // fenêtre trop courte pour juger
        double odd = 0, even = 0;
        for (int k = 1; k <= 6; ++k)
        {
            if (k * h > 0.45 * fs) break;
            double best = 0;   // petite recherche locale (+/- 30 cents) pour l'inharmonicité / le désaccord
            for (int d = -1; d <= 1; ++d) best = std::max (best, bandPower (x, k * h * std::pow (2.0, d * 30.0 / 1200.0)));
            (k % 2 ? odd : even) += best;
        }
        return even > 0 && odd > 0.12 * even;
    }

    void analyseMono (double now)
    {
        const double lvl = rmsRecent ((int) (fs * 0.05));
        inputRms = lvl;
        const double thr = std::pow (10.0, -66.0 / 20.0);
        if (lvl < thr || written < (int64_t) (fs * 0.1))
        {
            if (now - monoLastActive > 0.25) { coarseHz = 0; monoActiveFlag = false; monoStableSince = 1e9; }
            return;
        }

        frame (1024, frS);
        const Pitch ps = mpm (frS, fftShort, 70.0, 1600.0);
        Pitch pl;
        const int longN = (int) std::min<int64_t> (4096, written);
        if (longN >= 2048) { frame (longN, frL); pl = mpm (frL, fftLong, 15.0, 400.0); }

        double f = 0, clar = 0;
        if (pl.clarity >= 0.75 && pl.hz < 100.0)                         { f = pl.hz; clar = pl.clarity; }
        else if (ps.clarity >= 0.80)
        {
            f = ps.hz; clar = ps.clarity;
            if (pl.clarity >= 0.70 && pl.hz > 0 && pl.hz < 120.0 && ps.hz < 500.0)
            {
                const double r = ps.hz / pl.hz;
                for (int m = 2; m <= 4; ++m) if (std::abs (r - m) < 0.03 * m) { f = pl.hz; clar = pl.clarity; break; }
            }
        }
        else if (pl.clarity >= 0.75)                                     { f = pl.hz; clar = pl.clarity; }

        if (f <= 0) { if (now - monoLastActive > 0.25) { monoActiveFlag = false; monoStableSince = 1e9; } return; }

        if (longN >= 2048 && subOctaveIsReal (frL, f)) f *= 0.5;

        coarseHz = f; coarseClarity = clar;
        monoLastActive = now;
        monoActiveFlag = true;

        const int cand = (int) std::lround (hzToMidi (f, ref));
        if (cand == monoMidi) { candCount = 0; }
        else
        {
            if (cand == candMidi) ++candCount; else { candMidi = cand; candCount = 1; }
            if (monoMidi < 0 || candCount >= 3) startMonoNote (cand, now);
        }

        // relance de l'attaque (corde repincée) : on oublie l'historique de phase
        const int prev = (levelIdx - 1 - 32 + 4 * kLevelHist) % kLevelHist;   // ~80 ms plus tôt
        if (levelHist[prev] > 0 && lvl > 2.2 * levelHist[prev] && monoMidi >= 0)
        {
            monoHist.clear();
            monoSettleUntil = now + 0.045;
        }
    }

    void startMonoNote (int midi, double now)
    {
        monoMidi = midi; candMidi = -1; candCount = 0;
        const double fn = midiToHz (midi, ref);
        int k = 1;
        if (frL.size() >= 2048)
        {
            const double p1 = bandPower (frL, fn), p2 = (2 * fn < 0.45 * fs) ? bandPower (frL, 2 * fn) : 0.0;
            if (p1 < 0.03 * p2) k = 2;   // fondamentale très faible (-15 dB) : on mesure l'octave
        }
        monoPartial = k;
        const double fc = std::clamp (0.2 * fn, 1.5, 15.0);
        monoDem.configure (k * fn, fc, fs);
        monoHist.clear();
        monoSettleUntil = now + monoDem.settleTime();
        monoWindow = std::clamp (20.0 / fn, 0.2, 1.0);
        monoStableSince = now;
        monoCentsSm = 0; monoHaveFine = false;
    }

    void computeMonoFine (double now)
    {
        if (monoMidi < 0) return;
        const double fn = midiToHz (monoMidi, ref);
        const double t0 = std::max (monoSettleUntil, now - monoWindow);
        double sl = 0, res = 0, span = 0;
        if (now - t0 >= 0.06 && monoHist.slope (t0, sl, res, span) && span >= std::max (0.05, 0.4 * monoWindow) && res < 0.9)
        {
            const double fk = monoPartial * fn + sl / kTwoPi;
            const double c = centsOf (fk / monoPartial, fn);
            monoCentsSm = monoHaveFine ? monoCentsSm + 0.5 * (c - monoCentsSm) : c;
            monoHaveFine = true;
            monoFineHz = fk / monoPartial;
        }
        else if (coarseHz > 0 && ! monoHaveFine && std::abs (hzToMidi (coarseHz, ref) - monoMidi) < 0.6)
        {
            monoCentsSm = centsOf (coarseHz, fn);
            monoFineHz = coarseHz;
        }
    }

    //--------------------------------------------------------------------------
    void analysePoly (double now)
    {
        computeMonoFine (now);

        double maxAmp = 0;
        for (int s = 0; s < numStrings; ++s)
        {
            Str& st = str[s];
            st.amp = st.hist.meanAmp (now, 0.06);
            maxAmp = std::max (maxAmp, st.amp);
            // attaque : forte montée d'amplitude par rapport à ~100 ms plus tôt
            st.ampLog[st.ampIdx] = st.amp;
            const int back = (st.ampIdx + 1) % Str::kAmpLog;      // ~120 ms plus tôt (3 analyses)
            if (st.ampLog[back] > 0 && st.amp > 2.0 * st.ampLog[back] && st.amp > 1e-5) st.onset = now;
            st.ampIdx = (st.ampIdx + 1) % Str::kAmpLog;
        }
        const double absThr = 0.5 * std::pow (10.0, -70.0 / 20.0);
        for (int s = 0; s < numStrings; ++s)
        {
            Str& st = str[s];
            st.activeNow = false;
            if (st.amp < absThr || st.amp < 0.025 * maxAmp) continue;
            const double t0 = std::max ({ now - st.window, st.onset + 0.07, st.configTime + st.dem.settleTime() });
            double sl = 0, res = 0, span = 0;
            if (now - t0 < 0.12 || ! st.hist.slope (t0, sl, res, span) || span < 0.1 || res > 0.6) continue;
            const double fk = st.partial * st.f0 + sl / kTwoPi;
            const double c = centsOf (fk / st.partial, st.f0);
            if (std::abs (c) > 60.0) continue;
            st.cents = st.measured && now - st.lastHeard < 0.5 ? st.cents + 0.45 * (c - st.cents) : c;
            st.measured = true;
            st.lastHeard = now;
            st.activeNow = true;
        }
        maxAmpPoly = maxAmp;
    }

    void publish (double now)
    {
        Snapshot s;
        s.time = now;
        s.inputDb = (float) (20.0 * std::log10 (std::max (1e-6, inputRms)));
        s.monoActive = monoActiveFlag && monoMidi >= 0;
        s.monoMidi = monoMidi;
        s.monoCents = (float) monoCentsSm;
        s.monoHz = (float) (monoHaveFine ? monoFineHz : coarseHz);
        s.monoClarity = (float) coarseClarity;
        s.monoPrecise = monoHaveFine;
        s.monoAge = (float) (now - monoLastActive);

        s.numStrings = numStrings;
        bool all = numStrings > 0, any = false;
        for (int i = 0; i < numStrings; ++i)
        {
            const Str& st = str[i];
            StringState& o = s.strings[i];
            o.midi = st.midi; o.targetHz = (float) st.f0; o.cents = (float) st.cents;
            o.level = (float) (maxAmpPoly > 0 ? st.amp / maxAmpPoly : 0.0);
            o.active = st.activeNow; o.measured = st.measured;
            o.age = st.measured ? (float) (now - st.lastHeard) : 1e9f;
            o.partial = st.partial; o.shared = st.shared;
            any = any || st.activeNow;
            if (! st.measured || now - st.lastHeard > 8.0 || std::abs (st.cents) > kPolyInTune) all = false;
        }
        s.allInTune = all;
        s.anyStringActive = any;

        // note seule hors accordage (pour la bascule automatique en mode POLY)
        bool off = false;
        if (s.monoActive && coarseClarity > 0.9 && now - monoStableSince > 0.3)
        {
            off = true;
            for (int i = 0; i < numStrings; ++i) if (str[i].midi == monoMidi) { off = false; break; }
        }
        s.monoOffTuning = off;

        std::lock_guard<std::mutex> lk (snapMutex);
        snap = s;
    }

    //--------------------------------------------------------------------------
    struct Str
    {
        int midi = 0; double f0 = 0; int partial = 1; bool shared = false;
        Demod dem; PhaseHistory hist;
        double window = 0.5, onset = -1e9, lastHeard = -1e9, configTime = 0;
        double amp = 0, cents = 0; bool measured = false, activeNow = false;
        static constexpr int kAmpLog = 3;
        double ampLog[kAmpLog] {}; int ampIdx = 0;
    };

    // configuration
    std::mutex cfgMutex;
    double pendingRef = 440.0, ref = 440.0;
    std::vector<int> pendingTuning, tuning;
    bool cfgDirty = true, prepared = false;

    // chaîne d'entrée
    double fsIn = 44100, fs = 11025;
    int decim = 4, decPhase = 0;
    Biquad aa[4];
    double hpA = 1, hpX = 0, hpY = 0;
    std::vector<float> ring;
    int64_t written = 0;
    int zHop = 28, zCounter = 0, monoHop = 220, monoCounter = 0, polyHop = 441, polyCounter = 0;

    static constexpr int kLevelHist = 64;
    double levelHist[kLevelHist] {}; int levelIdx = 0;
    double inputRms = 0;

    // mono
    FFT fftLong, fftShort;
    std::vector<std::complex<double>> buf;
    std::vector<double> nsdf, frS, frL, hann;
    std::vector<int> keys;
    Demod monoDem; PhaseHistory monoHist;
    int monoMidi = -1, candMidi = -1, candCount = 0, monoPartial = 1;
    double monoSettleUntil = 0, monoWindow = 0.5, monoLastActive = -1e9, monoStableSince = 1e9;
    double coarseHz = 0, coarseClarity = 0, monoCentsSm = 0, monoFineHz = 0;
    bool monoHaveFine = false, monoActiveFlag = false;

    // poly
    Str str[kMaxStrings];
    int numStrings = 0;
    double maxAmpPoly = 0;

    mutable std::mutex snapMutex;
    Snapshot snap;
};
} // namespace tuna
