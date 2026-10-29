#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

//==============================================================================
TunaFonts::TunaFonts()
{
    monoTf = Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize);
    sansTf = Typeface::createSystemTypefaceFor (BinaryData::IBMPlexSansSemiBold_ttf, BinaryData::IBMPlexSansSemiBold_ttfSize);
}
Font TunaFonts::mono (float h) const { return Font (FontOptions (monoTf).withHeight (h)); }
Font TunaFonts::sans (float h) const { return Font (FontOptions (sansTf).withHeight (h)); }

static float textWidth (const Font& f, const String& s) { return GlyphArrangement::getStringWidth (f, s); }

static String sign1 (float v)
{
    const float r = std::round (v * 10.0f) / 10.0f;
    return (r > 0 ? "+" : (r < 0 ? "-" : "")) + String (std::abs (r), 1);
}

static Path starPath (Point<float> c, float r)
{
    Path p;
    p.addStar (c, 5, r * 0.45f, r, 0.0f);
    return p;
}

static Path pencilPath (Rectangle<float> b)
{
    Path p;
    const float w = b.getWidth();
    p.startNewSubPath (b.getX() + w * 0.15f, b.getBottom() - w * 0.15f);
    p.lineTo (b.getX() + w * 0.15f, b.getBottom() - w * 0.38f);
    p.lineTo (b.getRight() - w * 0.30f, b.getY() + w * 0.08f);
    p.lineTo (b.getRight() - w * 0.08f, b.getY() + w * 0.30f);
    p.lineTo (b.getX() + w * 0.38f, b.getBottom() - w * 0.15f);
    p.closeSubPath();
    return p;
}

static Path checkPath (Rectangle<float> b)
{
    Path p;
    p.startNewSubPath (b.getX(), b.getCentreY());
    p.lineTo (b.getX() + b.getWidth() * 0.38f, b.getBottom());
    p.lineTo (b.getRight(), b.getY());
    return p;
}

static String instrumentTag (const tuna::Tuning& t)
{
    const String cat = String::fromUTF8 (t.category.c_str());
    const int n = (int) t.notes.size();
    if (t.user) return "P" + String (n);
    if (cat.startsWith ("BASSE")) return "B" + String (n);
    return "G" + String (n);
}

static String upperName (const tuna::Tuning& t, bool fr) { return String::fromUTF8 (tuna::displayName (t, fr).c_str()).toUpperCase(); }
static String tr (bool fr, const char* en, const char* frUtf8) { return String::fromUTF8 (fr ? frUtf8 : en); }

//==============================================================================
// PedalKnob
bool PedalKnob::hitTest (int x, int y)
{
    return Point<float> ((float) x, (float) y).getDistanceFrom (getLocalBounds().toFloat().getCentre()) <= getWidth() * 0.5f;
}

void PedalKnob::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = b.getWidth() * 0.5f;
    const float sx = std::sin (angle), cy = -std::cos (angle);
    auto at = [&] (float r) { return Point<float> (c.x + sx * r, c.y + cy * r); };

    // trait gravé du centre jusqu'à l'index (ombre + reflet décalés : lumière fixe en haut à gauche)
    const float thin = jmax (1.0f, R * 0.022f);
    g.setColour (Colours::black.withAlpha (0.65f));
    g.drawLine (Line<float> (c, at (R * barR0)), thin);
    g.setColour (Colours::white.withAlpha (0.16f));
    g.drawLine (Line<float> (c.translated (-0.6f, -0.6f), at (R * barR0).translated (-0.6f, -0.6f)), thin * 0.6f);

    // index peint en blanc
    const float w = jmax (2.6f, R * 0.095f);
    Path bar;
    bar.addRoundedRectangle (-w * 0.5f, -R * barR1, w, R * (barR1 - barR0), w * 0.45f);
    const auto tr = AffineTransform::rotation (angle).translated (c);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillPath (bar, tr.translated (R * 0.012f + 0.5f, R * 0.018f + 0.6f));
    // l'index est plus lumineux quand il fait face à la lumière (haut-gauche)
    // liseré sombre : l'index reste lisible même sur le reflet clair du métal
    // liseré sombre large + halo : l'index reste lisible même sur le reflet clair du métal
    g.setColour (Colours::black.withAlpha (0.35f));
    g.strokePath (bar, PathStrokeType (jmax (2.4f, w * 1.1f)), tr);
    g.setColour (Colours::black.withAlpha (0.9f));
    g.strokePath (bar, PathStrokeType (jmax (1.4f, w * 0.5f)), tr);
    const float facing = 0.5f + 0.5f * std::cos (angle + MathConstants<float>::pi * 0.25f);
    g.setColour (Colours::white);
    g.fillPath (bar, tr);
    g.setColour (Colours::white.withAlpha (0.35f * facing));
    g.strokePath (bar, PathStrokeType (jmax (0.6f, w * 0.12f)), tr.translated (-0.3f, -0.3f));
}

void PedalKnob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (auto* ed = findParentComponentOfClass<TunaEditor>()) ed->showContextMenu(); return; }
    lastX = (float) e.position.x; lastY = (float) e.position.y; accum = 0; dragged = false;
}

void PedalKnob::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const float d = (lastY - e.position.y) + (e.position.x - lastX);
    lastX = e.position.x; lastY = e.position.y;
    if (std::abs ((float) e.getDistanceFromDragStart()) > 3) dragged = true;
    if (onDragDelta) onDragDelta (d);
    if (onDragSteps)
    {
        accum += d;
        while (accum >= stepPixels)  { accum -= stepPixels; onDragSteps (1); }
        while (accum <= -stepPixels) { accum += stepPixels; onDragSteps (-1); }
    }
}

void PedalKnob::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (! dragged && e.mouseWasClicked() && e.getNumberOfClicks() == 1 && onClick) onClick();
}

void PedalKnob::mouseDoubleClick (const MouseEvent&) { if (onDoubleClick) onDoubleClick(); }

void PedalKnob::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    wheelAccum += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    while (wheelAccum >= 0.12f)  { wheelAccum -= 0.12f; if (onWheel) onWheel (1); }
    while (wheelAccum <= -0.12f) { wheelAccum += 0.12f; if (onWheel) onWheel (-1); }
}

//==============================================================================
// TunaDisplay — tout est dessiné dans un repère de 828 x 594.5 unités (pixels du visuel)
static constexpr float kSW = 828.0f, kSH = 594.5f;

Rectangle<float> TunaDisplay::dropRect() const { return { 414.0f - 165.0f, 16.0f, 330.0f, 66.0f }; }

void TunaDisplay::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { ed.showContextMenu(); return; }
    const float s = getWidth() / kSW;
    const auto p = e.position / s;
    if (dropRect().expanded (6).contains (p)) { ed.openMenu(); return; }
    if (p.y < 90 && p.x < 220)
    {
        // clic sur le nom du mode : mode suivant
        if (auto* prm = ed.proc.apvts.getParameter ("mode"))
        {
            const int m = (ed.modeIndex() + 1) % 3;
            prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) m)); prm->endChangeGesture();
        }
    }
}

