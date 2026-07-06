/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-main.h"
#include "pos-osk-widget.h"
#include "pos-resources.h"

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-xkb-info.h>

#include <glib.h>

static void
test_load_layouts (void)
{
  g_autoptr (GResource) pos_resource = NULL;
  g_autoptr (GError) err = NULL;
  g_auto (GStrv) names = NULL;
  g_autoptr (GRegex) lang_re = g_regex_new ("^[a-z]{2,3}$",
                                            G_REGEX_DEFAULT,
                                            G_REGEX_MATCH_DEFAULT,
                                            NULL);
  g_autoptr (GRegex) region_re = g_regex_new ("^[a-z]{2,8}$",
                                              G_REGEX_DEFAULT,
                                              G_REGEX_MATCH_DEFAULT,
                                              NULL);
  g_autoptr (GnomeXkbInfo) xkbinfo = gnome_xkb_info_new ();

  pos_init ();
  pos_resource = pos_get_resource ();
  g_assert_nonnull (pos_resource);

  names = g_resource_enumerate_children (pos_resource,
                                         "/mobi/phosh/stevia/layouts",
                                         G_RESOURCE_LOOKUP_FLAGS_NONE,
                                         &err);
  g_assert_no_error (err);
  for (int i = 0; names[i]; i++) {
    PosOskWidget *osk_widget;
    g_autofree char *layout_id = NULL;
    const char *layout, *variant;

    if (g_str_has_suffix (names[i], "/"))
      continue;

    osk_widget = g_object_ref_sink (pos_osk_widget_new (PHOSH_OSK_FEATURE_DEFAULT));
    g_assert (g_str_has_suffix (names[i], ".json"));
    layout_id = g_strndup (names[i], strlen (names[i]) - strlen (".json"));
    g_test_message ("Loading layout %s", layout_id);

    if (g_strcmp0 (names[i], "terminal.json")) {
      g_assert_true (gnome_xkb_info_get_layout_info (xkbinfo,
                                                     layout_id,
                                                     NULL,
                                                     NULL,
                                                     &layout,
                                                     &variant));
      pos_osk_widget_set_layout (osk_widget,
                                 "doesnotmatter",
                                 layout_id,
                                 "Test",
                                 layout,
                                 variant,
                                 &err);
      g_assert_nonnull (pos_osk_widget_get_lang (osk_widget));
      g_assert_nonnull (pos_osk_widget_get_region (osk_widget));
      g_assert_true (g_regex_match (lang_re, pos_osk_widget_get_lang (osk_widget),
                                    G_REGEX_MATCH_DEFAULT,
                                    NULL));
      g_assert_true (g_regex_match (region_re, pos_osk_widget_get_region (osk_widget),
                                    G_REGEX_MATCH_DEFAULT,
                                    NULL));
    }

    g_assert_no_error (err);
    g_assert_finalize_object (osk_widget);
  }
}


int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pos/completer/load_layouts", test_load_layouts);

  return g_test_run ();
}
