/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-indicator-popup"

#include "pos-config.h"

#include "pos-indicator-popup.h"
#include "pos-osk-key.h"

#include "gtk/gtk.h"

#define INDICATOR_TIMEOUT 200 /* ms */

/**
 * PosIndicatorPopup:
 *
 * A popup indicating the currently pressed character
 */

struct _PosIndicatorPopup {
  GtkPopover parent;

  GtkLabel  *label;
  guint      timeout_id;

};
G_DEFINE_TYPE (PosIndicatorPopup, pos_indicator_popup, GTK_TYPE_POPOVER)


static void
pos_indicator_popup_show (GtkWidget *widget)
{
  PosIndicatorPopup *self = POS_INDICATOR_POPUP (widget);
  cairo_region_t *input_region;
  GdkWindow *parent, *popup;

  GTK_WIDGET_CLASS (pos_indicator_popup_parent_class)->show (widget);

  /* GTK sets the input region to the popover so we need to reverse this */
  input_region = cairo_region_create ();
  parent = gtk_widget_get_parent_window (GTK_WIDGET (self));
  gdk_window_input_shape_combine_region (parent, input_region, 0, 0);
  cairo_region_destroy (input_region);

  popup = gtk_widget_get_window (GTK_WIDGET (self));
  gdk_window_input_shape_combine_region (popup, NULL, 0, 0);
}


static void
pos_indicator_popup_dispose (GObject *object)
{
  PosIndicatorPopup *self = POS_INDICATOR_POPUP (object);

  g_clear_handle_id (&self->timeout_id, g_source_remove);

  G_OBJECT_CLASS (pos_indicator_popup_parent_class)->dispose (object);
}


static void
pos_indicator_popup_class_init (PosIndicatorPopupClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = pos_indicator_popup_dispose;

  widget_class->show = pos_indicator_popup_show;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/stevia/ui/indicator-popup.ui");

  gtk_widget_class_bind_template_child (widget_class, PosIndicatorPopup, label);

  gtk_widget_class_set_css_name (widget_class, "pos-indicator-popup");
}


static void
pos_indicator_popup_init (PosIndicatorPopup *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_popover_set_modal (GTK_POPOVER (self), FALSE);
  gtk_popover_set_constrain_to (GTK_POPOVER (self), GTK_POPOVER_CONSTRAINT_NONE);
}


PosIndicatorPopup *
pos_indicator_popup_new (void)
{
  return g_object_new (POS_TYPE_INDICATOR_POPUP, NULL);
}

void
pos_indicator_popup_show_key (PosIndicatorPopup *self, PosOskKey *key)
{
  const GdkRectangle *box = pos_osk_key_get_box (key);
  GdkRectangle pos = { 0 };

  g_assert (POS_IS_INDICATOR_POPUP (self));
  g_assert (POS_IS_OSK_KEY (key));

  gtk_label_set_text (self->label, pos_osk_key_get_symbol (key));

  pos.x = box->x + (0.5 * box->width);
  pos.y = box->y + (0.2 * box->height);

  gtk_popover_set_pointing_to (GTK_POPOVER (self), &pos);

  gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
}


static void
on_indicator_timeout (gpointer data)
{
  PosIndicatorPopup *self = POS_INDICATOR_POPUP (data);

  self->timeout_id = 0;
  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
}


void
pos_indicator_popup_hide (PosIndicatorPopup *self, gboolean now)
{
  g_assert (POS_IS_INDICATOR_POPUP (self));

  g_clear_handle_id (&self->timeout_id, g_source_remove);

  if (now) {
    gtk_widget_hide (GTK_WIDGET (self));
  } else {
    g_clear_handle_id (&self->timeout_id, g_source_remove);
    self->timeout_id = g_timeout_add_once (INDICATOR_TIMEOUT, on_indicator_timeout, self);
  }
}
