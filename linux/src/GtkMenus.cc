#include "GtkMenus.h"

#include "rngtk/CxxModule.h"
#include "GtkMountingManager.h"
#include "PlatformModules.h"
#include "rn_text_input.h"

#include <glog/logging.h>
#include <react/renderer/components/view/ViewProps.h>

#include <atomic>
#include <cctype>
#include <cstring>

using namespace facebook::react;
using facebook::jsi::Runtime;

namespace rngtk {

namespace {

void on_main(std::function<void()> fn) {
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return G_SOURCE_REMOVE;
      },
      new std::function<void()>(std::move(fn)),
      [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}

std::string str(const folly::dynamic &d, const char *key) {
  if (!d.isObject()) return "";
  auto it = d.find(key);
  return it != d.items().end() && it->second.isString() ? it->second.getString() : "";
}

bool flag(const folly::dynamic &d, const char *key) {
  if (!d.isObject()) return false;
  auto it = d.find(key);
  return it != d.items().end() && it->second.isBool() && it->second.getBool();
}

// An action that calls onSelect: with its item's id, or (radio) with the
// id it is activated for.
GSimpleAction *select_action(const std::string &name, const GVariantType *parameter,
                             GVariant *state, const std::string &id, const MenuSelect &onSelect) {
  GSimpleAction *action = state ? g_simple_action_new_stateful(name.c_str(), parameter, state)
                                : g_simple_action_new(name.c_str(), parameter);
  struct Data {
    std::string id;
    MenuSelect onSelect;
  };
  g_signal_connect_data(
      action, "activate",
      G_CALLBACK(+[](GSimpleAction *, GVariant *parameter, gpointer p) {
        auto *d = static_cast<Data *>(p);
        if (!d->onSelect) return;
        if (parameter && g_variant_is_of_type(parameter, G_VARIANT_TYPE_STRING)) {
          d->onSelect(g_variant_get_string(parameter, nullptr));
        } else {
          d->onSelect(d->id);
        }
      }),
      new Data{id, onSelect}, +[](gpointer p, GClosure *) { delete static_cast<Data *>(p); },
      GConnectFlags(0));
  return action;
}

struct Builder {
  GActionMap *actions;
  std::string group, prefix;
  const MenuSelect &onSelect;
  std::vector<std::pair<std::string, std::string>> *accels;
  int next = 0;

  std::string detailed(const std::string &name, GVariant *target) {
    std::string full = group + "." + name;
    if (!target) return full;
    gchar *s = g_action_print_detailed_name(full.c_str(), target);
    std::string out = s;
    g_free(s);
    return out;
  }

