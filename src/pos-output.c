/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-output"

#include "pos-config.h"

#include "pos.h"
#include "pos-output-priv.h"

#include <wayland-client.h>

/**
 * PosOutput:
 *
 * An output
 */

enum {
  PROP_0,
  PROP_WL_OUTPUT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  CONFIGURED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

struct _PosOutput {
  GObject parent;

  struct wl_output              *wl_output;
  struct zxdg_output_v1         *xdg_output;

  struct _PhocOutputState {
  char    *name;
  char    *description;
  int      logical_width;
  int      logical_height;
  int      width_mm;
  int      height_mm;
  enum wl_output_transform transform;
  } current, pending;
};
G_DEFINE_TYPE (PosOutput, pos_output, G_TYPE_OBJECT)


static void
output_handle_geometry (void             *data,
                        struct wl_output *wl_output,
                        int               x,
                        int               y,
                        int               physical_width,
                        int               physical_height,
                        int               subpixel,
                        const char       *make,
                        const char       *model,
                        int32_t           transform)
{
  PosOutput *self = data;

  g_debug ("Output geometry for %p: %d,%d, %dmm x %dmm, subpixel layout %d, transform %d",
           self, x, y, physical_width, physical_height, subpixel, transform);

  self->pending.width_mm = physical_width;
  self->pending.height_mm = physical_height;
  self->pending.transform = transform;
}


static void
output_handle_done (void             *data,
                    struct wl_output *wl_output)
{
  PosOutput *self = data;

  g_set_str (&self->current.name, self->pending.name);
  g_set_str (&self->current.description, self->pending.description);
  self->current.logical_width = self->pending.logical_width;
  self->current.logical_height = self->pending.logical_height;
  self->current.width_mm = self->pending.width_mm;
  self->current.height_mm = self->pending.height_mm;
  self->current.transform = self->pending.transform;

  g_signal_emit (self, signals[CONFIGURED], 0);
}


static void
output_handle_scale (void             *data,
                     struct wl_output *wl_output,
                     int32_t           scale)
{
}


static void
output_handle_mode (void             *data,
                    struct wl_output *wl_output,
                    uint32_t          flags,
                    int               width,
                    int               height,
                    int               refresh)
{
}


static void
output_handle_name (void *data, struct wl_output *wl_output, const char *name)
{
  PosOutput *self = data;

  g_debug ("Output name for %p: '%s'", self, name);
  g_set_str (&self->pending.name, name);
}


static void
output_handle_description (void *data, struct wl_output *wl_outpout, const char *description)
{
  PosOutput *self = data;

  g_debug ("Output description for %p: '%s'", self, description);
  g_set_str (&self->pending.description, description);
}


static const struct wl_output_listener wl_output_listener =
{
  .geometry = output_handle_geometry,
  .mode = output_handle_mode,
  .done = output_handle_done,
  .scale = output_handle_scale,
  .name = output_handle_name,
  .description = output_handle_description,
};


static void
xdg_output_v1_handle_logical_position (void                  *data,
                                       struct zxdg_output_v1 *zxdg_output_v1,
                                       int32_t                x,
                                       int32_t                y)
{
}


static void
xdg_output_v1_handle_logical_size (void                  *data,
                                   struct zxdg_output_v1 *zxdg_output_v1,
                                   int32_t                width,
                                   int32_t                height)
{
  PosOutput *self = data;

  g_debug ("Output %p Logical size: %dx%d", self, width, height);
  self->pending.logical_width = width;
  self->pending.logical_height = height;
}


static void
xdg_output_v1_handle_done (void *data, struct zxdg_output_v1 *zxdg_output_v1)
{
}


static void
xdg_output_v1_handle_name (void                  *data,
                           struct zxdg_output_v1 *zxdg_output_v1,
                           const char            *name)
{
}


static void
xdg_output_v1_handle_description (void                  *data,
                                  struct zxdg_output_v1 *zxdg_output_v1,
                                  const char            *description)
{
}


static const struct zxdg_output_v1_listener xdg_output_v1_listener =
{
  .logical_position = xdg_output_v1_handle_logical_position,
  .logical_size = xdg_output_v1_handle_logical_size,
  .done = xdg_output_v1_handle_done,
  .name = xdg_output_v1_handle_name,
  .description = xdg_output_v1_handle_description,
};


static void
set_wl_output (PosOutput *self, struct wl_output *wl_output)
{
  PosWayland *wayland;
  struct zxdg_output_manager_v1 *xdg_output_manager;

  if (!wl_output)
    return;

  wayland = pos_wayland_get_default ();
  self->wl_output = wl_output;
  wl_output_add_listener (self->wl_output, &wl_output_listener, self);

  xdg_output_manager = pos_wayland_get_zxdg_output_manager_v1 (wayland);
  self->xdg_output = zxdg_output_manager_v1_get_xdg_output (xdg_output_manager,
                                                            self->wl_output);
  zxdg_output_v1_add_listener (self->xdg_output, &xdg_output_v1_listener, self);
}


static void
pos_output_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  PosOutput *self = POS_OUTPUT (object);

