/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-completer-manager"

#include "pos-config.h"

#include "pos-completer-manager.h"
#include "completers/pos-completer-presage.h"
#include "completers/pos-completer-pipe.h"
#ifdef POS_HAVE_FZF
# include "completers/pos-completer-fzf.h"
#endif
#ifdef POS_HAVE_HUNSPELL
# include "completers/pos-completer-hunspell.h"
#endif
#ifdef POS_HAVE_VARNAM
# include "completers/pos-completer-varnam.h"
#endif

#include "gmobile.h"

#include <gio/gio.h>

/**
 * PosCompleterManager:
 *
 * Manages initialization and lookup of the different completion engines.
 */

/**
 * PosCompletionInfo:
 *
 * Info about a completer for certain region/language.
 */

enum {
  PROP_0,
  PROP_DEFAULT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PosCompleterManager {
  GObject           parent;

  PosCompleter     *default_;
  GSettings        *settings;

  GHashTable       *completers; /* key: engine name, value: PosCompleter */
};
G_DEFINE_TYPE (PosCompleterManager, pos_completer_manager, G_TYPE_OBJECT)


static PosCompletionInfo *
pos_completion_info_new (void)
{
  return g_new0 (PosCompletionInfo, 1);
}


void
pos_completion_info_free (PosCompletionInfo *info)
{
  g_clear_object (&info->completer);
  g_clear_pointer (&info->lang, g_free);
  g_clear_pointer (&info->region, g_free);
  g_clear_pointer (&info->display_name, g_free);

  g_free (info);
}


static PosCompleter *
init_completer (PosCompleterManager *self, const char *name, GError **err)
{
  g_autoptr (PosCompleter) completer = NULL;

  completer = g_hash_table_lookup (self->completers, name);
  if (completer)
    return g_steal_pointer (&completer);

  if (g_strcmp0 (name, "pipe") == 0) {
    completer = pos_completer_pipe_new (err);
    if (completer)
      goto done;
    return NULL;
#ifdef POS_HAVE_PRESAGE
  } else if (g_strcmp0 (name, "presage") == 0) {
    completer = pos_completer_presage_new (err);
    if (completer)
      goto done;
    return NULL;
#endif
#ifdef POS_HAVE_FZF
  } else if (g_strcmp0 (name, "fzf") == 0) {
    completer = pos_completer_fzf_new (err);
    if (completer)
      goto done;
    return NULL;
#endif
#ifdef POS_HAVE_HUNSPELL
  } else if (g_strcmp0 (name, "hunspell") == 0) {
    completer = pos_completer_hunspell_new (err);
    if (completer)
      goto done;
    return NULL;
#endif
#ifdef POS_HAVE_VARNAM
  } else if (g_strcmp0 (name, "varnam") == 0) {
    completer = pos_completer_varnam_new (err);
    if (completer)
      goto done;
    return NULL;
#endif
    /* Other optional completer go here */
  }

  g_set_error (err,
               G_IO_ERROR,
               G_IO_ERROR_NOT_FOUND,
               "Completion engine '%s' not found", name);
  return NULL;

 done:
  if (!g_hash_table_contains (self->completers, name))
    g_hash_table_insert (self->completers, g_strdup (name), g_object_ref (completer));
  return completer;
}


static void
on_default_completer_changed (PosCompleterManager *self)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *default_name = NULL;
  PosCompleter *default_ = NULL;

  g_assert (POS_IS_COMPLETER_MANAGER (self));

  default_name = g_settings_get_string (self->settings, "default");
  if (!gm_str_is_null_or_empty (default_name)) {
    default_ = init_completer (self, default_name, &err);
    if (default_ == NULL) {
      g_critical ("Failed to init default completer '%s': %s", default_name,
                  err ? err->message : "Completer does not exist");
      g_clear_error (&err);
    }
  }

  /* Fallback */
  if (default_ == NULL)
    default_ = init_completer (self, POS_DEFAULT_COMPLETER, &err);
  if (default_ == NULL) {
    g_warning ("Failed to init default completer '%s': %s", POS_DEFAULT_COMPLETER,
                err ? err->message : "Completer does not exist");
    g_clear_error (&err);
  }

