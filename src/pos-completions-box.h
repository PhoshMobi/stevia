/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define POS_TYPE_COMPLETIONS_BOX (pos_completions_box_get_type ())

G_DECLARE_FINAL_TYPE (PosCompletionsBox, pos_completions_box, POS, COMPLETIONS_BOX, GtkBox)

PosCompletionsBox *     pos_completions_box_new (void);
void                    pos_completion_box_set_completions (PosCompletionsBox *self,
                                                            GStrv              completions);

G_END_DECLS
