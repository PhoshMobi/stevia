/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define POS_TYPE_EMOJI_DB (pos_emoji_db_get_type ())

G_DECLARE_FINAL_TYPE (PosEmojiDb, pos_emoji_db, POS, EMOJI_DB, GObject)

PosEmojiDb *            pos_emoji_db_get_default (void);
GVariant *              pos_emoji_db_get_data (PosEmojiDb *self);
GStrv                   pos_emoji_db_match_by_name (PosEmojiDb *self,
                                                    const char *match,
                                                    guint       max_matches);

G_END_DECLS