void TunaDisplay::mouseMove (const MouseEvent& e)
{
    const float s = getWidth() / kSW;
    const bool h = dropRect().expanded (6).contains (e.position / s);
    if (h != hoverDrop) { hoverDrop = h; repaint(); }
    setMouseCursor (h ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void TunaDisplay::paint (Graphics& g)
{
    const float s = getWidth() / kSW;
    g.addTransform (AffineTransform::scale (s));
    g.setColour (Colour (0xff050706));
    g.fillRoundedRectangle (0, 0, kSW, kSH, 6.0f);

    const auto& sn = ed.snap;
    const int mode = ed.modeIndex();
    const bool autoMono = mode == 1 && sn.monoOffTuning && ! sn.anyStringActive;
    paintHeader (g, sn, mode, autoMono);
    if (mode == 0 || autoMono) paintMono (g, sn);
    else if (mode == 1)        paintPoly (g, sn);
    else                       paintStrobe (g, sn);

    // message temporaire (réglage REF, accordage, MIDI learn...)
    if (Time::getMillisecondCounterHiRes() * 0.001 < flashUntil && flashText.isNotEmpty())
    {
        const auto f = ed.fonts.mono (40.0f);
        const float w = jmin (760.0f, textWidth (f, flashText) + 70.0f);
        Rectangle<float> r (414.0f - w * 0.5f, 250.0f, w, 92.0f);
        g.setColour (Colour (0xf0080a09));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (tunacol::amber.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 10.0f, 2.0f);
        g.setFont (f);
        g.setColour (tunacol::amber);
        g.drawFittedText (flashText, r.toNearestInt(), Justification::centred, 1, 0.6f);
    }

    // reflet de la vitre
    ColourGradient glass (Colours::white.withAlpha (0.075f), 0, 0, Colours::white.withAlpha (0.0f), kSW * 0.42f, kSH * 0.55f, false);
    g.setGradientFill (glass);
    Path p;
    p.startNewSubPath (0, 0); p.lineTo (kSW * 0.62f, 0); p.lineTo (0, kSH * 0.78f); p.closeSubPath();
    g.fillPath (p);
    g.setColour (Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (1, 1, kSW - 2, kSH - 2, 6.0f, 1.5f);
}

void TunaDisplay::paintHeader (Graphics& g, const tuna::Snapshot& sn, int mode, bool autoMono)
{
    static const char* names[3] = { "MONO", "POLY", "STROBE" };
    g.setFont (ed.fonts.mono (42.0f));
    g.setColour (tunacol::orange);
    g.drawText (autoMono ? String ("POLY>MONO") : String (names[jlimit (0, 2, mode)]), Rectangle<float> (28, 18, 230, 62), Justification::centredLeft);
    const int ref = (int) ed.proc.apvts.getRawParameterValue ("ref")->load();
    g.drawText ("A=" + String (ref), Rectangle<float> (kSW - 28 - 200, 18, 200, 62), Justification::centredRight);

    // bouton du menu déroulant
    const auto& t = ed.proc.library.get (ed.proc.getTuningIndex());
    const bool green = mode == 1 && ! autoMono && sn.allInTune;
    const auto col = green ? tunacol::green : tunacol::amber;
    const auto r = dropRect();
    g.setColour (col.withAlpha (hoverDrop ? 0.16f : 0.07f));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (col.withAlpha (green ? 0.85f : 0.55f));
    g.drawRoundedRectangle (r.reduced (1), 7.0f, 2.0f);
    // étiquette instrument (G6, G7, B5...)
    const String tag = instrumentTag (t);
    g.setFont (ed.fonts.mono (20.0f));
    g.setColour (col.withAlpha (0.6f));
    g.drawText (tag, Rectangle<float> (r.getX() + 12, r.getY(), 40, r.getHeight()), Justification::centredLeft);
    // nom
    const String nm = upperName (t, ed.proc.library.french);
    float fh = 38.0f;
    while (fh > 18.0f && textWidth (ed.fonts.mono (fh), nm) > r.getWidth() - 100) fh -= 1.0f;
    g.setFont (ed.fonts.mono (fh));
    g.setColour (col);
    g.drawText (nm, Rectangle<float> (r.getX() + 48, r.getY(), r.getWidth() - 86, r.getHeight()), Justification::centred, true);
    if (green)
    {
        g.setColour (col.withAlpha (0.18f));
        g.drawText (nm, Rectangle<float> (r.getX() + 48, r.getY() + 1, r.getWidth() - 86, r.getHeight()), Justification::centred, true);
    }
    Path tri;
    const float tx = r.getRight() - 30, ty = r.getCentreY();
    tri.addTriangle (tx - 9, ty - 5, tx + 9, ty - 5, tx, ty + 7);
    g.fillPath (tri);

    g.setColour (tunacol::amber.withAlpha (0.25f));
    g.fillRect (28.0f, 96.0f, kSW - 56.0f, 2.0f);
}

static void drawSegment (Graphics& g, Rectangle<float> r, Colour c, bool lit)
{
    if (lit)
    {
        g.setColour (c.withAlpha (0.10f)); g.fillRoundedRectangle (r.expanded (7, 6), 6.0f);
        g.setColour (c.withAlpha (0.22f)); g.fillRoundedRectangle (r.expanded (3.5f, 3.0f), 4.0f);
        g.setColour (c); g.fillRoundedRectangle (r, 2.5f);
        g.setColour (Colours::white.withAlpha (0.35f)); g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.3f, r.getHeight() * 0.2f), 1.5f);
    }
    else { g.setColour (c); g.fillRoundedRectangle (r, 2.5f); }
}

void TunaDisplay::paintPoly (Graphics& g, const tuna::Snapshot& sn)
{
    const int n = jmax (1, sn.numStrings);
    const float top = 112.0f, area = 420.0f;
    const float rowH = jmin (68.0f, area / n);
    const float y0 = top + (area - rowH * n) * 0.5f;
    const float x0 = 166.0f, x1 = 800.0f;
    const int segs = 25, mid = 12;
    const float pitch = (x1 - x0) / segs, segW = pitch * 0.62f;

    // repère central
    g.setColour (tunacol::green.withAlpha (0.13f));
    for (float y = y0; y < y0 + rowH * n; y += 9.0f) g.fillRect (x0 + pitch * (mid + 0.5f) - 0.75f, y, 1.5f, 4.0f);

    for (int row = 0; row < n; ++row)
    {
        const int idx = n - 1 - row;   // corde la plus aiguë en haut
        const auto& st = sn.strings[idx];
        const float cy = y0 + rowH * (row + 0.5f);
        float alpha = 0.32f;
        if (st.measured) alpha = st.active ? 1.0f : jmap (jlimit (0.4f, 6.0f, st.age), 0.4f, 6.0f, 1.0f, 0.45f);
        const bool inTune = st.measured && std::abs (st.cents) <= (float) tuna::TunerEngine::kPolyInTune;
        const auto nameCol = (inTune ? tunacol::green : tunacol::amber).withAlpha (alpha);

        g.setFont (ed.fonts.mono (jmin (46.0f, rowH * 0.72f)));
        g.setColour (nameCol);
        g.drawText (String (tuna::noteName (st.midi)), Rectangle<float> (28, cy - rowH * 0.5f, 132, rowH), Justification::centredLeft);

        const int lit = st.measured ? jlimit (0, segs - 1, mid + (int) std::lround (st.cents / 4.0f)) : -1;
        const float h = rowH * 0.50f;
        for (int k = 0; k < segs; ++k)
        {
            const float hh = (k == mid ? rowH * 0.66f : h);
            Rectangle<float> r (x0 + pitch * k + (pitch - segW) * 0.5f, cy - hh * 0.5f, segW, hh);
            if (k == lit) drawSegment (g, r, (inTune ? tunacol::green : tunacol::amber).withAlpha (alpha), true);
            else if (lit >= 0 && ((k > mid && k < lit) || (k < mid && k > lit)))
                drawSegment (g, r, tunacol::amber.withAlpha (0.30f * alpha), false);
            else drawSegment (g, r, k == mid ? tunacol::greenDim : tunacol::amberDim, false);
        }
    }

    g.setFont (ed.fonts.mono (27.0f));
    g.setColour (tunacol::grey.withAlpha (0.75f));
    g.drawText ("-50", Rectangle<float> (x0 - 4, 544, 100, 36), Justification::centredLeft);
    g.drawText ("0", Rectangle<float> (x0 + pitch * (mid + 0.5f) - 50, 544, 100, 36), Justification::centred);
    g.drawText ("+50", Rectangle<float> (x1 - 96, 544, 100, 36), Justification::centredRight);
}

void TunaDisplay::paintMono (Graphics& g, const tuna::Snapshot& sn)
{
    const bool has = sn.monoMidi >= 0 && (sn.monoActive || sn.monoAge < 2.0f);
    const float alpha = ! has ? 0.3f : (sn.monoActive ? 1.0f : jmap (jlimit (0.25f, 2.0f, sn.monoAge), 0.25f, 2.0f, 1.0f, 0.4f));
    const float c = has ? sn.monoCents : 0.0f;
    const bool inTune = has && sn.monoPrecise && std::abs (c) <= (float) tuna::TunerEngine::kMonoInTune;
    const auto col = (inTune ? tunacol::green : tunacol::amber).withAlpha (alpha);

    // grande note
    const String pc = has ? String (tuna::pitchClassName (sn.monoMidi)) : String ("--");
    const String letter = pc.substring (0, 1), acc = pc.substring (1);
    const String oct = has ? String (tuna::octaveOf (sn.monoMidi)) : String();
    const auto big = ed.fonts.mono (230.0f), small = ed.fonts.mono (82.0f);
    const float wL = textWidth (big, has ? letter : pc), wA = textWidth (small, acc), wO = textWidth (small, oct);
    const float total = wL + jmax (wA, wO) + 6.0f;
    const float x = 414.0f - total * 0.5f;
    g.setColour (col.withAlpha (alpha * 0.18f));
    g.setFont (big);
    g.drawText (has ? letter : pc, Rectangle<float> (x - 3, 98, wL + 6, 250), Justification::centredLeft);
    g.setColour (col);
    g.drawText (has ? letter : pc, Rectangle<float> (x, 100, wL + 4, 250), Justification::centredLeft);
    g.setFont (small);
    if (acc.isNotEmpty()) g.drawText (acc, Rectangle<float> (x + wL + 6, 128, wA + 6, 90), Justification::centredLeft);
    g.drawText (oct, Rectangle<float> (x + wL + 6, 240, wO + 6, 90), Justification::centredLeft);

    // barre +/- 50 cents : graduations fixes, repère central blanc (vert quand c'est juste)
    // et curseur jaune de même taille qui glisse avec la note jouée
    const int segs = 41, mid = 20;
    const float x0 = 64.0f, x1 = kSW - 64.0f, pitch = (x1 - x0) / segs, segW = pitch * 0.6f;
    const float cy = 392.0f;
    for (int k = 0; k < segs; ++k)
    {
        if (k == mid) continue;
        const float hh = (k - mid) % 4 == 0 ? 52.0f : 36.0f;
        drawSegment (g, Rectangle<float> (x0 + pitch * k + (pitch - segW) * 0.5f, cy - hh * 0.5f, segW, hh), tunacol::amberDim, false);
    }
    const float markW = segW * 1.1f, markH = 118.0f;      // repère central
    const float curW = segW * 0.8f, curH = 88.0f;         // curseur jaune, plus petit que le repère
    const float centreX = x0 + pitch * (mid + 0.5f);
    const Rectangle<float> centre (centreX - markW * 0.5f, cy - markH * 0.5f, markW, markH);
    if (inTune) drawSegment (g, centre, tunacol::green.withAlpha (alpha), true);
    else
    {
        g.setColour (Colours::white.withAlpha (0.10f)); g.fillRoundedRectangle (centre.expanded (4, 3), 4.0f);
        g.setColour (Colours::white.withAlpha (has ? 0.95f : 0.55f)); g.fillRoundedRectangle (centre, 2.5f);
        if (has)
        {
            const float cc = jlimit (-50.0f, 50.0f, c);
            const float cx = centreX + cc / 2.5f * pitch;
            drawSegment (g, Rectangle<float> (cx - curW * 0.5f, cy - curH * 0.5f, curW, curH), tunacol::amber.withAlpha (alpha), true);
        }
    }
    g.setFont (ed.fonts.mono (26.0f));
    g.setColour (tunacol::grey.withAlpha (0.7f));
    g.drawText ("-50", Rectangle<float> (x0 - 4, 448, 100, 32), Justification::centredLeft);
    g.drawText ("+50", Rectangle<float> (x1 - 96, 448, 100, 32), Justification::centredRight);

    // valeurs
    g.setFont (ed.fonts.mono (56.0f));
    g.setColour (col);
    const String cents = has ? ((sn.monoPrecise ? String() : String ("~")) + sign1 (c) + " c") : String ("-- c");
    g.drawText (cents, Rectangle<float> (234, 466, 360, 80), Justification::centred);
    g.setFont (ed.fonts.mono (28.0f));
    g.setColour (tunacol::grey.withAlpha (0.75f * jmax (0.4f, alpha)));
    if (has) g.drawText (String (sn.monoHz, sn.monoHz < 100 ? 2 : 1) + " Hz", Rectangle<float> (28, 476, 220, 66), Justification::centredLeft);
    // corde correspondante dans l'accordage choisi (corde 1 = la plus aiguë)
    if (has)
        for (int i = 0; i < sn.numStrings; ++i)
            if (sn.strings[i].midi == sn.monoMidi)
            {
                g.drawText (tr (ed.proc.library.french, "STRING ", "CORDE ") + String (sn.numStrings - i), Rectangle<float> (kSW - 248, 476, 220, 66), Justification::centredRight);
                break;
            }
}

void TunaDisplay::paintStrobe (Graphics& g, const tuna::Snapshot& sn)
{
    const bool has = sn.monoMidi >= 0 && (sn.monoActive || sn.monoAge < 2.0f);
    const float alpha = ! has ? 0.3f : (sn.monoActive ? 1.0f : 0.45f);
    const float c = has ? sn.monoCents : 0.0f;
    const bool inTune = has && sn.monoPrecise && std::abs (c) <= (float) tuna::TunerEngine::kMonoInTune;
    const auto col = (inTune ? tunacol::green : tunacol::amber).withAlpha (alpha);

    // note + écart
    g.setFont (ed.fonts.mono (120.0f));
    g.setColour (col);
    const String nm = has ? String (tuna::noteName (sn.monoMidi)) : String ("--");
    g.drawText (nm, Rectangle<float> (40, 104, 420, 150), Justification::centredLeft);
    g.setFont (ed.fonts.mono (58.0f));
    g.drawText (has ? sign1 (c) + " c" : String ("-- c"), Rectangle<float> (kSW - 400, 120, 372, 96), Justification::centredRight);
    g.setFont (ed.fonts.mono (28.0f));
    g.setColour (tunacol::grey.withAlpha (0.75f * jmax (0.4f, alpha)));
    if (has) g.drawText (String (sn.monoHz, sn.monoHz < 100 ? 3 : 2) + " Hz", Rectangle<float> (kSW - 400, 206, 372, 50), Justification::centredRight);

    // bandes stroboscopiques : chaque bande défile à une vitesse proportionnelle à l'écart
    const Rectangle<float> area (44, 284, kSW - 88, 230);
    g.setColour (Colour (0xff0c0f0d));
    g.fillRoundedRectangle (area, 6.0f);
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (area.toNearestInt());
    const float bandH[3] = { 86.0f, 66.0f, 50.0f };
    const float period[3] = { 96.0f, 48.0f, 24.0f };
    float y = area.getY() + 10.0f;
    for (int b = 0; b < 3; ++b)
    {
        const float P = period[b];
        float off = std::fmod (strobePhase * period[0] * (float) (1 << b), P);
        if (off < 0) off += P;
        for (float x = area.getX() - P + off; x < area.getRight(); x += P)
        {
            g.setColour (col.withAlpha (alpha * (0.78f - 0.14f * b)));
            g.fillRect (Rectangle<float> (x, y, P * 0.5f, bandH[b]));
        }
        y += bandH[b] + 9.0f;
    }
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRect (area.getCentreX() - 2.0f, area.getY(), 4.0f, area.getHeight());
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillRect (area.getCentreX() - 0.75f, area.getY(), 1.5f, area.getHeight());

    g.setFont (ed.fonts.mono (26.0f));
    g.setColour (tunacol::grey.withAlpha (0.7f));
    const bool fr = ed.proc.library.french;
    g.drawText (tr (fr, "< FLAT", "< GRAVE"), Rectangle<float> (44, 530, 200, 40), Justification::centredLeft);
    g.drawText (tr (fr, "SHARP >", "AIGU >"), Rectangle<float> (kSW - 244, 530, 200, 40), Justification::centredRight);
}

//==============================================================================
// ScreenButton
void ScreenButton::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.5f);
    const bool over = isMouseOver();
    g.setColour (tunacol::amber.withAlpha (primary ? (over ? 1.0f : 0.88f) : (over ? 0.16f : 0.06f)));
    g.fillRoundedRectangle (b, b.getHeight() * 0.18f);
    g.setColour (tunacol::amber.withAlpha (0.65f));
    g.drawRoundedRectangle (b, b.getHeight() * 0.18f, 1.5f);
    g.setColour (primary ? Colour (0xff14100a) : tunacol::amber);
    g.setFont (ed.fonts.mono (b.getHeight() * 0.42f));
    g.drawText (text, b, Justification::centred);
}

