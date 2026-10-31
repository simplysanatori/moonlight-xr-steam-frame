// Moonlight XR Native launcher (lane flat-input): a gamepad's thumbsticks on the flat window's pages. See mlxrstick.h.
// GPLv3.
#include "mlxrstick.h"

#include <QDebug>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtMath>

#include <cstdlib>

namespace {

constexpr int kTickMs = 16;      // the right stick's scroll step: a frame at 60 Hz
constexpr int kStaleMs = 250;    // no poll for this long (navigation off, a stream running): the sticks are centred
constexpr int kAxisMax = 32767;

bool isFlickable(const QQuickItem* i)
{
    return i && i->inherits("QQuickFlickable");
}

bool isItemView(const QQuickItem* i)
{
    return i && i->inherits("QQuickItemView");
}

bool isPopup(const QQuickItem* i)
{
    return i && i->inherits("QQuickPopupItem");
}

double num(const QObject* o, const char* name)
{
    return o->property(name).toDouble();
}

// Room to scroll up and down: a visible, interactive Flickable taller inside than out
bool canScrollV(const QQuickItem* f)
{
    if (!isFlickable(f) || !f->isVisible() || !f->property("interactive").toBool() || f->height() <= 1) {
        return false;
    }
    return num(f, "contentHeight") + num(f, "topMargin") + num(f, "bottomMargin") > f->height() + 1.0;
}

bool isInside(const QQuickItem* item, const QQuickItem* ancestor)
{
    for (const QQuickItem* i = item; i; i = i->parentItem()) {
        if (i == ancestor) {
            return true;
        }
    }
    return false;
}

// The largest scrollable Flickable under root (root included), breadth first
QQuickItem* largestScrollable(QQuickItem* root)
{
    QQuickItem* best = nullptr;
    double bestArea = 0;
    QList<QQuickItem*> todo { root };
    while (!todo.isEmpty()) {
        QQuickItem* i = todo.takeFirst();
        if (!i->isVisible()) {
            continue;
        }
        if (canScrollV(i) && i->width() * i->height() > bestArea) {
            best = i;
            bestArea = i->width() * i->height();
        }
        todo += i->childItems();
    }
    return best;
}

QQuickItem* findStackView(QQuickItem* root)
{
    QList<QQuickItem*> todo { root };
    while (!todo.isEmpty()) {
        QQuickItem* i = todo.takeFirst();
        if (i->inherits("QQuickStackView")) {
            return i;
        }
        todo += i->childItems();
    }
    return nullptr;
}

// Wholly inside the Flickable's visible area
bool inSight(const QQuickItem* item, const QQuickItem* f)
{
    if (!item->isVisible() || item->height() <= 0) {
        return false;
    }
    const QRectF r = item->mapRectToItem(f, QRectF(0, 0, item->width(), item->height()));
    return r.top() >= -0.5 && r.bottom() <= f->height() + 0.5;
}

} // namespace

MlxrStick* MlxrStick::get()
{
    static MlxrStick* s = new MlxrStick();
    return s;
}

MlxrStick::MlxrStick()
{
    m_clock.start();
    m_timer.setInterval(kTickMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &MlxrStick::tick);
    // tests: "ry=24000@2500+800;ly=32767@4000+400"
    const QString spec = qEnvironmentVariable("MLXR_LAUNCHER_STICK");
    for (const QString& part : spec.split(';', Qt::SkipEmptyParts)) {
        const int eq = part.indexOf('='), at = part.indexOf('@'), plus = part.indexOf('+', at);
        if (eq <= 0 || at < eq || plus < at) {
            qWarning() << "MLXR_LAUNCHER_STICK: cannot read" << part;
            continue;
        }
        Inject in;
        in.axis = part.left(eq).trimmed();
        in.value = qBound(-32768, part.mid(eq + 1, at - eq - 1).toInt(), kAxisMax);
        in.from = part.mid(at + 1, plus - at - 1).toLongLong();
        in.until = in.from + part.mid(plus + 1).toLongLong();
        m_inject.append(in);
    }
    if (!m_inject.isEmpty()) {
        m_timer.start();
    }
}

double MlxrStick::scrollSpeed(int axis)
{
    const int a = std::abs(axis);
    if (a <= kDeadZone) {
        return 0.0;
    }
    const double t = qMin(1.0, double(a - kDeadZone) / double(kAxisMax - kDeadZone));
    return (axis < 0 ? -1.0 : 1.0) * t * kMaxSpeed;
}

QQuickWindow* MlxrStick::window()
{
    if (QQuickWindow* w = qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow())) {
        return w;
    }
    for (QWindow* w : QGuiApplication::topLevelWindows()) {
        if (w->isVisible() && qobject_cast<QQuickWindow*>(w)) {
            return static_cast<QQuickWindow*>(w);
        }
    }
    return nullptr;
}

