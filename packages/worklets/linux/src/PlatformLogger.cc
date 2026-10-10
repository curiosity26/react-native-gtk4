// worklets' PlatformLogger on Linux: stderr, as the host's console is.
#include <worklets/Tools/PlatformLogger.h>

#include <cstdio>
#include <string>

namespace worklets {

void PlatformLogger::log(const char *str) { fprintf(stderr, "[Worklets] %s\n", str); }
void PlatformLogger::log(const std::string &str) { log(str.c_str()); }
void PlatformLogger::log(const double d) { fprintf(stderr, "[Worklets] %f\n", d); }
void PlatformLogger::log(const int i) { fprintf(stderr, "[Worklets] %d\n", i); }
void PlatformLogger::log(const bool b) { log(b ? "true" : "false"); }

}  // namespace worklets
