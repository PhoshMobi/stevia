/*
 * Copyright © 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-config.h"

#include "pos-clipboard-manager.h"

#include <glib.h>

static void
test_clipboard_manager_new (void)
{
  PosClipboardManager *manager = pos_clipboard_manager_new (NULL, NULL);

  g_assert_finalize_object (manager);
}


int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pos/clipboard-manager/new", test_clipboard_manager_new);

  return g_test_run ();
}
