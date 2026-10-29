#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
// Géométrie du visuel (pixels de background.png : 1641 x 2026)
namespace art
{
constexpr float W = 1641.0f, H = 2026.0f;
struct Circle { float x, y, r; };
const juce::Rectangle<float> screen { 408.2f, 394.3f, 828.0f, 594.5f };
constexpr Circle modeKnob   { 466.9f, 1261.0f, 68.8f };
constexpr Circle tuningKnob { 817.9f, 1263.8f, 100.3f };
constexpr Circle refKnob    { 1176.0f, 1261.0f, 68.8f };
constexpr Circle arrowLeft  { 663.2f, 1265.3f, 30.0f };
constexpr Circle arrowRight { 966.9f, 1265.3f, 30.0f };
constexpr Circle ledMono    { 644.6f, 1106.2f, 15.0f };
constexpr Circle ledPoly    { 819.3f, 1106.2f, 15.0f };
constexpr Circle ledStrobe  { 995.5f, 1106.2f, 15.0f };
constexpr Circle ledMute    { 819.3f, 1523.1f, 15.0f };
constexpr Circle footswitch { 813.6f, 1695.0f, 87.4f };
constexpr float  footCap = 53.0f;
}

//==============================================================================
namespace tunacol
{
const juce::Colour orange   { 0xffff7a3a };
const juce::Colour amber    { 0xffffb23a };
const juce::Colour amberDim { 0xff3a2808 };
const juce::Colour green    { 0xff5dff8a };
const juce::Colour greenDim { 0xff12351f };
const juce::Colour grey     { 0xffc9ced3 };
const juce::Colour panel    { 0xff0b0d0f };
}

struct TunaFonts
{
    TunaFonts();
    juce::Font mono (float h) const;
    juce::Font sans (float h) const;
    juce::Typeface::Ptr monoTf, sansTf;
};

class TunaEditor;

//==============================================================================
// Potard dessiné par-dessus le visuel : seul l'index tourne, l'éclairage du métal reste fixe.
class PedalKnob : public juce::Component
{
public:
    PedalKnob (float barFrom, float barTo) : barR0 (barFrom), barR1 (barTo) { setRepaintsOnMouseActivity (false); }
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

    float angle = 0.0f;           // radians, 0 = vers le haut
    std::function<void (int)> onDragSteps;     // potards crantés
    std::function<void (float)> onDragDelta;   // potards continus (delta en "pixels")
    std::function<void()> onClick, onDoubleClick;
    std::function<void (int)> onWheel;
    float stepPixels = 18.0f;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float barR0, barR1;           // index : rayon de début / fin (fraction du rayon)
    float accum = 0, lastY = 0, lastX = 0;
    bool dragged = false;
    float wheelAccum = 0;
};

//==============================================================================
class TunaDisplay : public juce::Component
{
public:
    TunaDisplay (TunaEditor& e) : ed (e) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverDrop = false; repaint(); }

    juce::Rectangle<float> dropRect() const;   // coordonnées "écran" (828 x 594.5)
    float strobePhase = 0;
    juce::String flashText;
    double flashUntil = 0;

private:
    void paintHeader (juce::Graphics&, const tuna::Snapshot&, int mode, bool autoMono);
    void paintPoly (juce::Graphics&, const tuna::Snapshot&);
    void paintMono (juce::Graphics&, const tuna::Snapshot&);
    void paintStrobe (juce::Graphics&, const tuna::Snapshot&);
    TunaEditor& ed;
    bool hoverDrop = false;
};

//==============================================================================
// Petit bouton plat au style de l'écran
class ScreenButton : public juce::Component
{
public:
    ScreenButton (TunaEditor& e, juce::String t) : ed (e), text (std::move (t)) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;
private:
    TunaEditor& ed;
public:
    juce::String text;
    bool primary = false;
};

//==============================================================================
// Menu déroulant des accordages (style écran ambre)
class TuningMenu : public juce::Component
{
public:
    TuningMenu (TunaEditor& e) : ed (e) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void rebuild (bool scrollToSelection);
    int tab = 0;                  // 0 = tous, 1 = favoris, 2 = perso

private:
    struct Row { int kind; juce::String text; int index; };   // kind 0 = catégorie, 1 = accordage, 2 = "nouvel accordage"
    float u() const;              // pixels par unité "écran"
    float rowH (const Row& r) const { return r.kind == 0 ? 50.0f : 54.0f; }
    float contentHeight() const;
    float listTop() const { return 64.0f; }
    float listHeight() const;
    int rowAt (float yUnits) const;
    juce::Rectangle<float> tabRect (int t) const;
    juce::Rectangle<float> langRect() const;
    void clampScroll();

