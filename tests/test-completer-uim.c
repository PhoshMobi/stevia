/*
 * 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-test-completer.h"

#include "pos-completer-uim.h"

#include <gtk/gtk.h>
#include <gio/gio.h>

#define MAX_ITERATIONS 4

typedef struct {
  GMainLoop       *loop;
  const char      *mode_name;
  PosCompleterUim *uim;
  int iterations;
} WaitData;


static gboolean
wait_mode (gpointer data)
{
  WaitData *w = data;
  g_autofree char *mode_name = NULL;

  g_object_get (w->uim, "mode-name", &mode_name, NULL);

  if (g_str_equal (w->mode_name,mode_name)) {
    g_test_message ("Found mode '%s'", w->mode_name);
    g_main_loop_quit (w->loop);
  }

  if (w->iterations >= MAX_ITERATIONS)
    g_assert_cmpstr (w->mode_name, ==, mode_name);

  w->iterations++;

  return G_SOURCE_CONTINUE;
}


static void
test_completer_uim_object (void)
{
  g_autoptr (GError) err = NULL;
  g_auto (GStrv) completions = NULL;
  g_autofree char *mode_name = NULL;
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  WaitData w = {
    .loop = loop,
  };

  w.uim = g_initable_new (POS_TYPE_COMPLETER_UIM, NULL, &err, NULL);
  g_assert_no_error (err);
  pos_completer_assert_initial_state (POS_COMPLETER (w.uim), "uim", "Japan (anthy)");

  /* Direct mode */
  w.mode_name = "直接入力";
  /* Wait as the prop list updates happens async via an external process */
  g_timeout_add (500, wait_mode, &w);
  g_main_loop_run (loop);

  /* Switch to Hirakana */
  pos_completer_toggle_mode (POS_COMPLETER (w.uim));
  w.iterations = 0;
  w.mode_name = "ひらがな";
  g_timeout_add (500, wait_mode, &w);
  g_main_loop_run (loop);

  g_assert_finalize_object (w.uim);
}


int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/pos/completer/uim/object", test_completer_uim_object);

  return g_test_run ();
}
