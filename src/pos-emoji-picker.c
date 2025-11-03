/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Heavily based on GTK's:
 * gtkemojichooser.c: An Emoji chooser widget
 * Copyright 2017, Red Hat, Inc.
 * Author: Matthias Clasen <mclasen@redhat.com>
 */

#include "pos-emoji-db.h"
#include "pos-emoji-picker.h"

#define BOX_SPACE 6

enum {
  EMOJI_PICKED,
  DELETE_LAST,
  DONE,
  LAST_SIGNAL
};

static int signals[LAST_SIGNAL];

typedef struct {
  GtkWidget *box;
  GtkWidget *button;
  int        group;
  gunichar   label;
  gboolean   empty;
} EmojiSection;

/**
 * PosEmojiPicker
 *
 * A widget to pick emojis from.
 */
struct _PosEmojiPicker {
  GtkBox        parent_instance;

  GtkWidget    *scrolled_window;

  int           emoji_max_width;

  GtkScrolledWindow *scrolled_sections;

  EmojiSection  recent;
  EmojiSection  people;
  EmojiSection  body;
  EmojiSection  nature;
  EmojiSection  food;
  EmojiSection  travel;
  EmojiSection  activities;
  EmojiSection  objects;
  EmojiSection  symbols;
  EmojiSection  flags;

  GtkGesture   *recent_long_press;
  GtkGesture   *recent_multi_press;
  GtkGesture   *people_long_press;
  GtkGesture   *people_multi_press;
  GtkGesture   *body_long_press;
  GtkGesture   *body_multi_press;

  GVariant     *data;
  GtkWidget    *box;
  GVariantIter *iter;
  guint         populate_idle;

  GSettings    *settings;
};

G_DEFINE_TYPE (PosEmojiPicker, pos_emoji_picker, GTK_TYPE_BOX)

static void
on_done_clicked (PosEmojiPicker *self)
{
  g_signal_emit (self, signals[DONE], 0);
}

static void
on_backspace_clicked (PosEmojiPicker *self)
{
  g_signal_emit (self, signals[DELETE_LAST], 0);
}


static void
scroll_to_section (GtkButton *button,
                   gpointer   data)
{
  EmojiSection *section = data;
  PosEmojiPicker *self;
  GtkAdjustment *adj;
  GtkAllocation alloc = { 0 };
  double value;

  self = POS_EMOJI_PICKER (gtk_widget_get_ancestor (GTK_WIDGET (button), POS_TYPE_EMOJI_PICKER));

  adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));

  gtk_widget_get_allocation (section->box, &alloc);

  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL) {
    double width = gtk_widget_get_allocated_width (GTK_WIDGET (self));
    value = alloc.x + alloc.width - width;
  } else {
    value = alloc.x - BOX_SPACE;
  }

  gtk_adjustment_set_value (adj, value);
}


static void
add_emoji (GtkWidget      *box,
           gboolean        prepend,
           GVariant       *item,
           gunichar        modifier,
           PosEmojiPicker *self);

#define MAX_RECENT (4 * 4)

static void
populate_recent_section (PosEmojiPicker *self)
{
  g_autoptr (GVariant) variant = NULL;
  GVariant *item;
  GVariantIter iter;
  gboolean empty = TRUE;

  variant = g_settings_get_value (self->settings, "recent-emoji");
  g_variant_iter_init (&iter, variant);
  while ((item = g_variant_iter_next_value (&iter))) {
    g_autoptr (GVariant) emoji_data = NULL;
    gunichar modifier;

    emoji_data = g_variant_get_child_value (item, 0);
    g_variant_get_child (item, 1, "u", &modifier);
    add_emoji (self->recent.box, FALSE, emoji_data, modifier, self);
    g_variant_unref (item);
    empty = FALSE;
  }

  gtk_widget_set_visible (self->recent.box, !empty);
  gtk_widget_set_sensitive (self->recent.button, !empty);
}

