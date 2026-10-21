#pragma once
// Tuna (Studio Wiga) — accordages d'usine + utilitaires de notes.
// Les accordages sont écrits du grave vers l'aigu (corde la plus grave en premier).
#include <string>
#include <vector>
#include <cmath>
#include <cctype>
#include <map>

namespace tuna
{
struct Tuning
{
    std::string name;
    std::string category;
    std::vector<int> notes;   // numéros MIDI, du grave vers l'aigu
    bool user = false;
};

// "C#4", "Bb1", "F0" -> numéro MIDI (C4 = 60). Retourne -1 si invalide.
inline int parseNote (const std::string& s)
{
    if (s.empty()) return -1;
    static const int pcs[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const char L = (char) std::toupper ((unsigned char) s[0]);
    if (L < 'A' || L > 'G') return -1;
    int pc = pcs[L - 'A'];
    size_t i = 1;
    if (i < s.size() && (s[i] == '#' || s[i] == 'b')) { pc += (s[i] == '#' ? 1 : -1); ++i; }
    if (i >= s.size()) return -1;
    int oct = 0; bool neg = false;
    if (s[i] == '-') { neg = true; ++i; }
    if (i >= s.size() || ! std::isdigit ((unsigned char) s[i])) return -1;
    while (i < s.size() && std::isdigit ((unsigned char) s[i])) oct = oct * 10 + (s[i++] - '0');
    if (neg) oct = -oct;
    return 12 * (oct + 1) + pc;
}

inline std::vector<int> parseNotes (const std::string& list)
{
    std::vector<int> out;
    std::string cur;
    for (size_t i = 0; i <= list.size(); ++i)
    {
        if (i == list.size() || list[i] == ' ')
        {
            if (! cur.empty()) { const int m = parseNote (cur); if (m >= 0) out.push_back (m); cur.clear(); }
        }
        else cur += list[i];
    }
    return out;
}

inline const char* pitchClassName (int midi)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((midi % 12) + 12) % 12];
}
inline int octaveOf (int midi) { return (int) std::floor (midi / 12.0) - 1; }
inline std::string noteName (int midi) { return std::string (pitchClassName (midi)) + std::to_string (octaveOf (midi)); }

inline std::string notesToString (const std::vector<int>& n)
{
    std::string s;
    for (size_t i = 0; i < n.size(); ++i) { if (i) s += ' '; s += noteName (n[i]); }
    return s;
}

