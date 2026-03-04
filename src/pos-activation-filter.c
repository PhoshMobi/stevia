/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-activation-filter"

#include "pos-activation-filter.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <gdk/gdkwayland.h>

#define IGNORE_ACTIVATION_KEY "ignore-activation"

/**
 * PosActivationFilter:
 *
 * Allows to suppress OSK activation based on the app-id of the
 * currently active application.
 */

enum {
  PROP_0,
  PROP_FOREIGN_TOPLEVEL_MANAGER,
  PROP_ALLOW_ACTIVE,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PosToplevel PosToplevel;

struct _PosActivationFilter {
  GObject                                     parent;

  GSettings                                  *settings;
  GStrv                                       filtered_app_ids;

  struct zwlr_foreign_toplevel_management_v1 *foreign_toplevel_manager;
  GPtrArray                                  *toplevels;
  PosToplevel                                *active;

  gboolean                                    allow_active;
};

G_DEFINE_TYPE (PosActivationFilter, pos_activation_filter, G_TYPE_OBJECT);


struct _PosToplevel {
  struct zwlr_foreign_toplevel_handle_v1 *handle;
  char                                   *app_id;
  char                                   *title;
  gboolean                                activated;
  gboolean                                configured;

  PosActivationFilter                    *filter; /* (unowned) */
};


static void
pos_activation_filter_update_active (PosActivationFilter *self, PosToplevel *active)
{
  self->allow_active = TRUE;
  self->active = active;

  if (!self->active || !self->active->app_id)
    return;

  if (!self->filtered_app_ids ||
      g_strv_contains ((const char *const *)self->filtered_app_ids, self->active->app_id) == FALSE)
    return;

  g_debug ("Not unfolding OSK for %s", active->app_id);
  self->allow_active = FALSE;
}


static void
pos_activation_filter_remove_toplevel (PosActivationFilter *self, PosToplevel *toplevel)
{
  g_ptr_array_remove (self->toplevels, toplevel);
}


static void
handle_zwlr_foreign_toplevel_handle_title (
  void                                   *data,
  struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
  const char                            * title)
{
  PosToplevel *self = data;

  g_free (self->title);
  self->title = g_strdup (title);

  g_debug ("%p: Got title %s", zwlr_foreign_toplevel_handle_v1, title);
}


static void
handle_zwlr_foreign_toplevel_handle_app_id (
  void                                   *data,
  struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
  const char                            * app_id)
{
  PosToplevel *toplevel = data;

  g_free (toplevel->app_id);
  toplevel->app_id = g_strdup (app_id);

  g_debug ("%p: Got app_id %s", zwlr_foreign_toplevel_handle_v1, app_id);
}


static void
handle_zwlr_foreign_toplevel_handle_output_enter (
  void                                   *data,
  struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
  struct wl_output                       *output)
{
}


static void
handle_zwlr_foreign_toplevel_handle_output_leave (
  void                                   *data,
  struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
  struct wl_output                       *output)
{
}


static void
handle_zwlr_foreign_toplevel_handle_state (
  void                                   *data,
  struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
  struct wl_array                        *state)
{
  PosToplevel *toplevel = data;
  gboolean active = FALSE;
  enum zwlr_foreign_toplevel_handle_v1_state *value;
  PosActivationFilter *filter = toplevel->filter;

  wl_array_for_each (value, state)
  {
    if (*value == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
      g_debug ("toplevel_handle %p (%s): is active", toplevel, toplevel->app_id);
      active = TRUE;
      break;
    }
  }

  if (active) {
    /* New active toplevel */
    pos_activation_filter_update_active (filter, toplevel);
  } else if (toplevel == filter->active) {
    /* Current active toplevel became inactive */
    pos_activation_filter_update_active (filter, NULL);
  }
}


static void
handle_zwlr_foreign_toplevel_handle_done (
  void                                   *data,
  struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1)
{
}


static void
handle_zwlr_foreign_toplevel_handle_closed (
  void                                   *data,
  struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1)
{
  PosToplevel *toplevel = data;

  pos_activation_filter_remove_toplevel (toplevel->filter, toplevel);
}


static const struct zwlr_foreign_toplevel_handle_v1_listener zwlr_foreign_toplevel_handle_listener = {
  handle_zwlr_foreign_toplevel_handle_title,
  handle_zwlr_foreign_toplevel_handle_app_id,
  handle_zwlr_foreign_toplevel_handle_output_enter,
  handle_zwlr_foreign_toplevel_handle_output_leave,
  handle_zwlr_foreign_toplevel_handle_state,
  handle_zwlr_foreign_toplevel_handle_done,
  handle_zwlr_foreign_toplevel_handle_closed
};


static void
toplevel_dispose (PosToplevel *toplevel)
{
  g_free (toplevel->app_id);
  g_free (toplevel->title);
  g_free (toplevel);
}


static PosToplevel *
toplevel_new_from_handle (PosActivationFilter *self, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
  PosToplevel *toplevel = g_new0 (PosToplevel, 1);

  toplevel->filter = self;
  toplevel->handle = handle;

  zwlr_foreign_toplevel_handle_v1_add_listener (toplevel->handle, &zwlr_foreign_toplevel_handle_listener, toplevel);

  return toplevel;
}


static void
handle_zwlr_foreign_toplevel_manager_toplevel (void                                    *data,
                                               struct zwlr_foreign_toplevel_manager_v1 *zwlr_foreign_toplevel_manager_v1,
                                               struct zwlr_foreign_toplevel_handle_v1  *handle)
{
  PosActivationFilter *self = POS_ACTIVATION_FILTER (data);
  PosToplevel *toplevel;

