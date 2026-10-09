#include "Dialogs.h"

#include "rngtk/CxxModule.h"
#include "PlatformModules.h"

#include <glog/logging.h>
#include <react/bridging/Promise.h>

#include <vector>

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

std::string str(const folly::dynamic &d, const char *key, const std::string &fallback = "") {
  auto it = d.isObject() ? d.find(key) : d.items().end();
  return d.isObject() && it != d.items().end() && it->second.isString() ? it->second.getString()
                                                                       : fallback;
}

bool flag(const folly::dynamic &d, const char *key) {
  if (!d.isObject()) return false;
  auto it = d.find(key);
  return it != d.items().end() && it->second.isBool() && it->second.getBool();
}

// GTK's own translations of the stock labels ("_OK" -> "_Aceptar").
const char *gtk_label(const char *label) { return g_dgettext("gtk40", label); }

// ---- Alert -------------------------------------------------------------------

// GtkMessageDialog is deprecated, but it is what GtkAlertDialog shows in
// GTK 4.14, and unlike GtkAlertDialog it takes text fields, styled buttons
// and a say in whether Escape closes it.
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

struct Alert {
  AsyncCallback<int, folly::dynamic> callback;
  GtkWidget *dialog = nullptr;
  GtkWidget *login = nullptr, *password = nullptr, *text = nullptr;
  int cancelIndex = -1;
  bool cancelable = false;
  bool done = false;
};

void finish_alert(Alert *alert, int id) {
  if (alert->done) return;
  alert->done = true;
  folly::dynamic value = nullptr;
  if (alert->login) {
    value = folly::dynamic::object("login", gtk_editable_get_text(GTK_EDITABLE(alert->login)))(
        "password", gtk_editable_get_text(GTK_EDITABLE(alert->password)));
  } else if (alert->text) {
    value = gtk_editable_get_text(GTK_EDITABLE(alert->text));
  }
  alert->callback.call(id, std::move(value));
  gtk_window_destroy(GTK_WINDOW(alert->dialog));
}

GtkInputPurpose purpose_for(const std::string &keyboardType) {
  if (keyboardType == "email-address") return GTK_INPUT_PURPOSE_EMAIL;
  if (keyboardType == "number-pad") return GTK_INPUT_PURPOSE_DIGITS;
  if (keyboardType == "numeric" || keyboardType == "decimal-pad") return GTK_INPUT_PURPOSE_NUMBER;
  if (keyboardType == "phone-pad") return GTK_INPUT_PURPOSE_PHONE;
  if (keyboardType == "url") return GTK_INPUT_PURPOSE_URL;
  return GTK_INPUT_PURPOSE_FREE_FORM;
}

GtkWidget *add_field(GtkWidget *box, bool secret, const std::string &placeholder,
                     const std::string &value) {
  GtkWidget *field = secret ? gtk_password_entry_new() : gtk_entry_new();
  if (secret) {
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(field), TRUE);
    g_object_set(field, "activates-default", TRUE, "placeholder-text", placeholder.c_str(),
                 nullptr);
  } else {
    gtk_entry_set_activates_default(GTK_ENTRY(field), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(field), placeholder.c_str());
  }
  gtk_editable_set_text(GTK_EDITABLE(field), value.c_str());
  gtk_accessible_update_property(GTK_ACCESSIBLE(field), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                 placeholder.c_str(), -1);
  gtk_box_append(GTK_BOX(box), field);
  return field;
}

