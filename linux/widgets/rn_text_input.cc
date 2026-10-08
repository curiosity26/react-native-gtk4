#include "rn_text_input.h"

#include "rn_css.h"
#include "rn_view.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

struct _RNTextInput {
  GtkWidget parent_instance;
  gboolean multiline;
  GtkWidget *background;   // RNView
  GtkWidget *editor;       // GtkText, or GtkTextView
  GtkWidget *scroller;     // multiline: GtkScrolledWindow around the view
  GtkWidget *placeholder;  // multiline: GtkLabel (GtkText has its own)
  float insets[4];         // top, right, bottom, left
  int max_length;
  gboolean submit_on_enter;
  int programmatic;        // > 0 while we change the text ourselves
};

G_DEFINE_FINAL_TYPE(RNTextInput, rn_text_input, GTK_TYPE_WIDGET)

enum {
  SIGNAL_TEXT_CHANGED,
  SIGNAL_SELECTION_CHANGED,
  SIGNAL_FOCUS_CHANGED,
  SIGNAL_SUBMIT,
  SIGNAL_KEY_PRESSED,
  N_SIGNALS,
};
static guint signals[N_SIGNALS];

static GtkTextBuffer *buffer(RNTextInput *self) {
  return gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->editor));
}

static void update_placeholder(RNTextInput *self) {
  if (!self->placeholder) return;
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buffer(self), &start, &end);
  gtk_widget_set_visible(self->placeholder, gtk_text_iter_equal(&start, &end));
}

static void on_changed(gpointer, gpointer data) {
  auto *self = RN_TEXT_INPUT(data);
  update_placeholder(self);
  if (self->programmatic == 0) {
    g_signal_emit(self, signals[SIGNAL_TEXT_CHANGED], 0);
  }
}

static void on_selection(GObject *, GParamSpec *, gpointer data) {
  g_signal_emit(data, signals[SIGNAL_SELECTION_CHANGED], 0);
}

static void on_mark_set(GtkTextBuffer *, GtkTextIter *, GtkTextMark *mark,
                        gpointer data) {
  auto *self = RN_TEXT_INPUT(data);
  GtkTextBuffer *b = buffer(self);
  if (mark == gtk_text_buffer_get_insert(b) ||
      mark == gtk_text_buffer_get_selection_bound(b)) {
    g_signal_emit(self, signals[SIGNAL_SELECTION_CHANGED], 0);
  }
}

// maxLength for the text view (GtkText has max-length itself): cut the
// insertion to what still fits.
static void on_insert_text(GtkTextBuffer *b, GtkTextIter *location,
                           char *text, int len, gpointer data) {
  auto *self = RN_TEXT_INPUT(data);
  if (self->max_length <= 0 || self->programmatic > 0) return;
  int current = gtk_text_buffer_get_char_count(b);
  glong inserted = g_utf8_strlen(text, len);
  if (current + inserted <= self->max_length) return;
  g_signal_stop_emission_by_name(b, "insert-text");
  int room = std::max(0, self->max_length - current);
  if (room == 0) return;
  const char *cut = g_utf8_offset_to_pointer(text, room);
  g_signal_handlers_block_by_func(b, (gpointer)on_insert_text, data);
  gtk_text_buffer_insert(b, location, text, int(cut - text));
  g_signal_handlers_unblock_by_func(b, (gpointer)on_insert_text, data);
}

static void on_activate(GtkText *, gpointer data) {
  g_signal_emit(data, signals[SIGNAL_SUBMIT], 0);
}

