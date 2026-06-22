/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-input-surface.c"

#include <glib.h>


static void
test_build_layout_name (void)
{
  g_assert_cmpstr (build_ibus_layout_name (NULL, "ibus:ml:govarnam"),
                   ==,
                   "ibus:ml");
}


int
main (int argc, char *argv[])
{
  int ret;

  gtk_test_init (&argc, &argv, NULL);

  pos_init ();

  g_test_add_func ("/pos/osk-input-surface/build-layout-name", test_build_layout_name);

  ret = g_test_run ();

  pos_uninit ();
  return ret;
}