static GVariant *
get_recent_emoji_data (GtkWidget *widget)
{
  GVariant *emoji_data = g_object_get_data (G_OBJECT (widget), "emoji-data");
  GVariantIter *codes_iter;
  GVariantIter *keywords_iter;
  GVariantBuilder codes_builder;
  const char *name;
  const char *shortname;
  guint code;
  guint group;

  g_assert (emoji_data);

  if (g_variant_is_of_type (emoji_data, G_VARIANT_TYPE ("(auss)")))
    return emoji_data;

  g_variant_get (emoji_data, "(au&sasu)", &codes_iter, &name, &keywords_iter, &group);

  g_variant_builder_init (&codes_builder, G_VARIANT_TYPE ("au"));
  while (g_variant_iter_loop (codes_iter, "u", &code))
    g_variant_builder_add (&codes_builder, "u", code);

  g_variant_iter_free (codes_iter);
  g_variant_iter_free (keywords_iter);

  shortname = "";

  return g_variant_new ("(auss)", &codes_builder, name, shortname);
}

static void
add_recent_item (PosEmojiPicker *self, GVariant *item, gunichar modifier)
{
  GList *children, *l;
  int i;
  GVariantBuilder builder;

  g_variant_ref (item);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a((auss)u)"));
  g_variant_builder_add (&builder, "(@(auss)u)", item, modifier);

  children = gtk_container_get_children (GTK_CONTAINER (self->recent.box));
  for (l = children, i = 1; l; l = l->next, i++) {
    GVariant *item2 = get_recent_emoji_data (GTK_WIDGET (l->data));
    gunichar modifier2 = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (l->data), "modifier"));

    if (modifier == modifier2 && g_variant_equal (item, item2)) {
      gtk_widget_destroy (GTK_WIDGET (l->data));
      i--;
      continue;
    }
    if (i >= MAX_RECENT) {
      gtk_widget_destroy (GTK_WIDGET (l->data));
      continue;
    }

    g_variant_builder_add (&builder, "(@(auss)u)", item2, modifier2);
  }
  g_list_free (children);

  add_emoji (self->recent.box, TRUE, item, modifier, self);

  /* Enable recent */
  gtk_widget_set_visible (self->recent.box, TRUE);
  gtk_widget_set_sensitive (self->recent.button, TRUE);

  g_settings_set_value (self->settings, "recent-emoji", g_variant_builder_end (&builder));

  g_variant_unref (item);
}


static void
on_emoji_activated (GtkFlowBox *box, GtkFlowBoxChild *child, gpointer data)
{
  PosEmojiPicker *self = POS_EMOJI_PICKER (data);
  g_autofree char *text = NULL;
  GtkWidget *label;
  GVariant *item;
  gunichar modifier;

  label = gtk_bin_get_child (GTK_BIN (child));
  text = g_strdup (gtk_label_get_label (GTK_LABEL (label)));

  item = get_recent_emoji_data (GTK_WIDGET (child));
  modifier = (gunichar) GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (child), "modifier"));
  add_recent_item (self, item, modifier);

  g_signal_emit (data, signals[EMOJI_PICKED], 0, text);
}

static gboolean
has_variations (GVariant *emoji_data)
{
  GVariant *codes;
  int i;
  gboolean has_variations;

  has_variations = FALSE;
  codes = g_variant_get_child_value (emoji_data, 0);
  for (i = 0; i < g_variant_n_children (codes); i++) {
    gunichar code;
    g_variant_get_child (codes, i, "u", &code);
    if (code == 0) {
      has_variations = TRUE;
      break;
    }
  }
  g_variant_unref (codes);

  return has_variations;
}

static void
show_variations (PosEmojiPicker *self, GtkWidget *child)
{
  GtkWidget *popover;
  GtkWidget *view;
  GtkWidget *box;
  GVariant *emoji_data;
  GtkWidget *parent;
  gunichar modifier;

  if (!child)
    return;

  emoji_data = (GVariant*) g_object_get_data (G_OBJECT (child), "emoji-data");
  if (!emoji_data)
    return;

  if (!has_variations (emoji_data))
    return;

  parent = gtk_widget_get_ancestor (child, POS_TYPE_EMOJI_PICKER);
  popover = gtk_popover_new (child);
  view = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_style_context_add_class (gtk_widget_get_style_context (view), "view");
  box = gtk_flow_box_new ();
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (box), TRUE);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (box), 6);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (box), 6);
  gtk_flow_box_set_activate_on_single_click (GTK_FLOW_BOX (box), TRUE);
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (box), GTK_SELECTION_NONE);
  gtk_container_add (GTK_CONTAINER (popover), view);
  gtk_container_add (GTK_CONTAINER (view), box);

  g_signal_connect (box, "child-activated", G_CALLBACK (on_emoji_activated), parent);

  add_emoji (box, FALSE, emoji_data, 0, self);
  for (modifier = 0x1f3fb; modifier <= 0x1f3ff; modifier++)
    add_emoji (box, FALSE, emoji_data, modifier, self);

  gtk_widget_show_all (view);
  gtk_popover_popup (GTK_POPOVER (popover));
}


