// Menus: context menus (the `contextMenu` view prop, ContextMenu) as
// GtkPopoverMenus, and the app's menu bar (MenuBar.setMenu) as a GMenu
// that GtkApplicationWindow shows, with accelerators that work while it is
// closed.
//
// Items come from js/menuItems.js, already numbered:
//   {id: '2/0', title, type: 'item' | 'checkbox' | 'radio' | 'separator',
//    disabled, checked, shortcut: 'Ctrl+Shift+S' or '<Control><Shift>s',
//    items: [...] (a submenu)}
// Separators split a menu into GMenu sections; radio items in one section
// form a group. Choosing an item calls back with its id; checkbox and radio
// items show the `checked` they were given (JS owns the state).
#pragma once

#include <ReactCommon/TurboModule.h>
#include <folly/dynamic.h>
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rngtk {

struct PlatformState;

using MenuSelect = std::function<void(const std::string &id)>;

// A GMenu for `items`, its actions added to `actions` as `<prefix>N`
// (named under `group` in detailed action names: "group.prefixN").
// `accels` collects (detailed action, accelerator) for items with shortcuts.
GMenu *build_menu(const folly::dynamic &items, GActionMap *actions, const std::string &group,
                  const std::string &prefix, const MenuSelect &onSelect,
                  std::vector<std::pair<std::string, std::string>> *accels);

// "Ctrl+Shift+S" (or GTK's "<Control><Shift>s") as a GTK accelerator, or ""
// if it isn't one.
std::string accelerator_for(const std::string &shortcut);

// Main thread: pops `items` up over `parent` at (x, y) in its coordinates,
// or pointing at `around` (in `parent`'s coordinates) when x < 0. The
// popover goes away when closed.
GtkWidget *popup_menu(GtkWidget *parent, double x, double y, const graphene_rect_t *around,
                      const folly::dynamic &items, MenuSelect onSelect);

// The MenuBar module (LinuxMenuBar), or null.
std::shared_ptr<facebook::react::TurboModule> makeMenuModule(
    const std::string &name, const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker,
    const std::shared_ptr<PlatformState> &state);

}  // namespace rngtk