  if (default_ != NULL && self->default_ != default_) {
    g_debug ("Switching default completer to '%s'", pos_completer_get_name (default_));

    self->default_ = default_;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DEFAULT]);
  }
}


static void
pos_completer_manager_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  PosCompleterManager *self = POS_COMPLETER_MANAGER (object);

  switch (property_id) {
  case PROP_DEFAULT:
    g_value_set_object (value, self->default_);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_completer_manager_finalize (GObject *object)
{
  PosCompleterManager *self = POS_COMPLETER_MANAGER(object);

  g_clear_object (&self->settings);
  g_clear_pointer (&self->completers, g_hash_table_destroy);
  self->default_ = NULL;

  G_OBJECT_CLASS (pos_completer_manager_parent_class)->finalize (object);
}


static void
pos_completer_manager_class_init (PosCompleterManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_completer_manager_get_property;
  object_class->finalize = pos_completer_manager_finalize;

  /**
   * PosCompleterManager:default:
   *
   * The default completer. This completer is used as fallback when
   * no other completer is more suitable.
   */
  props[PROP_DEFAULT] =
    g_param_spec_object ("default", "", "",
                         POS_TYPE_COMPLETER,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
set_initial_completer (PosCompleterManager *self)
{
  const char *name = g_getenv ("POS_TEST_COMPLETER");
  g_autoptr (GError) err = NULL;

  /* Environment */
  if (name) {
    self->default_ = init_completer (self, name, &err);
    if (self->default_) {
      g_debug ("Completer '%s' set via environment", pos_completer_get_name (self->default_));
      return;
    }
    g_critical ("Failed to init test completer '%s': %s", name,
                err ? err->message : "Completer does not exist");
  }

  /* GSetting */
  /* Only listen for changes when not using POS_TEST_COMPLETER */
  g_signal_connect_swapped (self->settings, "changed::default",
                            G_CALLBACK (on_default_completer_changed),
                            self);
  on_default_completer_changed (self);
}


static void
pos_completer_manager_init (PosCompleterManager *self)
{
  self->settings = g_settings_new ("sm.puri.phosh.osk.Completers");
  self->completers = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            g_object_unref);
  set_initial_completer (self);
}


PosCompleterManager *
pos_completer_manager_new (void)
{
  return POS_COMPLETER_MANAGER (g_object_new (POS_TYPE_COMPLETER_MANAGER, NULL));
}

/**
 * pos_completer_manager_get_default_completer:
 * @self: The completer manager
 *
 * The default completer to be used when no other completer is a better match.
 *
 * Returns:(transfer none)(nullable): The default completer.
 */
PosCompleter *
pos_completer_manager_get_default_completer (PosCompleterManager *self)
{
  g_return_val_if_fail (POS_COMPLETER_MANAGER (self), NULL);

  return self->default_;
}

/**
 * pos_completer_manager_get_info: (skip)
 * @self: The completer manager
 * @engine: The desired completion engine
 * @lang: The desired language to use with `engine`
 * @region:(nullable): The desired region to use with `engine`
 * @err: (nullable): An error location
 *
 * Get an info object that can later be used to select a completer for
 * a given language.
 *
 * Given the engine name and a language fills in the necessary
 * information and initializes the completion engine.
 *
 * Returns:(transfer full)(nullable): The completion information or %NULL on error.
 */
PosCompletionInfo *
pos_completer_manager_get_info (PosCompleterManager *self,
                                const char          *engine,
                                const char          *lang,
                                const char          *region,
                                GError             **err)
{
  PosCompleter *completer;
  PosCompletionInfo *info = NULL;

  g_return_val_if_fail (POS_COMPLETER_MANAGER (self), NULL);
  g_return_val_if_fail (engine, NULL);
  g_return_val_if_fail (lang, NULL);
  g_return_val_if_fail (err == NULL || *err == NULL, NULL);

  completer = init_completer (self, engine, err);
  if (!completer)
    return NULL;

  if (!pos_completer_set_language (completer, lang, region, err))
    return NULL;

  info = pos_completion_info_new ();
  info->completer = g_object_ref (completer);
  info->lang = g_strdup (lang);
  info->region = g_strdup (region);
  info->display_name = pos_completer_get_display_name (completer);
  if (!info->display_name)
    info->display_name = g_strdup (lang);

  return info;
}