QQuickItem* MlxrStick::scrollTarget(QQuickWindow* w)
{
    if (!w) {
        return nullptr;
    }
    // the focus's own scrollable list or page; inside a dialog nothing behind it
    bool inPopup = false;
    for (QQuickItem* i = w->activeFocusItem(); i; i = i->parentItem()) {
        if (canScrollV(i)) {
            return i;
        }
        inPopup = inPopup || isPopup(i);
    }
    if (inPopup) {
        return nullptr;
    }
    // else the page on top of the StackView (the PC grid, an app grid, a Settings page)
    QQuickItem* root = w->contentItem();
    if (QQuickItem* stack = findStackView(root)) {
        if (QQuickItem* page = stack->property("currentItem").value<QQuickItem*>()) {
            return largestScrollable(page);
        }
    }
    return largestScrollable(root);
}

double MlxrStick::scrollBy(QQuickItem* f, double dy)
{
    if (!isFlickable(f)) {
        return 0.0;
    }
    const double y = num(f, "contentY");
    const double minY = num(f, "originY") - num(f, "topMargin");
    const double maxY = qMax(minY, num(f, "originY") + num(f, "contentHeight") + num(f, "bottomMargin") - f->height());
    const double ny = qBound(minY, y + dy, maxY);
    if (qFuzzyCompare(ny + 1.0, y + 1.0)) {
        return 0.0;
    }
    if (f->property("moving").toBool()) {
        QMetaObject::invokeMethod(f, "cancelFlick");
    }
    f->setProperty("contentY", ny);
    return ny - y;
}

bool MlxrStick::listTakesVertical(QWindow* w, bool down)
{
    QQuickWindow* qw = qobject_cast<QQuickWindow*>(w);
    if (!qw) {
        return false;
    }
    for (QQuickItem* i = qw->activeFocusItem(); i; i = i->parentItem()) {
        if (!isFlickable(i)) {
            continue;
        }
        // the nearest Flickable decides: a page is not a list
        if (!isItemView(i) || !i->property("keyNavigationEnabled").toBool() || !i->isVisible()) {
            return false;
        }
        if (i->inherits("QQuickListView") && i->property("orientation").toInt() != Qt::Vertical) {
            return false;
        }
        const int count = i->property("count").toInt(), cur = i->property("currentIndex").toInt();
        return count > 0 && (down ? cur < count - 1 : cur > 0);
    }
    return false;
}

void MlxrStick::poll(int leftX, int leftY, int rightX, int rightY)
{
    m_lx = leftX;
    m_ly = leftY;
    m_rx = rightX;
    m_ry = rightY;
    m_lastPoll = m_clock.elapsed();
    ensureTimer();
}

void MlxrStick::reset()
{
    m_lx = m_ly = m_rx = m_ry = 0;
    m_lastPoll = -100000;
}

void MlxrStick::ensureTimer()
{
    const bool moved = std::abs(m_ry) > kDeadZone || std::abs(m_lx) > kNavThreshold || std::abs(m_ly) > kNavThreshold;
    if ((moved || m_scrolled) && !m_timer.isActive()) {
        m_lastTick = -1;
        m_timer.start();
    }
}

void MlxrStick::sendKey(int key, bool shift)
{
    QWindow* w = window();
    if (!w) {
        return;
    }
    const Qt::KeyboardModifiers mods = shift ? Qt::ShiftModifier : Qt::NoModifier;
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QGuiApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QGuiApplication::sendEvent(w, &release);
}

QString MlxrStick::describeFocus() const
{
    QQuickWindow* w = window();
    QQuickItem* f = w ? w->activeFocusItem() : nullptr;
    if (!f) {
        return "none";
    }
    QString name;
    for (QQuickItem* i = f; i && name.isEmpty(); i = i->parentItem()) {
        name = i->objectName();
    }
    QString s = QString("%1 (%2)").arg(name.isEmpty() ? QString("-") : name, f->metaObject()->className());
    if (f->inherits("QQuickSlider")) {
        s += QString(", value %1").arg(num(f, "value"));
    }
    for (QQuickItem* i = f; i; i = i->parentItem()) {
        if (isItemView(i)) {
            s += QString(", list index %1 of %2, list contentY %3")
                     .arg(i->property("currentIndex").toInt())
                     .arg(i->property("count").toInt())
                     .arg(num(i, "contentY"), 0, 'f', 1);
            break;
        }
    }
    return s;
}

