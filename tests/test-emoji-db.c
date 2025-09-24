/*
 * Copyright © 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos.h"
#include "pos-resources.h"

#include <glib.h>

static void
test_emoji_db_match (void)
{
  g_auto (GStrv) found = NULL;
  PosEmojiDb *emoji_db = pos_emoji_db_get_default ();

  found = pos_emoji_db_match_by_name (emoji_db, "smil", 3);
  g_assert_cmpstrv (found,
                    ((const char *const[]) {
                      "\360\237\230\204\357\270\217",
                      "\360\237\230\201\357\270\217",
                      "\360\237\231\202\357\270\217",
                      NULL,
                    }));

  g_assert_finalize_object (emoji_db);
}


int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pos/emoji-db/match", test_emoji_db_match);

  return g_test_run ();
}
