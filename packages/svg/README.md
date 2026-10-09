# @curiosity26/react-native-gtk4-svg

The Linux (GTK4) side of [`react-native-svg`](https://www.npmjs.com/package/react-native-svg) 15
for [@curiosity26/react-native-gtk4](https://github.com/curiosity26/react-native-gtk4).
An `<Svg>` is one widget. The elements inside it (G, Path, Rect, Circle,
Ellipse, Line, Polygon, Text, TSpan, TextPath, Use, Image, Defs, ClipPath,
Mask, Pattern, Symbol, Marker, gradients, filters) are turned back into an
SVG document, which [librsvg](https://gitlab.gnome.org/GNOME/librsvg)
draws. `SvgXml`, `SvgUri` and `ref.toDataURL()` work too.

```sh
sudo apt install librsvg2-dev     # Fedora: sudo dnf install librsvg2-devel
npm install react-native-svg @curiosity26/react-native-gtk4-svg
npx react-native run-linux
```

Not yet: touch events on individual elements (`onPress` on a `<Path>`; the
`<Svg>` as a whole receives them), `RNSVGRenderableModule`
(`isPointInFill`, `getBBox`, ...), and `ForeignObject` (drawn as a group).
`RNGTK_SVG_DEBUG=1` prints each document librsvg gets. See
[docs/libraries.md](../../docs/libraries.md).
