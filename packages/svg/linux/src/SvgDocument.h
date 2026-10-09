// react-native-svg's element tree, back as an SVG document.
//
// Each RNSVG* component instance is a Node: its component name, its props
// as react-native-svg's JS sends them (folly::dynamic: brushes as
// {type, payload|brushRef}, transforms as 6-number matrices, enums as
// numbers, lengths as numbers or strings) and its children. svgDocument()
// writes the <svg> for an RNSVGSvgView node, which librsvg draws.
#pragma once

#include <folly/dynamic.h>

#include <string>
#include <vector>

namespace rngtk_svg {

struct Node {
  std::string component;  // "RNSVGPath"
  folly::dynamic props = folly::dynamic::object;
  std::vector<Node *> children;
  Node *parent = nullptr;
};

// The SVG document for an RNSVGSvgView node, drawn at width x height.
std::string svgDocument(const Node &root, double width, double height);

// The attribute value for a react-native-svg color: an ARGB integer, as
// processColor makes it on Linux ("rgba(r,g,b,a)"), or "" if it isn't one.
std::string cssColor(const folly::dynamic &color);

}  // namespace rngtk_svg