inline const std::vector<Tuning>& factoryTunings()
{
    static const std::vector<Tuning> list = []
    {
        std::vector<Tuning> v;
        auto add = [&v] (const char* cat, const char* name, const char* notes) { v.push_back ({ name, cat, parseNotes (notes), false }); };

        const char* g6 = "GUITARE 6 · STANDARD & BAS";
        add (g6, "Standard",        "E2 A2 D3 G3 B3 E4");
        add (g6, "Drop D",          "D2 A2 D3 G3 B3 E4");
        add (g6, "Mib standard",    "D#2 G#2 C#3 F#3 A#3 D#4");
        add (g6, "Drop C#",         "C#2 G#2 C#3 F#3 A#3 D#4");
        add (g6, "Ré standard",     "D2 G2 C3 F3 A3 D4");
        add (g6, "Drop C",          "C2 G2 C3 F3 A3 D4");
        add (g6, "Do# standard",    "C#2 F#2 B2 E3 G#3 C#4");
        add (g6, "Drop B",          "B1 F#2 B2 E3 G#3 C#4");
        add (g6, "Do standard",     "C2 F2 A#2 D#3 G3 C4");
        add (g6, "Drop A#",         "A#1 F2 A#2 D#3 G3 C4");
        add (g6, "Si standard",     "B1 E2 A2 D3 F#3 B3");
        add (g6, "Drop A",          "A1 E2 A2 D3 F#3 B3");
        add (g6, "La# standard",    "A#1 D#2 G#2 C#3 F3 A#3");
        add (g6, "Drop G#",         "G#1 D#2 G#2 C#3 F3 A#3");
        add (g6, "La standard",     "A1 D2 G2 C3 E3 A3");
        add (g6, "Drop G",          "G1 D2 G2 C3 E3 A3");
        add (g6, "Drop F#",         "F#1 C#2 F#2 B2 D#3 G#3");
        add (g6, "Drop F",          "F1 C2 F2 A#2 D3 G3");
        add (g6, "Double Drop D",   "D2 A2 D3 G3 B3 D4");

        const char* op = "GUITARE 6 · OPEN TUNINGS";
        add (op, "Open D",          "D2 A2 D3 F#3 A3 D4");
        add (op, "Open D mineur",   "D2 A2 D3 F3 A3 D4");
        add (op, "Open G",          "D2 G2 D3 G3 B3 D4");
        add (op, "Open G mineur",   "D2 G2 D3 G3 A#3 D4");
        add (op, "Open G6",         "D2 G2 D3 G3 B3 E4");
        add (op, "Open E",          "E2 B2 E3 G#3 B3 E4");
        add (op, "Open E mineur",   "E2 B2 E3 G3 B3 E4");
        add (op, "Open A",          "E2 A2 E3 A3 C#4 E4");
        add (op, "Open A mineur",   "E2 A2 E3 A3 C4 E4");
        add (op, "Open C",          "C2 G2 C3 G3 C4 E4");
        add (op, "Open C mineur",   "C2 G2 C3 G3 C4 D#4");
        add (op, "Open C6",         "C2 A2 C3 G3 C4 E4");
        add (op, "Open B",          "B1 F#2 B2 F#3 B3 D#4");
        add (op, "Open F",          "F2 A2 C3 F3 C4 F4");
        add (op, "DADGAD",          "D2 A2 D3 G3 A3 D4");
        add (op, "CGCGCD",          "C2 G2 C3 G3 C4 D4");
        add (op, "Orkney (CGDGCD)", "C2 G2 D3 G3 C4 D4");
        add (op, "Open D (Drop C)", "C2 A2 D3 F#3 A3 D4");

        const char* g7 = "GUITARE 7 CORDES";
        add (g7, "Standard",        "B1 E2 A2 D3 G3 B3 E4");
        add (g7, "Drop A",          "A1 E2 A2 D3 G3 B3 E4");
        add (g7, "La# standard",    "A#1 D#2 G#2 C#3 F#3 A#3 D#4");
        add (g7, "Drop G#",         "G#1 D#2 G#2 C#3 F#3 A#3 D#4");
        add (g7, "La standard",     "A1 D2 G2 C3 F3 A3 D4");
        add (g7, "Drop G",          "G1 D2 G2 C3 F3 A3 D4");
        add (g7, "Sol standard",    "G1 C2 F2 A#2 D#3 G3 C4");
        add (g7, "Drop F#",         "F#1 C#2 F#2 B2 E3 G#3 C#4");
        add (g7, "Drop F",          "F1 C2 F2 A#2 D#3 G3 C4");
        add (g7, "Drop E",          "E1 B1 F#2 B2 E3 G#3 C#4");

        const char* g8 = "GUITARE 8 CORDES";
        add (g8, "Standard",        "F#1 B1 E2 A2 D3 G3 B3 E4");
        add (g8, "Drop E",          "E1 B1 E2 A2 D3 G3 B3 E4");
        add (g8, "Fa standard",     "F1 A#1 D#2 G#2 C#3 F#3 A#3 D#4");
        add (g8, "Drop D#",         "D#1 A#1 D#2 G#2 C#3 F#3 A#3 D#4");
        add (g8, "Mi standard",     "E1 A1 D2 G2 C3 F3 A3 D4");
        add (g8, "Drop D",          "D1 A1 D2 G2 C3 F3 A3 D4");

        const char* b4 = "BASSE 4 CORDES";
        add (b4, "Standard",        "E1 A1 D2 G2");
        add (b4, "Drop D",          "D1 A1 D2 G2");
        add (b4, "Mib standard",    "D#1 G#1 C#2 F#2");
        add (b4, "Ré standard",     "D1 G1 C2 F2");
        add (b4, "Drop C",          "C1 G1 C2 F2");
        add (b4, "Do standard",     "C1 F1 A#1 D#2");
        add (b4, "Drop B",          "B0 F#1 B1 E2");
        add (b4, "Si standard",     "B0 E1 A1 D2");
        add (b4, "Drop A",          "A0 E1 A1 D2");
        add (b4, "La standard",     "A0 D1 G1 C2");
        add (b4, "Ténor (aigu)",    "A1 D2 G2 C3");

        const char* b5 = "BASSE 5 CORDES";
        add (b5, "Standard",        "B0 E1 A1 D2 G2");
        add (b5, "Aigu (E à C)",    "E1 A1 D2 G2 C3");
        add (b5, "Drop A",          "A0 E1 A1 D2 G2");
        add (b5, "La standard",     "A0 D1 G1 C2 F2");
        add (b5, "Drop G",          "G0 D1 G1 C2 F2");
        add (b5, "G# standard",     "G#0 C#1 F#1 B1 E2");
        add (b5, "Drop G#",         "G#0 D#1 G#1 C#2 F#2");
        add (b5, "Sol standard",    "G0 C1 F1 A#1 D#2");
        add (b5, "F# standard",     "F#0 B0 E1 A1 D2");
        add (b5, "F standard",      "F0 A#0 D#1 G#1 C#2");
        add (b5, "Drop F",          "F0 C1 F1 A#1 D#2");
        add (b5, "Mi standard",     "E0 A0 D1 G1 C2");
        add (b5, "Drop E",          "E0 B0 E1 A1 D2");

        const char* b6 = "BASSE 6 CORDES";
        add (b6, "Standard",        "B0 E1 A1 D2 G2 C3");
        add (b6, "Drop A",          "A0 E1 A1 D2 G2 C3");
        add (b6, "La standard",     "A0 D1 G1 C2 F2 A#2");
        add (b6, "F# standard",     "F#0 B0 E1 A1 D2 G2");
        add (b6, "F standard",      "F0 A#0 D#1 G#1 C#2 F#2");
        return v;
    }();
    return list;
}
// ---- affichage EN / FR (les clés internes restent en français) ----
inline std::string displayName (const Tuning& t, bool french)
{
    if (french || t.user) return t.name;
    static const std::map<std::string, std::string> en {
        { "Mib standard", "Eb standard" }, { "Ré standard", "D standard" }, { "Do# standard", "C# standard" },
        { "Do standard", "C standard" }, { "Si standard", "B standard" }, { "La# standard", "A# standard" },
        { "La standard", "A standard" }, { "Sol standard", "G standard" }, { "Fa standard", "F standard" },
        { "Mi standard", "E standard" }, { "Open D mineur", "Open D minor" }, { "Open G mineur", "Open G minor" },
        { "Open E mineur", "Open E minor" }, { "Open A mineur", "Open A minor" }, { "Open C mineur", "Open C minor" },
        { "Ténor (aigu)", "Tenor (high)" }, { "Aigu (E à C)", "High (E to C)" } };
    const auto it = en.find (t.name);
    return it != en.end() ? it->second : t.name;
}

inline std::string displayCategory (const std::string& cat, bool french)
{
    if (french) return cat;
    static const std::map<std::string, std::string> en {
        { "GUITARE 6 · STANDARD & BAS", "6-STRING GUITAR · STANDARD & LOW" },
        { "GUITARE 6 · OPEN TUNINGS", "6-STRING GUITAR · OPEN TUNINGS" },
        { "GUITARE 7 CORDES", "7-STRING GUITAR" }, { "GUITARE 8 CORDES", "8-STRING GUITAR" },
        { "BASSE 4 CORDES", "4-STRING BASS" }, { "BASSE 5 CORDES", "5-STRING BASS" },
        { "BASSE 6 CORDES", "6-STRING BASS" }, { "PERSO", "CUSTOM" } };
    const auto it = en.find (cat);
    return it != en.end() ? it->second : cat;
}
} // namespace tuna