  GMenu *build(const folly::dynamic &items) {
    GMenu *menu = g_menu_new();
    GMenu *section = g_menu_new();
    // The radio group of this section: one action, its state the id of
    // the checked item.
    GSimpleAction *radio = nullptr;
    auto endSection = [&] {
      if (g_menu_model_get_n_items(G_MENU_MODEL(section)) > 0) {
        g_menu_append_section(menu, nullptr, G_MENU_MODEL(section));
      }
      g_object_unref(section);
      section = g_menu_new();
      radio = nullptr;
    };
    if (items.isArray()) {
      for (const auto &item : items) {
        std::string type = str(item, "type");
        if (type == "separator") {
          endSection();
          continue;
        }
        std::string id = str(item, "id"), title = str(item, "title");
        GMenuItem *entry = nullptr;
        if (item.isObject() && item.count("items") && item["items"].isArray()) {
          GMenu *submenu = build(item["items"]);
          entry = g_menu_item_new_submenu(title.c_str(), G_MENU_MODEL(submenu));
          g_object_unref(submenu);
        } else {
          std::string name;
          GVariant *target = nullptr;
          GSimpleAction *action = nullptr;
          if (type == "radio") {
            if (!radio) {
              name = prefix + std::to_string(next++);
              radio = select_action(name, G_VARIANT_TYPE_STRING, g_variant_new_string(""), id,
                                    onSelect);
              g_action_map_add_action(actions, G_ACTION(radio));
              g_object_unref(radio);
            }
            name = g_action_get_name(G_ACTION(radio));
            if (flag(item, "checked")) g_simple_action_set_state(radio, g_variant_new_string(id.c_str()));
            target = g_variant_ref_sink(g_variant_new_string(id.c_str()));
            // A disabled radio item can't be told apart from its group's
            // action: GTK shows the group's sensitivity.
          } else {
            name = prefix + std::to_string(next++);
            action = select_action(name, nullptr,
                                   type == "checkbox" ? g_variant_new_boolean(flag(item, "checked"))
                                                      : nullptr,
                                   id, onSelect);
            g_simple_action_set_enabled(action, !flag(item, "disabled"));
            g_action_map_add_action(actions, G_ACTION(action));
            g_object_unref(action);
          }
          std::string full = detailed(name, target);
          entry = g_menu_item_new(title.c_str(), full.c_str());
          std::string accel = accelerator_for(str(item, "shortcut"));
          if (!accel.empty()) {
            g_menu_item_set_attribute(entry, "accel", "s", accel.c_str());
            if (accels) accels->emplace_back(full, accel);
          }
          if (target) g_variant_unref(target);
        }
        g_menu_append_item(section, entry);
        g_object_unref(entry);
      }
    }
    endSection();
    g_object_unref(section);
    return menu;
  }
};

}  // namespace

std::string accelerator_for(const std::string &shortcut) {
  if (shortcut.empty()) return "";
  std::string accel;
  if (shortcut.find('<') != std::string::npos) {
    accel = shortcut;
  } else {
    // "Ctrl+Shift+S", "Alt+F4", "Ctrl+Plus": modifiers, then the key.
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= shortcut.size(); i++) {
      // A lone '+' as the key ("Ctrl++").
      if (i == shortcut.size() || (shortcut[i] == '+' && i > start)) {
        parts.push_back(shortcut.substr(start, i - start));
        start = i + 1;
      }
    }
    if (parts.empty()) return "";
    std::string key = parts.back();
    parts.pop_back();
    for (auto mod : parts) {
      for (auto &c : mod) c = char(std::tolower(c));
      if (mod == "ctrl" || mod == "control" || mod == "cmd" || mod == "cmdorctrl" ||
          mod == "commandorcontrol" || mod == "primary") {
        accel += "<Control>";
      } else if (mod == "shift") {
        accel += "<Shift>";
      } else if (mod == "alt" || mod == "option") {
        accel += "<Alt>";
      } else if (mod == "super" || mod == "meta" || mod == "win") {
        accel += "<Super>";
      } else {
        return "";
      }
    }
    static const std::pair<const char *, const char *> names[] = {
        {"enter", "Return"},   {"return", "Return"},     {"esc", "Escape"},
        {"escape", "Escape"},  {"space", "space"},       {"tab", "Tab"},
        {"backspace", "BackSpace"}, {"delete", "Delete"}, {"del", "Delete"},
        {"insert", "Insert"},  {"home", "Home"},         {"end", "End"},
        {"pageup", "Page_Up"}, {"pagedown", "Page_Down"}, {"up", "Up"},
        {"down", "Down"},      {"left", "Left"},         {"right", "Right"},
        {"plus", "plus"},      {"+", "plus"},            {"minus", "minus"},
        {"-", "minus"},        {",", "comma"},           {".", "period"},
        {"/", "slash"},        {"=", "equal"},
    };
    std::string lower = key;
    for (auto &c : lower) c = char(std::tolower(c));
    std::string name;
    for (const auto &[from, to] : names) {
      if (lower == from) name = to;
    }
    if (name.empty()) name = key.size() == 1 ? lower : key;
    accel += name;
  }
  guint keyval = 0;
  GdkModifierType mods{};
  if (!gtk_accelerator_parse(accel.c_str(), &keyval, &mods) || keyval == 0) {
    LOG(WARNING) << "Menu: not a shortcut: " << shortcut;
    return "";
  }
  gchar *normalized = gtk_accelerator_name(keyval, mods);
  std::string out = normalized;
  g_free(normalized);
  return out;
}

GMenu *build_menu(const folly::dynamic &items, GActionMap *actions, const std::string &group,
                  const std::string &prefix, const MenuSelect &onSelect,
                  std::vector<std::pair<std::string, std::string>> *accels) {
  Builder builder{actions, group, prefix, onSelect, accels};
  return builder.build(items);
}

