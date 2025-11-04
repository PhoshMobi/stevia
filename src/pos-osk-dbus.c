/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-osk-dbus"

#include "pos-config.h"

#include "pos-app.h"
#include "pos-osk-dbus.h"
#include "pos-osk0-dbus.h"

#define OSK0_BUS_PATH "/sm/puri/OSK0"
#define OSK0_BUS_NAME "sm.puri.OSK0"


enum {
  PROP_0,
  PROP_NAME_OWNER_FLAGS,
  PROP_HAS_NAME,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PosOskDbus:
 *
 * Provides the sm.puri.OSK0 DBus interface
 */
struct _PosOskDbus {
  PosDbusOSK0Skeleton parent;

  gboolean            visible;
  guint               dbus_name_id;
  gboolean            has_name;
  GBusNameOwnerFlags  name_owner_flags;
};

static void pos_osk_dbus_osk0_iface_init (PosDbusOSK0Iface *iface);

G_DEFINE_TYPE_WITH_CODE (PosOskDbus, pos_osk_dbus,
                         POS_DBUS_TYPE_OSK0_SKELETON,
                         G_IMPLEMENT_INTERFACE (POS_DBUS_TYPE_OSK0, pos_osk_dbus_osk0_iface_init))


static void
pos_osk_dbus_set_property (GObject      *object,
                           guint         property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  PosOskDbus *self = POS_OSK_DBUS (object);

  switch (property_id) {
  case PROP_NAME_OWNER_FLAGS:
    self->name_owner_flags = g_value_get_flags (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_osk_dbus_get_property (GObject    *object,
                           guint       property_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  PosOskDbus *self = POS_OSK_DBUS (object);

  switch (property_id) {
  case PROP_NAME_OWNER_FLAGS:
    g_value_set_flags (value, self->name_owner_flags);
    break;
  case PROP_HAS_NAME:
    g_value_set_boolean (value, self->has_name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_osk_dbus_finalize (GObject *object)
{
  PosOskDbus *self = POS_OSK_DBUS (object);

  g_clear_handle_id (&self->dbus_name_id, g_bus_unown_name);

  if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));

  G_OBJECT_CLASS (pos_osk_dbus_parent_class)->finalize (object);
}

static gboolean
pos_osk_dbus_handle_set_visible (PosDbusOSK0           *object,
                                 GDBusMethodInvocation *invocation,
                                 gboolean               arg_visible)
{
  PosOskDbus *self = POS_OSK_DBUS (object);

  g_debug ("%s: %d", __func__, arg_visible);
  g_object_set (self, "visible", arg_visible, NULL);
  pos_dbus_osk0_complete_set_visible (object, invocation);

  return TRUE;
}

static void
pos_osk_dbus_osk0_iface_init (PosDbusOSK0Iface *iface)
{
  iface->handle_set_visible = pos_osk_dbus_handle_set_visible;
}


static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  PosOskDbus *self = POS_OSK_DBUS (user_data);

  g_debug ("Acquired name %s", name);
  self->has_name = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_NAME]);
}


static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  PosOskDbus *self = POS_OSK_DBUS (user_data);

  if (connection == NULL) {
    g_critical ("Failed to connect to session DBus");
    return;
  }

  if (self->has_name) {
    g_debug ("Lost DBus name '%s'", name);
    self->has_name = FALSE;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_NAME]);
  } else {
    g_warning ("Failed to acquire DBus name '%s'", name);
    pos_app_quit (pos_app_get_default (), EXIT_FAILURE);
  }
}


static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;
  PosOskDbus *self = POS_OSK_DBUS (user_data);

  success = g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                              connection,
                                              OSK0_BUS_PATH,
                                              &err);
  if (!success) {
    g_warning ("Failed to export osk interface: %s", err->message);
    return;
  }
}


static void
pos_osk_dbus_constructed (GObject *object)
{
  PosOskDbus *self = POS_OSK_DBUS (object);

  G_OBJECT_CLASS (pos_osk_dbus_parent_class)->constructed (object);

  self->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       OSK0_BUS_NAME,
                                       self->name_owner_flags,
                                       on_bus_acquired,
                                       on_name_acquired,
                                       on_name_lost,
                                       self,
                                       NULL);
}


static void
pos_osk_dbus_class_init (PosOskDbusClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = pos_osk_dbus_finalize;
  object_class->constructed = pos_osk_dbus_constructed;
  object_class->set_property = pos_osk_dbus_set_property;
  object_class->get_property = pos_osk_dbus_get_property;

  props[PROP_NAME_OWNER_FLAGS] =
    g_param_spec_flags ("name-owner-flags", "", "",
                        G_TYPE_BUS_NAME_OWNER_FLAGS,
                        0,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  props[PROP_HAS_NAME] =
    g_param_spec_boolean ("has-name", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
pos_osk_dbus_init (PosOskDbus *self)
{
}


PosOskDbus *
pos_osk_dbus_new (GBusNameOwnerFlags flags)
{
  return POS_OSK_DBUS (g_object_new (POS_TYPE_OSK_DBUS,
                                     "name-owner-flags", flags,
                                     NULL));
}


gboolean
pos_osk_dbus_has_name (PosOskDbus *self)
{
  g_return_val_if_fail (POS_IS_OSK_DBUS (self), FALSE);

  return self->has_name;
}
