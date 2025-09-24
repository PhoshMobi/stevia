/*
 * Copyright: (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-config.h"

#include "pos-test-completer.h"

#include "pos.h"
#include "pos-completer-hunspell.h"

#include <gio/gio.h>
#include <glib.h>


static void
test_completer_hunspell_object (void)
{
#ifdef POS_HAVE_HUNSPELL
  PosCompleter *hunspell;
  g_autoptr (GError) err = NULL;
  g_auto (GStrv) completions = NULL;

  if (!pos_completer_hunspell_find_dict (POS_COMPLETER_DEFAULT_LANG,
                                         POS_COMPLETER_DEFAULT_REGION,
                                         NULL,
                                         NULL)) {
    g_test_skip ("Hunspell data not found");
    return;
  }

  hunspell = g_initable_new (POS_TYPE_COMPLETER_HUNSPELL, NULL, NULL, NULL);
  g_assert_no_error (err);
  pos_completer_assert_initial_state (hunspell, "hunspell", NULL);

  pos_completer_feed_symbol (hunspell, "t");
  completions = pos_completer_get_completions (hunspell);
  g_assert_cmpint (g_strv_length (completions), >, 0);

  g_assert_finalize_object (hunspell);
#else
  g_test_skip ("Hunspell completer not available");
#endif
}


int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pos/completer/hunspell/object", test_completer_hunspell_object);

  return g_test_run ();
}
