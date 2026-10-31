// Moonlight XR Native launcher (lane flat-input): a gamepad's thumbsticks on the flat window's pages. New file, added
// to app/streaming by launcher/build.sh; SdlGamepadKeyNavigation (patch 0018) hands it the sticks. GPLv3.
//
// Stock moonlight-qt turns only the left stick into focus moves (Up/Down/Left/Right, Tab/Backtab on the Settings page)
// and ignores the right stick. Here:
//   right stick  scrolls the page smoothly, its speed in proportion to how far the stick is pushed past a dead zone
//                (kDeadZone; full deflection kMaxSpeed logical pixels a second). The page is the scrollable Flickable
//                (a Settings page, a list, the PC or app grid) that holds the focus, else the page on top of the
//                StackView; in an open dialog only the dialog's own list scrolls. When the stick is let go and the
//                focused row was scrolled out of sight, the focus goes to the first row in sight (the item in the middle
//                of a list or grid), so the next press of A, the d-pad or the left stick carries on from what is shown.
//   left stick   moves the focus as stock does (past kNavThreshold, a step every kNavRepeatMs), and the page follows
//                the focus (each page keeps its focused row on screen). In a list that takes Up/Down (a dialog's
//                choices, a combo box's list), Up/Down go to the list instead of Tab/Backtab, so a long list scrolls
//                under the stick (and the d-pad) and only its last (first) entry hands the focus on to the buttons.
//
// Tests (tests/launcher/run_tests.sh): MLXR_LAUNCHER_STICK="ry=24000@2500+800;ly=32767@4000+400" injects stick values
// (lx, ly, rx, ry; SDL's -32768..32767) from <ms after start> for <ms>; each logs "MLXR_STICK: ..." when it ends:
// the scrolled item's contentY before and after, and where the focus is.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>

class QQuickItem;
class QQuickWindow;
class QWindow;

class MlxrStick : public QObject
{
    Q_OBJECT

public:
    static MlxrStick* get();

    // SdlGamepadKeyNavigation's 50 ms poll: per axis the most pushed value of all open pads
    void poll(int leftX, int leftY, int rightX, int rightY);
    // SdlGamepadKeyNavigation::setUiNavMode: up/down are Tab/Backtab (the Settings, bindings and profiles pages)
    void setUiNavMode(bool on) { m_uiNav = on; }
    // navigation stopped (a stream starts): the sticks count as centred
    void reset();
    // The d-pad's and the left stick's up and down in UI nav mode: true when the focus is in a list (ListView,
    // GridView) that can still move that way, so Up/Down go to it instead of Tab/Backtab
    static bool listTakesVertical(QWindow* window, bool down);

    static constexpr int kDeadZone = 8000;        // right stick: about a quarter of its travel does nothing
    static constexpr double kMaxSpeed = 2400.0;   // right stick: logical px/s at full deflection
    static constexpr int kNavThreshold = 30000;   // left stick: stock's
    static constexpr int kNavRepeatMs = 150;      // left stick: stock's AXIS_NAVIGATION_REPEAT_DELAY

    // Pure parts. Scroll speed in px/s for a stick value (signed; 0 inside the dead zone, linear to kMaxSpeed)
    static double scrollSpeed(int axis);
    // What the right stick scrolls in this window now, or null
    static QQuickItem* scrollTarget(QQuickWindow* window);
    // Moves a Flickable's contentY by dy within its bounds; the change made
    static double scrollBy(QQuickItem* flickable, double dy);

private:
    MlxrStick();
    void tick();
    void ensureTimer();
    void settle(QQuickItem* flickable, bool down);
    static QQuickWindow* window();
    static void sendKey(int key, bool shift);
    QString describeFocus() const;

    struct Inject {
        QString axis;
        int value = 0;
        qint64 from = 0, until = 0;
        bool started = false, ended = false;
        QPointer<QQuickItem> target;
        double before = 0;
    };

    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastTick = -1;
    qint64 m_lastPoll = -100000;
    qint64 m_lastNav = -100000;
    int m_lx = 0, m_ly = 0, m_rx = 0, m_ry = 0;
    bool m_uiNav = false;
    QVector<Inject> m_inject;
    // the Flickable the right stick is moving, until it is let go
    QPointer<QQuickItem> m_scrolled;
    double m_scrolledFrom = 0;
    bool m_scrolledDown = true;
};
