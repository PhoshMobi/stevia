/*
 * Copyright © 2020 Lugsole
 *             2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-completer-priv.h"

#include <glib.h>

static void
test_grab_last_word(void)
{
  char *new_before = NULL;
  char *word = NULL;

  g_assert_false (pos_completer_grab_last_word (NULL, &new_before, &word));
  g_assert_null (new_before);
  g_assert_null (word);

  g_assert_false (pos_completer_grab_last_word ("", &new_before, &word));
  g_assert_null (new_before);
  g_assert_null (word);

  g_assert_false (pos_completer_grab_last_word ("ends with ws ", &new_before, &word));
  g_assert_null (new_before);
  g_assert_null (word);

  g_assert_true (pos_completer_grab_last_word ("justoneword", &new_before, &word));
  g_assert_null (new_before);
  g_assert_cmpstr (word, ==, "justoneword");
  g_clear_pointer (&word, g_free);

  g_assert_true (pos_completer_grab_last_word ("ends with word", &new_before, &word));
  g_assert_cmpstr (new_before, ==, "ends with ");
  g_clear_pointer (&new_before, g_free);
  g_assert_cmpstr (word, ==, "word");
  g_clear_pointer (&word, g_free);
}


int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pos/completer/grab_last_word", test_grab_last_word);

  return g_test_run ();
}
