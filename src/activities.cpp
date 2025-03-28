/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "activities.h"
// KWin
#include "virtualdesktops.h"
#include "window.h"
#include "workspace.h"
#if KWIN_BUILD_X11
#include "x11window.h"
#endif
// KDE
#include <KConfigGroup>
// Qt
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace KWin
{

Activities::Activities(const KSharedConfig::Ptr &config)
    : m_controller(new KActivities::Controller(this))
    , m_config(config)
{
    connect(m_controller, &KActivities::Controller::activityRemoved, this, &Activities::slotRemoved);
    connect(m_controller, &KActivities::Controller::activityRemoved, this, &Activities::removed);
    connect(m_controller, &KActivities::Controller::activityAdded, this, &Activities::added);
    connect(m_controller, &KActivities::Controller::currentActivityChanged, this, &Activities::slotCurrentChanged);
    connect(m_controller, &KActivities::Controller::serviceStatusChanged, this, &Activities::slotServiceStatusChanged);

    const auto group = m_config->group("Activities").group("LastVirtualDesktop");
    for (const auto &activity : group.keyList()) {
        const QString desktop = group.readEntry(activity);
        if (!desktop.isEmpty()) {
            m_lastVirtualDesktop[activity] = desktop;
        }
    }
}

KActivities::Consumer::ServiceStatus Activities::serviceStatus() const
{
    return m_controller->serviceStatus();
}

void Activities::slotServiceStatusChanged()
{
    if (m_controller->serviceStatus() != KActivities::Consumer::Running) {
        return;
    }
    const auto windows = Workspace::self()->windows();
    for (auto *const window : windows) {
        if (!window->isClient()) {
            continue;
        }
        if (window->isDesktop()) {
            continue;
        }
        window->checkActivities();
    }
}

void Activities::setCurrent(const QString &activity, VirtualDesktop *desktop)
{
    if (desktop) {
        m_lastVirtualDesktop[activity] = desktop->id();
    }
    m_controller->setCurrentActivity(activity);
}

void Activities::notifyCurrentDesktopChanged(VirtualDesktop *desktop)
{
    m_lastVirtualDesktop[m_current] = desktop->id();
    m_config->group("Activities").group("LastVirtualDesktop").writeEntry(m_current, desktop->id());
}

void Activities::slotCurrentChanged(const QString &newActivity)
{
    if (m_current == newActivity) {
        return;
    }
    Q_EMIT currentAboutToChange();
    m_previous = m_current;
    m_current = newActivity;

    if (m_previous != nullUuid()) {
        m_lastVirtualDesktop[m_previous] = VirtualDesktopManager::self()->currentDesktop()->id();
    }
    const auto it = m_lastVirtualDesktop.find(m_current);
    if (it != m_lastVirtualDesktop.end()) {
        VirtualDesktop *desktop = VirtualDesktopManager::self()->desktopForId(it->second);
        if (desktop) {
            VirtualDesktopManager::self()->setCurrent(desktop);
        }
    }

    Q_EMIT currentChanged(newActivity);
}

void Activities::slotRemoved(const QString &activity)
{
    const auto windows = Workspace::self()->windows();
    for (auto *const window : windows) {
        if (!window->isClient()) {
            continue;
        }
        if (window->isDesktop()) {
            continue;
        }
        window->setOnActivity(activity, false);
    }
    // toss out any session data for it
    KConfigGroup cg(KSharedConfig::openConfig(), QLatin1String("SubSession: ") + activity);
    cg.deleteGroup();

    m_lastVirtualDesktop.erase(activity);
    m_config->group("Activities").group("LastVirtualDesktop").deleteEntry(activity);
}

void Activities::toggleWindowOnActivity(Window *window, const QString &activity, bool dont_activate)
{
    // int old_desktop = window->desktop();
    bool was_on_activity = window->isOnActivity(activity);
    bool was_on_all = window->isOnAllActivities();
    // note: all activities === no activities
    bool enable = was_on_all || !was_on_activity;
    window->setOnActivity(activity, enable);
    if (window->isOnActivity(activity) == was_on_activity && window->isOnAllActivities() == was_on_all) { // No change
        return;
    }

    Workspace *ws = Workspace::self();
    if (window->isOnCurrentActivity()) {
        if (window->wantsTabFocus() && options->focusPolicyIsReasonable() && !was_on_activity && // for stickyness changes
                                                                                                 // FIXME not sure if the line above refers to the correct activity
            !dont_activate) {
            ws->requestFocus(window);
        } else {
            ws->restackWindowUnderActive(window);
        }
    } else {
        ws->raiseWindow(window);
    }

    // notifyWindowDesktopChanged( c, old_desktop );

    const auto transients_stacking_order = ws->ensureStackingOrder(window->transients());
    for (auto *const window : transients_stacking_order) {
        if (!window) {
            continue;
        }
        toggleWindowOnActivity(window, activity, dont_activate);
    }
    ws->rearrange();
}

bool Activities::start(const QString &id)
{
    Workspace *ws = Workspace::self();
    if (ws->sessionManager()->state() == SessionState::Saving) {
        return false; // ksmserver doesn't queue requests (yet)
    }

    if (!all().contains(id)) {
        return false; // bogus id
    }

    ws->sessionManager()->loadSubSessionInfo(id);

    QDBusInterface ksmserver("org.kde.ksmserver", "/KSMServer", "org.kde.KSMServerInterface");
    if (ksmserver.isValid()) {
        ksmserver.asyncCall("restoreSubSession", id);
    } else {
        qCDebug(KWIN_CORE) << "couldn't get ksmserver interface";
        return false;
    }
    return true;
}

bool Activities::stop(const QString &id)
{
    if (Workspace::self()->sessionManager()->state() == SessionState::Saving) {
        return false; // ksmserver doesn't queue requests (yet)
        // FIXME what about session *loading*?
    }

    // ugly hack to avoid dbus deadlocks
    QMetaObject::invokeMethod(
        this, [this, id] {
            reallyStop(id);
        },
        Qt::QueuedConnection);
    // then lie and assume it worked.
    return true;
}

void Activities::reallyStop(const QString &id)
{
    Workspace *ws = Workspace::self();
    if (ws->sessionManager()->state() == SessionState::Saving) {
        return; // ksmserver doesn't queue requests (yet)
    }

    qCDebug(KWIN_CORE) << id;

    QSet<QByteArray> saveSessionIds;
    QSet<QByteArray> dontCloseSessionIds;
#if KWIN_BUILD_X11
    const auto windows = ws->windows();
    for (auto *const window : windows) {
        auto x11Window = qobject_cast<X11Window *>(window);
        if (!x11Window || window->isUnmanaged()) {
            continue;
        }
        if (window->isDesktop()) {
            continue;
        }
        const QByteArray sessionId = x11Window->sessionId();
        if (sessionId.isEmpty()) {
            continue; // TODO support old wm_command apps too?
        }

        // qDebug() << sessionId;

        // if it's on the activity that's closing, it needs saving
        // but if a process is on some other open activity, I don't wanna close it yet
        // this is, of course, complicated by a process having many windows.
        if (window->isOnAllActivities()) {
            dontCloseSessionIds << sessionId;
            continue;
        }

        const QStringList activities = window->activities();
        for (const QString &activityId : activities) {
            if (activityId == id) {
                saveSessionIds << sessionId;
            } else if (running().contains(activityId)) {
                dontCloseSessionIds << sessionId;
            }
        }
    }
    ws->sessionManager()->storeSubSession(id, saveSessionIds);
#endif

    QStringList saveAndClose;
    QStringList saveOnly;
    for (const QByteArray &sessionId : std::as_const(saveSessionIds)) {
        if (dontCloseSessionIds.contains(sessionId)) {
            saveOnly << sessionId;
        } else {
            saveAndClose << sessionId;
        }
    }

    qCDebug(KWIN_CORE) << "saveActivity" << id << saveAndClose << saveOnly;

    // pass off to ksmserver
    QDBusInterface ksmserver("org.kde.ksmserver", "/KSMServer", "org.kde.KSMServerInterface");
    if (ksmserver.isValid()) {
        ksmserver.asyncCall("saveSubSession", id, saveAndClose, saveOnly);
    } else {
        qCDebug(KWIN_CORE) << "couldn't get ksmserver interface";
    }
}

} // namespace

#include "moc_activities.cpp"
