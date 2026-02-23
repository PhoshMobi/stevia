/*
 * Copyright (C) 2022 Purism SPC
 *               2023-2024 The Phosh Developers
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-completion-bar"

#include "pos-config.h"

#include "pos-completion-bar.h"
#include "pos-completions-box.h"

enum {
  PROP_0,
  PROP_MODE_NAME,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  SELECTED,
  MODE_PRESSED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

/**
 * PosCompletionBar:
 *
 * A button bar that displays completions and emits "selected" if one
 * is picked.
 */
struct _PosCompletionBar {
  GtkBox parent;

  PosCompletionsBox *completions_box;
  GtkScrolledWindow *scrolled_window;

  char * mode_name;
};
G_DEFINE_TYPE (PosCompletionBar, pos_completion_bar, GTK_TYPE_BOX)


static void
set_mode_name (PosCompletionBar *self, const char *mode_name)
{
  if (!g_set_str (&self->mode_name, mode_name))
    return;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODE_NAME]);
}


static void
pos_completion_bar_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PosCompletionBar *self = POS_COMPLETION_BAR (object);

  switch (property_id) {
  case PROP_MODE_NAME:
    set_mode_name (self, g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_completion_bar_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PosCompletionBar *self = POS_COMPLETION_BAR (object);

  switch (property_id) {
  case PROP_MODE_NAME:
    g_value_set_string (value, self->mode_name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
on_completion_selected (PosCompletionBar *self, const char *completion)
{
  g_assert (POS_IS_COMPLETION_BAR (self));

  g_signal_emit (self, signals[SELECTED], 0, completion);
}


static void
on_mode_button_clicked (PosCompletionBar *self)
{
  g_assert (POS_IS_COMPLETION_BAR (self));

  g_signal_emit (self, signals[MODE_PRESSED], 0);
}


static void
pos_completion_bar_finalize (GObject *object)
{
  PosCompletionBar *self = POS_COMPLETION_BAR (object);

  g_clear_pointer (&self->mode_name, g_free);

  G_OBJECT_CLASS (pos_completion_bar_parent_class)->finalize (object);
}


static void
pos_completion_bar_class_init (PosCompletionBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = pos_completion_bar_get_property;
  object_class->set_property = pos_completion_bar_set_property;
  object_class->finalize = pos_completion_bar_finalize;

  /**
   * PosCompletionBar:mode-name
   *
   * Name of the mode displayed on the mode toggle.
   */
  props[PROP_MODE_NAME] =
    g_param_spec_string ("mode-name", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[SELECTED] = g_signal_new ("selected",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    0, NULL, NULL, NULL,
                                    G_TYPE_NONE,
                                    1,
                                    G_TYPE_STRING);

  signals[MODE_PRESSED] = g_signal_new ("mode-pressed",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL, NULL,
                                        G_TYPE_NONE,
                                        0);

  g_type_ensure (POS_TYPE_COMPLETIONS_BOX);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/stevia/ui/completion-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, PosCompletionBar, completions_box);
  gtk_widget_class_bind_template_child (widget_class, PosCompletionBar, scrolled_window);

  gtk_widget_class_bind_template_callback (widget_class, on_completion_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_mode_button_clicked);

  gtk_widget_class_set_css_name (widget_class, "pos-completion-bar");
}


static void
pos_completion_bar_init (PosCompletionBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


PosCompletionBar *
pos_completion_bar_new (void)
{
  return POS_COMPLETION_BAR (g_object_new (POS_TYPE_COMPLETION_BAR, NULL));
}


void
pos_completion_bar_set_completions (PosCompletionBar *self, GStrv completions)
{
  g_return_if_fail (POS_IS_COMPLETION_BAR (self));

  pos_completion_box_set_completions (self->completions_box, completions);
}
