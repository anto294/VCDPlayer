#include "SleepInhibitor.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#else
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QVariantMap>
#endif

namespace {

constexpr int kReleaseDelayMs = 5000;

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
constexpr int kDBusTimeoutMs = 2000;
const char* const kReason = "Playing a Video CD";
#endif

}  // namespace

SleepInhibitor::SleepInhibitor(QObject* parent) : QObject(parent) {
  releaseDelay_.setSingleShot(true);
  releaseDelay_.setInterval(kReleaseDelayMs);
  connect(&releaseDelay_, &QTimer::timeout, this, &SleepInhibitor::release);
}

SleepInhibitor::~SleepInhibitor() { release(); }

void SleepInhibitor::setActive(bool active) {
  if (active) {
    releaseDelay_.stop();
    if (!held_) acquire();
  } else if (held_ && !releaseDelay_.isActive()) {
    releaseDelay_.start();
  }
}

#if defined(Q_OS_WIN)

void SleepInhibitor::acquire() {
  SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
  held_ = true;
}

void SleepInhibitor::release() {
  if (!held_) return;
  SetThreadExecutionState(ES_CONTINUOUS);
  held_ = false;
}

#elif defined(Q_OS_MACOS)

void SleepInhibitor::acquire() {
  IOPMAssertionID id = 0;
  if (IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn,
                                  CFSTR("Playing a Video CD"), &id) == kIOReturnSuccess) {
    assertionId_ = id;
    held_ = true;
  }
}

void SleepInhibitor::release() {
  if (!held_) return;
  IOPMAssertionRelease(assertionId_);
  assertionId_ = 0;
  held_ = false;
}

#else

void SleepInhibitor::acquire() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) return;

  QDBusMessage call = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
      QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("Inhibit"));
  call << QStringLiteral("VCD Player") << QString::fromLatin1(kReason);
  const QDBusReply<uint> cookie = bus.call(call, QDBus::Block, kDBusTimeoutMs);
  if (cookie.isValid()) {
    screenSaverCookie_ = cookie.value();
    held_ = true;
    return;
  }

  // No ScreenSaver service (GNOME, sandboxes): the portal. flags 8 = idle.
  call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
                                        QStringLiteral("/org/freedesktop/portal/desktop"),
                                        QStringLiteral("org.freedesktop.portal.Inhibit"), QStringLiteral("Inhibit"));
  call << QString() << uint{8} << QVariantMap{{QStringLiteral("reason"), QString::fromLatin1(kReason)}};
  const QDBusReply<QDBusObjectPath> request = bus.call(call, QDBus::Block, kDBusTimeoutMs);
  if (request.isValid()) {
    portalRequest_ = request.value().path();
    held_ = true;
  }
}

void SleepInhibitor::release() {
  if (!held_) return;
  held_ = false;
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!portalRequest_.isEmpty()) {
    bus.call(QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"), portalRequest_,
                                            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close")),
             QDBus::Block, kDBusTimeoutMs);
    portalRequest_.clear();
    return;
  }
  QDBusMessage call = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
      QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("UnInhibit"));
  call << screenSaverCookie_;
  bus.call(call, QDBus::Block, kDBusTimeoutMs);
  screenSaverCookie_ = 0;
}

#endif