// RN's onKeyPress names: the character typed, or Enter/Backspace/Tab...
static gboolean on_key_pressed(GtkEventControllerKey *, guint keyval, guint,
                               GdkModifierType state, gpointer data) {
  auto *self = RN_TEXT_INPUT(data);
  const char *name = nullptr;
  char utf8[8] = {0};
  switch (keyval) {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
      name = "Enter";
      break;
    case GDK_KEY_BackSpace:
      name = "Backspace";
      break;
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab:
      name = "Tab";
      break;
    case GDK_KEY_Escape:
      name = "Escape";
      break;
    case GDK_KEY_Delete:
      name = "Delete";
      break;
    default: {
      if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) break;
      gunichar c = gdk_keyval_to_unicode(keyval);
      if (c && g_unichar_isprint(c)) {
        utf8[g_unichar_to_utf8(c, utf8)] = 0;
        name = utf8;
      }
    }
  }
  if (name) g_signal_emit(self, signals[SIGNAL_KEY_PRESSED], 0, name);
  // Multiline with submitBehavior submit: Enter submits, no newline.
  if (self->multiline && self->submit_on_enter && name &&
      !strcmp(name, "Enter") && !(state & GDK_SHIFT_MASK)) {
    g_signal_emit(self, signals[SIGNAL_SUBMIT], 0);
    return TRUE;
  }
  return FALSE;
}

static void on_focus_enter(GtkEventControllerFocus *, gpointer data) {
  g_signal_emit(data, signals[SIGNAL_FOCUS_CHANGED], 0, TRUE);
}

static void on_focus_leave(GtkEventControllerFocus *, gpointer data) {
  g_signal_emit(data, signals[SIGNAL_FOCUS_CHANGED], 0, FALSE);
}

static void rn_text_input_measure(GtkWidget *widget, GtkOrientation o, int,
                                  int *minimum, int *natural, int *min_baseline,
                                  int *nat_baseline) {
  // Yoga owns the frame, as for RNView.
  graphene_rect_t f = rn_widget_get_frame(widget);
  *minimum = 0;
  *natural = int(o == GTK_ORIENTATION_HORIZONTAL ? f.size.width : f.size.height);
  *min_baseline = *nat_baseline = -1;
}

static void rn_text_input_size_allocate(GtkWidget *widget, int width,
                                        int height, int) {
  auto *self = RN_TEXT_INPUT(widget);
  gtk_widget_allocate(self->background, width, height, -1, nullptr);
  const float *in = self->insets;
  int cw = std::max(0, int(width - in[1] - in[3]));
  int ch = std::max(0, int(height - in[0] - in[2]));
  GtkWidget *child = self->multiline ? self->scroller : self->editor;
  // GTK wants at least the editor's minimum; RN may size it smaller.
  int min_h = 0, nat;
  gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, cw, &min_h, &nat, nullptr,
                     nullptr);
  int h = std::max(ch, min_h);
  graphene_point_t at = GRAPHENE_POINT_INIT(in[3], in[0] + (ch - h) / 2.0f);
  gtk_widget_allocate(child, cw, h, -1, gsk_transform_translate(nullptr, &at));
  if (self->placeholder && gtk_widget_get_visible(self->placeholder)) {
    int pw, ph, unused;
    gtk_widget_measure(self->placeholder, GTK_ORIENTATION_VERTICAL, cw, &ph,
                       &unused, nullptr, nullptr);
    gtk_widget_measure(self->placeholder, GTK_ORIENTATION_HORIZONTAL, -1, &pw,
                       &unused, nullptr, nullptr);
    graphene_point_t p = GRAPHENE_POINT_INIT(in[3], in[0]);
    gtk_widget_allocate(self->placeholder, cw, std::max(ph, ch), -1,
                        gsk_transform_translate(nullptr, &p));
  }
}

static void rn_text_input_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  auto *self = RN_TEXT_INPUT(widget);
  gtk_widget_snapshot_child(widget, self->background, snapshot);
  // Text doesn't spill over the input's box.
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(
      0, 0, float(gtk_widget_get_width(widget)),
      float(gtk_widget_get_height(widget)));
  gtk_snapshot_push_clip(snapshot, &bounds);
  gtk_widget_snapshot_child(widget, self->multiline ? self->scroller : self->editor,
                            snapshot);
  if (self->placeholder) {
    gtk_widget_snapshot_child(widget, self->placeholder, snapshot);
  }
  gtk_snapshot_pop(snapshot);
}