  switch (property_id) {
  case PROP_WL_OUTPUT:
    set_wl_output (self, g_value_get_pointer (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_output_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  PosOutput *self = POS_OUTPUT (object);

  switch (property_id) {
  case PROP_WL_OUTPUT:
    g_value_set_pointer (value, self->wl_output);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_output_dispose (GObject *object)
{
  PosOutput *self = POS_OUTPUT (object);

  g_clear_pointer (&self->pending.name, g_free);
  g_clear_pointer (&self->pending.description, g_free);
  g_clear_pointer (&self->current.name, g_free);
  g_clear_pointer (&self->current.description, g_free);

  g_clear_pointer (&self->xdg_output, zxdg_output_v1_destroy);
  g_clear_pointer (&self->wl_output, wl_output_destroy);

  G_OBJECT_CLASS (pos_output_parent_class)->dispose (object);
}


static void
pos_output_class_init (PosOutputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_output_get_property;
  object_class->set_property = pos_output_set_property;
  object_class->dispose = pos_output_dispose;

  props[PROP_WL_OUTPUT] =
    g_param_spec_pointer ("wl-output", "", "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[CONFIGURED] =
    g_signal_new ("configured",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}


static void
pos_output_init (PosOutput *self)
{
}


PosOutput *
pos_output_new (struct wl_output *wl_output)

{
  return g_object_new (POS_TYPE_OUTPUT,
                       "wl-output", wl_output,
                       NULL);
}


guint
pos_output_get_logical_width (PosOutput *self)
{
  g_assert (POS_IS_OUTPUT (self));

  return self->current.logical_width;
}


guint
pos_output_get_logical_height (PosOutput *self)
{
  g_assert (POS_IS_OUTPUT (self));

  return self->current.logical_height;
}


gboolean
pos_output_is_portrait (PosOutput *self)
{
  g_assert (POS_IS_OUTPUT (self));

  return self->current.logical_height > self->current.logical_width;
}


double
pos_output_get_logical_pixel_size (PosOutput *self)
{
  double phys_height;

  g_assert (POS_IS_OUTPUT (self));

  phys_height = self->current.height_mm;

  if (self->current.transform % 2)
    phys_height = self->current.width_mm;

  /* assume square pixels */
  return phys_height / self->current.logical_height;
}

/**
 * pos_output_get_transformed_phys_height:
 * @self: The output
 *
 * Get the height of the output taking the current transform into
 * account. So when the output is flipped 90⁰ the return value will
 * be the physical width.
 *
 * Returns: The physical height
 */
guint
pos_output_get_transformed_phys_height (PosOutput *self)
{
  g_assert (POS_IS_OUTPUT (self));

  if (!(self->current.transform % 2))
    return self->current.height_mm;
  else
    return self->current.width_mm;
}

/**
 * pos_output_get_transformed_phys_width:
 * @self: The output
 *
 * Get the height of the output taking the current transform into
 * account. So when the output is flipped 90⁰ the return value will
 * be the physical height.
 *
 * Returns: The physical width
 */
guint
pos_output_get_transformed_phys_width (PosOutput *self)
{
  g_assert (POS_IS_OUTPUT (self));

  if (self->current.transform % 2)
    return self->current.width_mm;
  else
    return self->current.height_mm;
}

/****************************************************/
/* Only meant to setup outputs for tests */

void
pos_output_set_logical_height (PosOutput *self, guint height)
{
  self->current.logical_height = height;
}


void
pos_output_set_logical_width (PosOutput *self, guint width)
{
  self->current.logical_width = width;
}


void
pos_output_set_physical_height (PosOutput *self, guint height)
{
  self->current.height_mm = height;
}


void
pos_output_set_physical_width (PosOutput *self, guint width)
{
  self->current.width_mm = width;
}

/****************************************************/
