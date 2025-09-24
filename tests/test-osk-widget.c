/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-osk-widget.c"


#include "pos-osk-key.h"
#include "pos-main.h"

#include <glib.h>


#define pos_assert_is_shift(w, l, i) G_STMT_START{ \
  PosOskWidgetKeyboardLayer *__layer = pos_osk_widget_get_current_layer ((w)); \
  PosOskWidgetRow *__shift_row = pos_osk_widget_get_row (w, __layer->n_rows) - 2; \
  PosOskKey *__shift = g_ptr_array_index (__shift_row->keys, 0); \
  g_assert_cmpstr (pos_osk_key_get_label (__shift), ==, (l)); \
  g_assert_cmpstr (pos_osk_key_get_icon (__shift), ==, (i)); \
}G_STMT_END


#define pos_assert_is_symbols(w, l) G_STMT_START{ \
  PosOskWidgetKeyboardLayer *__layer = pos_osk_widget_get_current_layer ((w)); \
  PosOskWidgetRow *__sym_row = pos_osk_widget_get_row (w, __layer->n_rows) - 1; \
  PosOskKey *__sym = g_ptr_array_index (__sym_row->keys, 0); \
  g_assert_cmpstr (pos_osk_key_get_label (__sym), ==, (l)); \
  g_assert_null (pos_osk_key_get_icon (__sym)); \
}G_STMT_END


static void
test_switch_layer (void)
{
  gboolean success;
  g_autoptr (GError) err = NULL;
  PosOskWidget *osk_widget = pos_osk_widget_new (PHOSH_OSK_FEATURE_DEFAULT);
  PosOskWidgetRow *row;
  PosOskKey *shift, *symbols, *any;

  pos_init ();

  success = pos_osk_widget_set_layout (osk_widget, "us", "us", "English (US)", "us", NULL, &err);
  g_assert_no_error (err);
  g_assert_true (success);

  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_NORMAL);

  row = pos_osk_widget_get_row (osk_widget, 2);
  shift = g_ptr_array_index (row->keys, 0);
  g_assert_cmpint (pos_osk_key_get_use (shift), ==, POS_OSK_KEY_USE_TOGGLE);
  g_assert_cmpint (pos_osk_key_get_layer (shift), ==, POS_OSK_WIDGET_LAYER_CAPS);

  row = pos_osk_widget_get_row (osk_widget, 3);
  symbols = g_ptr_array_index (row->keys, 0);
  g_assert_cmpint (pos_osk_key_get_use (symbols), ==, POS_OSK_KEY_USE_TOGGLE);
  g_assert_cmpint (pos_osk_key_get_layer (symbols), ==, POS_OSK_WIDGET_LAYER_SYMBOLS);

  row = pos_osk_widget_get_row (osk_widget, 0);
  any = g_ptr_array_index (row->keys, 0);
  g_assert_cmpint (pos_osk_key_get_use (any), ==, POS_OSK_KEY_USE_KEY);

  /* Regular key doesn't switch layers */
  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_NORMAL);
  switch_layer (osk_widget, any);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_NORMAL);

  /* shift switches to caps layer… */
  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_NORMAL);
  switch_layer (osk_widget, shift);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_CAPS);
  pos_assert_is_shift (osk_widget, NULL, "keyboard-shift-filled-symbolic");
  pos_assert_is_symbols (osk_widget, "={<");
  /* …and back  to normal */
  switch_layer (osk_widget, shift);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_NORMAL);
  pos_assert_is_shift (osk_widget, NULL, "keyboard-shift-filled-symbolic");
  pos_assert_is_symbols (osk_widget, "123");

  /* symbols switches to symbols layer… */
  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_NORMAL);
  switch_layer (osk_widget, symbols);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_SYMBOLS);
  pos_assert_is_shift (osk_widget, "={<", NULL);
  pos_assert_is_symbols (osk_widget, "ABC");
  /* …and back  to normal */
  switch_layer (osk_widget, symbols);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_NORMAL);
  pos_assert_is_shift (osk_widget, NULL, "keyboard-shift-filled-symbolic");
  pos_assert_is_symbols (osk_widget, "123");

  /* symbols then shift switches to symbols2 layer… */
  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_NORMAL);
  switch_layer (osk_widget, symbols);
  switch_layer (osk_widget, shift);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_SYMBOLS2);
  pos_assert_is_shift (osk_widget, "123", NULL);
  pos_assert_is_symbols (osk_widget, "ABC");
  /* … shift then back  to symbols */
  switch_layer (osk_widget, shift);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_SYMBOLS);
  pos_assert_is_shift (osk_widget, "={<", NULL);
  pos_assert_is_symbols (osk_widget, "ABC");
  /* … symbols then back to normal */
  switch_layer (osk_widget, symbols);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_NORMAL);
  pos_assert_is_shift (osk_widget, NULL, "keyboard-shift-filled-symbolic");

  /* shift then symbols switches to symbols2 layer… */
  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_NORMAL);
  switch_layer (osk_widget, shift);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_CAPS);
  pos_assert_is_shift (osk_widget, NULL, "keyboard-shift-filled-symbolic");
  switch_layer (osk_widget, symbols);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_SYMBOLS2);
  pos_assert_is_shift (osk_widget, "123", NULL);
  pos_assert_is_symbols (osk_widget, "ABC");
  /* … symbols then back to normal */
  switch_layer (osk_widget, symbols);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_NORMAL);
  pos_assert_is_shift (osk_widget, NULL, "keyboard-shift-filled-symbolic");
  /* shift on symbols2 goes back to symbols */
  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_SYMBOLS2);
  switch_layer (osk_widget, shift);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_SYMBOLS);
  pos_assert_is_shift (osk_widget, "={<", NULL);
  pos_assert_is_symbols (osk_widget, "ABC");

  /* shift switches caps lock back to normal */
  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_NORMAL);
  set_caps_lock (osk_widget, TRUE);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_CAPS);
  switch_layer (osk_widget, shift);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_NORMAL);
  g_assert_false (osk_widget->caps_lock);

  /* symbol switches caps lock to symbol and disables caps lock */
  pos_osk_widget_set_layer (osk_widget, POS_OSK_WIDGET_LAYER_NORMAL);
  set_caps_lock (osk_widget, TRUE);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_CAPS);
  switch_layer (osk_widget, symbols);
  g_assert_cmpint (pos_osk_widget_get_layer (osk_widget), ==, POS_OSK_WIDGET_LAYER_SYMBOLS2);
  g_assert_false (osk_widget->caps_lock);
}


int
main (int argc, char *argv[])
{
  int ret;

  gtk_test_init (&argc, &argv, NULL);

  pos_init ();

  g_test_add_func ("/pos/osk-widget/switch_layer", test_switch_layer);

  ret = g_test_run ();

  pos_uninit ();
  return ret;
}
