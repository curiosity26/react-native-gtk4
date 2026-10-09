#include "Store.h"

#include <folly/json.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <stdexcept>

namespace rngtk_async_storage {

Store::Store(std::string path) : path_(std::move(path)) {}

void Store::load() {
  if (loaded_) return;
  loaded_ = true;
  gchar *text = nullptr;
  gsize length = 0;
  if (!g_file_get_contents(path_.c_str(), &text, &length, nullptr)) return;  // a new database
  try {
    folly::dynamic data = folly::parseJson(std::string_view(text, length));
    if (data.isObject()) {
      for (auto &[k, v] : data.items()) {
        if (k.isString() && v.isString()) values_[k.asString()] = v.asString();
      }
    }
  } catch (const std::exception &e) {
    // A damaged file: keep it aside rather than lose it on the next write.
    g_warning("async-storage: %s isn't valid JSON (%s); starting it empty", path_.c_str(), e.what());
    std::string aside = path_ + ".corrupt";
    g_rename(path_.c_str(), aside.c_str());
  }
  g_free(text);
}

std::optional<std::string> Store::get(const std::string &key) {
  load();
  auto it = values_.find(key);
  if (it == values_.end()) return std::nullopt;
  return it->second;
}

void Store::set(const std::string &key, const std::optional<std::string> &value) {
  load();
  if (!value) {
    remove(key);
    return;
  }
  values_[key] = *value;
  dirty_ = true;
}

void Store::remove(const std::string &key) {
  load();
  if (values_.erase(key)) dirty_ = true;
}

void mergeRecursive(folly::dynamic &base, const folly::dynamic &patch) {
  if (!base.isObject() || !patch.isObject()) {
    base = patch;
    return;
  }
  for (auto &[k, v] : patch.items()) {
    auto *existing = base.get_ptr(k);
    if (existing && existing->isObject() && v.isObject()) {
      mergeRecursive(*existing, v);
    } else {
      base[k] = v;
    }
  }
}

void Store::merge(const std::string &key, const std::string &json) {
  load();
  folly::dynamic patch = folly::parseJson(json);  // throws on bad JSON
  auto it = values_.find(key);
  folly::dynamic base = nullptr;
  if (it != values_.end()) {
    try {
      base = folly::parseJson(it->second);
    } catch (const std::exception &) {
      base = nullptr;
    }
  }
  mergeRecursive(base, patch);
  values_[key] = folly::toJson(base);
  dirty_ = true;
}

std::vector<std::string> Store::keys() {
  load();
  std::vector<std::string> out;
  out.reserve(values_.size());
  for (auto &[k, _] : values_) out.push_back(k);
  return out;
}

void Store::clear() {
  load();
  if (!values_.empty()) dirty_ = true;
  values_.clear();
}

void Store::flush() {
  if (!dirty_) return;
  folly::dynamic data = folly::dynamic::object;
  for (auto &[k, v] : values_) data[k] = v;
  std::string text = folly::toJson(data);
  gchar *dir = g_path_get_dirname(path_.c_str());
  g_mkdir_with_parents(dir, 0700);
  g_free(dir);
  GError *error = nullptr;
  if (!g_file_set_contents_full(path_.c_str(), text.data(), static_cast<gssize>(text.size()),
                                G_FILE_SET_CONTENTS_CONSISTENT, 0600, &error)) {
    std::string message = error ? error->message : "write failed";
    g_clear_error(&error);
    throw std::runtime_error(message);
  }
  dirty_ = false;
}

static std::string safeName(const std::string &name) {
  std::string out;
  for (unsigned char c : name) {
    if (g_ascii_isalnum(c) || c == '-' || c == '_' || c == '.') {
      out += static_cast<char>(c);
    } else {
      char hex[4];
      g_snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  if (out.empty() || out[0] == '.') out = "%" + out;
  return out;
}

std::string storePath(const std::string &database) {
  GApplication *app = g_application_get_default();
  const char *id = app ? g_application_get_application_id(app) : nullptr;
  std::string appId = id ? id : (g_get_prgname() ? g_get_prgname() : "react-native-app");
  gchar *path = g_build_filename(g_get_user_data_dir(), appId.c_str(), "async-storage",
                                 (safeName(database) + ".json").c_str(), nullptr);
  std::string out = path;
  g_free(path);
  return out;
}

}  // namespace rngtk_async_storage
