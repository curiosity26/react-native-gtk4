# Components: View, Text and pointer input

What `<View>`, `<Text>` and the pressables do on GTK4 today. The gallery
(`examples/hello-world/Gallery.js`, AppRegistry name `Gallery`) shows most of
it:

```sh
(cd examples/hello-world && npm run bundle)
build/linux/rn-gtk-host --bundle examples/hello-world/build/index.bundle.js \
  --module Gallery --width 940 --height 680 [--self-test]
```

![The gallery under Wayland](images/gallery-wayland.png)

`--self-test` checks pixels for the key cases. These include per-side
borders, corner radii, a rounded overflow clip, box shadows, a transformed
view's position, filters, gradients, nested Text colors and ellipsis. It
also checks that every Text draws at the size Yoga measured, and it clicks
the pressables, hovers one, holds TouchableOpacity and copies selectable
text. It passes on Wayland and X11.

**Supported** means it matches iOS/Android for the common cases.
**Partial** means it works with the limits noted. **Not yet** means the
prop is accepted but has no effect.

## View

| Prop | Status | Notes |
| --- | --- | --- |
| `backgroundColor` | Supported | |
| `opacity` | Supported | GTK widget opacity (one offscreen pass per translucent view) |
| `borderWidth`, `border{Top,Right,Bottom,Left}Width` | Supported | per side |
| `borderColor`, `border{Top,Right,Bottom,Left}Color` | Supported | per side; default black |
| `border{Start,End}*` | Supported | resolved by Fabric for the layout direction |
| `borderRadius`, per-corner radii | Supported | percentages give elliptical corners; radii that don't fit scale down like CSS |
| `borderStyle: 'dashed' \| 'dotted'` | Partial | drawn with one stroke: the widest side's width and the top color |
| `borderCurve` | Not yet | circular corners only |
| `overflow: 'hidden'` | Supported | clips children to the rounded outline; hit-testing follows it |
| `boxShadow` (outset, inset, spread, blur, several) | Supported | GSK shadow nodes |
| `shadowColor/Offset/Opacity/Radius` | Partial | drawn as an outset box shadow of the view's box (iOS shades the content's alpha) |
| `elevation` | Not yet | Android only |
| `outlineWidth/Color/Style/Offset` | Supported | dashed/dotted as for borders |
| `transform` (translate, scale, rotate, rotateX/Y/Z, skew, perspective, matrix) | Supported | 2D and 3D, around the centre like iOS/Android |
| `transformOrigin` | Supported | |
| `backfaceVisibility: 'hidden'` | Partial | hides the view when its own transform faces away; ancestors' rotations aren't combined |
| `filter`: brightness, contrast, grayscale, hue-rotate, invert, opacity, saturate, sepia | Supported | GSK color matrices (Filter Effects spec) |
| `filter`: blur, drop-shadow | Supported | GSK blur and shadow nodes |
| `experimental_backgroundImage`: linear-gradient | Supported | angles, `to <side/corner>`, stop positions in % or px |
| `experimental_backgroundImage`: radial-gradient | Partial | circle or ellipse at farthest-corner with a position; explicit sizes not yet |
| `backgroundSize/Position/Repeat` | Not yet | |
| `mixBlendMode`, `isolation` | Not yet | |
| `pointerEvents` | Supported | `none`, `box-none`, `box-only` in hit-testing |
| `hitSlop` | Not yet | |
| `zIndex` | Supported | Fabric orders the children; GTK paints and hit-tests in that order |
| `cursor` | Supported | every RN cursor maps to a GTK/CSS cursor name |
| `nativeID` | Supported | |
| Accessibility props | Not yet | Phase 2 (AT-SPI through GtkAccessible) |

## Text

Text is laid out with Pango. Measuring (TextLayoutManager) and drawing
(RNText) both use `create_pango_layout`. The gallery self-test checks that
every mounted Text draws at the size Yoga measured.