//==============================================================================
// TuningMenu
float TuningMenu::u() const { return getWidth() / 808.0f; }

float TuningMenu::contentHeight() const
{
    float h = 0;
    for (auto& r : rows) h += rowH (r);
    return h;
}
float TuningMenu::listHeight() const { return getHeight() / u() - listTop() - 12.0f; }

void TuningMenu::clampScroll() { scroll = jlimit (0.0f, jmax (0.0f, contentHeight() - listHeight()), scroll); }

Rectangle<float> TuningMenu::tabRect (int t) const
{
    const float w[3] = { 92.0f, 168.0f, 140.0f };
    float x = 808.0f - 18.0f;
    for (int i = 2; i >= t; --i) x -= w[i] + (i < 2 ? 6.0f : 0.0f);
    return { x, 10.0f, w[t], 42.0f };
}

Rectangle<float> TuningMenu::langRect() const { return { 236.0f, 13.0f, 56.0f, 36.0f }; }

void TuningMenu::rebuild (bool scrollToSelection)
{
    rows.clear();
    auto& lib = ed.proc.library;
    std::string lastCat;
    auto addItem = [&] (int i)
    {
        const auto& t = lib.get (i);
        if (t.category != lastCat) { rows.push_back ({ 0, String::fromUTF8 (tuna::displayCategory (t.category, lib.french).c_str()), -1 }); lastCat = t.category; }
        rows.push_back ({ 1, String::fromUTF8 (tuna::displayName (t, lib.french).c_str()), i });
    };
    if (tab == 0) for (int i = 0; i < lib.size(); ++i) addItem (i);
    else if (tab == 1)
    {
        const auto fav = lib.favouriteIndices();
        if (fav.empty()) rows.push_back ({ 0, tr (lib.french, "NO FAVORITES YET: CLICK THE STAR OF A TUNING", "AUCUN FAVORI : CLIQUEZ SUR L'ÉTOILE D'UN ACCORDAGE"), -1 });
        for (int i : fav) addItem (i);
    }
    else
    {
        rows.push_back ({ 2, tr (lib.french, "+  NEW TUNING (COPY OF THE CURRENT ONE)", "+  NOUVEL ACCORDAGE (COPIE DE L'ACTUEL)"), -1 });
        for (int i = 0; i < lib.size(); ++i) if (lib.isUser (i)) addItem (i);
    }
    if (scrollToSelection)
    {
        float y = 0;
        for (auto& r : rows)
        {
            if (r.kind == 1 && r.index == ed.proc.getTuningIndex()) { scroll = y - listHeight() * 0.4f; break; }
            y += rowH (r);
        }
    }
    clampScroll();
    repaint();
}

