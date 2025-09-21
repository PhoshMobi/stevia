/*
 * 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-test-completer.h"

#include "pos.h"
#include "pos-completer-presage.h"

#include <gio/gio.h>
#include <glib.h>

static void
test_completer_presage_object (void)
{
  PosCompleter *presage;
  g_autoptr (GError) err = NULL;
  g_auto (GStrv) completions = NULL;

  presage = g_initable_new (POS_TYPE_COMPLETER_PRESAGE,
                            NULL,
                            &err,
                            "dict-dir", POS_PRESAGE_TEST_DB_DIR,
                            NULL);
  g_assert_no_error (err);
  pos_completer_assert_initial_state (presage, "presage", NULL);

  pos_completer_feed_symbol (presage, "t");
  /* TODO: For exact matches we need specify presage.xml as otherwise we might get results
   * from the system wide one or the users personal dictionary */
  completions = pos_completer_get_completions (presage);
  g_assert_cmpint (g_strv_length (completions), >, 0);

  g_assert_finalize_object (presage);
}


int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pos/completer/presage/object", test_completer_presage_object);

  return g_test_run ();
}