GtkWidget *popup_menu(GtkWidget *parent, double x, double y, const graphene_rect_t *around,
                      const folly::dynamic &items, MenuSelect onSelect) {
  GSimpleActionGroup *group = g_simple_action_group_new();
  GMenu *menu = build_menu(items, G_ACTION_MAP(group), "rngtk-menu", "item", onSelect, nullptr);
  GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
  g_object_unref(menu);
  gtk_widget_insert_action_group(popover, "rngtk-menu", G_ACTION_GROUP(group));
  // (Tests read the actions' states here; GTK has no getter.)
  g_object_set_data_full(G_OBJECT(popover), "rngtk-menu-actions", group, g_object_unref);
  gtk_widget_add_css_class(popover, "context-menu");
  // RNView presents popovers parented to it (see rn_view_size_allocate).
  gtk_widget_set_parent(popover, parent);
  GdkRectangle at = x >= 0 ? GdkRectangle{int(x), int(y), 1, 1}
                    : around ? GdkRectangle{int(around->origin.x), int(around->origin.y),
                                            std::max(1, int(around->size.width)),
                                            std::max(1, int(around->size.height))}
                             : GdkRectangle{0, 0, 1, 1};
  gtk_popover_set_pointing_to(GTK_POPOVER(popover), &at);
  if (x >= 0) {
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
  }
  g_signal_connect(popover, "closed", G_CALLBACK(+[](GtkPopover *p, gpointer) {
                     // Unparent once GTK is done with the close (an item's
                     // action runs after "closed").
                     g_idle_add_full(
                         G_PRIORITY_DEFAULT_IDLE,
                         [](gpointer w) -> gboolean {
                           if (gtk_widget_get_parent(GTK_WIDGET(w))) {
                             gtk_widget_unparent(GTK_WIDGET(w));
                           }
                           return G_SOURCE_REMOVE;
                         },
                         g_object_ref(p), g_object_unref);
                   }),
                   nullptr);
  gtk_popover_popup(GTK_POPOVER(popover));
  return popover;
}

// ---- Context menus -------------------------------------------------------------

bool GtkMountingManager::showContextMenu(GtkWidget *root, GtkWidget *widget, double x, double y) {
  bool deepest = true;
  for (GtkWidget *w = widget; w; w = gtk_widget_get_parent(w)) {
    EventTarget target = targetForView(w);
    if (target.tag != 0) {
      auto props = std::dynamic_pointer_cast<const HostPlatformViewProps>(propsForTag(target.tag));
      if (props && !props->contextMenu.empty() && target.emitter) {
        graphene_rect_t bounds{};
        if (x < 0 && !gtk_widget_compute_bounds(w, root, &bounds)) return false;
        if (contextMenu_) gtk_popover_popdown(GTK_POPOVER(contextMenu_));
        SharedEventEmitter emitter = target.emitter;
        contextMenu_ = popup_menu(root, x, y, &bounds, props->contextMenu.items,
                                  [emitter](const std::string &id) {
                                    emitter->dispatchEvent("contextMenuSelect",
                                                           folly::dynamic::object("id", id),
                                                           RawEvent::Category::Discrete);
                                  });
        g_object_add_weak_pointer(G_OBJECT(contextMenu_), reinterpret_cast<gpointer *>(&contextMenu_));
        return true;
      }
      // Text fields and selectable text have menus of their own (GTK's,
      // and Copy / Select All).
      if (deepest && (RN_IS_TEXT_INPUT(w) || isSelectableText(target.tag))) return false;
      deepest = false;
    }
    if (w == root) break;
  }
  return false;
}

// ---- MenuBar ----------------------------------------------------------------