static void
long_pressed_cb (GtkGesture *gesture, double x, double y, gpointer data)
{
  PosEmojiPicker *self = data;
  GtkWidget *box;
  GtkWidget *child;

  box = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  child = GTK_WIDGET (gtk_flow_box_get_child_at_pos (GTK_FLOW_BOX (box), x, y));
  show_variations (self, child);
}

static gboolean
popup_menu (GtkWidget *widget, gpointer data)
{
  PosEmojiPicker *self = data;

  show_variations (self, widget);
  return TRUE;
}

static void
add_emoji (GtkWidget      *box,
           gboolean        prepend,
           GVariant       *item,
           gunichar        modifier,
           PosEmojiPicker *self)
{
  GtkWidget *child;
  GtkWidget *label;
  g_autoptr (PangoAttrList) attrs = NULL;
  GVariant *codes;
  char text[64];
  char *p = text;
  int i;
  PangoLayout *layout;
  PangoRectangle rect;

  codes = g_variant_get_child_value (item, 0);
  for (i = 0; i < g_variant_n_children (codes); i++) {
    gunichar code;

    g_variant_get_child (codes, i, "u", &code);
    if (code == 0)
      code = modifier;
    if (code != 0)
      p += g_unichar_to_utf8 (code, p);
  }
  g_variant_unref (codes);
  p += g_unichar_to_utf8 (0xFE0F, p); /* U+FE0F is the Emoji variation selector */
  p[0] = 0;

  label = gtk_label_new (text);
  attrs = pango_attr_list_new ();
  pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_X_LARGE));
  gtk_label_set_attributes (GTK_LABEL (label), attrs);

  layout = gtk_label_get_layout (GTK_LABEL (label));
  pango_layout_get_extents (layout, &rect, NULL);

  /* Check for fallback rendering that generates too wide items */
  if (pango_layout_get_unknown_glyphs_count (layout) > 0 ||
      rect.width >= 1.5 * self->emoji_max_width) {
    gtk_widget_destroy (label);
    return;
  }

  child = gtk_flow_box_child_new ();
  gtk_style_context_add_class (gtk_widget_get_style_context (child), "emoji");
  g_object_set_data_full (G_OBJECT (child), "emoji-data",
                          g_variant_ref (item),
                          (GDestroyNotify)g_variant_unref);
  if (modifier != 0)
    g_object_set_data (G_OBJECT (child), "modifier", GUINT_TO_POINTER (modifier));

  gtk_container_add (GTK_CONTAINER (child), label);
  gtk_widget_show_all (child);

  if (self)
    g_signal_connect (child, "popup-menu", G_CALLBACK (popup_menu), self);

  gtk_flow_box_insert (GTK_FLOW_BOX (box), child, prepend ? 0 : -1);
}


static gboolean
populate_emoji_chooser (gpointer data)
{
  PosEmojiPicker *self = data;
  GVariant *item;
  guint64 start, now;

  start = g_get_monotonic_time ();

  if (!self->data) {
    PosEmojiDb *emoji_db = pos_emoji_db_get_default ();

    self->data = g_variant_ref (pos_emoji_db_get_data (emoji_db));
  }

  if (!self->iter) {
    self->iter = g_variant_iter_new (self->data);
    self->box = self->people.box;
  }
  while ((item = g_variant_iter_next_value (self->iter))) {
    guint group;

    g_variant_get_child (item, 3, "u", &group);

    if (group == self->people.group)
      self->box = self->people.box;
    else if (group == self->body.group)
      self->box = self->body.box;
    else if (group == self->nature.group)
      self->box = self->nature.box;
    else if (group == self->food.group)
      self->box = self->food.box;
    else if (group == self->travel.group)
      self->box = self->travel.box;
    else if (group == self->activities.group)
      self->box = self->activities.box;
    else if (group == self->objects.group)
      self->box = self->objects.box;
    else if (group == self->symbols.group)
      self->box = self->symbols.box;
    else if (group == self->flags.group)
      self->box = self->flags.box;

    add_emoji (self->box, FALSE, item, 0, self);
    g_variant_unref (item);

    now = g_get_monotonic_time ();
    if (now > start + 8000)
      return G_SOURCE_CONTINUE;
  }

  g_variant_iter_free (self->iter);
  self->iter = NULL;
  self->box = NULL;
  self->populate_idle = 0;

  return G_SOURCE_REMOVE;
}