// The right stick was let go after scrolling: a focus scrolled out of sight goes to what is in sight
void MlxrStick::settle(QQuickItem* f, bool down)
{
    QQuickWindow* w = f->window();
    if (!w) {
        return;
    }
    if (isItemView(f)) {
        QQuickItem* cur = f->property("currentItem").value<QQuickItem*>();
        if (cur && inSight(cur, f)) {
            return;
        }
        // the item in the middle of what is shown (a grid's middle may be a gap: try across)
        for (double fx : { 0.5, 0.25, 0.75, 0.1, 0.9 }) {
            int idx = -1;
            QMetaObject::invokeMethod(f, "indexAt", Q_RETURN_ARG(int, idx), Q_ARG(qreal, num(f, "contentX") + f->width() * fx),
                                      Q_ARG(qreal, num(f, "contentY") + f->height() * 0.5));
            if (idx >= 0) {
                f->setProperty("currentIndex", idx);
                return;
            }
        }
        return;
    }
    QQuickItem* focus = w->activeFocusItem();
    const bool focusHere = focus && isInside(focus, f);
    if (focusHere && inSight(focus, f)) {
        return;
    }
    // along the Tab order, the first row wholly in sight: from the top of what is shown after scrolling down, from
    // its bottom after scrolling up
    QQuickItem* start = focusHere ? focus : f;
    QQuickItem* i = start;
    for (int n = 0; n < 2000; n++) {
        i = i->nextItemInFocusChain(down);
        if (!i || i == start) {
            break;
        }
        if (isInside(i, f) && i->isEnabled() && inSight(i, f)) {
            i->forceActiveFocus(Qt::TabFocusReason);
            return;
        }
    }
}

void MlxrStick::tick()
{
    const qint64 now = m_clock.elapsed();
    const double dt = m_lastTick < 0 ? kTickMs / 1000.0 : qMin(0.1, (now - m_lastTick) / 1000.0);
    m_lastTick = now;

    int lx = m_lx, ly = m_ly, ry = m_ry;
    if (now - m_lastPoll > kStaleMs) {
        lx = ly = ry = 0;
    }
    // tests: injected values replace the pads'
    bool injecting = false;
    for (Inject& in : m_inject) {
        if (in.ended) {
            continue;
        }
        injecting = true;
        if (now < in.from) {
            continue;
        }
        if (!in.started) {
            in.started = true;
            in.target = scrollTarget(window());
            in.before = in.target ? num(in.target, "contentY") : 0.0;
        }
        if (now >= in.until) {
            continue;   // ends below, once the release has been handled
        }
        if (in.axis == "lx") lx = in.value;
        else if (in.axis == "ly") ly = in.value;
        else if (in.axis == "ry") ry = in.value;
    }

    // left stick: focus moves, stock's thresholds and repeat
    if (now - m_lastNav >= kNavRepeatMs) {
        if (ly < -kNavThreshold || ly > kNavThreshold) {
            const bool down = ly > 0;
            if (m_uiNav && !listTakesVertical(window(), down)) {
                sendKey(Qt::Key_Tab, !down);   // Backtab is Shift+Tab, as stock sends it
            }
            else {
                sendKey(down ? Qt::Key_Down : Qt::Key_Up, false);
            }
            m_lastNav = now;
        }
        else if (lx < -kNavThreshold || lx > kNavThreshold) {
            sendKey(lx > 0 ? Qt::Key_Right : Qt::Key_Left, false);
            m_lastNav = now;
        }
    }

    // right stick: smooth scroll of the page or list in front
    const double v = scrollSpeed(ry);
    if (v != 0.0) {
        QQuickItem* f = m_scrolled ? m_scrolled.data() : scrollTarget(window());
        if (f && !m_scrolled) {
            m_scrolled = f;
            m_scrolledFrom = num(f, "contentY");
        }
        if (f) {
            scrollBy(f, v * dt);
            m_scrolledDown = v > 0;
        }
    }
    else if (m_scrolled) {
        QQuickItem* f = m_scrolled;
        m_scrolled = nullptr;
        qInfo().noquote() << QString("gamepad: the right stick scrolled %1 from y %2 to %3")
                                 .arg(f->objectName().isEmpty() ? QString(f->metaObject()->className()) : f->objectName())
                                 .arg(m_scrolledFrom, 0, 'f', 0)
                                 .arg(num(f, "contentY"), 0, 'f', 0);
        settle(f, m_scrolledDown);
    }

    for (Inject& in : m_inject) {
        if (in.started && !in.ended && now >= in.until) {
            in.ended = true;
            QString moved = "no scrollable item";
            if (in.target) {
                moved = QString("%1 contentY %2 -> %3")
                            .arg(in.target->objectName().isEmpty() ? QString(in.target->metaObject()->className())
                                                                   : in.target->objectName())
                            .arg(in.before, 0, 'f', 1)
                            .arg(num(in.target, "contentY"), 0, 'f', 1);
            }
            qInfo().noquote() << QString("MLXR_STICK: %1=%2 for %3 ms done: %4; focus %5")
                                     .arg(in.axis).arg(in.value).arg(in.until - in.from).arg(moved, describeFocus());
        }
    }

    const bool active = std::abs(ry) > kDeadZone || std::abs(lx) > kNavThreshold || std::abs(ly) > kNavThreshold;
    if (!active && !m_scrolled && !injecting) {
        m_timer.stop();
    }
}
