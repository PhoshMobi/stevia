/*
 * Copyright (C) 2025 Phosh e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-emoji-db"

#include "pos-config.h"

#include "pos-emoji-db.h"

#include <gtk/gtk.h>

/**
 * PosEmojiDb:
 *
 * The emoji database
 */

struct _PosEmojiDb {
  GObject   parent;

  GVariant *emoji_data;
};
G_DEFINE_TYPE (PosEmojiDb, pos_emoji_db, G_TYPE_OBJECT)


static void
pos_emoji_db_finalize (GObject *object)
{
  PosEmojiDb *self = POS_EMOJI_DB (object);

  g_clear_pointer (&self->emoji_data, g_variant_unref);

  G_OBJECT_CLASS (pos_emoji_db_parent_class)->finalize (object);
}


static void
pos_emoji_db_class_init (PosEmojiDbClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = pos_emoji_db_finalize;
}


static void
pos_emoji_db_init (PosEmojiDb *self)
{
  g_autoptr (GBytes) bytes = NULL;
  g_autoptr (GError) err = NULL;

  bytes = g_resources_lookup_data ("/mobi/phosh/stevia/emoji/en.data", 0, &err);
  if (!bytes) {
    g_critical ("Failed to load emoji data: %s", err->message);
  } else {
    self->emoji_data = g_variant_ref_sink (g_variant_new_from_bytes (G_VARIANT_TYPE ("a(ausasu)"),
                                                                     bytes,
                                                                     TRUE));
  }
}


/**
 * pos_emoji_db_get_default:
 *
 * Return Value: (transfer none): The global emoji db
 */
PosEmojiDb *
pos_emoji_db_get_default (void)
{
  static PosEmojiDb *instance;

  if (instance == NULL) {
    instance = g_object_new (POS_TYPE_EMOJI_DB, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *) &instance);
  }

  return instance;
}

/**
 * pos_emoji_db_get_data:
 * @self: The emoji db
 *
 * Returns:(transfer none): The emoji data
 */
GVariant *
pos_emoji_db_get_data (PosEmojiDb *self)
{
  g_assert (POS_IS_EMOJI_DB (self));

  return self->emoji_data;
}

/**
 * pos_emoji_match_by_name:
 * @self: The emoji db
 * @name: The emoji name (or prefix) to match on
 * @max_mathces: The maximum number of matches. `0` means "all matches".
 *
 * Returns: (transfer full): The matched emojis
 */
GStrv
pos_emoji_db_match_by_name (PosEmojiDb *self, const char *match, guint max_matches)
{
  GVariantIter iter;
  GVariant *item;
  guint matches = 0;
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

  g_assert (POS_IS_EMOJI_DB (self));
  g_assert (match);

  g_variant_iter_init (&iter, self->emoji_data);
  while ((item = g_variant_iter_next_value (&iter))) {
    const char *name;

    g_variant_get_child (item, 1, "&s", &name);

    /* TODO: We always match on en.dat, would be nice to use per language data */
    if (g_strstr_len (name, -1, match)) {
      g_autoptr (GVariant) codes = NULL;
      char text[64];
      char *p = text;

      codes = g_variant_get_child_value (item, 0);
      for (int i = 0; i < g_variant_n_children (codes); i++) {
        gunichar code;

        g_variant_get_child (codes, i, "u", &code);
        if (code)
          p += g_unichar_to_utf8 (code, p);
      }
      p += g_unichar_to_utf8 (0xFE0F, p); /* U+FE0F is the Emoji variation selector */
      *p = '\0';

      g_strv_builder_add (builder, text);
      matches++;

      if (matches == max_matches)
        break;
    }

    g_variant_unref (item);
  }

  return g_strv_builder_end (builder);
}
