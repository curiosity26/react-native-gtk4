// react-native-svg's components (SvgComponents.cc) and what
// RNSVGSvgViewModule needs from them.
#pragma once

#include <rngtk/Extensions.h>

#include <string>
#include <vector>

namespace rngtk_svg {

// RNSVGSvgView, RNSVGGroup, RNSVGPath, ... for rngtk::Package.
std::vector<rngtk::NativeComponent> svgComponents();

// The SvgView with React tag `tag`'s document and size, or "" (main thread).
std::string documentForTag(int tag, double *width, double *height);

}  // namespace rngtk_svg
