// <TextInput> support in GtkMountingManager (RNTextInput widgets).
//
// Controlled values follow iOS's RCTTextInputComponentView: every native
// edit bumps a native event count, sends onChange with it and updates the
// shadow node's TextInputState (so layout measures the new text). JS echoes
// the count back (mostRecentEventCount) with its value; the shadow node puts
// that value in the state, and we apply a state's text only when its count
// equals ours, i.e. when JS has seen every edit. Typing never fights JS, and
// a value JS rewrites (say, uppercased) lands with the caret kept in place.
#include "GtkMountingManager.h"
#include "GtkViewProps.h"
#include "PangoText.h"

#include "rn_text_input.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/iostextinput/TextInputShadowNode.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>

#include <climits>
#include <cmath>

using namespace facebook::react;

namespace rngtk {

namespace {

struct SignalData {
  std::weak_ptr<GtkMountingManager> manager;
  Tag tag;
};

GtkInputPurpose input_purpose(KeyboardType type) {
  switch (type) {
    case KeyboardType::EmailAddress: return GTK_INPUT_PURPOSE_EMAIL;
    case KeyboardType::Numeric:
    case KeyboardType::DecimalPad:
    case KeyboardType::NumbersAndPunctuation: return GTK_INPUT_PURPOSE_NUMBER;
    case KeyboardType::NumberPad:
    case KeyboardType::ASCIICapableNumberPad: return GTK_INPUT_PURPOSE_DIGITS;
    case KeyboardType::PhonePad:
    case KeyboardType::NamePhonePad: return GTK_INPUT_PURPOSE_PHONE;
    case KeyboardType::URL: return GTK_INPUT_PURPOSE_URL;
    default: return GTK_INPUT_PURPOSE_FREE_FORM;
  }
}

GtkInputHints input_hints(const TextInputTraits &traits) {
  int hints = GTK_INPUT_HINT_NONE;
  switch (traits.autocapitalizationType) {
    case AutocapitalizationType::Characters: hints |= GTK_INPUT_HINT_UPPERCASE_CHARS; break;
    case AutocapitalizationType::Words: hints |= GTK_INPUT_HINT_UPPERCASE_WORDS; break;
    case AutocapitalizationType::Sentences: hints |= GTK_INPUT_HINT_UPPERCASE_SENTENCES; break;
    default: break;
  }
  bool spell = traits.spellCheck.value_or(traits.autoCorrect.value_or(true));
  hints |= spell ? GTK_INPUT_HINT_SPELLCHECK : GTK_INPUT_HINT_NO_SPELLCHECK;
  return GtkInputHints(hints);
}

int text_align(const TextAttributes &a) {
  if (!a.alignment) return 0;
  switch (*a.alignment) {
    case TextAlignment::Center: return 1;
    case TextAlignment::Right:
    case TextAlignment::End: return 2;
    default: return 0;
  }
}

AttributedString attributed(const TextInputProps &props, const std::string &text) {
  AttributedString s;
  auto attrs = props.getEffectiveTextAttributes(1);
  s.setBaseTextAttributes(attrs);
  s.appendFragment(AttributedString::Fragment{.string = text,
                                              .textAttributes = attrs});
  return s;
}

}  // namespace

void GtkMountingManager::connectTextInput(GtkWidget *widget, Tag tag) {
  textInputs_[tag] = TextInputTracking{};
  struct Data : SignalData {
    std::string type;
  };
  auto connect = [&](const char *signal, GCallback cb, const char *type) {
    g_signal_connect_data(widget, signal, cb,
                          new Data{{weak_from_this(), tag}, type},
                          +[](gpointer d, GClosure *) { delete static_cast<Data *>(d); },
                          GConnectFlags(0));
  };
  auto simple = G_CALLBACK(+[](RNTextInput *, gpointer data) {
    auto *d = static_cast<Data *>(data);
    if (auto self = d->manager.lock()) self->onTextInputEvent(d->tag, d->type, "");
  });
  connect("text-changed", simple, "change");
  connect("selection-changed", simple, "selection");
  connect("submit", simple, "submit");
  connect("focus-changed", G_CALLBACK(+[](RNTextInput *, gboolean in, gpointer data) {
            auto *d = static_cast<Data *>(data);
            if (auto self = d->manager.lock()) {
              self->onTextInputEvent(d->tag, in ? "focus" : "blur", "");
            }
          }),
          "focus");
  connect("key-pressed", G_CALLBACK(+[](RNTextInput *, const char *key, gpointer data) {
            auto *d = static_cast<Data *>(data);
            if (auto self = d->manager.lock()) self->onTextInputEvent(d->tag, "key", key);
          }),
          "key");
}

void GtkMountingManager::updateTextInput(GtkWidget *widget,
                                         const ShadowView &oldView,
                                         const ShadowView &newView) {
  auto *input = RN_TEXT_INPUT(widget);
  auto props = std::dynamic_pointer_cast<const TextInputProps>(newView.props);
  if (!props) return;
  auto &tracking = textInputs_[newView.tag];

  const EdgeInsets &in = newView.layoutMetrics.contentInsets;
  rn_text_input_set_insets(input, in.top, in.right, in.bottom, in.left);

  if (oldView.props != newView.props) {
    auto attrs = props->getEffectiveTextAttributes(fontScale_);
    RNTextInputStyle style{};
    std::string family = attrs.fontFamily;
    style.font_family = family.empty() ? nullptr : family.c_str();
    style.font_size = effective_font_size(attrs);
    style.font_weight = attrs.fontWeight ? int(*attrs.fontWeight) : 0;
    style.italic = attrs.fontStyle == FontStyle::Italic;
    style.color = attrs.foregroundColor ? to_rgba(attrs.foregroundColor)
                                        : GdkRGBA{0, 0, 0, 1};
    // iOS's default placeholder gray.
    style.placeholder_color = props->placeholderTextColor
                                  ? to_rgba(props->placeholderTextColor)
                                  : GdkRGBA{0.78f, 0.78f, 0.80f, 1};
    style.caret_color = to_rgba(props->cursorColor);
    style.selection_color = to_rgba(props->selectionColor);
    style.letter_spacing = std::isnan(attrs.letterSpacing) ? 0 : attrs.letterSpacing;
    style.text_align = text_align(attrs);
    style.caret_hidden = props->traits.caretHidden;
    rn_text_input_set_style(input, &style);
    rn_text_input_set_placeholder(input, props->placeholder.c_str());
    rn_text_input_set_editable(input, props->editable && !props->readOnly &&
                                          props->traits.editable);
    rn_text_input_set_max_length(
        input, props->maxLength == INT_MAX ? 0 : props->maxLength);
    rn_text_input_set_secure(input, props->traits.secureTextEntry);
    rn_text_input_set_input_purpose(
        input,
        props->traits.secureTextEntry ? GTK_INPUT_PURPOSE_PASSWORD
                                      : input_purpose(props->traits.keyboardType),
        input_hints(props->traits));
    auto submit = props->getNonDefaultSubmitBehavior();
    rn_text_input_set_submit_on_enter(input, submit != SubmitBehavior::Newline);
  }

  // JS's value, once JS has seen all our edits.
  if (auto state =
          std::dynamic_pointer_cast<const TextInputShadowNode::ConcreteState>(
              newView.state)) {
    const auto &data = state->getData();
    if (data.mostRecentEventCount == tracking.nativeEventCount) {
      std::string text = data.attributedStringBox.getValue().getString();
      gchar *current = rn_text_input_get_text(input);
      bool differs = text != current;
      g_free(current);
      if (differs) rn_text_input_set_text(input, text.c_str());
      if (props->selection && (oldView.props != newView.props)) {
        rn_text_input_set_selection(input, props->selection->start,
                                    props->selection->end);
      }
    }
  }

  if (props->autoFocus && !tracking.autoFocused) {
    tracking.autoFocused = true;
    // After it's in the window.
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer w) -> gboolean {
          rn_text_input_focus(RN_TEXT_INPUT(w));
          return G_SOURCE_REMOVE;
        },
        g_object_ref(widget), g_object_unref);
  }
}