  toplevel = toplevel_new_from_handle (self, handle);
  g_ptr_array_add (self->toplevels, toplevel);

  g_debug ("Got toplevel %p", toplevel);
}


static void
handle_zwlr_foreign_toplevel_manager_finished (void                                    *data,
                                               struct zwlr_foreign_toplevel_manager_v1 *zwlr_foreign_toplevel_management_v1)
{
  g_debug ("wlr_foreign_toplevel_management_finished");
}


static const struct zwlr_foreign_toplevel_manager_v1_listener zwlr_foreign_toplevel_manager_listener = {
  handle_zwlr_foreign_toplevel_manager_toplevel,
  handle_zwlr_foreign_toplevel_manager_finished,
};


static void
pos_activation_filter_set_foreign_toplevel_manager (PosActivationFilter *self,
                                                    gpointer             foreign_toplevel_manager)
{
  self->foreign_toplevel_manager = foreign_toplevel_manager;
  zwlr_foreign_toplevel_manager_v1_add_listener (foreign_toplevel_manager,
                                                 &zwlr_foreign_toplevel_manager_listener,
                                                 self);

}


static void
on_activation_filter_changed (PosActivationFilter *self)
{
  g_strfreev (self->filtered_app_ids);
  self->filtered_app_ids = g_settings_get_strv (self->settings, IGNORE_ACTIVATION_KEY);
}


static void
pos_activation_filter_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  PosActivationFilter *self = POS_ACTIVATION_FILTER (object);

  switch (property_id) {
  case PROP_FOREIGN_TOPLEVEL_MANAGER:
    pos_activation_filter_set_foreign_toplevel_manager (self, g_value_get_pointer (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_activation_filter_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  PosActivationFilter *self = POS_ACTIVATION_FILTER (object);

  switch (property_id) {
  case PROP_FOREIGN_TOPLEVEL_MANAGER:
    g_value_set_pointer (value, self->foreign_toplevel_manager);
    break;
  case PROP_ALLOW_ACTIVE:
    g_value_set_boolean (value, self->allow_active);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_activation_filter_dispose (GObject *object)
{
  PosActivationFilter *self = POS_ACTIVATION_FILTER (object);

  if (self->toplevels) {
    g_ptr_array_free (self->toplevels, TRUE);
    self->toplevels = NULL;
  }
  g_clear_object (&self->settings);
  g_clear_pointer (&self->filtered_app_ids, g_strfreev);

  G_OBJECT_CLASS (pos_activation_filter_parent_class)->dispose (object);
}


static void
pos_activation_filter_class_init (PosActivationFilterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = pos_activation_filter_dispose;
  object_class->set_property = pos_activation_filter_set_property;
  object_class->get_property = pos_activation_filter_get_property;

  props[PROP_FOREIGN_TOPLEVEL_MANAGER] =
    g_param_spec_pointer ("foreign-toplevel-manager", "", "",
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ALLOW_ACTIVE] =
    g_param_spec_boolean ("allow-active", "", "",
                          TRUE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
pos_activation_filter_init (PosActivationFilter *self)
{
  self->allow_active = TRUE;
  self->toplevels = g_ptr_array_new_with_free_func ((GDestroyNotify) (toplevel_dispose));

  self->settings = g_settings_new ("mobi.phosh.osk");

  g_signal_connect_swapped (self->settings, "changed::" IGNORE_ACTIVATION_KEY,
                            G_CALLBACK (on_activation_filter_changed),
                            self);
  on_activation_filter_changed (self);
}


PosActivationFilter *
pos_activation_filter_new (gpointer foreign_toplevel_manager)
{
  return g_object_new (POS_TYPE_ACTIVATION_FILTER,
                       "foreign-toplevel-manager", foreign_toplevel_manager,
                       NULL);
}


gboolean
pos_activation_filter_allow_active (PosActivationFilter *self)
{
  return self->allow_active;
}
