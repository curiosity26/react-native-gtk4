// Included first in react-native-worklets' (and Reanimated's) shared C++
// on Linux: what it expects of the platform where its code assumes Apple
// for anything not Android.
#pragma once

// What their Android build's precompiled header brings in.
#include <algorithm>
#include <atomic>
#include <functional>
#include <iterator>
#include <sstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <jsi/jsi.h>
#include <pthread.h>

// Apple's pthread_setname_np names the calling thread. (Linux limits names
// to 15 characters; longer ones are left unnamed.)
inline int pthread_setname_np(const char *name) {
  return pthread_setname_np(pthread_self(), name);
}