static gboolean rn_text_input_grab_focus(GtkWidget *widget) {
  return gtk_widget_grab_focus(RN_TEXT_INPUT(widget)->editor);
}

static void rn_text_input_dispose(GObject *object) {
  auto *self = RN_TEXT_INPUT(object);
  g_clear_pointer(&self->background, gtk_widget_unparent);
  g_clear_pointer(&self->placeholder, gtk_widget_unparent);
  if (self->multiline) {
    g_clear_pointer(&self->scroller, gtk_widget_unparent);
    self->editor = nullptr;
  } else {
    g_clear_pointer(&self->editor, gtk_widget_unparent);
  }
  G_OBJECT_CLASS(rn_text_input_parent_class)->dispose(object);
}

static void rn_text_input_class_init(RNTextInputClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = rn_text_input_dispose;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->measure = rn_text_input_measure;
  widget_class->size_allocate = rn_text_input_size_allocate;
  widget_class->snapshot = rn_text_input_snapshot;
  widget_class->grab_focus = rn_text_input_grab_focus;
  gtk_widget_class_set_css_name(widget_class, "rn-text-input");
  const char *names[] = {"text-changed", "selection-changed"};
  for (int i = 0; i < 2; i++) {
    signals[i] = g_signal_new(names[i], G_TYPE_FROM_CLASS(klass),
                              G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr,
                              G_TYPE_NONE, 0);
  }
  signals[SIGNAL_FOCUS_CHANGED] = g_signal_new(
      "focus-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, nullptr,
      nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  signals[SIGNAL_SUBMIT] =
      g_signal_new("submit", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                   nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  signals[SIGNAL_KEY_PRESSED] = g_signal_new(
      "key-pressed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, nullptr,
      nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void rn_text_input_init(RNTextInput *self) {
  self->background = rn_view_new();
  gtk_widget_set_parent(self->background, GTK_WIDGET(self));
}

GtkWidget *rn_text_input_new(gboolean multiline) {
  auto *self = RN_TEXT_INPUT(g_object_new(RN_TYPE_TEXT_INPUT, nullptr));
  self->multiline = multiline;
  if (multiline) {
    self->editor = gtk_text_view_new();
    GtkTextView *view = GTK_TEXT_VIEW(self->editor);
    gtk_text_view_set_wrap_mode(view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(view, 0);
    gtk_text_view_set_right_margin(view, 0);
    gtk_text_view_set_top_margin(view, 0);
    gtk_text_view_set_bottom_margin(view, 0);
    gtk_text_view_set_accepts_tab(view, FALSE);
    self->scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scroller),
                                  self->editor);
    gtk_widget_set_parent(self->scroller, GTK_WIDGET(self));
    GtkTextBuffer *b = buffer(self);
    g_signal_connect(b, "changed", G_CALLBACK(on_changed), self);
    g_signal_connect(b, "mark-set", G_CALLBACK(on_mark_set), self);
    g_signal_connect(b, "insert-text", G_CALLBACK(on_insert_text), self);
    self->placeholder = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(self->placeholder), 0);
    gtk_label_set_yalign(GTK_LABEL(self->placeholder), 0);
    gtk_label_set_wrap(GTK_LABEL(self->placeholder), TRUE);
    gtk_widget_set_can_target(self->placeholder, FALSE);
    gtk_widget_set_parent(self->placeholder, GTK_WIDGET(self));
  } else {
    self->editor = gtk_text_new();
    gtk_widget_set_parent(self->editor, GTK_WIDGET(self));
    g_signal_connect(self->editor, "changed", G_CALLBACK(on_changed), self);
    g_signal_connect(self->editor, "notify::cursor-position",
                     G_CALLBACK(on_selection), self);
    g_signal_connect(self->editor, "notify::selection-bound",
                     G_CALLBACK(on_selection), self);
    g_signal_connect(self->editor, "activate", G_CALLBACK(on_activate), self);
  }
  gtk_widget_add_css_class(self->editor, "rn-text-input-editor");

  GtkEventController *keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), self);
  gtk_widget_add_controller(self->editor, keys);
  GtkEventController *focus = gtk_event_controller_focus_new();
  g_signal_connect(focus, "enter", G_CALLBACK(on_focus_enter), self);
  g_signal_connect(focus, "leave", G_CALLBACK(on_focus_leave), self);
  gtk_widget_add_controller(self->editor, focus);
  update_placeholder(self);
  return GTK_WIDGET(self);
}

