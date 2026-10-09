// Drag and drop, with react-native-macos' props and events:
//
// - Dropping in: a view's draggedTypes say what it takes: 'fileUrl' (files
//   and links: a URI list), 'string' (text), 'image' (image data). A drag
//   over it sends onDragEnter, leaving it onDragLeave, and dropping onDrop,
//   each with {clientX, clientY, dataTransfer: {files, items, types}}: files
//   [{name, type, uri, size}] (an image dropped as data is saved to a PNG
//   in the user's cache, with width and height), items [{kind, type}], the
//   MIME types; Linux adds `text` and `urls` (links that aren't files). The
//   nearest view under the pointer that takes one of the kinds gets it; a
//   TextInput takes text drops itself (GTK's).
// - Dragging out: the selected text of selectable Text (a press inside the
//   selection, then a drag), and an Image with `draggable`: its picture,
//   plus its file (a local image) or its URL.
//
// One GtkDropTargetAsync and one GtkDragSource per root (a window's, a
// Modal's).
#include "GtkPointerHandler.h"

#include "GtkMountingManager.h"
#include "rn_text.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/view/ViewProps.h>

#include <algorithm>
#include <atomic>
#include <unistd.h>

using namespace facebook::react;

namespace rngtk {

namespace {

bool contains(const std::vector<std::string> &list, const std::string &item) {
  return std::find(list.begin(), list.end(), item) != list.end();
}

// What a drag offers, in draggedTypes' terms.
std::vector<std::string> kinds_of(GdkContentFormats *formats) {
  std::vector<std::string> kinds;
  if (gdk_content_formats_contain_gtype(formats, GDK_TYPE_FILE_LIST) ||
      gdk_content_formats_contain_mime_type(formats, "text/uri-list")) {
    kinds.push_back("fileUrl");
  }
  if (gdk_content_formats_contain_gtype(formats, G_TYPE_STRING) ||
      gdk_content_formats_contain_mime_type(formats, "text/plain") ||
      gdk_content_formats_contain_mime_type(formats, "text/plain;charset=utf-8")) {
    kinds.push_back("string");
  }
  bool image = gdk_content_formats_contain_gtype(formats, GDK_TYPE_TEXTURE);
  gsize n = 0;
  const char *const *mimes = gdk_content_formats_get_mime_types(formats, &n);
  for (gsize i = 0; i < n && !image; i++) image = g_str_has_prefix(mimes[i], "image/");
  if (image) kinds.push_back("image");
  return kinds;
}

std::vector<std::string> mime_types_of(GdkContentFormats *formats) {
  std::vector<std::string> types;
  gsize n = 0;
  const char *const *mimes = gdk_content_formats_get_mime_types(formats, &n);
  for (gsize i = 0; i < n; i++) types.emplace_back(mimes[i]);
  return types;
}

folly::dynamic array_of(const std::vector<std::string> &list) {
  folly::dynamic array = folly::dynamic::array();
  for (const auto &item : list) array.push_back(item);
  return array;
}

folly::dynamic items_for(const std::vector<std::string> &types) {
  folly::dynamic items = folly::dynamic::array();
  for (const auto &type : types) {
    items.push_back(folly::dynamic::object("kind", type == "text/uri-list" ? "file" : "string")(
        "type", type));
  }
  return items;
}

folly::dynamic file_entry(GFile *file) {
  char *uri = g_file_get_uri(file);
  char *name = g_file_get_basename(file);
  folly::dynamic entry = folly::dynamic::object("uri", uri ? uri : "")("name", name ? name : "");
  g_free(uri);
  g_free(name);
  GFileInfo *info = g_file_query_info(
      file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
      G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
  if (info) {
    if (const char *type = g_file_info_get_content_type(info)) {
      char *mime = g_content_type_get_mime_type(type);
      entry["type"] = mime ? mime : type;
      g_free(mime);
    }
    entry["size"] = double(g_file_info_get_size(info));
    g_object_unref(info);
  }
  if (!entry.count("type")) entry["type"] = "";
  return entry;
}

// An image dropped as data, saved where the app can read it.
folly::dynamic save_texture(GdkTexture *texture) {
  static std::atomic<int> next{0};
  std::string dir = std::string(g_get_user_cache_dir()) + "/react-native-gtk4/drops";
  g_mkdir_with_parents(dir.c_str(), 0700);
  std::string path =
      dir + "/drop-" + std::to_string(getpid()) + "-" + std::to_string(next++) + ".png";
  if (!gdk_texture_save_to_png(texture, path.c_str())) return nullptr;
  GFile *file = g_file_new_for_path(path.c_str());
  folly::dynamic entry = file_entry(file);
  g_object_unref(file);
  entry["type"] = "image/png";
  entry["width"] = gdk_texture_get_width(texture);
  entry["height"] = gdk_texture_get_height(texture);
  return entry;
}

const HostPlatformViewProps *view_props(const Props::Shared &props) {
  return dynamic_cast<const HostPlatformViewProps *>(props.get());
}

// A GdkDrop being read: files, then text, then an image, as the target
// takes them.
struct PendingDrop {
  std::weak_ptr<int> alive;
  GtkPointerHandler *handler;
  GdkDrop *drop;
  double x, y;
  std::vector<std::string> wanted;  // kinds left to read
  GtkPointerHandler::DropData data;
  GdkTexture *texture = nullptr;
};

void read_next(PendingDrop *p);

void finish_drop(PendingDrop *p) {
  bool taken = false;
  if (p->alive.lock()) {
    p->data.texture = p->texture;
    taken = p->handler->drop(p->x, p->y, p->data);
  }
  gdk_drop_finish(p->drop, taken ? GDK_ACTION_COPY : GdkDragAction(0));
  g_object_unref(p->drop);
  g_clear_object(&p->texture);
  delete p;
}

void on_value(GObject *source, GAsyncResult *result, gpointer data) {
  auto *p = static_cast<PendingDrop *>(data);
  GError *error = nullptr;
  const GValue *value = gdk_drop_read_value_finish(GDK_DROP(source), result, &error);
  std::string kind = p->wanted.front();
  p->wanted.erase(p->wanted.begin());
  if (!value) {
    LOG(WARNING) << "Drop: can't read " << kind << ": " << (error ? error->message : "?");
    g_clear_error(&error);
  } else if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
    GSList *files = gdk_file_list_get_files(static_cast<GdkFileList *>(g_value_get_boxed(value)));
    for (GSList *l = files; l; l = l->next) {
      char *uri = g_file_get_uri(G_FILE(l->data));
      if (uri) p->data.uris.emplace_back(uri);
      g_free(uri);
    }
    g_slist_free(files);
  } else if (G_VALUE_HOLDS(value, G_TYPE_STRING)) {
    const char *text = g_value_get_string(value);
    p->data.hasText = text != nullptr;
    p->data.text = text ? text : "";
  } else if (G_VALUE_HOLDS(value, GDK_TYPE_TEXTURE)) {
    g_set_object(&p->texture, GDK_TEXTURE(g_value_get_object(value)));
  }
  read_next(p);
}

void read_next(PendingDrop *p) {
  if (p->wanted.empty()) {
    finish_drop(p);
    return;
  }
  const std::string &kind = p->wanted.front();
  GType type = kind == "fileUrl" ? GDK_TYPE_FILE_LIST
               : kind == "string" ? G_TYPE_STRING
                                  : GDK_TYPE_TEXTURE;
  gdk_drop_read_value_async(p->drop, type, G_PRIORITY_DEFAULT, nullptr, on_value, p);
}

}  // namespace

void GtkPointerHandler::setUpDragAndDrop() {
  // Dropping in.
  GdkContentFormats *formats = gdk_content_formats_new_for_gtype(GDK_TYPE_FILE_LIST);
  formats = gdk_content_formats_union(formats, gdk_content_formats_new_for_gtype(G_TYPE_STRING));
  formats = gdk_content_formats_union(formats, gdk_content_formats_new_for_gtype(GDK_TYPE_TEXTURE));
  GtkDropTargetAsync *target = gtk_drop_target_async_new(formats, GDK_ACTION_COPY);
  g_signal_connect(target, "accept", G_CALLBACK(+[](GtkDropTargetAsync *, GdkDrop *drop, gpointer) -> gboolean {
                     return !kinds_of(gdk_drop_get_formats(drop)).empty();
                   }),
                   nullptr);
  auto motion = +[](GtkDropTargetAsync *, GdkDrop *drop, double x, double y,
                    gpointer self) -> GdkDragAction {
    GdkContentFormats *formats = gdk_drop_get_formats(drop);
    return static_cast<GtkPointerHandler *>(self)->dragMotion(x, y, kinds_of(formats),
                                                              mime_types_of(formats))
               ? GDK_ACTION_COPY
               : GdkDragAction(0);
  };
  g_signal_connect(target, "drag-enter", G_CALLBACK(motion), this);
  g_signal_connect(target, "drag-motion", G_CALLBACK(motion), this);
  g_signal_connect(target, "drag-leave", G_CALLBACK(+[](GtkDropTargetAsync *, GdkDrop *, gpointer self) {
                     static_cast<GtkPointerHandler *>(self)->dragLeave();
                   }),
                   this);
  g_signal_connect(target, "drop", G_CALLBACK(+[](GtkDropTargetAsync *, GdkDrop *drop, double x,
                                                  double y, gpointer self) -> gboolean {
                     auto *handler = static_cast<GtkPointerHandler *>(self);
                     std::vector<std::string> kinds = kinds_of(gdk_drop_get_formats(drop));
                     Target to = handler->dropTargetAt(x, y, kinds);
                     auto props = view_props(handler->mountingManager_.propsForTag(to.tag));
                     if (!to.tag || !props) return FALSE;
                     // Read what both the drop offers and the view takes.
                     auto *p = new PendingDrop();
                     p->alive = handler->alive_;
                     p->handler = handler;
                     p->drop = GDK_DROP(g_object_ref(drop));
                     p->x = x;
                     p->y = y;
                     for (const auto &kind : kinds) {
                       if (contains(props->draggedTypes, kind)) p->wanted.push_back(kind);
                     }
                     read_next(p);
                     return TRUE;
                   }),
                   this);
  dropController_ = GTK_EVENT_CONTROLLER(target);
  gtk_widget_add_controller(root_, dropController_);

  // Dragging out.
  GtkDragSource *source = gtk_drag_source_new();
  gtk_drag_source_set_actions(source, GDK_ACTION_COPY);
  g_signal_connect(source, "prepare", G_CALLBACK(+[](GtkDragSource *, double x, double y,
                                                     gpointer self) -> GdkContentProvider * {
                     return static_cast<GtkPointerHandler *>(self)->dragContentAt(x, y);
                   }),
                   this);
  g_signal_connect(source, "drag-begin", G_CALLBACK(+[](GtkDragSource *source, GdkDrag *,
                                                        gpointer self) {
                     auto *handler = static_cast<GtkPointerHandler *>(self);
                     // The drag took over the press.
                     handler->cancelTouches();
                     handler->pressInSelection_ = false;
                     handler->selecting_ = false;
                     auto *icon = static_cast<GdkPaintable *>(
                         g_object_get_data(G_OBJECT(source), "rngtk-drag-icon"));
                     if (icon) gtk_drag_source_set_icon(source, icon, 0, 0);
                   }),
                   this);
  dragController_ = GTK_EVENT_CONTROLLER(source);
  gtk_widget_add_controller(root_, dragController_);
}

void GtkPointerHandler::tearDownDragAndDrop() {
  if (dropController_) {
    g_signal_handlers_disconnect_by_data(dropController_, this);
    gtk_widget_remove_controller(root_, dropController_);
  }
  if (dragController_) {
    g_signal_handlers_disconnect_by_data(dragController_, this);
    gtk_widget_remove_controller(root_, dragController_);
  }
  dropController_ = dragController_ = nullptr;
}

// The nearest view at (x, y) whose draggedTypes take one of `kinds`.
GtkPointerHandler::Target GtkPointerHandler::dropTargetAt(
    double x, double y, const std::vector<std::string> &kinds) const {
  Target hit = targetAt(x, y, nullptr, nullptr);
  for (GtkWidget *w = hit.widget.get(); w; w = gtk_widget_get_parent(w)) {
    auto t = mountingManager_.targetForView(w);
    if (t.tag != 0) {
      if (auto props = view_props(mountingManager_.propsForTag(t.tag))) {
        for (const auto &kind : kinds) {
          if (contains(props->draggedTypes, kind)) {
            g_object_ref(w);
            return Target{t.tag, t.emitter,
                          std::shared_ptr<GtkWidget>(w, [](GtkWidget *p) { g_object_unref(p); })};
          }
        }
      }
    }
    if (w == root_) break;
  }
  return {};
}

folly::dynamic GtkPointerHandler::dragEvent(double x, double y, folly::dynamic dataTransfer) const {
  return folly::dynamic::object("clientX", x)("clientY", y)("dataTransfer", std::move(dataTransfer));
}

bool GtkPointerHandler::dragMotion(double x, double y, const std::vector<std::string> &kinds,
                                   const std::vector<std::string> &types) {
  Target to = dropTargetAt(x, y, kinds);
  if (to.tag != dragOver_.tag) {
    folly::dynamic transfer = folly::dynamic::object("files", folly::dynamic::array())(
        "items", items_for(types))("types", array_of(types));
    if (dragOver_.tag && dragOver_.emitter) {
      dragOver_.emitter->dispatchEvent("dragLeave", dragEvent(x, y, transfer),
                                       RawEvent::Category::Discrete);
    }
    dragOver_ = to;
    dragTypes_ = types;
    if (to.tag && to.emitter) {
      to.emitter->dispatchEvent("dragEnter", dragEvent(x, y, transfer),
                                RawEvent::Category::Discrete);
    }
  }
  return to.tag != 0;
}

void GtkPointerHandler::dragLeave() {
  if (dragOver_.tag && dragOver_.emitter) {
    folly::dynamic transfer = folly::dynamic::object("files", folly::dynamic::array())(
        "items", items_for(dragTypes_))("types", array_of(dragTypes_));
    dragOver_.emitter->dispatchEvent("dragLeave", dragEvent(-1, -1, transfer),
                                     RawEvent::Category::Discrete);
  }
  dragOver_ = Target{};
}

bool GtkPointerHandler::drop(double x, double y, const DropData &data) {
  std::vector<std::string> kinds;
  if (!data.uris.empty()) kinds.push_back("fileUrl");
  if (data.hasText) kinds.push_back("string");
  if (data.texture) kinds.push_back("image");
  Target to = dropTargetAt(x, y, kinds);
  dragOver_ = Target{};
  if (!to.tag || !to.emitter) return false;
  auto props = view_props(mountingManager_.propsForTag(to.tag));
  auto takes = [&](const char *kind) { return props && contains(props->draggedTypes, kind); };

  folly::dynamic files = folly::dynamic::array(), urls = folly::dynamic::array();
  std::vector<std::string> types;
  if (takes("fileUrl")) {
    for (const auto &uri : data.uris) {
      if (g_str_has_prefix(uri.c_str(), "file:")) {
        GFile *file = g_file_new_for_uri(uri.c_str());
        files.push_back(file_entry(file));
        g_object_unref(file);
      } else {
        urls.push_back(uri);
      }
    }
    if (!data.uris.empty()) types.push_back("text/uri-list");
  }
  // An image that comes as a file too (a dragged Image, a picture from
  // Files) is that file: only image data alone becomes a saved PNG.
  if (takes("image") && data.texture && files.empty()) {
    folly::dynamic entry = save_texture(data.texture);
    if (!entry.isNull()) files.push_back(std::move(entry));
    types.push_back("image/png");
  }
  folly::dynamic transfer = folly::dynamic::object("files", std::move(files))("urls", std::move(urls));
  if (takes("string") && data.hasText) {
    transfer["text"] = data.text;
    types.push_back("text/plain");
  }
  transfer["items"] = items_for(types);
  transfer["types"] = array_of(types);
  to.emitter->dispatchEvent("drop", dragEvent(x, y, std::move(transfer)),
                            RawEvent::Category::Discrete);
  return true;
}

GdkContentProvider *GtkPointerHandler::dragContentAt(double x, double y) {
  double lx = 0, ly = 0;
  Target hit = targetAt(x, y, &lx, &ly);
  GtkWidget *widget = hit.widget.get();
  if (!widget) return nullptr;
  GObject *source = G_OBJECT(dragController_);
  // Selected text, from a press inside the selection.
  if (RN_IS_TEXT(widget) && selectionWidget_.get() == widget) {
    auto *text = RN_TEXT(widget);
    int start = 0, end = 0;
    graphene_point_t p{float(x), float(y)}, local;
    if (rn_text_get_selection(text, &start, &end) && start != end &&
        gtk_widget_compute_point(root_, widget, &p, &local)) {
      int at = rn_text_index_at(text, local.x, local.y, FALSE);
      if (at >= start && at < end) {
        std::string selected = selectedText();
        if (source) {
          g_object_set_data_full(source, "rngtk-drag-icon", gtk_widget_paintable_new(widget),
                                 g_object_unref);
        }
        return gdk_content_provider_new_typed(G_TYPE_STRING, selected.c_str());
      }
    }
    return nullptr;
  }
  // A draggable Image: its picture, and its file or URL.
  for (GtkWidget *w = widget; w; w = gtk_widget_get_parent(w)) {
    auto t = mountingManager_.targetForView(w);
    if (t.tag != 0) {
      auto props = mountingManager_.propsForTag(t.tag);
      auto image = std::dynamic_pointer_cast<const ImageProps>(props);
      auto view = view_props(props);
      if (view && view->draggable && image && RN_IS_VIEW(w)) {
        GdkTexture *texture = rn_view_get_image(RN_VIEW(w));
        if (!texture) return nullptr;
        std::vector<GdkContentProvider *> providers;
        providers.push_back(gdk_content_provider_new_typed(GDK_TYPE_TEXTURE, texture));
        std::string uri = image->sources.empty() ? "" : image->sources.front().uri;
        if (g_str_has_prefix(uri.c_str(), "file:")) {
          GFile *file = g_file_new_for_uri(uri.c_str());
          providers.push_back(gdk_content_provider_new_typed(G_TYPE_FILE, file));
          g_object_unref(file);
        } else if (g_str_has_prefix(uri.c_str(), "http")) {
          providers.push_back(gdk_content_provider_new_typed(G_TYPE_STRING, uri.c_str()));
        }
        if (source) {
          g_object_set_data_full(source, "rngtk-drag-icon", g_object_ref(texture), g_object_unref);
        }
        return gdk_content_provider_new_union(providers.data(), providers.size());
      }
      // Only the deepest view (or an Image's own) decides.
      if (!view || !view->draggable) return nullptr;
    }
    if (w == root_) break;
  }
  return nullptr;
}

}  // namespace rngtk
