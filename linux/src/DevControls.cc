#include "DevControls.h"

#include <glog/logging.h>

#include "RNGtkHost.h"

namespace rngtk {
namespace {

void addAction(GActionMap *map, const char *name, RNGtkHost *host,
               void (*fn)(RNGtkHost *)) {
  GSimpleAction *action = g_simple_action_new(name, nullptr);
  g_object_set_data(G_OBJECT(action), "rngtk-host", host);
  g_signal_connect(action, "activate",
                   G_CALLBACK(+[](GSimpleAction *action, GVariant *, gpointer fn) {
                     auto *host = static_cast<RNGtkHost *>(
                         g_object_get_data(G_OBJECT(action), "rngtk-host"));
                     reinterpret_cast<void (*)(RNGtkHost *)>(fn)(host);
                   }),
                   reinterpret_cast<gpointer>(fn));
  g_action_map_add_action(map, G_ACTION(action));
  g_object_unref(action);
}

void addShortcut(GtkShortcutController *controller, const char *trigger,
                 const char *action) {
  gtk_shortcut_controller_add_shortcut(
      controller, gtk_shortcut_new(gtk_shortcut_trigger_parse_string(trigger),
                                   gtk_named_action_new(action)));
}

}  // namespace

void addDevControls(GtkWidget *window, RNGtkHost *host, bool verbose) {
  GSimpleActionGroup *group = g_simple_action_group_new();
  addAction(G_ACTION_MAP(group), "reload", host,
            [](RNGtkHost *host) { host->reload(); });
  addAction(G_ACTION_MAP(group), "open-debugger", host,
            [](RNGtkHost *host) { host->openDebugger(); });
  addAction(G_ACTION_MAP(group), "menu", host,
            [](RNGtkHost *host) { host->showDevMenu(); });
  gtk_widget_insert_action_group(window, "dev", G_ACTION_GROUP(group));
  g_object_unref(group);

  GtkEventController *controller = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller),
                                    GTK_SHORTCUT_SCOPE_GLOBAL);
  auto *shortcuts = GTK_SHORTCUT_CONTROLLER(controller);
  addShortcut(shortcuts, "<Control>r", "dev.reload");
  addShortcut(shortcuts, "<Control>d", "dev.menu");
  addShortcut(shortcuts, "<Control>m", "dev.menu");
  gtk_widget_add_controller(window, controller);

  if (verbose) {
    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed",
                     G_CALLBACK(+[](GtkEventControllerKey *, guint keyval,
                                    guint, GdkModifierType state, gpointer) {
                       LOG(INFO) << "key " << gdk_keyval_name(keyval)
                                 << " modifiers " << state;
                       return FALSE;
                     }),
                     nullptr);
    gtk_widget_add_controller(window, keys);
  }
}

}  // namespace rngtk