int TuningMenu::rowAt (float yUnits) const
{
    float y = listTop() - scroll;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const float h = rowH (rows[(size_t) i]);
        if (yUnits >= y && yUnits < y + h) return i;
        y += h;
    }
    return -1;
}

void TuningMenu::paint (Graphics& g)
{
    const float k = u();
    g.addTransform (AffineTransform::scale (k));
    const float W = 808.0f, H = getHeight() / k;
    auto& lib = ed.proc.library;

    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (4, 8, W - 4, H - 8, 12.0f);
    g.setColour (tunacol::panel);
    g.fillRoundedRectangle (0, 0, W - 6, H - 6, 12.0f);
    const float PW = W - 6;
    g.setColour (tunacol::amber.withAlpha (0.08f));
    g.fillRoundedRectangle (1, 1, PW - 2, 58, 11.0f);
    g.setColour (tunacol::amber.withAlpha (0.3f));
    g.fillRect (0.0f, 60.0f, PW, 1.5f);
    g.setColour (tunacol::amber.withAlpha (0.55f));
    g.drawRoundedRectangle (0.75f, 0.75f, PW - 1.5f, H - 7.5f, 12.0f, 1.5f);

    g.setFont (ed.fonts.mono (27.0f).withExtraKerningFactor (0.1f));
    g.setColour (tunacol::amber);
    g.drawText (tr (lib.french, "TUNING", "ACCORDAGE"), Rectangle<float> (22, 6, 250, 50), Justification::centredLeft);
    // bouton de langue : affiche la langue vers laquelle on bascule
    {
        const auto lr = langRect();
        g.setColour (tunacol::amber.withAlpha (hoverLang ? 0.22f : 0.08f));
        g.fillRoundedRectangle (lr, 5.0f);
        g.setColour (tunacol::amber.withAlpha (0.7f));
        g.drawRoundedRectangle (lr, 5.0f, 1.5f);
        g.setFont (ed.fonts.mono (18.0f));
        g.setColour (tunacol::amber);
        g.drawText (lib.french ? "EN" : "FR", lr, Justification::centred);
    }

    const String tabNames[3] = { tr (lib.french, "ALL", "TOUS"), tr (lib.french, "FAVORITES", "FAVORIS"), tr (lib.french, "CUSTOM", "PERSO") };
    for (int t = 0; t < 3; ++t)
    {
        auto r = tabRect (t).translated (-6.0f, 0.0f);
        const bool on = tab == t;
        if (on) { g.setColour (tunacol::amber.withAlpha (0.15f)); g.fillRoundedRectangle (r, 6.0f); }
        const auto c = tunacol::amber.withAlpha (on ? 1.0f : 0.5f);
        g.setColour (c);
        float tx = r.getX() + 14;
        if (t == 1) { g.fillPath (starPath ({ tx + 9, r.getCentreY() }, 10.0f)); tx += 26; }
        if (t == 2) { g.fillPath (pencilPath ({ tx, r.getCentreY() - 10, 20, 20 })); tx += 28; }
        g.setFont (ed.fonts.mono (20.0f));
        g.drawText (tabNames[t], Rectangle<float> (tx, r.getY(), r.getRight() - tx - 8, r.getHeight()), Justification::centredLeft);
        if (on) g.fillRect (r.getX() + 10, r.getBottom() - 3, r.getWidth() - 20, 2.0f);
    }

    // liste
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (Rectangle<float> (2, listTop(), PW - 4, listHeight() + 6).toNearestInt());
    float y = listTop() - scroll;
    const int sel = ed.proc.getTuningIndex();
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& r = rows[(size_t) i];
        const float h = rowH (r);
        if (y + h < listTop() || y > listTop() + listHeight() + 6) { y += h; continue; }
        if (r.kind == 0)
        {
            g.setFont (ed.fonts.mono (23.0f).withExtraKerningFactor (0.05f));
            g.setColour (Colour (0xff8a96a3));
            g.drawText (r.text, Rectangle<float> (22, y + 8, PW - 60, 32), Justification::centredLeft, true);
            g.setColour (Colour (0xff8a96a3).withAlpha (0.25f));
            g.fillRect (22.0f, y + h - 6, PW - 70, 1.0f);
        }
        else if (r.kind == 2)
        {
            Rectangle<float> rr (12, y + 4, PW - 50, h - 8);
            g.setColour (tunacol::amber.withAlpha (hoverRow == i ? 0.18f : 0.08f));
            g.fillRoundedRectangle (rr, 5.0f);
            g.setColour (tunacol::amber);
            g.setFont (ed.fonts.mono (21.0f));
            g.drawText (r.text, rr.reduced (14, 0), Justification::centredLeft);
        }
        else
        {
            const auto& t = lib.get (r.index);
            const bool isSel = r.index == sel;
            Rectangle<float> rr (10, y + 2, PW - 46, h - 4);
            if (isSel) { g.setColour (tunacol::amber); g.fillRoundedRectangle (rr, 5.0f); }
            else if (hoverRow == i) { g.setColour (tunacol::amber.withAlpha (0.12f)); g.fillRoundedRectangle (rr, 5.0f); }
            const auto txt = isSel ? Colour (0xff14100a) : tunacol::amber;
            g.setColour (txt);
            if (isSel) g.strokePath (checkPath ({ 22, y + h * 0.5f - 6, 14, 12 }), PathStrokeType (2.5f));
            g.setFont (ed.fonts.sans (29.0f));
            g.drawText (String::fromUTF8 (tuna::displayName (t, lib.french).c_str()), Rectangle<float> (46, y, 290, h), Justification::centredLeft, true);
            const float notesRight = PW - 46 - 50 - (t.user ? 46 : 0);
            const String nts (tuna::notesToString (t.notes));
            const float notesW = notesRight - 300.0f;
            float nf = 24.0f;
            while (nf > 17.0f && textWidth (ed.fonts.mono (nf), nts) > notesW) nf -= 0.5f;
            g.setFont (ed.fonts.mono (nf));
            g.setColour (txt.withAlpha (isSel ? 1.0f : 0.7f));
            g.drawText (nts, Rectangle<float> (300, y, notesW, h), Justification::centredRight, false);
            // crayon (perso) + étoile (favori)
            if (t.user)
            {
                g.setColour (txt.withAlpha (0.8f));
                g.fillPath (pencilPath ({ PW - 46 - 78, y + h * 0.5f - 10, 20, 20 }));
            }
            const auto star = starPath ({ PW - 46 - 26, y + h * 0.5f }, 11.0f);
            if (lib.isFavourite (r.index)) { g.setColour (isSel ? txt : tunacol::amber); g.fillPath (star); }
            else { g.setColour (txt.withAlpha (0.45f)); g.strokePath (star, PathStrokeType (1.5f)); }
        }
        y += h;
    }

    // barre de défilement
    const float ch = contentHeight(), lh = listHeight();
    if (ch > lh)
    {
        const float trackX = PW - 24, trackY = listTop() + 4, trackH = lh - 8;
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (trackX, trackY, 7, trackH, 3.5f);
        const float th = jmax (40.0f, trackH * lh / ch);
        const float ty = trackY + (trackH - th) * (scroll / (ch - lh));
        g.setColour (tunacol::amber.withAlpha (0.7f));
        g.fillRoundedRectangle (trackX, ty, 7, th, 3.5f);
    }
}