| Prop | Status | Notes |
| --- | --- | --- |
| Nested `<Text>` spans | Supported | each fragment becomes Pango attributes |
| `color`, `fontSize`, `fontFamily`, `fontWeight`, `fontStyle` | Supported | per span |
| Font fallback | Supported | fontconfig fallback (CJK, symbols); names like `serif`/`monospace` work |
| `letterSpacing` | Supported | |
| `lineHeight` | Supported | absolute Pango line height |
| `textDecorationLine` | Supported | underline, line-through, both |
| `textDecorationColor` | Supported | |
| `textDecorationStyle` | Partial | `double` and wavy-ish (`wavy` uses Pango's error underline); dotted/dashed draw solid |
| `textTransform` | Supported | |
| `backgroundColor` on a span | Supported | |
| `textAlign` incl. `justify` | Supported | |
| `numberOfLines` + `ellipsizeMode` head/middle/tail | Supported | |
| `ellipsizeMode: 'clip'` | Supported | measures N lines and clips the rest |
| Padding/border on Text | Supported | text draws inside the content box |
| `selectable` | Partial | right-click shows a Copy menu that copies the whole text; no drag selection yet |
| `textShadow*` | Not yet | |
| Inline views (`<View>` inside `<Text>`) | Not yet | take no space |
| `adjustsFontSizeToFit`, `allowFontScaling` | Not yet | |
| `onPress` on nested spans | Supported | hit-testing finds the span under the pointer |

## Pointer input

`linux/src/GtkPointerHandler.cc` turns GDK input on each surface root (the
app and LogBox) into Fabric events. It hit-tests our widgets with
transforms, rounded overflow clips, `pointerEvents`, and nested Text spans.

| Feature | Status | Notes |
| --- | --- | --- |
| Touch events (touchStart/Move/End/Cancel) | Supported | mouse primary button and touchscreen points; drive the responder system |
| `Pressable`, `TouchableOpacity`, `TouchableHighlight`, `Button` | Supported | `onPress`, `onPressIn/Out`, `onLongPress`, pressed styles |
| TouchableOpacity fade | Supported | native-driver Animated through C++ Animated (`synchronouslyUpdateViewOnUIThread`) |
| W3C pointer events: down/move/up/cancel, click | Supported | only sent to views (and ancestors) that listen |
| Hover: pointerover/out, pointerenter/leave | Supported | `onHoverIn`/`onHoverOut` on Pressable (W3C hover flag on) |
| Modifier keys, buttons, pointerType | Supported | `ctrlKey`... `buttons`, `button`, `mouse`/`touch` |
| Right/middle button | Partial | pointer events only; right-click opens Copy on selectable text |
| Scroll wheel | Not yet | slice 4 (ScrollView) |
| Keyboard focus, `onKeyDown` | Not yet | Phase 2 |
| Pen pressure/tilt | Not yet | |

## Performance

The richer styling keeps plain views cheap: the rarely used styles
(shadows, filters, gradients) live off the widget until set. The Phase 0
widget benchmark, run on the GNOME Wayland session with the NGL renderer,
10,000 RNViews, before and after this work:

| moved/frame | frame p50 ms | frame p95 ms | RSS MB |
| --- | --- | --- | --- |
| 100% | 7.1 → 6.7–7.1 | 8.2 → 7.8–8.2 | 28 → 29 |
| 1% | 6.3 → 6.3–6.6 | 7.3 → 6.9–7.6 | 28 → 29 |

(Two runs after; differences are within run-to-run noise.)

## Known differences

- **`Button` has no Linux look.** Its styles `Platform.select` only `ios`
  and `android`, so on Linux it renders as plain text that responds to
  presses.
- **Shadows draw the view's box.** Like CSS, but unlike iOS's legacy
  `shadow*` props, which shade the content's alpha.
- **3D transforms render in GSK**, but GTK hit-testing through strongly
  rotated or perspective views is approximate.
