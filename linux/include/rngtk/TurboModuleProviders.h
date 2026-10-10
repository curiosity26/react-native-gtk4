// facebook::react::TurboModuleProvider(s): what makes a TurboModule by name,
// as ReactCxxPlatform's ReactHost takes them. React Native 0.83 calls them
// TurboModuleManagerDelegate(s) (react/nativemodule/TurboModuleManager.h);
// later versions have react/nativemodule/TurboModuleProvider.h.
#pragma once

#if __has_include(<react/nativemodule/TurboModuleProvider.h>)
#include <react/nativemodule/TurboModuleProvider.h>
#else
#include <react/nativemodule/TurboModuleManager.h>

namespace facebook::react {
using TurboModuleProvider = TurboModuleManagerDelegate;
using TurboModuleProviders = TurboModuleManagerDelegates;
}  // namespace facebook::react
#endif
