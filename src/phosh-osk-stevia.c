/*
 * Copyright (C) 2018 Purism SPC
 *               2022-2024 The Phosh Developers
 *               2025-2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-osk-stevia"

#include "pos-config.h"
#include "pos.h"

#include "dh-dconf-migration.h"

#include <gio/gio.h>
#include <glib-unix.h>

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include <libfeedback.h>


/* TODO: allow to force virtual-keyboard instead of input-method */


static void G_GNUC_NORETURN
print_version (void)
{
  g_message ("Stevia %s\n", PHOSH_OSK_STEVIA_VERSION);
  exit (0);
}


static void
migrate_gsettings (void)
{
  g_autoptr (GSettings) settings = g_settings_new ("mobi.phosh.osk");
  DhDconfMigration *migration = _dh_dconf_migration_new ();
  const gchar *keys[] = {
    "osk/completion-mode",
    "osk/osk-features",
    "osk/ignore-activation",
    "osk/ignore-hw-keyboards",
    "osk/scaling",
    "osk/emoji-picker/recent-emoji",
    "osk/terminal/shortcuts",
    "osk/completers/default",
    "osk/completers/sources",
    "osk/completers/pipe/command",
    NULL,
  };

  if (g_settings_get_uint (settings, "schema-migration") > 0) {
    g_debug ("Schema already migrated, doing nothing.");
    return;
  }

  g_message ("Migrating schema to /mobi/phosh/osk/");
  for (int i = 0; keys[i] != NULL; i++) {
    const char *cur_key = keys[i];
    g_autofree char *mobi_key = NULL, *puri_key = NULL;

    mobi_key = g_strconcat ("/mobi/phosh/", cur_key, NULL);
    puri_key = g_strconcat ("/sm/puri/phosh/", cur_key, NULL);

    _dh_dconf_migration_migrate_key (migration,
                                     mobi_key,
                                     puri_key,
                                     NULL);
  }

  _dh_dconf_migration_sync_and_free (migration);

  g_settings_set_uint (settings, "schema-migration", 1);
}


int
main (int argc, char *argv[])
{
  g_autoptr (GOptionContext) opt_context = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (PosOskDbus) osk_dbus = NULL;
  g_autoptr (PosApp) app = NULL;
  gboolean version = FALSE, replace = FALSE, allow_replace = FALSE;
  GBusNameOwnerFlags flags;
  g_autoptr (PosWayland) wayland = NULL;
  int ret;

  const GOptionEntry options [] = {
    {"replace", 0, 0, G_OPTION_ARG_NONE, &replace,
     "Replace DBus service", NULL},
    {"allow-replacement", 0, 0, G_OPTION_ARG_NONE, &allow_replace,
     "Allow replacement of DBus service", NULL},
    {"version", 0, 0, G_OPTION_ARG_NONE, &version,
     "Show version information", NULL},
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
  };

  opt_context = g_option_context_new ("- A OSK for phosh");
  g_option_context_add_main_entries (opt_context, options, NULL);
  if (!g_option_context_parse (opt_context, &argc, &argv, &err)) {
    g_warning ("%s", err->message);
    return EXIT_FAILURE;
  }

  if (version)
    print_version ();

  migrate_gsettings ();

  gdk_set_allowed_backends ("wayland");
  pos_init ();
  lfb_init (POS_APP_ID, NULL);
  gtk_init (&argc, &argv);

  wayland = pos_wayland_get_default ();
  app = pos_app_get_default ();

  flags = (allow_replace ? G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT : 0) |
    (replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0);
  osk_dbus = pos_osk_dbus_new (flags);

  if (!pos_app_setup_input_method (app, osk_dbus))
    return EXIT_FAILURE;

  ret = pos_app_run (app);

  pos_uninit ();

  return ret;
}