bool GtkMountingManager::textInputCommand(GtkWidget *widget, Tag tag,
                                          const std::string &name,
                                          const folly::dynamic &args) {
  auto *input = RN_TEXT_INPUT(widget);
  if (name == "focus") {
    rn_text_input_focus(input);
    return true;
  }
  if (name == "blur") {
    rn_text_input_blur(input);
    return true;
  }
  if (name == "setTextAndSelection") {
    // [mostRecentEventCount, value | null, start, end]; stale ones drop.
    auto &tracking = textInputs_[tag];
    if (!args.isArray() || args.size() < 4) return true;
    if (args[0].isNumber() && args[0].asInt() < tracking.nativeEventCount) {
      return true;
    }
    if (args[1].isString()) {
      rn_text_input_set_text(input, args[1].getString().c_str());
    }
    if (args[2].isNumber() && args[3].isNumber() && args[2].asInt() >= 0) {
      rn_text_input_set_selection(input, int(args[2].asInt()), int(args[3].asInt()));
    }
    return true;
  }
  return false;
}

void GtkMountingManager::onTextInputEvent(Tag tag, const std::string &type,
                                          const std::string &arg) {
  GtkWidget *widget = viewForTag(tag);
  auto view = shadowViews_.find(tag);
  if (!widget || view == shadowViews_.end()) return;
  auto *input = RN_TEXT_INPUT(widget);
  auto emitter = std::dynamic_pointer_cast<const TextInputEventEmitter>(
      view->second.eventEmitter);
  auto props = std::dynamic_pointer_cast<const TextInputProps>(view->second.props);
  if (!emitter || !props) return;
  auto &tracking = textInputs_[tag];

  gchar *raw = rn_text_input_get_text(input);
  std::string text = raw;
  g_free(raw);
  int start = 0, end = 0;
  rn_text_input_get_selection(input, &start, &end);
  const auto &frame = view->second.layoutMetrics.frame;
  const EdgeInsets &insets = view->second.layoutMetrics.contentInsets;

  TextInputEventEmitter::Metrics metrics{};
  metrics.text = text;
  metrics.selectionRange = {.location = start, .length = end - start};
  metrics.eventCount = tracking.nativeEventCount;
  metrics.target = tag;
  metrics.containerSize = frame.size;
  metrics.layoutMeasurement = frame.size;
  metrics.zoomScale = 1;
  // Content size as the shadow node measures it (same Pango layout).
  {
    float width = frame.size.width - insets.left - insets.right;
    PangoLayout *layout = create_pango_layout(
        pango_context_for_current_thread(), attributed(*props, text.empty() ? " " : text),
        props->paragraphAttributes,
        rn_text_input_is_multiline(input) ? width : -1);
    float w = 0, h = 0;
    pango_layout_size_px(layout, &w, &h);
    g_object_unref(layout);
    metrics.contentSize = {.width = text.empty() ? 0 : w, .height = h};
  }

  if (type == "change") {
    tracking.nativeEventCount++;
    metrics.eventCount = tracking.nativeEventCount;
    // The shadow node measures from the state: keep it current.
    if (auto state = std::dynamic_pointer_cast<const TextInputShadowNode::ConcreteState>(
            view->second.state)) {
      const auto &data = state->getData();
      // The shadow node only measures a state whose react-tree string has
      // the layout's font size multiplier; the initial state's has none.
      // Its fragments (what JS last set) stay as they are.
      AttributedString reactTree = data.reactTreeAttributedString;
      reactTree.setBaseTextAttributes(props->getEffectiveTextAttributes(1));
      state->updateState(TextInputState{
          AttributedStringBox{attributed(*props, text)}, reactTree,
          data.paragraphAttributes, tracking.nativeEventCount});
    }
    emitter->onChange(metrics);
    if (rn_text_input_is_multiline(input) &&
        (metrics.contentSize.width != tracking.contentSize.width ||
         metrics.contentSize.height != tracking.contentSize.height)) {
      tracking.contentSize = metrics.contentSize;
      emitter->onContentSizeChange(metrics);
    }
  } else if (type == "selection") {
    emitter->onSelectionChange(metrics);
  } else if (type == "focus") {
    if (props->traits.clearTextOnFocus && !text.empty()) {
      rn_text_input_set_text(input, "");
      onTextInputEvent(tag, "change", "");
    }
    if (props->traits.selectTextOnFocus) {
      rn_text_input_set_selection(input, 0, int(g_utf8_strlen(text.c_str(), -1)));
    }
    emitter->onFocus(metrics);
  } else if (type == "blur") {
    emitter->onEndEditing(metrics);
    emitter->onBlur(metrics);
  } else if (type == "submit") {
    emitter->onSubmitEditing(metrics);
    // Single line: blur unless told otherwise; multiline: only with
    // blurAndSubmit.
    auto behavior = props->getNonDefaultSubmitBehavior();
    if (behavior == SubmitBehavior::BlurAndSubmit) rn_text_input_blur(input);
  } else if (type == "key") {
    emitter->onKeyPress({.text = arg, .eventCount = tracking.nativeEventCount});
  }
}

}  // namespace rngtk
