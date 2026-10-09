// A key-value database in a JSON file: {"key": "value", ...}, read once and
// written whole (atomically: g_file_set_contents) after each change.
#pragma once

#include <folly/dynamic.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rngtk_async_storage {

class Store {
 public:
  explicit Store(std::string path);

  std::optional<std::string> get(const std::string &key);
  // nullopt removes the key.
  void set(const std::string &key, const std::optional<std::string> &value);
  void remove(const std::string &key);
  // The value becomes the JSON objects merged, deeply (RCTMergeRecursive).
  void merge(const std::string &key, const std::string &json);
  std::vector<std::string> keys();
  void clear();
  // Writes the file if anything changed; throws std::runtime_error.
  void flush();

  const std::string &path() const { return path_; }

 private:
  void load();

  std::string path_;
  bool loaded_ = false;
  bool dirty_ = false;
  std::map<std::string, std::string> values_;
};

// $XDG_DATA_HOME/<app id>/async-storage/<database>.json (Flatpak: under
// ~/.var/app/<app id>/data). Database names are made safe for a file name.
std::string storePath(const std::string &database);

// Merges `patch` into `base`: objects key by key, recursively; anything
// else replaces.
void mergeRecursive(folly::dynamic &base, const folly::dynamic &patch);

}  // namespace rngtk_async_storage