void TuningMenu::mouseMove (const MouseEvent& e)
{
    const auto p = e.position / u();
    const int r = p.y >= listTop() ? rowAt (p.y) : -1;
    const bool hl = langRect().contains (p);
    if (r != hoverRow || hl != hoverLang) { hoverRow = r; hoverLang = hl; repaint(); }
}

void TuningMenu::mouseDown (const MouseEvent& e)
{
    const auto p = e.position / u();
    const float PW = 808.0f - 6.0f;
    draggingBar = false;
    if (langRect().contains (p))
    {
        ed.proc.library.setFrench (! ed.proc.library.french);
        rebuild (false);
        ed.repaint();
        return;
    }
    for (int t = 0; t < 3; ++t)
        if (tabRect (t).translated (-6.0f, 0.0f).contains (p)) { tab = t; scroll = 0; rebuild (t == 0); return; }
    if (p.y < listTop()) return;
    if (p.x > PW - 34 && contentHeight() > listHeight())
    {
        draggingBar = true; dragStartY = p.y; dragStartScroll = scroll;
        return;
    }
    const int ri = rowAt (p.y);
    if (ri < 0) return;
    const auto r = rows[(size_t) ri];
    auto& lib = ed.proc.library;
    if (r.kind == 2) { ed.openTuningEditor (-1); return; }
    if (r.kind != 1) return;
    if (p.x > PW - 46 - 50 && p.x < PW - 46)
    {
        lib.toggleFavourite (r.index);
        if (tab == 1) rebuild (false); else repaint();
        return;
    }
    if (lib.isUser (r.index) && p.x > PW - 46 - 88 && p.x <= PW - 46 - 50) { ed.openTuningEditor (r.index); return; }
    ed.proc.selectTuning (r.index);
    ed.closeMenu();
    ed.flash (upperName (lib.get (r.index), lib.french));
}

void TuningMenu::mouseDrag (const MouseEvent& e)
{
    if (! draggingBar) return;
    const float dy = e.position.y / u() - dragStartY;
    const float ch = contentHeight(), lh = listHeight();
    const float trackH = lh - 8, th = jmax (40.0f, trackH * lh / ch);
    scroll = dragStartScroll + dy * (ch - lh) / jmax (1.0f, trackH - th);
    clampScroll(); repaint();
}

void TuningMenu::mouseUp (const MouseEvent&) { draggingBar = false; }

void TuningMenu::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    scroll -= w.deltaY * (w.isReversed ? -1.0f : 1.0f) * 260.0f;
    clampScroll(); repaint();
}

bool TuningMenu::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { ed.closeMenu(); return true; }
    if (k == KeyPress::upKey || k == KeyPress::downKey)
    {
        ed.proc.stepTuning (k == KeyPress::upKey ? -1 : 1);
        rebuild (true);
        return true;
    }
    if (k == KeyPress::returnKey) { ed.closeMenu(); return true; }
    return false;
}

//==============================================================================
// TuningEditorPanel
TuningEditorPanel::TuningEditorPanel (TunaEditor& e)
    : ed (e), save (e, "SAVE"), del (e, "DELETE"), cancel (e, "CANCEL")
{
    addAndMakeVisible (name);
    addAndMakeVisible (save); addAndMakeVisible (del); addAndMakeVisible (cancel);
    save.primary = true;
    name.setColour (TextEditor::backgroundColourId, Colour (0xff050605));
    name.setColour (TextEditor::textColourId, tunacol::amber);
    name.setColour (TextEditor::outlineColourId, tunacol::amber.withAlpha (0.45f));
    name.setColour (TextEditor::focusedOutlineColourId, tunacol::amber);
    name.setColour (TextEditor::highlightColourId, tunacol::amber.withAlpha (0.3f));
    name.setColour (TextEditor::highlightedTextColourId, tunacol::amber);
    name.setColour (CaretComponent::caretColourId, tunacol::amber);
    name.setInputRestrictions (32);

    cancel.onClick = [this] { ed.closeTuningEditor(); };
    del.onClick = [this]
    {
        if (editing < 0) return;
        const bool wasCurrent = ed.proc.getTuningIndex() == editing;
        ed.proc.library.removeUser (editing);
        if (wasCurrent) ed.proc.selectTuning (0); else ed.proc.refreshEngineTuning();
        ed.closeTuningEditor();
    };
    save.onClick = [this]
    {
        auto& lib = ed.proc.library;
        int idx = editing;
        if (idx >= 0) lib.updateUser (idx, name.getText(), notes);
        else idx = lib.addUser (name.getText(), notes);
        ed.proc.selectTuning (idx);
        ed.closeTuningEditor();
        ed.flash (upperName (lib.get (idx), lib.french));
    };
}

float TuningEditorPanel::u() const { return getWidth() / 808.0f; }

void TuningEditorPanel::open (int libraryIndex)
{
    auto& lib = ed.proc.library;
    editing = (libraryIndex >= 0 && lib.isUser (libraryIndex)) ? libraryIndex : -1;
    const auto& src = editing >= 0 ? lib.get (editing) : lib.get (ed.proc.getTuningIndex());
    notes = src.notes;
    const bool fr = lib.french;
    name.setText (editing >= 0 ? String::fromUTF8 (src.name.c_str())
                               : String::fromUTF8 (tuna::displayName (src, fr).c_str()) + (fr ? " perso" : " custom"), false);
    save.text = tr (fr, "SAVE", "ENREGISTRER");
    del.text = tr (fr, "DELETE", "SUPPRIMER");
    cancel.text = tr (fr, "CANCEL", "ANNULER");
    del.setVisible (editing >= 0);
    resized();
    repaint();
}

void TuningEditorPanel::resized()
{
    const float k = u();
    auto R = [k] (float x, float y, float w, float h) { return Rectangle<float> (x * k, y * k, w * k, h * k).toNearestInt(); };
    name.setBounds (R (150, 76, 620, 50));
    name.setFont (ed.fonts.sans (26.0f * k));
    name.setIndents ((int) (10 * k), (int) (9 * k));
    const float by = getHeight() / k - 96.0f;
    cancel.setBounds (R (24, by, 236, 64));
    del.setBounds (R (284, by, 236, 64));
    save.setBounds (R (544, by, 236, 64));
}

Rectangle<float> TuningEditorPanel::rowRect (int i) const
{
    const float H = getHeight() / u();
    const int n = jmax (1, (int) notes.size());
    const float step = jmin (58.0f, (H - 110.0f - 214.0f) / (float) n);
    return { 24.0f, 214.0f + i * step, 760.0f, step - 6.0f };
}

