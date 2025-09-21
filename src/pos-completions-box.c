/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-completions-box"

#include "pos-config.h"

#include "pos-completions-box.h"

#include <gmobile.h>
#include <gtk/gtk.h>

/**
 * PosCompletionsBox:
 *
 * A box that contains completions
 */

enum {
  SELECTED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];


struct _PosCompletionsBox {
  GtkBox parent;
};
G_DEFINE_TYPE (PosCompletionsBox, pos_completions_box, GTK_TYPE_BOX)


static void
pos_completions_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  PosCompletionsBox *self = POS_COMPLETIONS_BOX (widget);
  g_autoptr (GList) all_children = NULL;
  GtkTextDirection direction = gtk_widget_get_direction (widget);
  GList *children;
  int n_children;
  int size, child_size, i, spacing, x = 0, max_width = 0, extra = 0;
  GtkAllocation child_allocation;
  g_autofree GtkRequestedSize *sizes = NULL;
  gboolean homogeneous = FALSE;

  g_assert (gtk_orientable_get_orientation (GTK_ORIENTABLE (self)) == GTK_ORIENTATION_HORIZONTAL);

  GTK_WIDGET_CLASS (pos_completions_box_parent_class)->size_allocate (widget, allocation);

  all_children = gtk_container_get_children (GTK_CONTAINER (self));
  n_children = g_list_length (all_children);
  if (!n_children)
    return;

  spacing = gtk_box_get_spacing (GTK_BOX (self));
  size = allocation->width - (n_children - 1) * spacing;
  sizes = g_new0 (GtkRequestedSize, n_children);

  /* Retrieve desired size each completions */
  for (children = all_children, i = 0; children; children = children->next, i++) {
    GtkWidget *child = children->data;

    /* All completions must be visible */
    g_assert (gtk_widget_get_visible (widget));

    gtk_widget_get_preferred_width_for_height (child,
                                               allocation->height,
                                               &sizes[i].minimum_size,
                                               &sizes[i].natural_size);

    if (sizes[i].minimum_size < 0){
      g_error ("PosCompletionsBox child %s minimum %s: %d < 0 for %s %d",
               gtk_widget_get_name (GTK_WIDGET (child)),
               "width",
               sizes[i].minimum_size,
               "height",
               allocation->height);
    }

    if (sizes[i].natural_size < sizes[i].minimum_size){
      g_error ("PosCompletionsBox child %s natural %s: %d < minimum %d for %s %d",
               gtk_widget_get_name (GTK_WIDGET (child)),
               "width",
               sizes[i].natural_size,
               sizes[i].minimum_size,
               "height",
               allocation->height);
    }

    size -= sizes[i].minimum_size;
    sizes[i].data = child;

    if (sizes[i].natural_size > max_width)
      max_width = sizes[i].natural_size;
  }

  /* With enough space distribute children homogeneously */
  if (((max_width * n_children) + ((n_children - 1) * spacing))  < allocation->width)
    homogeneous = TRUE;

  if (homogeneous) {
    size = allocation->width - (n_children - 1) * spacing;
  } else {
    /* Bring children up to size first */
    size = gtk_distribute_natural_allocation (MAX (0, size), n_children, sizes);
    extra = size / n_children;
  }

  g_debug ("allocated width: %d, size: %d, homogeneous: %d, extra: %d",
           allocation->width,
           size,
           homogeneous,
           extra);

  /* Allocate child sizes. */
  for (i = 0, children = all_children; children; children = children->next, i++) {
    /* Assign the child's size. */
    if (homogeneous)
      child_size = size / n_children;
    else
      child_size = sizes[i].minimum_size + extra;

    sizes[i].natural_size = child_size;
  }

  /* Allocate child positions. */
  child_allocation.y = allocation->y;
  child_allocation.height = MAX (1, allocation->height);
  x = allocation->x;

  for (i = 0, children = all_children; children; children = children->next, i++) {
    GtkWidget *child = children->data;

    child_size = sizes[i].natural_size;

    child_allocation.width = child_size;
    child_allocation.x = x + (child_size - child_allocation.width) / 2;

    x += child_size + spacing;

    if (direction == GTK_TEXT_DIR_RTL)
      child_allocation.x = allocation->x + allocation->width -
                           (child_allocation.x - allocation->x) - child_allocation.width;

    gtk_widget_size_allocate_with_baseline (child, &child_allocation, -1);
  }
}


static void
pos_completions_box_class_init (PosCompletionsBoxClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->size_allocate = pos_completions_box_size_allocate;

  signals[SELECTED] = g_signal_new ("selected",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    0, NULL, NULL, NULL,
                                    G_TYPE_NONE,
                                    1,
                                    G_TYPE_STRING);
}


static void
pos_completions_box_init (PosCompletionsBox *self)
{
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing (GTK_BOX (self), 6);
}


PosCompletionsBox *
pos_completions_box_new (void)
{
  return g_object_new (POS_TYPE_COMPLETIONS_BOX, NULL);
}


static void
on_button_clicked (PosCompletionsBox *self, GtkButton *btn)
{
  const char *completion;

  g_assert (POS_IS_COMPLETIONS_BOX (self));
  g_assert (GTK_IS_BUTTON (btn));

  completion = g_object_get_data (G_OBJECT (btn), "pos-text");
  g_assert (completion != NULL);

  g_signal_emit (self, signals[SELECTED], 0, completion);
}


void
pos_completion_box_set_completions (PosCompletionsBox *self, GStrv completions)
{
  guint n_completions;

  g_return_if_fail (POS_IS_COMPLETIONS_BOX (self));

  gtk_container_foreach (GTK_CONTAINER (self), (GtkCallback) gtk_widget_destroy, NULL);

  if (gm_strv_is_null_or_empty (completions))
    return;

  n_completions = g_strv_length (completions);
  for (int i = 0; i < n_completions; i++) {
    GtkWidget *btn;

    btn = gtk_button_new_with_label (completions[i]);
    gtk_widget_set_visible (GTK_WIDGET (btn), TRUE);
    g_object_set_data_full (G_OBJECT (btn), "pos-text", g_strdup (completions[i]), g_free);

    g_signal_connect_swapped (btn, "clicked", G_CALLBACK (on_button_clicked), self);
    gtk_container_add (GTK_CONTAINER (self), btn);
  }
}
