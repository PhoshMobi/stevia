/*
 * Copyright © 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-completer-base.h"

#include <glib.h>


static void
test_punctuation (void)
{
  PosCompleterBase *base = pos_completer_base_new ();

  g_assert_cmpstr (pos_completer_base_get_before_text (base), ==, "");
  g_assert_cmpstr (pos_completer_base_get_after_text (base), ==, "");

  pos_completer_base_set_surrounding_text (base, "before", "after");

  g_assert_cmpstr (pos_completer_base_get_before_text (base), ==, "before");
  g_assert_cmpstr (pos_completer_base_get_after_text (base), ==, "after");

  g_assert_false (pos_completer_base_wants_punctuation_swap (base, "."));
  g_assert_false (pos_completer_base_wants_punctuation_swap (base, "abc"));

  pos_completer_base_set_surrounding_text (base, "word ", NULL);
  g_assert_cmpstr (pos_completer_base_get_before_text (base), ==, "word ");
  g_assert_false (pos_completer_base_wants_punctuation_swap (base, "abc"));
  g_assert_true (pos_completer_base_wants_punctuation_swap (base, "."));
  g_assert_true (pos_completer_base_wants_punctuation_swap (base, ","));
  g_assert_true (pos_completer_base_wants_punctuation_swap (base, "!"));

  g_assert_finalize_object (base);
}


int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pos/completer-base/test_punctuation", test_punctuation);

  return g_test_run ();
}