void TuningEditorPanel::paint (Graphics& g)
{
    const float k = u();
    g.addTransform (AffineTransform::scale (k));
    const float W = 808.0f, H = getHeight() / k;
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (4, 8, W - 4, H - 8, 12.0f);
    const float PW = W - 6;
    g.setColour (tunacol::panel);
    g.fillRoundedRectangle (0, 0, PW, H - 6, 12.0f);
    g.setColour (tunacol::amber.withAlpha (0.55f));
    g.drawRoundedRectangle (0.75f, 0.75f, PW - 1.5f, H - 7.5f, 12.0f, 1.5f);

    g.setFont (ed.fonts.mono (24.0f).withExtraKerningFactor (0.1f));
    g.setColour (tunacol::amber);
    const bool fr = ed.proc.library.french;
    g.drawText (editing >= 0 ? tr (fr, "EDIT TUNING", "MODIFIER L'ACCORDAGE") : tr (fr, "NEW TUNING", "NOUVEL ACCORDAGE"), Rectangle<float> (24, 10, 600, 50), Justification::centredLeft);
    g.setColour (tunacol::amber.withAlpha (0.3f));
    g.fillRect (0.0f, 60.0f, PW, 1.5f);

    g.setFont (ed.fonts.mono (22.0f));
    g.setColour (Colour (0xff8a96a3));
    g.drawText (tr (fr, "NAME", "NOM"), Rectangle<float> (24, 76, 120, 50), Justification::centredLeft);
    g.drawText (tr (fr, "STRINGS", "CORDES"), Rectangle<float> (24, 146, 120, 50), Justification::centredLeft);

    auto btn = [&] (Rectangle<float> r, const String& t)
    {
        g.setColour (tunacol::amber.withAlpha (0.08f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (tunacol::amber.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 6.0f, 1.5f);
        g.setColour (tunacol::amber);
        g.setFont (ed.fonts.mono (r.getHeight() * 0.55f));
        g.drawText (t, r, Justification::centred);
    };
    btn ({ 150, 148, 52, 46 }, "-");
    g.setFont (ed.fonts.mono (30.0f));
    g.setColour (tunacol::amber);
    g.drawText (String ((int) notes.size()), Rectangle<float> (206, 146, 70, 50), Justification::centred);
    btn ({ 280, 148, 52, 46 }, "+");
    g.setFont (ed.fonts.mono (18.0f));
    g.setColour (Colour (0xff8a96a3));
    g.drawText (tr (fr, "« » = OCTAVE   - + = SEMITONE", "« » = OCTAVE   - + = DEMI-TON"), Rectangle<float> (360, 146, 420, 50), Justification::centredRight);

    const int n = (int) notes.size();
    const int ref = (int) ed.proc.apvts.getRawParameterValue ("ref")->load();
    for (int i = 0; i < n; ++i)
    {
        const int idx = n - 1 - i;
        const auto r = rowRect (i);
        g.setColour (tunacol::amber.withAlpha (i % 2 ? 0.03f : 0.06f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setFont (ed.fonts.mono (21.0f));
        g.setColour (Colour (0xff8a96a3));
        g.drawText (tr (fr, "STRING ", "CORDE ") + String (i + 1), Rectangle<float> (r.getX() + 14, r.getY(), 150, r.getHeight()), Justification::centredLeft);
        const float bh = jmin (44.0f, r.getHeight() - 4.0f), by = r.getCentreY() - bh * 0.5f;
        btn ({ 196, by, 56, bh }, String::fromUTF8 ("«"));
        btn ({ 260, by, 56, bh }, "-");
        g.setFont (ed.fonts.mono (jmin (32.0f, r.getHeight() * 0.7f)));
        g.setColour (tunacol::amber);
        g.drawText (String (tuna::noteName (notes[(size_t) idx])), Rectangle<float> (322, r.getY(), 140, r.getHeight()), Justification::centred);
        btn ({ 468, by, 56, bh }, "+");
        btn ({ 532, by, 56, bh }, String::fromUTF8 ("»"));
        g.setFont (ed.fonts.mono (20.0f));
        g.setColour (tunacol::grey.withAlpha (0.65f));
        g.drawText (String (tuna::midiToHz (notes[(size_t) idx], ref), 2) + " Hz", Rectangle<float> (596, r.getY(), 176, r.getHeight()), Justification::centredRight);
    }
}

void TuningEditorPanel::mouseDown (const MouseEvent& e)
{
    const auto p = e.position / u();
    auto hit = [&p] (Rectangle<float> r) { return r.contains (p); };
    if (hit ({ 150, 148, 52, 46 }) && notes.size() > 1) notes.pop_back();
    else if (hit ({ 280, 148, 52, 46 }) && notes.size() < (size_t) tuna::kMaxStrings)
        notes.push_back (jmin (88, (notes.empty() ? 40 : notes.back() + 5)));
    else
    {
        const int n = (int) notes.size();
        for (int i = 0; i < n; ++i)
        {
            const auto r = rowRect (i);
            auto& nt = notes[(size_t) (n - 1 - i)];
            const float bh = jmin (44.0f, r.getHeight() - 4.0f), by = r.getCentreY() - bh * 0.5f;
            if (hit ({ 196, by, 56, bh })) nt -= 12;
            else if (hit ({ 260, by, 56, bh })) nt -= 1;
            else if (hit ({ 468, by, 56, bh })) nt += 1;
            else if (hit ({ 532, by, 56, bh })) nt += 12;
            else continue;
            nt = jlimit (12, 88, nt);   // C0 .. E6
        }
    }
    repaint();
}

//==============================================================================
// TunaEditor
TunaEditor::TunaEditor (TunaProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    background = ImageCache::getFromMemory (BinaryData::background_png, BinaryData::background_pngSize);

    addAndMakeVisible (display);
    for (auto* k : { &modeKnob, &tuningKnob, &refKnob }) addAndMakeVisible (k);
    addAndMakeVisible (arrowL); addAndMakeVisible (arrowR);
    addAndMakeVisible (foot);
    addChildComponent (dismiss);
    addChildComponent (menu);
    addChildComponent (tuningEditor);
    menu.setWantsKeyboardFocus (true);
    arrowL.setMouseCursor (MouseCursor::PointingHandCursor);
    arrowR.setMouseCursor (MouseCursor::PointingHandCursor);
    foot.setMouseCursor (MouseCursor::PointingHandCursor);

    auto setParam = [this] (const char* id, float plain)
    {
        if (auto* prm = proc.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
            prm->endChangeGesture();
        }
    };

    // MODE : sélecteur 3 positions
    modeKnob.stepPixels = 26.0f;
    modeKnob.onDragSteps = [this, setParam] (int d) { setParam ("mode", (float) jlimit (0, 2, modeIndex() + d)); };
    modeKnob.onWheel = modeKnob.onDragSteps;
    modeKnob.onClick = [this, setParam] { setParam ("mode", (float) ((modeIndex() + 1) % 3)); };

    // TUNING : encodeur sans fin (un cran = un accordage), clic = ouvre le menu
    auto step = [this] (int d)
    {
        proc.stepTuning (d);
        tuningAngle += d * MathConstants<float>::pi / 10.0f;
        updateKnobAngles();
        flash (upperName (proc.library.get (proc.getTuningIndex()), proc.library.french));
        if (menu.isVisible()) menu.rebuild (true);
    };
    tuningKnob.stepPixels = 16.0f;
    tuningKnob.onDragSteps = step;
    tuningKnob.onWheel = step;
    tuningKnob.onClick = [this] { openMenu(); };
    arrowL.onClick = [step] { step (-1); };
    arrowR.onClick = [step] { step (1); };

    // REF : 430 - 450 Hz
    auto refStep = [this, setParam] (int d)
    {
        const int r = jlimit (430, 450, (int) proc.apvts.getRawParameterValue ("ref")->load() + d);
        setParam ("ref", (float) r);
        flash ("A4 = " + String (r) + " Hz");
    };
    refKnob.onDragDelta = [this, refStep] (float d)
    {
        refDragAccum += d;
        while (refDragAccum >= 7.0f)  { refDragAccum -= 7.0f; refStep (1); }
        while (refDragAccum <= -7.0f) { refDragAccum += 7.0f; refStep (-1); }
    };
    refKnob.onWheel = refStep;
    refKnob.onDoubleClick = [this, setParam] { setParam ("ref", 440.0f); flash ("A4 = 440 Hz"); };

    // footswitch : TUNE / MUTE
    foot.onPress = [this, setParam] { setParam ("mute", isMuted() ? 0.0f : 1.0f); };
    foot.onRight = [this]
    {
        PopupMenu m;
        m.addSectionHeader ("Footswitch MUTE (MIDI)");
        m.addItem (1, "MIDI Learn (current: CC " + String (proc.ccMute.load()) + ")");
        m.addItem (2, "Latching pedal (127 = mute, 0 = on)", true, proc.apvts.getRawParameterValue ("ccmomentary")->load() < 0.5f);
        m.addItem (3, "Momentary pedal (each press toggles)", true, proc.apvts.getRawParameterValue ("ccmomentary")->load() > 0.5f);
        m.showMenuAsync (PopupMenu::Options().withMousePosition(), [this] (int r)
        {
            if (r == 1) { proc.learnTarget.store (0); flash ("MIDI LEARN..."); }
            if (r == 2 || r == 3)
                if (auto* prm = proc.apvts.getParameter ("ccmomentary")) prm->setValueNotifyingHost (r == 3 ? 1.0f : 0.0f);
        });
    };

    dismiss.onClick = [this] { closeTuningEditor(); closeMenu(); };

    // taille : identique à Freeze Shimmer, jamais plus petite que la taille par défaut
    const double aspect = (double) art::W / (double) art::H;
    setResizable (true, true);
    setResizeLimits (roundToInt (tunaui::heightDefault * aspect), tunaui::heightDefault,
                     roundToInt (tunaui::heightMax * aspect), tunaui::heightMax);
    getConstrainer()->setFixedAspectRatio (aspect);
    setOpaque (true);                      // anti-gel : l'hôte ne redessine jamais ce qui est derrière la fenêtre
    const int h0 = jlimit (tunaui::heightDefault, tunaui::heightMax, proc.uiHeight.load());
    setSize (roundToInt (h0 * aspect), h0);
    rebuildCaches();

    snap = proc.getSnapshot();
    updateKnobAngles();
    lastTime = Time::getMillisecondCounterHiRes() * 0.001;
    updateTimerRate();
}

TunaEditor::~TunaEditor() { stopTimer(); }

int TunaEditor::modeIndex() const { return jlimit (0, 2, (int) std::lround (proc.apvts.getRawParameterValue ("mode")->load())); }
bool TunaEditor::isMuted() const { return proc.apvts.getRawParameterValue ("mute")->load() > 0.5f; }

Rectangle<int> TunaEditor::scaled (Rectangle<float> r) const
{
    const float s = scale();
    return Rectangle<float> (r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s).toNearestInt();
}

Rectangle<int> TunaEditor::scaled (const art::Circle& c, float rMul) const
{
    return scaled (Rectangle<float> (c.x - c.r * rMul, c.y - c.r * rMul, 2 * c.r * rMul, 2 * c.r * rMul));
}

Rectangle<int> TunaEditor::menuBounds() const
{
    // panneau large (toute la pédale) pour rester lisible même à la taille par défaut
    return scaled (Rectangle<float> (40.0f, art::screen.getY() + 96.0f, art::W - 80.0f, 1990.0f - art::screen.getY() - 96.0f));
}

//==============================================================================
// Anti-gel de la fenêtre (Windows) : JUCE 8 dessine par défaut avec Direct2D (carte graphique).
// Avec certains pilotes (Intel notamment) la fenêtre du plugin cesse de se rafraîchir alors que
// le DAW continue de tourner. Le moteur logiciel est stable partout et largement assez rapide ici.
//
// Anti-gel v2 (Studio Wiga) :
//  1. moteur de rendu logiciel forcé sur Windows, appliqué à chaque nouvelle fenêtre hôte (peer)
//     et revérifié toutes les secondes, jamais en boucle ;
//  2. aucun calcul lourd dans paint() : le fond mis à l'échelle est refait par le timer seulement,
//     après stabilisation de la taille ou quand l'échelle d'affichage change (au plus toutes les 250 ms) ;
//  3. chien de garde : si un redessin demandé n'est pas arrivé au bout de 1,5 s alors que la fenêtre
//     est visible, le moteur de rendu est réappliqué et toute la fenêtre est redessinée ;
//  4. côté audio : aucun appel à l'hôte depuis le thread audio (voir PluginProcessor).
void TunaEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if (peer != lastPeer) { lastPeer = peer; repaint(); }
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw)
            peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void TunaEditor::parentHierarchyChanged() { useSoftwareRenderer(); }
void TunaEditor::visibilityChanged()      { useSoftwareRenderer(); if (isShowing()) repaint(); }

void TunaEditor::askRepaint (Rectangle<int> r)
{
    if (repaintAskedMs <= 0.0) repaintAskedMs = Time::getMillisecondCounterHiRes();
    repaint (r);
}

float TunaEditor::physicalScale() const
{
    return jlimit (1.0f, 4.0f, (float) Component::getApproximateScaleFactorForComponent (this));
}

// fond déjà à la taille de la fenêtre (pixels physiques) : appelé par le constructeur et le timer, jamais par paint()
void TunaEditor::rebuildCaches (float sc)
{
    if (sc <= 0.0f) sc = cacheScale > 0.0f ? cacheScale : physicalScale();
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    if (w == cacheW && h == cacheH && std::abs (sc - cacheScale) < 0.01f && bgScaled.isValid()) return;

    cacheW = w; cacheH = h; cacheScale = sc;
    const int pw = roundToInt ((float) w * sc), ph = roundToInt ((float) h * sc);
    bgScaled = (pw >= background.getWidth()) ? background : background.rescaled (pw, ph, Graphics::highResamplingQuality);
    repaint();
}

// Cadence de l'affichage : 30 i/s suffisent en MONO / POLY (le moteur publie ses mesures 25 fois
// par seconde, les segments sont discrets). En STROBE on garde 60 i/s : à 30 i/s la bande fine
// (période 24) semblerait s'arrêter vers ±7,5 cents et tourner à l'envers dès ±3,75 cents
// (effet « roue de diligence »), ce qui fausserait la lecture près de la justesse.
void TunaEditor::updateTimerRate()
{
    const int hz = modeIndex() == 2 ? 60 : 30;
    if (hz != timerHz) { timerHz = hz; startTimerHz (hz); }
}

void TunaEditor::resized()
{
    // anti-gel : jamais de setSize ici (mise à l'échelle Windows 125 / 150 % : l'hôte arrondit les tailles) ;
    // les limites du constrainer suffisent, sinon boucle de redimensionnement avec l'hôte
    proc.uiHeight.store (jlimit (tunaui::heightDefault, tunaui::heightMax, getHeight()));

    display.setBounds (scaled (art::screen));
    modeKnob.setBounds (scaled (art::modeKnob));
    tuningKnob.setBounds (scaled (art::tuningKnob));
    refKnob.setBounds (scaled (art::refKnob));
    arrowL.setBounds (scaled (art::arrowLeft));
    arrowR.setBounds (scaled (art::arrowRight));
    foot.setBounds (scaled (art::footswitch));
    dismiss.setBounds (getLocalBounds());
    menu.setBounds (menuBounds());
    tuningEditor.setBounds (menuBounds());
    resizeSettle = 4;              // le fond sera refait par le timer quand la taille ne bouge plus
}

void TunaEditor::setWindowHeight (int h)
{
    h = jlimit (tunaui::heightDefault, tunaui::heightMax, h);
    setSize (roundToInt ((double) h * art::W / art::H), h);
}

void TunaEditor::paintLed (Graphics& g, const art::Circle& c, Colour col, bool on) const
{
    if (! on) return;
    const float s = scale();
    const Point<float> p (c.x * s, c.y * s);
    const float r = c.r * s;
    ColourGradient halo (col.withAlpha (0.45f), p.x, p.y, col.withAlpha (0.0f), p.x + r * 4.2f, p.y, true);
    g.setGradientFill (halo);
    g.fillEllipse (p.x - r * 4.2f, p.y - r * 4.2f, r * 8.4f, r * 8.4f);
    ColourGradient lens (Colours::white, p.x - r * 0.25f, p.y - r * 0.3f, col.darker (0.6f), p.x + r, p.y + r, true);
    lens.addColour (0.35, col.brighter (0.3f));
    lens.addColour (0.75, col);
    g.setGradientFill (lens);
    g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
    g.setColour (Colours::white.withAlpha (0.75f));
    g.fillEllipse (p.x - r * 0.45f, p.y - r * 0.55f, r * 0.42f, r * 0.3f);
}

void TunaEditor::paint (Graphics& g)
{
    lastPaintMs = Time::getMillisecondCounterHiRes();
    repaintAskedMs = 0.0;
    g.fillAll (Colour (13, 13, 14));
    // anti-gel : aucun rescale ici, on dessine le cache existant (ou l'original en attendant le timer)
    const Image& bg = bgScaled.isValid() ? bgScaled : background;
    g.setImageResamplingQuality (&bg == &background ? Graphics::highResamplingQuality : Graphics::lowResamplingQuality);
    if (bg.isValid())
        g.drawImage (bg, getLocalBounds().toFloat());

    const int m = modeIndex();
    const auto amber = Colour (0xffffa020);
    paintLed (g, art::ledMono, amber, m == 0);
    paintLed (g, art::ledPoly, amber, m == 1);
    paintLed (g, art::ledStrobe, amber, m == 2);
    paintLed (g, art::ledMute, Colour (0xffff2a18), isMuted());

    if (foot.down)
    {
        // footswitch enfoncé : le chapeau descend (ombre interne)
        const float s = scale();
        const Point<float> c (art::footswitch.x * s, art::footswitch.y * s);
        const float r = art::footCap * s;
        ColourGradient sh (Colours::black.withAlpha (0.0f), c.x, c.y, Colours::black.withAlpha (0.45f), c.x + r, c.y, true);
        g.setGradientFill (sh);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (Colours::black.withAlpha (0.18f));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    }
}

void TunaEditor::paintOverChildren (Graphics&) {}

void TunaEditor::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showContextMenu();
}

void TunaEditor::showContextMenu()
{
    PopupMenu size;
    const int h = getHeight();
    size.addItem (101, "60 % (default)", true, std::abs (h - tunaui::heightDefault) < 8);
    size.addItem (103, "75 %", true, std::abs (h - tunaui::height75) < 8);
    size.addItem (104, "100 %", true, std::abs (h - tunaui::height100) < 8);

    PopupMenu midi, chan;
    const int ch = (int) proc.apvts.getRawParameterValue ("midichan")->load();
    chan.addItem (300, "Omni", true, ch == 0);
    for (int c = 1; c <= 16; ++c) chan.addItem (300 + c, String (c), true, ch == c);
    midi.addSubMenu ("Channel", chan);
    midi.addSeparator();
    midi.addItem (201, "Learn: Mute footswitch (CC " + String (proc.ccMute.load()) + ")");
    midi.addItem (202, "Learn: Mode (CC " + String (proc.ccMode.load()) + ")");
    midi.addItem (203, "Learn: Next tuning (CC " + String (proc.ccNext.load()) + ")");
    midi.addItem (204, "Learn: Previous tuning (CC " + String (proc.ccPrev.load()) + ")");
    midi.addItem (205, "Learn: Reference A4 (CC " + String (proc.ccRef.load()) + ")");
    midi.addItem (206, "Reset MIDI assignments (CC 80-84)");
    midi.addSeparator();
    const bool mom = proc.apvts.getRawParameterValue ("ccmomentary")->load() > 0.5f;
    midi.addItem (210, "Footswitch: latching pedal", true, ! mom);
    midi.addItem (211, "Footswitch: momentary pedal", true, mom);
    midi.addItem (-1, "Program Change n = favourite n (or tuning n)", false, false);

    PopupMenu m;
    m.addSubMenu ("Plugin size", size);
    m.addSubMenu ("MIDI", midi);
    m.addItem (401, "Custom tunings...");
    m.addItem (402, "Show tunings file");

    m.showMenuAsync (PopupMenu::Options().withMousePosition(), [this] (int r)
    {
        if (r == 101) setWindowHeight (tunaui::heightDefault);
        else if (r == 102) setWindowHeight (tunaui::height60);
        else if (r == 103) setWindowHeight (tunaui::height75);
        else if (r == 104) setWindowHeight (tunaui::height100);
        else if (r >= 201 && r <= 205) { proc.learnTarget.store (r - 201); flash ("MIDI LEARN..."); }
        else if (r == 206) { proc.ccMute = 80; proc.ccMode = 81; proc.ccNext = 82; proc.ccPrev = 83; proc.ccRef = 84; flash ("CC 80-84"); }
        else if (r == 210 || r == 211) { if (auto* p = proc.apvts.getParameter ("ccmomentary")) p->setValueNotifyingHost (r == 211 ? 1.0f : 0.0f); }
        else if (r >= 300 && r <= 316) { if (auto* p = proc.apvts.getParameter ("midichan")) p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 300))); }
        else if (r == 401) { openMenu(); menu.tab = 2; menu.rebuild (false); }
        else if (r == 402)
        {
            auto f = TuningLibrary::file();
            if (f.existsAsFile()) f.revealToUser();
            else { f.getParentDirectory().createDirectory(); f.getParentDirectory().revealToUser(); }
        }
    });
}