// args: {title, message, buttons: [{text, style, isPreferred}], type,
// defaultValue, keyboardType, cancelable}. The callback gets the button's
// index, or -1 when the alert was dismissed without one (cancelable).
void show_alert(GtkWindow *parent, const folly::dynamic &args,
                AsyncCallback<int, folly::dynamic> callback) {
  std::string title = str(args, "title"), message = str(args, "message");
  std::string primary = title.empty() ? message : title;
  std::string secondary = title.empty() ? "" : message;
  // An alert dialog to screen readers (GtkMessageDialog says "dialog"),
  // which Orca reads in full when it opens.
  GtkWidget *dialog = GTK_WIDGET(g_object_new(
      GTK_TYPE_MESSAGE_DIALOG, "accessible-role", GTK_ACCESSIBLE_ROLE_ALERT_DIALOG,
      "message-type", GTK_MESSAGE_OTHER, "buttons", GTK_BUTTONS_NONE, "text", primary.c_str(),
      "modal", TRUE, "destroy-with-parent", TRUE, "transient-for", parent, nullptr));
  if (!secondary.empty()) {
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
                                             secondary.c_str());
  }
  gtk_accessible_update_property(GTK_ACCESSIBLE(dialog), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                 primary.c_str(), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                 secondary.c_str(), -1);
  if (GtkApplication *app = parent ? gtk_window_get_application(parent) : nullptr) {
    gtk_window_set_application(GTK_WINDOW(dialog), app);
  }

  auto *alert = new Alert{std::move(callback)};
  alert->dialog = dialog;
  alert->cancelable = flag(args, "cancelable");
  g_object_set_data_full(G_OBJECT(dialog), "rngtk-alert", alert,
                         [](gpointer p) { delete static_cast<Alert *>(p); });

  std::string type = str(args, "type", "default");
  std::string value = str(args, "defaultValue");
  GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
  if (type == "plain-text" || type == "secure-text") {
    alert->text = add_field(area, type == "secure-text", "", value);
    if (type == "plain-text") {
      gtk_entry_set_input_purpose(GTK_ENTRY(alert->text),
                                  purpose_for(str(args, "keyboardType")));
    }
  } else if (type == "login-password") {
    alert->login = add_field(area, false, g_dgettext("gtk40", "Username"), value);
    alert->password = add_field(area, true, g_dgettext("gtk40", "Password"), "");
  }

  // Buttons: the app's order, with the cancel one first (GNOME puts it on
  // the left); none given is one OK.
  folly::dynamic buttons = args.isObject() && args.count("buttons") && args["buttons"].isArray()
                               ? args["buttons"]
                               : folly::dynamic::array();
  if (buttons.empty()) buttons.push_back(folly::dynamic::object("text", ""));
  std::vector<int> order;
  for (int i = 0; i < int(buttons.size()); i++) {
    if (str(buttons[i], "style") == "cancel" && alert->cancelIndex < 0) {
      alert->cancelIndex = i;
      order.insert(order.begin(), i);
    } else {
      order.push_back(i);
    }
  }
  int preferred = -1;
  for (int i = 0; i < int(buttons.size()); i++) {
    if (flag(buttons[i], "isPreferred")) preferred = i;
  }
  if (preferred < 0) {
    // The last button that neither cancels nor destroys.
    for (int i = int(buttons.size()) - 1; i >= 0 && preferred < 0; i--) {
      std::string style = str(buttons[i], "style");
      if (style != "cancel" && style != "destructive") preferred = i;
    }
  }
  for (int i : order) {
    std::string style = str(buttons[i], "style");
    std::string text = str(buttons[i], "text");
    bool stock = text.empty();
    if (stock) text = gtk_label(style == "cancel" ? "_Cancel" : "_OK");
    GtkWidget *button = gtk_dialog_add_button(GTK_DIALOG(dialog), text.c_str(), i);
    gtk_button_set_use_underline(GTK_BUTTON(button), stock);
    if (style == "destructive") {
      gtk_widget_add_css_class(button, "destructive-action");
    } else if (i == preferred && buttons.size() > 1) {
      gtk_widget_add_css_class(button, "suggested-action");
    }
  }
  if (preferred >= 0) gtk_dialog_set_default_response(GTK_DIALOG(dialog), preferred);

  g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog *d, int response, gpointer) {
                     auto *alert = static_cast<Alert *>(g_object_get_data(G_OBJECT(d), "rngtk-alert"));
                     if (response >= 0) finish_alert(alert, response);
                   }),
                   nullptr);
  // Escape and the close button: the cancel button if there is one, else
  // a dismissal if the alert is cancelable; otherwise it stays.
  g_signal_connect(dialog, "close-request", G_CALLBACK(+[](GtkWindow *w, gpointer) -> gboolean {
                     auto *alert = static_cast<Alert *>(g_object_get_data(G_OBJECT(w), "rngtk-alert"));
                     if (alert->cancelIndex >= 0) {
                       finish_alert(alert, alert->cancelIndex);
                     } else if (alert->cancelable) {
                       finish_alert(alert, -1);
                     }
                     return TRUE;
                   }),
                   nullptr);
  gtk_window_present(GTK_WINDOW(dialog));
  if (alert->text || alert->login) gtk_widget_grab_focus(alert->text ? alert->text : alert->login);
}

G_GNUC_END_IGNORE_DEPRECATIONS

class AlertManagerModule : public CxxModule<AlertManagerModule> {
 public:
  AlertManagerModule(std::shared_ptr<CallInvoker> jsInvoker, std::shared_ptr<PlatformState> state)
      : CxxModule("AlertManager", std::move(jsInvoker)), state_(std::move(state)) {
    method<&AlertManagerModule::alertWithArgs>("alertWithArgs");
  }

  void alertWithArgs(Runtime &, folly::dynamic args, AsyncCallback<int, folly::dynamic> callback) {
    on_main([state = state_, args = std::move(args), callback = std::move(callback)] {
      show_alert(dialogParent(*state), args, callback);
    });
  }

