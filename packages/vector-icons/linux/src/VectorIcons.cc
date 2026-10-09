// Registers the app's icon fonts with fontconfig before the host starts
// (autolinking calls rngtk_vector_icons_package() first), from fonts/ next
// to the executable or share/<app>/fonts in an installed app.
//
// react-native-vector-icons names a font by its PostScript name on Linux
// (as on iOS: "FontAwesome5Free-Solid"), which isn't always its family
// ("Font Awesome 5 Free", weight 900). Each font also answers to its
// PostScript name and its file's name, with its own weight and slant.
#include <fontconfig/fontconfig.h>
// (After fontconfig.h: FcFreeTypeQuery, which newer fontconfig declares here.)
#include <fontconfig/fcfreetype.h>
#include <glib.h>
#include <rngtk/Extensions.h>

#include <string>
#include <vector>

namespace {

std::string executableDir() {
  gchar *exe = g_file_read_link("/proc/self/exe", nullptr);
  if (!exe) return ".";
  gchar *dir = g_path_get_dirname(exe);
  std::string out = dir;
  g_free(dir);
  g_free(exe);
  return out;
}

std::string fontsDir() {
  std::string dir = executableDir();
  gchar *link = g_file_read_link("/proc/self/exe", nullptr);
  gchar *base = link ? g_path_get_basename(link) : nullptr;
  std::string exe = base ? base : "";
  g_free(base);
  g_free(link);
  for (std::string candidate : {dir + "/fonts", dir + "/../share/" + exe + "/fonts"}) {
    if (g_file_test(candidate.c_str(), G_FILE_TEST_IS_DIR)) return candidate;
  }
  return "";
}

std::string xmlEscape(const std::string &s) {
  gchar *e = g_markup_escape_text(s.c_str(), -1);
  std::string out = e;
  g_free(e);
  return out;
}

// <match>: a request for `alias` is one for `family` at its weight and slant.
std::string aliasRule(const std::string &alias, const std::string &family, int weight, int slant) {
  return "<match target=\"pattern\"><test name=\"family\"><string>" + xmlEscape(alias) +
         "</string></test><edit name=\"family\" mode=\"assign\" binding=\"strong\"><string>" +
         xmlEscape(family) + "</string></edit><edit name=\"weight\" mode=\"assign\"><int>" +
         std::to_string(weight) + "</int></edit><edit name=\"slant\" mode=\"assign\"><int>" +
         std::to_string(slant) + "</int></edit></match>";
}

void registerFonts() {
  std::string dir = fontsDir();
  if (dir.empty()) return;
  FcConfig *config = FcConfigGetCurrent();
  std::string rules;
  int count = 0;
  GDir *d = g_dir_open(dir.c_str(), 0, nullptr);
  if (!d) return;
  while (const char *entry = g_dir_read_name(d)) {
    if (!g_str_has_suffix(entry, ".ttf") && !g_str_has_suffix(entry, ".otf")) continue;
    std::string path = dir + "/" + entry;
    if (!FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8 *>(path.c_str()))) {
      g_warning("vector-icons: fontconfig can't load %s", path.c_str());
      continue;
    }
    count++;
    int faces = 0;
    FcPattern *font = FcFreeTypeQuery(reinterpret_cast<const FcChar8 *>(path.c_str()), 0, nullptr, &faces);
    if (!font) continue;
    FcChar8 *family = nullptr, *postscript = nullptr;
    int weight = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN;
    FcPatternGetString(font, FC_FAMILY, 0, &family);
    FcPatternGetString(font, FC_POSTSCRIPT_NAME, 0, &postscript);
    FcPatternGetInteger(font, FC_WEIGHT, 0, &weight);
    FcPatternGetInteger(font, FC_SLANT, 0, &slant);
    if (family) {
      std::string f = reinterpret_cast<const char *>(family);
      std::string base(entry, std::string(entry).rfind('.'));
      for (std::string alias : {postscript ? std::string(reinterpret_cast<const char *>(postscript)) : std::string(), base}) {
        if (!alias.empty() && alias != f) rules += aliasRule(alias, f, weight, slant);
      }
    }
    FcPatternDestroy(font);
  }
  g_dir_close(d);
  if (!rules.empty()) {
    std::string xml = "<?xml version=\"1.0\"?><!DOCTYPE fontconfig SYSTEM \"fonts.dtd\"><fontconfig>" + rules + "</fontconfig>";
    if (!FcConfigParseAndLoadFromMemory(config, reinterpret_cast<const FcChar8 *>(xml.c_str()), FcTrue)) {
      g_warning("vector-icons: the font aliases didn't load");
    }
  }
  g_debug("vector-icons: %d font(s) from %s", count, dir.c_str());
}

}  // namespace

std::shared_ptr<const rngtk::Package> rngtk_vector_icons_package() {
  static bool registered = (registerFonts(), true);
  (void)registered;
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-vector-icons";
  return package;
}