void TunaEditor::openMenu()
{
    closeTuningEditor();
    proc.library.reload();
    proc.refreshEngineTuning();
    dismiss.setVisible (true);
    dismiss.toFront (false);
    menu.setBounds (menuBounds());
    menu.setVisible (true);
    menu.toFront (true);
    menu.rebuild (true);
    menu.grabKeyboardFocus();
}

void TunaEditor::closeMenu()
{
    menu.setVisible (false);
    if (! tuningEditor.isVisible()) dismiss.setVisible (false);
}

void TunaEditor::openTuningEditor (int libraryIndex)
{
    menu.setVisible (false);
    dismiss.setVisible (true);
    dismiss.toFront (false);
    tuningEditor.setBounds (menuBounds());
    tuningEditor.open (libraryIndex);
    tuningEditor.setVisible (true);
    tuningEditor.toFront (true);
}

void TunaEditor::closeTuningEditor()
{
    tuningEditor.setVisible (false);
    if (! menu.isVisible()) dismiss.setVisible (false);
}

void TunaEditor::flash (const String& text)
{
    display.flashText = text;
    display.flashUntil = Time::getMillisecondCounterHiRes() * 0.001 + 1.1;
    display.repaint();
}

void TunaEditor::updateKnobAngles()
{
    const float deg = MathConstants<float>::pi / 180.0f;
    const float mA = (modeIndex() - 1) * 40.0f * deg;
    const float ref = proc.apvts.getRawParameterValue ("ref")->load();
    const float rA = jmap (ref, 430.0f, 450.0f, -135.0f, 135.0f) * deg;
    if (std::abs (modeKnob.angle - mA) > 1e-4f) { modeKnob.angle = mA; askRepaint (modeKnob.getBounds()); }
    if (std::abs (refKnob.angle - rA) > 1e-4f) { refKnob.angle = rA; askRepaint (refKnob.getBounds()); }
    if (std::abs (tuningKnob.angle - tuningAngle) > 1e-4f) { tuningKnob.angle = tuningAngle; askRepaint (tuningKnob.getBounds()); }
}

void TunaEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) jlimit (0.0, 0.1, now - lastTime);
    lastTime = now;
    snap = proc.getSnapshot();

    // moteur logiciel : seulement si la fenêtre hôte a changé, ou au plus une fois par seconde
    const double nowMs = now * 1000.0;
    if (getPeer() != lastPeer || nowMs - lastRendererCheckMs > 1000.0) { lastRendererCheckMs = nowMs; useSoftwareRenderer(); }

    // caches : reconstruits ici seulement (jamais dans paint)
    if (resizeSettle > 0 && --resizeSettle == 0) rebuildCaches();
    else if (resizeSettle == 0 && nowMs - lastScaleCheckMs > 250.0)
    {
        lastScaleCheckMs = nowMs;
        const float sc = physicalScale();
        if (std::abs (sc - cacheScale) > 0.01f) rebuildCaches (sc);
    }

    // chien de garde : un redessin demandé n'arrive pas -> on relance le moteur de rendu et on redessine tout
    if (repaintAskedMs > 0.0 && isShowing() && nowMs - repaintAskedMs > 1500.0 && lastPaintMs < repaintAskedMs)
    {
        repaintAskedMs = 0.0;
        lastPeer = nullptr;
        useSoftwareRenderer();
        repaint();
    }

    // stroboscope : 1 cent = un quart de période par seconde (bande du haut)
    if (snap.monoMidi >= 0 && (snap.monoActive || snap.monoAge < 0.3f))
        display.strobePhase += dt * jlimit (-6.0f, 6.0f, snap.monoCents * 0.25f);

    // MIDI learn terminé ?
    const int learn = proc.learnTarget.load();
    if (lastLearn >= 0 && learn < 0) flash ("CC " + String (proc.lastMidiCc.load()) + " OK");
    if (learn >= 0) { display.flashText = "MIDI LEARN..."; display.flashUntil = now + 0.2; }
    lastLearn = learn;

    // l'écran n'est redessiné que si ce qu'il affiche a changé (valeurs arrondies à ce qui est visible) ;
    // les potards seulement s'ils ont bougé (updateKnobAngles), les LED seulement si le mode / MUTE change.
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h] (int64_t v) { h ^= (uint64_t) v; h *= 1099511628211ull; };
        mix (modeIndex()); mix (proc.getTuningIndex()); mix (proc.library.french ? 1 : 0);
        mix ((int) proc.apvts.getRawParameterValue ("ref")->load());
        mix (snap.allInTune); mix (snap.monoOffTuning); mix (snap.anyStringActive); mix (snap.numStrings);
        for (int i = 0; i < snap.numStrings; ++i)
        {
            const auto& st = snap.strings[i];
            mix (st.midi); mix (st.measured); mix (st.active);
            mix ((int64_t) std::lround (st.cents * 4.0f));
            mix (st.active ? 0 : (int64_t) std::lround (jlimit (0.0f, 7.0f, st.age) * 8.0f));
        }
        mix (snap.monoMidi); mix (snap.monoActive); mix (snap.monoPrecise);
        mix ((int64_t) std::lround (snap.monoCents * 10.0f));
        mix ((int64_t) std::lround (snap.monoHz * 100.0f));
        mix (snap.monoActive ? 0 : (int64_t) std::lround (jlimit (0.0f, 3.0f, snap.monoAge) * 10.0f));
        if (modeIndex() == 2) mix ((int64_t) std::lround (display.strobePhase * 400.0f));
        const bool flashOn = now < display.flashUntil;
        mix (flashOn); if (flashOn) mix (display.flashText.hashCode64());
        if (h != lastDisplaySig) { lastDisplaySig = h; askRepaint (display.getBounds()); }
    }
    updateKnobAngles();

    const int m = modeIndex();
    const bool mu = isMuted();
    if (m != lastMode || mu != lastMute) { lastMode = m; lastMute = mu; askRepaint (getLocalBounds()); }
    updateTimerRate();
}