 private:
  std::shared_ptr<PlatformState> state_;
};

// ---- File dialogs -----------------------------------------------------------

// options.filters: [{name, extensions: ['png'], mimeTypes: ['image/*'],
// patterns: ['*.tar.gz']}]; the first is selected.
void set_filters(GtkFileDialog *dialog, const folly::dynamic &options) {
  if (!options.isObject() || !options.count("filters") || !options["filters"].isArray()) return;
  GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
  for (const auto &f : options["filters"]) {
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, str(f, "name").c_str());
    auto each = [&](const char *key, void (*add)(GtkFileFilter *, const char *)) {
      if (f.isObject() && f.count(key) && f[key].isArray()) {
        for (const auto &v : f[key]) {
          if (v.isString()) add(filter, v.getString().c_str());
        }
      }
    };
    each("extensions", [](GtkFileFilter *ff, const char *ext) {
      gtk_file_filter_add_suffix(ff, ext[0] == '.' ? ext + 1 : ext);
    });
    each("mimeTypes", gtk_file_filter_add_mime_type);
    each("patterns", gtk_file_filter_add_pattern);
    g_list_store_append(store, filter);
    if (g_list_model_get_n_items(G_LIST_MODEL(store)) == 1) {
      gtk_file_dialog_set_default_filter(dialog, filter);
    }
    g_object_unref(filter);
  }
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(store));
  g_object_unref(store);
}

std::string path_of(GFile *file) {
  char *path = g_file_get_path(file);
  std::string s;
  if (path) {
    s = path;
  } else {
    char *uri = g_file_get_uri(file);
    s = uri ? uri : "";
    g_free(uri);
  }
  g_free(path);
  return s;
}

enum class FileOp { Open, OpenMultiple, Save, Folder, Folders };

struct FileRequest {
  AsyncPromise<folly::dynamic> promise;
  FileOp op;
};

void file_dialog_done(GObject *source, GAsyncResult *result, gpointer data) {
  auto *request = static_cast<FileRequest *>(data);
  auto *dialog = GTK_FILE_DIALOG(source);
  GError *error = nullptr;
  folly::dynamic value = nullptr;
  switch (request->op) {
    case FileOp::Open:
    case FileOp::Save:
    case FileOp::Folder: {
      GFile *file = request->op == FileOp::Open   ? gtk_file_dialog_open_finish(dialog, result, &error)
                    : request->op == FileOp::Save ? gtk_file_dialog_save_finish(dialog, result, &error)
                                                  : gtk_file_dialog_select_folder_finish(dialog, result, &error);
      if (file) {
        value = request->op == FileOp::Save ? folly::dynamic(path_of(file))
                                            : folly::dynamic::array(path_of(file));
        g_object_unref(file);
      }
      break;
    }
    case FileOp::OpenMultiple:
    case FileOp::Folders: {
      GListModel *files = request->op == FileOp::OpenMultiple
                              ? gtk_file_dialog_open_multiple_finish(dialog, result, &error)
                              : gtk_file_dialog_select_multiple_folders_finish(dialog, result, &error);
      if (files) {
        value = folly::dynamic::array();
        for (guint i = 0; i < g_list_model_get_n_items(files); i++) {
          auto *file = G_FILE(g_list_model_get_item(files, i));
          value.push_back(path_of(file));
          g_object_unref(file);
        }
        g_object_unref(files);
      }
      break;
    }
  }
  if (error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
      !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED)) {
    request->promise.reject(Error(error->message));
  } else {
    // Cancelled: no paths (open, folders) or null (save).
    if (value.isNull() && request->op != FileOp::Save) value = folly::dynamic::array();
    request->promise.resolve(std::move(value));
  }
  g_clear_error(&error);
  g_object_unref(dialog);
  delete request;
}

