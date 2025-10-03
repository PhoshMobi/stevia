/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-size-manager"

#include "pos-config.h"

#include "phosh-osk-enums.h"
#include "pos.h"

#include <gtk/gtk.h>

#define SCALING_SCHEMA_ID "sm.puri.phosh.osk"
#define SCALING_KEY "scaling"

/**
 * PosSizeManager:
 *
 * Calculate a useful surface size
 */

enum {
  PROP_0,
  PROP_HEIGHT,
  PROP_DEAD_ZONE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PosSizeManager {
  GObject    parent;

  PosOutput *output;
  guint      height;
  guint      dead_zone;

  GSettings *settings;
};
G_DEFINE_TYPE (PosSizeManager, pos_size_manager, G_TYPE_OBJECT)


static void
set_height (PosSizeManager *self, guint height)
{
  if (self->height == height)
    return;

  self->height = height;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HEIGHT]);
}


static void
set_dead_zone (PosSizeManager *self, guint dead_zone)
{
  if (self->dead_zone == dead_zone)
    return;

  self->dead_zone = dead_zone;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DEAD_ZONE]);
}


#define MIN_PORTRAIT_RATIO 0.4
#define TARGET_PORTRAIT_PHYS_HEIGHT 40 /* mm */
#define TARGET_PORTRAIT_BOTTOM_DEAD_ZONE 8 /* mm */

static guint
calc_height (PosOutput *output, guint *dead_zone, PhoshOskScalingFlags flags)
{
  guint osk_height = POS_INPUT_SURFACE_DEFAULT_HEIGHT;
  double phys_dz;
  double pixel_size;
  double screen_logical_height;
  double screen_physical_height;

  g_assert (dead_zone);
  *dead_zone = 0;

  if (!output)
    return osk_height;

  if (!flags)
    return osk_height;

  if (!pos_output_is_portrait (output)) {
    /* TODO: set small height on phone displays so there's more screen estate but
     * use enough vertical space on landscape tablets */
    return osk_height;
  }

  screen_logical_height = pos_output_get_logical_height (output);

  /* vertical resolution not high enough to take up any extra space */
  if (osk_height > screen_logical_height * MIN_PORTRAIT_RATIO)
    return osk_height;

  screen_physical_height = pos_output_get_transformed_phys_height (output);
  /* vertical size not large enough for a larger OSK */
  if (TARGET_PORTRAIT_PHYS_HEIGHT > screen_physical_height * MIN_PORTRAIT_RATIO)
    return osk_height;

  pixel_size = pos_output_get_logical_pixel_size (output);

  if (flags & PHOSH_OSK_SCALING_AUTO_PORTRAIT)
    osk_height = TARGET_PORTRAIT_PHYS_HEIGHT / pixel_size;

  phys_dz = (MIN_PORTRAIT_RATIO * screen_physical_height) - (osk_height * pixel_size);

  if (flags & PHOSH_OSK_SCALING_BOTTOM_DEAD_ZONE)
    *dead_zone = MIN (TARGET_PORTRAIT_BOTTOM_DEAD_ZONE, phys_dz) / pixel_size;

  return osk_height;
}


static void
recalc_surface_size (PosSizeManager *self)
{
  guint height, dead_zone;
  PhoshOskScalingFlags flags;

  flags = g_settings_get_flags (self->settings, "scaling");
  height = calc_height (self->output, &dead_zone, flags);
  g_debug ("Target OSK height: %d, dead zone: %d", height, dead_zone);

  set_height (self, height);
  set_dead_zone (self, dead_zone);
}


static void
on_output_configured (PosSizeManager *self, PosOutput *output)
{
  g_assert (POS_IS_SIZE_MANAGER (self));
  g_assert (POS_IS_OUTPUT (output));

  recalc_surface_size (self);
}


static void
on_scaling_key_changed (PosSizeManager *self)
{
  g_assert (POS_IS_SIZE_MANAGER (self));

  recalc_surface_size (self);
}


static void
set_output (PosSizeManager *self, PosOutput *output)
{
  if (self->output)
    g_signal_handlers_disconnect_by_data (self->output, self);

  g_set_object (&self->output, output);

  if (!self->output)
    return;

  g_signal_connect_swapped (self->output,
                            "configured",
                            G_CALLBACK (on_output_configured),
                            self);
}


static void
on_outputs_changed (PosSizeManager *self, GParamSpec *pspec, PosWayland *wayland)
{
  PosOutput *output;
  GPtrArray *outputs;

  if (pos_wayland_get_n_outputs (wayland) != 1) {
    /* TODO: we can do better: if we didn't get a "surface enter" we can assume we're on
       the same output */
    set_output (self, NULL);
    set_height (self, POS_INPUT_SURFACE_DEFAULT_HEIGHT);
    return;
  }

  /* Pick our output */
  outputs = pos_wayland_get_outputs (wayland);
  output = g_ptr_array_index (outputs, 0);
  set_output (self, output);
  on_output_configured (self, self->output);
}


static void
pos_size_manager_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  PosSizeManager *self = POS_SIZE_MANAGER (object);

  switch (property_id) {
  case PROP_HEIGHT:
    g_value_set_uint (value, self->height);
    break;
  case PROP_DEAD_ZONE:
    g_value_set_uint (value, self->dead_zone);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_size_manager_dispose (GObject *object)
{
  PosSizeManager *self = POS_SIZE_MANAGER (object);

  set_output (self, NULL);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (pos_size_manager_parent_class)->dispose (object);
}


static void
pos_size_manager_class_init (PosSizeManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_size_manager_get_property;
  object_class->dispose = pos_size_manager_dispose;

  /**
   * PosSizeManager:height:
   *
   * The height in logical pixels the keyboard's key area should have.
   */
  props[PROP_HEIGHT] =
    g_param_spec_uint ("height", "", "",
                       0, G_MAXUINT,
                       0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosSizeManager:dead-zone:
   *
   * Empty space at the bottom of the keyboard.
   */
  props[PROP_DEAD_ZONE] =
    g_param_spec_uint ("dead-zone", "", "",
                       0, G_MAXUINT,
                       0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
pos_size_manager_init (PosSizeManager *self)
{
  PosWayland *wayland = pos_wayland_get_default ();

  self->settings = g_settings_new (SCALING_SCHEMA_ID);

  g_signal_connect_object (self->settings,
                           "changed::" SCALING_KEY,
                           G_CALLBACK (on_scaling_key_changed),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (wayland,
                           "notify::outputs",
                           G_CALLBACK (on_outputs_changed),
                           self,
                           G_CONNECT_SWAPPED);

  recalc_surface_size (self);
}


PosSizeManager *
pos_size_manager_new (void)
{
  return g_object_new (POS_TYPE_SIZE_MANAGER, NULL);
}
