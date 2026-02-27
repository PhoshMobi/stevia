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
  SELECTED,
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
};
G_DEFINE_TYPE (PosCompletionBar, pos_completion_bar, GTK_TYPE_BOX)


static void
on_completion_selected (PosCompletionBar *self, const char *completion)
{
  g_assert (POS_IS_COMPLETION_BAR (self));

  g_signal_emit (self, signals[SELECTED], 0, completion);
}


static void
pos_completion_bar_class_init (PosCompletionBarClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  signals[SELECTED] = g_signal_new ("selected",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    0, NULL, NULL, NULL,
                                    G_TYPE_NONE,
                                    1,
                                    G_TYPE_STRING);

  g_type_ensure (POS_TYPE_COMPLETIONS_BOX);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/stevia/ui/completion-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, PosCompletionBar, completions_box);
  gtk_widget_class_bind_template_child (widget_class, PosCompletionBar, scrolled_window);

  gtk_widget_class_bind_template_callback (widget_class, on_completion_selected);

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