static void
adj_value_changed (GtkAdjustment *adj, gpointer data)
{
  PosEmojiPicker *self = data;
  double width, value;
  EmojiSection const *sections[] = {
    &self->recent,
    &self->people,
    &self->body,
    &self->nature,
    &self->food,
    &self->travel,
    &self->activities,
    &self->objects,
    &self->symbols,
    &self->flags,
  };
  EmojiSection const *select_section = sections[0];
  gsize i;

  /* Figure out which section the current scroll position is within */
  width = gtk_widget_get_allocated_width (GTK_WIDGET (self));
  value = gtk_adjustment_get_value (adj);
  for (i = 0; i < G_N_ELEMENTS (sections); ++i) {
    EmojiSection const *section = sections[i];
    GtkAllocation alloc;

    if (!gtk_widget_get_visible (section->box))
      continue;

    gtk_widget_get_allocation (section->box, &alloc);

    if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL) {
      if (alloc.x == -1 || value > alloc.x + alloc.width - width)
        break;
    } else {
      if (alloc.x == -1 || value < alloc.x - BOX_SPACE)
        break;
    }

    select_section = section;
  }

  /* Un/Check the section buttons accordingly */
  for (i = 0; i < G_N_ELEMENTS (sections); ++i) {
    EmojiSection const *section = sections[i];

    /* TODO: scroll button into view */
    if (section == select_section) {
      GtkAdjustment *adj_sections;
      GtkAllocation alloc_btn = { 0 }, alloc_view = { 0 };
      double v, pos;

      gtk_widget_set_state_flags (section->button, GTK_STATE_FLAG_CHECKED, FALSE);

      /* Scroll section button into view */
      adj_sections = gtk_scrolled_window_get_hadjustment (self->scrolled_sections);
      gtk_widget_get_allocation (section->button, &alloc_btn);
      gtk_widget_get_allocation (GTK_WIDGET (self->scrolled_sections), &alloc_view);
      v = gtk_adjustment_get_value (adj_sections);

      /* Button is out of view to the right */
      if (alloc_btn.x + alloc_btn.width > v + alloc_view.width) {
        pos = (alloc_btn.x + alloc_btn.width - alloc_view.width);
        gtk_adjustment_set_value (adj_sections, pos);
      /* Button is out of view to the left */
      } else if (alloc_btn.x < v) {
        pos = alloc_btn.x;
        gtk_adjustment_set_value (adj_sections, alloc_btn.x);
      }

    } else {
      gtk_widget_unset_state_flags (section->button, GTK_STATE_FLAG_CHECKED);
    }
  }
}

static void
setup_section (PosEmojiPicker *self,
               EmojiSection   *section,
               int             group,
               const char     *icon)
{
  GtkWidget *image;

  section->group = group;

  image = gtk_bin_get_child (GTK_BIN (section->button));
  gtk_image_set_from_icon_name (GTK_IMAGE (image), icon, GTK_ICON_SIZE_BUTTON);

  g_signal_connect (section->button, "clicked", G_CALLBACK (scroll_to_section), section);
}