void show_file_dialog(GtkWindow *parent, FileOp op, const folly::dynamic &options,
                      AsyncPromise<folly::dynamic> promise) {
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_modal(dialog, TRUE);
  std::string title = str(options, "title");
  if (!title.empty()) gtk_file_dialog_set_title(dialog, title.c_str());
  std::string accept = str(options, "buttonLabel");
  if (!accept.empty()) gtk_file_dialog_set_accept_label(dialog, accept.c_str());
  set_filters(dialog, options);
  std::string defaultPath = str(options, "defaultPath");
  if (!defaultPath.empty()) {
    GFile *file = g_file_new_for_commandline_arg(defaultPath.c_str());
    if (g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, nullptr) == G_FILE_TYPE_DIRECTORY) {
      gtk_file_dialog_set_initial_folder(dialog, file);
    } else if (op == FileOp::Save) {
      // A file to save as: its folder, and its name.
      GFile *folder = g_file_get_parent(file);
      if (folder) gtk_file_dialog_set_initial_folder(dialog, folder);
      g_clear_object(&folder);
      char *name = g_file_get_basename(file);
      gtk_file_dialog_set_initial_name(dialog, name);
      g_free(name);
    } else {
      gtk_file_dialog_set_initial_file(dialog, file);
    }
    g_object_unref(file);
  }
  std::string defaultName = str(options, "defaultName");
  if (op == FileOp::Save && !defaultName.empty()) {
    gtk_file_dialog_set_initial_name(dialog, defaultName.c_str());
  }
  auto *request = new FileRequest{std::move(promise), op};
  switch (op) {
    case FileOp::Open: gtk_file_dialog_open(dialog, parent, nullptr, file_dialog_done, request); break;
    case FileOp::OpenMultiple:
      gtk_file_dialog_open_multiple(dialog, parent, nullptr, file_dialog_done, request);
      break;
    case FileOp::Save: gtk_file_dialog_save(dialog, parent, nullptr, file_dialog_done, request); break;
    case FileOp::Folder:
      gtk_file_dialog_select_folder(dialog, parent, nullptr, file_dialog_done, request);
      break;
    case FileOp::Folders:
      gtk_file_dialog_select_multiple_folders(dialog, parent, nullptr, file_dialog_done, request);
      break;
  }
}

class DialogsModule : public CxxModule<DialogsModule> {
 public:
  DialogsModule(std::shared_ptr<CallInvoker> jsInvoker, std::shared_ptr<PlatformState> state)
      : CxxModule("LinuxDialogs", std::move(jsInvoker)), state_(std::move(state)) {
    method<&DialogsModule::openFile>("openFile");
    method<&DialogsModule::saveFile>("saveFile");
    method<&DialogsModule::openFolder>("openFolder");
  }

  AsyncPromise<folly::dynamic> openFile(Runtime &rt, folly::dynamic options) {
    return show(rt, flag(options, "multiple") ? FileOp::OpenMultiple : FileOp::Open, options);
  }
  AsyncPromise<folly::dynamic> saveFile(Runtime &rt, folly::dynamic options) {
    return show(rt, FileOp::Save, options);
  }
  AsyncPromise<folly::dynamic> openFolder(Runtime &rt, folly::dynamic options) {
    return show(rt, flag(options, "multiple") ? FileOp::Folders : FileOp::Folder, options);
  }

 private:
  AsyncPromise<folly::dynamic> show(Runtime &rt, FileOp op, folly::dynamic options) {
    AsyncPromise<folly::dynamic> promise(rt, jsInvoker_);
    on_main([state = state_, op, options = std::move(options), promise] {
      show_file_dialog(dialogParent(*state), op, options, promise);
    });
    return promise;
  }

  std::shared_ptr<PlatformState> state_;
};

}  // namespace

GtkWindow *dialogParent(const PlatformState &state) {
  GtkWindow *main = state.window ? state.window() : nullptr;
  GtkApplication *app = main ? gtk_window_get_application(main) : nullptr;
  // The app's active window (its windows are listed most recently focused
  // first), or any toplevel of ours that is active, e.g. a Modal's.
  if (app) {
    for (GList *l = gtk_application_get_windows(app); l; l = l->next) {
      GtkWindow *w = GTK_WINDOW(l->data);
      if (gtk_window_is_active(w) && gtk_widget_get_visible(GTK_WIDGET(w))) return w;
    }
  }
  GListModel *toplevels = gtk_window_get_toplevels();
  GtkWindow *active = nullptr;
  for (guint i = 0; i < g_list_model_get_n_items(toplevels) && !active; i++) {
    auto *w = GTK_WINDOW(g_list_model_get_item(toplevels, i));
    if (gtk_window_is_active(w) && gtk_widget_get_visible(GTK_WIDGET(w))) active = w;
    g_object_unref(w);
  }
  if (active) return active;
  if (app) {
    // Not active (another app is): the most recently focused visible one.
    for (GList *l = gtk_application_get_windows(app); l; l = l->next) {
      if (gtk_widget_get_visible(GTK_WIDGET(l->data))) return GTK_WINDOW(l->data);
    }
  }
  return main;
}

std::shared_ptr<TurboModule> makeDialogModule(const std::string &name,
                                              const std::shared_ptr<CallInvoker> &jsInvoker,
                                              const std::shared_ptr<PlatformState> &state) {
  if (name == "AlertManager") return std::make_shared<AlertManagerModule>(jsInvoker, state);
  if (name == "LinuxDialogs") return std::make_shared<DialogsModule>(jsInvoker, state);
  return nullptr;
}

}  // namespace rngtk
