/*
 * Copyright © 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos.h"

#include <glib.h>
#include <glib/gprintf.h>


static void
on_text (PosSpeechInput *speech_input, const char *text)
{
  g_printf ("got '%s'", text);
}


int
main (int argc, char *argv[])
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (PosSpeechInput) speech_input = pos_speech_input_new ();

  pos_speech_input_connect (speech_input);
  pos_speech_input_set_enabled (speech_input, TRUE);
  g_signal_connect (speech_input, "text", G_CALLBACK (on_text), NULL);

  g_main_loop_run (loop);
}
