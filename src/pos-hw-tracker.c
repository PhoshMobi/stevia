/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-hw-tracker"

#include "pos-config.h"

#include "pos-hw-tracker.h"

#include <gio/gio.h>

#define IGNORE_HW_KEYBOARDS_KEY "ignore-hw-keyboards"

/**
 * PosHwTracker:
 *
 * Track connected hardware state (e.g. connected keyboards)
 */
enum {
  PROP_0,
  PROP_DEVICE_STATE,
  PROP_ALLOW_ACTIVE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PosHwTracker {
  GObject                       parent;

  struct zphoc_device_state_v1 *device_state;
  gboolean                      has_hw_kb;

  GSettings                    *settings;
  gboolean                      ignore_hw_kb;
};
G_DEFINE_TYPE (PosHwTracker, pos_hw_tracker, G_TYPE_OBJECT)


static void
device_state_handle_capabilities (void                         *data,
                                  struct zphoc_device_state_v1 *zphoc_device_state_v1,
                                  uint32_t                      capabilities)
{
  PosHwTracker *self = POS_HW_TRACKER (data);
  gboolean has_hw_kb;

  g_debug ("Device state capabilities: 0x%x", capabilities);

  has_hw_kb = !!(capabilities & ZPHOC_DEVICE_STATE_V1_CAPABILITY_KEYBOARD);

  if (self->has_hw_kb == has_hw_kb)
    return;

  self->has_hw_kb = has_hw_kb;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_ACTIVE]);
}



static const struct zphoc_device_state_v1_listener device_state_listener = {
  .capabilities = device_state_handle_capabilities,
};


static void
pos_hw_tracker_set_device_state (PosHwTracker *self,
                                 gpointer      device_state)
{
  self->device_state = device_state;
  zphoc_device_state_v1_add_listener (self->device_state, &device_state_listener, self);
}


static void
on_ignore_hw_keyboard_changed (PosHwTracker *self)
{
  gboolean ignore_hw_kb;

  ignore_hw_kb = g_settings_get_boolean (self->settings, IGNORE_HW_KEYBOARDS_KEY);
  if (self->ignore_hw_kb == ignore_hw_kb)
    return;

  self->ignore_hw_kb = ignore_hw_kb;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_ACTIVE]);
}


static void
pos_hw_tracker_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  PosHwTracker *self = POS_HW_TRACKER (object);

  switch (property_id) {
  case PROP_DEVICE_STATE:
    pos_hw_tracker_set_device_state (self, g_value_get_pointer (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_hw_tracker_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PosHwTracker *self = POS_HW_TRACKER (object);

  switch (property_id) {
  case PROP_DEVICE_STATE:
    g_value_set_pointer (value, self->device_state);
    break;
  case PROP_ALLOW_ACTIVE:
    g_value_set_boolean (value, pos_hw_tracker_get_allow_active (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_hw_tracker_finalize (GObject *object)
{
  PosHwTracker *self = POS_HW_TRACKER (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (pos_hw_tracker_parent_class)->finalize (object);
}


static void
pos_hw_tracker_class_init (PosHwTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = pos_hw_tracker_finalize;
  object_class->set_property = pos_hw_tracker_set_property;
  object_class->get_property = pos_hw_tracker_get_property;

  props[PROP_DEVICE_STATE] =
    g_param_spec_pointer ("device-state", "", "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  props[PROP_ALLOW_ACTIVE] =
    g_param_spec_boolean ("allow-active", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
pos_hw_tracker_init (PosHwTracker *self)
{
  self->settings = g_settings_new ("mobi.phosh.osk");

  g_signal_connect_swapped (self->settings, "changed::" IGNORE_HW_KEYBOARDS_KEY,
                            G_CALLBACK (on_ignore_hw_keyboard_changed),
                            self);
  on_ignore_hw_keyboard_changed (self);
}


PosHwTracker *
pos_hw_tracker_new (struct zphoc_device_state_v1 *device_state)
{
  return g_object_new (POS_TYPE_HW_TRACKER,
                       "device-state", device_state,
                       NULL);
}


gboolean
pos_hw_tracker_get_allow_active (PosHwTracker *self)
{
  g_assert (POS_IS_HW_TRACKER (self));

  if (self->ignore_hw_kb)
    return TRUE;

  return !self->has_hw_kb;
}