    TunaEditor& ed;
    std::vector<Row> rows;
    float scroll = 0;
    int hoverRow = -1;
    bool hoverLang = false;
    bool draggingBar = false;
    float dragStartY = 0, dragStartScroll = 0;
};

//==============================================================================
// Éditeur d'accordage perso
class TuningEditorPanel : public juce::Component
{
public:
    TuningEditorPanel (TunaEditor& e);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void open (int libraryIndex);   // -1 = nouveau (copie de l'accordage actif)

private:
    float u() const;
    juce::Rectangle<float> rowRect (int i) const;     // i = 0 : corde la plus aiguë
    TunaEditor& ed;
    int editing = -1;
    std::vector<int> notes;
    juce::TextEditor name;
    ScreenButton save, del, cancel;
};

//==============================================================================
class TunaEditor : public juce::AudioProcessorEditor,
                   private juce::Timer
{
public:
    explicit TunaEditor (TunaProcessor&);
    ~TunaEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void mouseDown (const juce::MouseEvent&) override;

    TunaProcessor& proc;
    TunaFonts fonts;
    tuna::Snapshot snap;
    int  modeIndex() const;
    bool isMuted() const;
    float scale() const { return (float) getWidth() / art::W; }

    void openMenu();
    void closeMenu();
    void openTuningEditor (int libraryIndex);
    void closeTuningEditor();
    void flash (const juce::String& text);
    void setWindowHeight (int h);
    void showContextMenu();

private:
    void timerCallback() override;
    void visibilityChanged() override;
    void useSoftwareRenderer();
    void updateTimerRate();
    int timerHz = 0;              // cadence actuelle du timer de l'éditeur (30 ou 60 en STROBE)
    uint64_t lastDisplaySig = 0;  // empreinte de ce que montre l'écran (redessin seulement si elle change)
    juce::Image bgScaled;         // fond mis à l'échelle de la fenêtre (cache, refait par le timer seulement)
    int cacheW = 0, cacheH = 0;
    float cacheScale = 0.0f;
    int resizeSettle = 0;
    void rebuildCaches (float scale = 0.0f);
    float physicalScale() const;
    // anti-gel v2 : suivi de la fenêtre hôte, de l'échelle et des redessins
    void* lastPeer = nullptr;
    double lastPaintMs = 0.0, repaintAskedMs = 0.0, lastScaleCheckMs = 0.0, lastRendererCheckMs = 0.0;
    void askRepaint (juce::Rectangle<int> r);
public:
    void updateKnobAngles();
private:
    void paintLed (juce::Graphics&, const art::Circle&, juce::Colour, bool on) const;
    juce::Rectangle<int> scaled (juce::Rectangle<float> r) const;
    juce::Rectangle<int> scaled (const art::Circle& c, float rMul = 1.0f) const;

    juce::Image background;
    TunaDisplay display { *this };
    PedalKnob modeKnob { 0.29f, 0.72f }, tuningKnob { 0.42f, 0.92f }, refKnob { 0.29f, 0.72f };
    struct Arrow : juce::Component { std::function<void()> onClick; void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); } };
    Arrow arrowL, arrowR;
    struct Foot : juce::Component
    {
        std::function<void()> onPress; std::function<void()> onRight;
        bool down = false;
        bool hitTest (int x, int y) override { return juce::Point<float> ((float) x, (float) y).getDistanceFrom (getLocalBounds().toFloat().getCentre()) <= getWidth() * 0.5f; }
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onRight) onRight(); return; } down = true; if (onPress) onPress(); getParentComponent()->repaint(); }
        void mouseUp (const juce::MouseEvent&) override { down = false; getParentComponent()->repaint(); }
    } foot;
    struct Dismiss : juce::Component { std::function<void()> onClick; void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); } } dismiss;

public:
    TuningMenu menu { *this };
    TuningEditorPanel tuningEditor { *this };

private:
    float tuningAngle = 0.0f;
    float refDragAccum = 0.0f;
    double lastTime = 0;
    int lastMode = -1; bool lastMute = false;
    int lastLearn = -1;
    juce::Rectangle<int> menuBounds() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunaEditor)
};