gboolean rn_text_input_is_multiline(RNTextInput *self) {
  return self->multiline;
}

GtkWidget *rn_text_input_get_background(RNTextInput *self) {
  return self->background;
}

GtkWidget *rn_text_input_get_editor(RNTextInput *self) { return self->editor; }

void rn_text_input_set_insets(RNTextInput *self, float top, float right,
                              float bottom, float left) {
  const float in[4] = {top, right, bottom, left};
  if (std::equal(in, in + 4, self->insets)) return;
  std::copy(in, in + 4, self->insets);
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

char *rn_text_input_get_text(RNTextInput *self) {
  if (!self->multiline) return g_strdup(gtk_editable_get_text(GTK_EDITABLE(self->editor)));
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buffer(self), &start, &end);
  return gtk_text_buffer_get_text(buffer(self), &start, &end, FALSE);
}

void rn_text_input_get_selection(RNTextInput *self, int *start, int *end) {
  if (!self->multiline) {
    if (!gtk_editable_get_selection_bounds(GTK_EDITABLE(self->editor), start, end)) {
      *start = *end = gtk_editable_get_position(GTK_EDITABLE(self->editor));
    }
    return;
  }
  GtkTextIter a, b;
  gtk_text_buffer_get_selection_bounds(buffer(self), &a, &b);
  *start = gtk_text_iter_get_offset(&a);
  *end = gtk_text_iter_get_offset(&b);
}

void rn_text_input_set_selection(RNTextInput *self, int start, int end) {
  if (!self->multiline) {
    int len = int(g_utf8_strlen(gtk_editable_get_text(GTK_EDITABLE(self->editor)), -1));
    gtk_editable_select_region(GTK_EDITABLE(self->editor), std::clamp(start, 0, len),
                               std::clamp(end, 0, len));
    return;
  }
  GtkTextIter a, b;
  gtk_text_buffer_get_iter_at_offset(buffer(self), &a, start);
  gtk_text_buffer_get_iter_at_offset(buffer(self), &b, end);
  gtk_text_buffer_select_range(buffer(self), &b, &a);
}

void rn_text_input_set_text(RNTextInput *self, const char *text) {
  char *old = rn_text_input_get_text(self);
  bool same = g_strcmp0(old, text) == 0;
  g_free(old);
  if (same) return;
  int start, end;
  rn_text_input_get_selection(self, &start, &end);
  self->programmatic++;
  if (self->multiline) {
    gtk_text_buffer_set_text(buffer(self), text, -1);
  } else {
    gtk_editable_set_text(GTK_EDITABLE(self->editor), text);
  }
  self->programmatic--;
  // Keep the caret where it was (clamped) rather than at the end.
  int len = int(g_utf8_strlen(text, -1));
  rn_text_input_set_selection(self, std::min(start, len), std::min(end, len));
}

static std::string css_rgba(const GdkRGBA &c) {
  char buf[96];
  snprintf(buf, sizeof buf, "rgba(%d,%d,%d,%.3f)", int(c.red * 255),
           int(c.green * 255), int(c.blue * 255), c.alpha);
  return buf;
}

