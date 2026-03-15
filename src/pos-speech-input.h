/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define POS_TYPE_SPEECH_INPUT (pos_speech_input_get_type ())

G_DECLARE_FINAL_TYPE (PosSpeechInput, pos_speech_input, POS, SPEECH_INPUT, GObject)

PosSpeechInput *pos_speech_input_new (void);
void pos_speech_input_set_enabled (PosSpeechInput *self, gboolean enable);
void pos_speech_input_connect (PosSpeechInput *self);

G_END_DECLS