namespace {

// The menu bar the app has now: its actions and accelerators on the
// GtkApplication, and which JS instance's module set it.
struct MenuBarState {
  int owner = 0;
  std::vector<std::string> actions;
  std::vector<std::string> accelActions;
};
MenuBarState &menubar() {
  static MenuBarState state;
  return state;
}
std::atomic<int> nextOwner{1};

// GtkApplicationWindow takes the app's menu bar when it is realized (or
// gtk-shell-shows-menubar changes), not when the app's changes later. So
// the app keeps one GMenu, whose items change, and windows show or hide it.
GMenu *&menubar_root_menu() {
  static GMenu *root = nullptr;
  return root;
}

bool menubar_has_items_impl() {
  GMenu *root = menubar_root_menu();
  return root && g_menu_model_get_n_items(G_MENU_MODEL(root)) > 0;
}

GMenu *menubar_root(GtkApplication *app) {
  GMenu *&root = menubar_root_menu();
  if (!root) root = g_menu_new();
  if (gtk_application_get_menubar(app) != G_MENU_MODEL(root)) {
    gtk_application_set_menubar(app, G_MENU_MODEL(root));
    // Windows realized before: have them look again.
    GdkDisplay *display = gdk_display_get_default();
    g_object_notify(G_OBJECT(gtk_settings_get_for_display(display)), "gtk-shell-shows-menubar");
  }
  return root;
}

void set_menubar(GtkApplication *app, int owner, const folly::dynamic &items,
                 MenuSelect onSelect) {
  // (Not while the app shuts down: it can't take a menu bar then.)
  if (!g_application_get_is_registered(G_APPLICATION(app))) return;
  MenuBarState &state = menubar();
  for (const auto &name : state.actions) {
    g_action_map_remove_action(G_ACTION_MAP(app), name.c_str());
  }
  const char *none[] = {nullptr};
  for (const auto &action : state.accelActions) {
    gtk_application_set_accels_for_action(app, action.c_str(), none);
  }
  state = MenuBarState{owner};
  bool empty = !items.isArray() || items.empty();
  GMenu *root = menubar_root(app);
  g_menu_remove_all(root);
  if (!empty) {
    // The actions go on a group of their own, then onto the app ("app.").
    GSimpleActionGroup *group = g_simple_action_group_new();
    std::vector<std::pair<std::string, std::string>> accels;
    GMenu *model =
        build_menu(items, G_ACTION_MAP(group), "app", "rngtk-menubar-", onSelect, &accels);
    gchar **names = g_action_group_list_actions(G_ACTION_GROUP(group));
    for (gchar **n = names; *n; n++) {
      g_action_map_add_action(G_ACTION_MAP(app),
                              g_action_map_lookup_action(G_ACTION_MAP(group), *n));
      state.actions.push_back(*n);
    }
    g_strfreev(names);
    g_object_unref(group);
    for (const auto &[action, accel] : accels) {
      const char *list[] = {accel.c_str(), nullptr};
      gtk_application_set_accels_for_action(app, action.c_str(), list);
      state.accelActions.push_back(action);
    }
    // The menus are the model's one section's items.
    GMenuModel *section = g_menu_model_get_item_link(G_MENU_MODEL(model), 0, G_MENU_LINK_SECTION);
    for (int i = 0; section && i < g_menu_model_get_n_items(section); i++) {
      GMenuItem *item = g_menu_item_new_from_model(section, i);
      g_menu_append_item(root, item);
      g_object_unref(item);
    }
    g_clear_object(&section);
    g_object_unref(model);
  }
  for (GList *l = gtk_application_get_windows(app); l; l = l->next) {
    if (GTK_IS_APPLICATION_WINDOW(l->data)) {
      gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(l->data), !empty);
    }
  }
}

class MenuBarModule : public CxxModule<MenuBarModule> {
 public:
  MenuBarModule(std::shared_ptr<CallInvoker> jsInvoker, std::shared_ptr<PlatformState> state)
      : CxxModule("LinuxMenuBar", std::move(jsInvoker)), state_(std::move(state)) {
    method<&MenuBarModule::setMenu>("setMenu");
  }

  // A reload: the old instance's menu bar goes, unless the new one has set
  // its own since.
  ~MenuBarModule() override {
    on_main([state = state_, owner = owner_] {
      GtkWindow *window = state->window ? state->window() : nullptr;
      GtkApplication *app = window ? gtk_window_get_application(window) : nullptr;
      if (app && owner && menubar().owner == owner) {
        set_menubar(app, 0, folly::dynamic::array(), nullptr);
      }
    });
  }

  // items: the menus, each {title, items}; onSelect(id) for every choice.
  void setMenu(Runtime &, folly::dynamic items, AsyncCallback<std::string> onSelect) {
    if (!owner_) owner_ = nextOwner++;
    on_main([state = state_, owner = owner_, items = std::move(items), onSelect] {
      GtkWindow *window = state->window ? state->window() : nullptr;
      GtkApplication *app = window ? gtk_window_get_application(window) : nullptr;
      if (!app) {
        LOG(WARNING) << "MenuBar: no GtkApplication";
        return;
      }
      set_menubar(app, owner, items, [onSelect](const std::string &id) {
        auto callback = onSelect;
        callback.call(id);
      });
    });
  }

 private:
  std::shared_ptr<PlatformState> state_;
  int owner_ = 0;
};

}  // namespace

bool menubar_has_items() { return menubar_has_items_impl(); }

std::shared_ptr<TurboModule> makeMenuModule(const std::string &name,
                                            const std::shared_ptr<CallInvoker> &jsInvoker,
                                            const std::shared_ptr<PlatformState> &state) {
  if (name == "LinuxMenuBar") return std::make_shared<MenuBarModule>(jsInvoker, state);
  return nullptr;
}

}  // namespace rngtk