static void
pos_emoji_picker_init (PosEmojiPicker *self)
{
  GtkAdjustment *adj;

  self->settings = g_settings_new ("sm.puri.phosh.osk.EmojiPicker");

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Get a reasonable maximum width for an emoji. We do this to
   * skip overly wide fallback rendering for certain emojis the
   * font does not contain and therefore end up being rendered
   * as multiply glyphs.
   */
  {
    g_autoptr (PangoLayout) layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), "🙂");
    g_autoptr (PangoAttrList) attrs = NULL;
    PangoRectangle rect;

    attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_X_LARGE));
    pango_layout_set_attributes (layout, attrs);

    pango_layout_get_extents (layout, &rect, NULL);
    self->emoji_max_width = rect.width;
  }

  self->recent_long_press = gtk_gesture_long_press_new (self->recent.box);
  g_signal_connect (self->recent_long_press, "pressed", G_CALLBACK (long_pressed_cb), self);

  self->people_long_press = gtk_gesture_long_press_new (self->people.box);
  g_signal_connect (self->people_long_press, "pressed", G_CALLBACK (long_pressed_cb), self);

  self->body_long_press = gtk_gesture_long_press_new (self->body.box);
  g_signal_connect (self->body_long_press, "pressed", G_CALLBACK (long_pressed_cb), self);

  adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));
  g_signal_connect (adj, "value-changed", G_CALLBACK (adj_value_changed), self);

  setup_section (self, &self->recent, -1, "emoji-recent-symbolic");
  setup_section (self, &self->people, 0, "emoji-people-symbolic");
  setup_section (self, &self->body, 1, "emoji-body-symbolic");
  setup_section (self, &self->nature, 3, "emoji-nature-symbolic");
  setup_section (self, &self->food, 4, "emoji-food-symbolic");
  setup_section (self, &self->travel, 5, "emoji-travel-symbolic");
  setup_section (self, &self->activities, 6, "emoji-activities-symbolic");
  setup_section (self, &self->objects, 7, "emoji-objects-symbolic");
  setup_section (self, &self->symbols, 8, "emoji-symbols-symbolic");
  setup_section (self, &self->flags, 9, "emoji-flags-symbolic");

  populate_recent_section (self);

  self->populate_idle = g_idle_add (populate_emoji_chooser, self);
  g_source_set_name_by_id (self->populate_idle, "[pos] populate_emoji_chooser");
}

static void
pos_emoji_picker_show (GtkWidget *widget)
{
  PosEmojiPicker *self = POS_EMOJI_PICKER (widget);
  GtkAdjustment *adj;

  GTK_WIDGET_CLASS (pos_emoji_picker_parent_class)->show (widget);

  adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));
  gtk_adjustment_set_value (adj, 0);
  adj_value_changed (adj, self);
}

static void
pos_emoji_picker_finalize (GObject *object)
{
  PosEmojiPicker *self = POS_EMOJI_PICKER (object);

  if (self->populate_idle)
    g_source_remove (self->populate_idle);

  g_clear_pointer (&self->data, g_variant_unref);
  g_object_unref (self->settings);

  g_clear_object (&self->recent_long_press);
  g_clear_object (&self->recent_multi_press);
  g_clear_object (&self->people_long_press);
  g_clear_object (&self->people_multi_press);
  g_clear_object (&self->body_long_press);
  g_clear_object (&self->body_multi_press);

  G_OBJECT_CLASS (pos_emoji_picker_parent_class)->finalize (object);
}

static void
pos_emoji_picker_class_init (PosEmojiPickerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = pos_emoji_picker_finalize;
  widget_class->show = pos_emoji_picker_show;

  /**
   * PosEmojiPicker::emoji-picked
   * @self: The emoji picker
   * @emoji: The emoji picked by the user.
   *
   * The user picked an emoji.
   */
  signals[EMOJI_PICKED] =
    g_signal_new ("emoji-picked",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 1, G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);
  /**
   * PosEmojiPicker::done:
   *
   * The user is done picking emojis.
   */
  signals[DONE] =
    g_signal_new ("done",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  /**
   * PosEmojiPicker::delete-last:
   *
   * Delete the last entered character.
   *
   * TODO: we can make this more flexible by allowing to add
   * arbitrary widgets to an `action-box`. This would also make
   * it simple to send custom events, use the same key repeat logic
   * etc.
   */
  signals[DELETE_LAST] =
    g_signal_new ("delete-last",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/stevia/ui/emoji-picker.ui");

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, scrolled_sections);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, recent.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, recent.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, people.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, people.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, body.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, body.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, nature.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, nature.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, food.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, food.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, travel.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, travel.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, activities.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, activities.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, objects.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, objects.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, symbols.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, symbols.button);

  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, flags.box);
  gtk_widget_class_bind_template_child (widget_class, PosEmojiPicker, flags.button);

  gtk_widget_class_bind_template_callback (widget_class, on_emoji_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_done_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_backspace_clicked);

  gtk_widget_class_set_css_name (widget_class, "pos-emoji-picker");
}

GtkWidget *
pos_emoji_picker_new (void)
{
  return GTK_WIDGET (g_object_new (POS_TYPE_EMOJI_PICKER, NULL));
}