void rn_text_input_set_style(RNTextInput *self, const RNTextInputStyle *s) {
  std::string font;
  if (s->font_family && *s->font_family) {
    font += "font-family: \"" + std::string(s->font_family) + "\";";
  }
  if (s->font_size > 0) font += "font-size: " + std::to_string(s->font_size) + "px;";
  if (s->font_weight > 0) font += "font-weight: " + std::to_string(s->font_weight) + ";";
  if (s->italic) font += "font-style: italic;";
  if (s->letter_spacing != 0) {
    font += "letter-spacing: " + std::to_string(s->letter_spacing) + "px;";
  }
  std::string caret = s->caret_hidden ? "transparent"
                      : s->caret_color.alpha > 0 ? css_rgba(s->caret_color)
                                                 : css_rgba(s->color);
  std::string text_rules = font + "color: " + css_rgba(s->color) +
                           "; caret-color: " + caret +
                           "; background: transparent; padding: 0; min-height: 0;"
                           " border: none; box-shadow: none; outline: none;";
  // On the RNTextInput: reaches the editor, its text node and placeholder,
  // and the multiline placeholder label.
  std::string css = "& .rn-text-input-editor, & .rn-text-input-editor text {" +
                    text_rules + "}";
  std::string placeholder = font + "color: " + css_rgba(s->placeholder_color) + ";";
  css += "& .rn-text-input-editor > placeholder, & > label {" + placeholder + "}";
  if (s->selection_color.alpha > 0) {
    css += "& .rn-text-input-editor selection, & .rn-text-input-editor text selection "
           "{ background-color: " + css_rgba(s->selection_color) + "; }";
  }
  rn_widget_set_css(GTK_WIDGET(self), css.c_str());
  if (self->multiline) {
    GtkJustification j = s->text_align == 1   ? GTK_JUSTIFY_CENTER
                         : s->text_align == 2 ? GTK_JUSTIFY_RIGHT
                                              : GTK_JUSTIFY_LEFT;
    gtk_text_view_set_justification(GTK_TEXT_VIEW(self->editor), j);
  } else {
    gtk_editable_set_alignment(GTK_EDITABLE(self->editor),
                               s->text_align == 1 ? 0.5f : s->text_align == 2 ? 1.0f : 0.0f);
  }
}

void rn_text_input_set_placeholder(RNTextInput *self, const char *text) {
  if (self->multiline) {
    gtk_label_set_text(GTK_LABEL(self->placeholder), text ? text : "");
  } else {
    gtk_text_set_placeholder_text(GTK_TEXT(self->editor), text);
  }
}

void rn_text_input_set_editable(RNTextInput *self, gboolean editable) {
  if (self->multiline) {
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->editor), editable);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(self->editor), editable);
  } else {
    gtk_editable_set_editable(GTK_EDITABLE(self->editor), editable);
  }
}

void rn_text_input_set_max_length(RNTextInput *self, int max_length) {
  self->max_length = std::max(0, max_length);
  if (!self->multiline) gtk_text_set_max_length(GTK_TEXT(self->editor), self->max_length);
}

void rn_text_input_set_secure(RNTextInput *self, gboolean secure) {
  if (self->multiline) return;  // RN ignores secureTextEntry when multiline
  gtk_text_set_visibility(GTK_TEXT(self->editor), !secure);
}

void rn_text_input_set_input_purpose(RNTextInput *self, GtkInputPurpose purpose,
                                     GtkInputHints hints) {
  if (self->multiline) {
    gtk_text_view_set_input_purpose(GTK_TEXT_VIEW(self->editor), purpose);
    gtk_text_view_set_input_hints(GTK_TEXT_VIEW(self->editor), hints);
  } else {
    gtk_text_set_input_purpose(GTK_TEXT(self->editor), purpose);
    gtk_text_set_input_hints(GTK_TEXT(self->editor), hints);
  }
}

void rn_text_input_set_submit_on_enter(RNTextInput *self, gboolean submit) {
  self->submit_on_enter = submit;
}

void rn_text_input_focus(RNTextInput *self) { gtk_widget_grab_focus(self->editor); }

void rn_text_input_blur(RNTextInput *self) {
  if (!rn_text_input_has_focus(self)) return;
  if (GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(self))) {
    gtk_root_set_focus(root, nullptr);
  }
}

gboolean rn_text_input_has_focus(RNTextInput *self) {
  return gtk_widget_has_focus(self->editor);
}
