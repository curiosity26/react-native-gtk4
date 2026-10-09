// Notifications (LinuxNotifications, js/Notifications.js): desktop
// notifications on GNotification, with a title, body, icon, priority and
// buttons. Clicking one raises the app's window and tells JS
// ('rngtkNotification': {id, action}, action 'default' for the
// notification itself or the button's id).
//
// GNOME shows GNotifications (org.gtk.Notifications) only for apps it knows,
// i.e. with an installed .desktop file named after the app id; an app
// without one (a development build) goes through the freedesktop
// notification server instead, which every desktop has. In a sandbox the
// notification portal does it.
#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>
#include <string>

namespace rngtk {

struct PlatformState;

// The module named `name`, or null.
std::shared_ptr<facebook::react::TurboModule> makeNotificationsModule(
    const std::string &name, const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker,
    const std::shared_ptr<PlatformState> &state);

}  // namespace rngtk
