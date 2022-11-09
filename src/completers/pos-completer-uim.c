/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-completer-uim"

#include "pos-config.h"

#include "pos-completer-priv.h"
#include "pos-completer-uim.h"

#include "util.h"

#include <gmobile.h>

#include <uim.h>
#include <uim-helper.h>
#include <uim-im-switcher.h>
#include <uim-util.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <locale.h>

// #define POS_UIM_TRACE_PROPS 1
#define MAX_COMPLETIONS 3
#define MAX_LEAFS 4

typedef struct {
  const char *id;
  const char *name;
  const char *locale;
  const char *uim;
  const char *used_actions[MAX_LEAFS];
  char       *names[MAX_LEAFS];
  char       *symbols[MAX_LEAFS];
  int         active;
} PosUimInputMethod;


static PosUimInputMethod ims[] = {
  {
    .id = "cn",
    .name = "Pinyin",
    .uim = "py",
    .used_actions = {
      "action_generic_off",
      "action_generic_on",
    },
  },
  {
    .id = "jp",
    .name = "Anthy",
    .uim = "anthy-utf8",
    .used_actions = {
      "action_anthy_utf8_direct",
      "action_anthy_utf8_hiragana",
      "action_anthy_utf8_katakana",
    },
  },
};

enum {
  PROP_0,
  PROP_NAME,
  PROP_PREEDIT,
  PROP_COMPLETIONS,
  PROP_MODE_NAME,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/* The disconnect callback doesn't have an argument so these can't be in the struct */
static int uim_helper_fd = -1;
static int uim_helper_fd_id;

/**
 * PosCompleterUim:
 *
 * A completer using uim.
 *
 * Uses [uim](https://uim.sourceforge.io/) for completions
 */
struct _PosCompleterUim {
  GObject            parent;

  char              *name;
  GString           *preedit;
  GStrv              completions;
  guint              max_completions;

  uim_context        context;

  char              *lang;
  char              *mode_name;
  PosUimInputMethod *uim;
};


static void     pos_completer_uim_interface_init (PosCompleterInterface *iface);
static void     pos_completer_uim_initable_interface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PosCompleterUim, pos_completer_uim, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (POS_TYPE_COMPLETER,
                                                pos_completer_uim_interface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                pos_completer_uim_initable_interface_init)
                         )

static gboolean pos_completer_uim_set_language (PosCompleter *completer,
                                                const char   *lang,
                                                const char   *region,
                                                GError      **error);


static void
pos_uim_input_method_free (PosUimInputMethod *uim)
{
  for (int k = 0; uim->used_actions[k]; k++) {
    g_clear_pointer (&uim->names[k], g_free);
    g_clear_pointer (&uim->symbols[k], g_free);
  }
}


static void
pos_uim_input_method_free_all (void)
{
  for (int i = 0; i < G_N_ELEMENTS (ims); i++)
    pos_uim_input_method_free (&ims[i]);
}


static const char *
pos_completer_uim_get_preedit (PosCompleter * iface)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (iface);

  return self->preedit->str;
}


static gboolean
feed_symbol (PosCompleterUim *self, int symbol)
{
  gboolean processed, state = 0;

  g_debug ("Feeding: '%c', state %d", symbol, state);
  processed = !uim_press_key (self->context, symbol, state);
  uim_release_key (self->context, symbol, state);

  return processed;
}


static void
pos_completer_uim_set_preedit (PosCompleter *iface, const char *preedit)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (iface);

  g_debug ("%s: '%s'", __func__, preedit);

  if (preedit != NULL) {
    g_critical ("Can't set non-empty preedit in uim");
    return;
  }

  feed_symbol (self, UKey_Escape);
}


static const char *
pos_completer_uim_get_mode_name (PosCompleterUim *self)
{
  /* Use current mode as info */
  return self->mode_name;
}


static void
pos_completer_uim_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (object);

  switch (property_id) {
  case PROP_PREEDIT:
    pos_completer_uim_set_preedit (POS_COMPLETER (self), g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_completer_uim_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (object);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  case PROP_PREEDIT:
    g_value_set_string (value, self->preedit->str);
    break;
  case PROP_COMPLETIONS:
    g_value_set_boxed (value, self->completions);
    break;
  case PROP_MODE_NAME:
    g_value_set_string (value, pos_completer_uim_get_mode_name (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
helper_disconnected (void)
{
  g_debug ("Disconnected");

  uim_helper_fd = -1;
  g_clear_handle_id (&uim_helper_fd_id, g_source_remove);
}


static void
parse_helper_str (const char *str)
{
  /* Invoked on config changes in uim-pref-gtk3 */
  g_debug ("%s: %s", __func__, str);
}


static gboolean
helper_read_cb (GIOChannel *channel, GIOCondition c, gpointer p)
{
  char *msg;
  int fd = g_io_channel_unix_get_fd (channel);

  uim_helper_read_proc (fd);
  while ((msg = uim_helper_get_message ())) {
    parse_helper_str (msg);
    free (msg);
  }
  return TRUE;
}


static void
pos_completer_uim_context_destroy (PosCompleterUim *self)
{
  g_clear_pointer (&self->context, uim_release_context);

  if (uim_helper_fd >= 0) {
    uim_helper_close_client_fd (uim_helper_fd);
    uim_helper_fd = -1;
  }
  g_clear_handle_id (&uim_helper_fd_id, g_source_remove);
}


static gboolean
pos_completer_uim_context_create (PosCompleterUim *self, GError **error)
{
  g_autoptr (GIOChannel) channel = NULL;

  uim_helper_fd = uim_helper_init_client_fd (helper_disconnected);
  if (uim_helper_fd < 0) {
    g_set_error (error,
                 POS_COMPLETER_ERROR, POS_COMPLETER_ERROR_ENGINE_INIT,
                 "Failed to init uim fd");
    return FALSE;
  }

  channel = g_io_channel_unix_new (uim_helper_fd);
  uim_helper_fd_id = g_io_add_watch (channel,
                                     G_IO_IN | G_IO_HUP | G_IO_ERR,
                                     helper_read_cb,
                                     NULL);

  return TRUE;
}


static void
pos_completer_uim_finalize (GObject *object)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (object);
  g_autoptr (GError) err = NULL;

  pos_uim_input_method_free_all ();
  g_clear_pointer (&self->completions, g_strfreev);
  g_string_free (self->preedit, TRUE);
  g_clear_pointer (&self->lang, g_free);
  g_clear_pointer (&self->mode_name, g_free);

  pos_completer_uim_context_destroy (self);

  uim_quit ();

  G_OBJECT_CLASS (pos_completer_uim_parent_class)->finalize (object);
}


static void
pos_completer_uim_class_init (PosCompleterUimClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_completer_uim_get_property;
  object_class->set_property = pos_completer_uim_set_property;
  object_class->finalize = pos_completer_uim_finalize;

  g_object_class_override_property (object_class, PROP_NAME, "name");
  props[PROP_NAME] = g_object_class_find_property (object_class, "name");

  g_object_class_override_property (object_class, PROP_PREEDIT, "preedit");
  props[PROP_PREEDIT] = g_object_class_find_property (object_class, "preedit");

  g_object_class_override_property (object_class, PROP_COMPLETIONS, "completions");
  props[PROP_COMPLETIONS] = g_object_class_find_property (object_class, "completions");

  g_object_class_override_property (object_class, PROP_MODE_NAME, "mode-name");
  props[PROP_MODE_NAME] = g_object_class_find_property (object_class, "mode-name");
}


static void
pos_completer_uim_preedit_clear (void *ptr)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (ptr);

  g_debug ("%s", __func__);
  g_assert (POS_IS_COMPLETER_UIM (self));

  g_string_truncate (self->preedit, 0);
}


static void
pos_completer_uim_preedit_pushback (void *ptr, int attr, const char *str)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (ptr);

  g_debug ("%s: %d '%s'", __func__, attr, str);
  g_assert (POS_IS_COMPLETER_UIM (self));

  g_string_append (self->preedit, str);
}


static void
pos_completer_uim_preedit_update (void *ptr)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (ptr);

  g_assert (POS_IS_COMPLETER_UIM (self));

  g_debug ("%s", __func__);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PREEDIT]);
}

/* Called when candidate window should be activated */
static void
pos_completer_uim_candidate_activate (void *ptr, int num, int limit)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (ptr);
  g_autoptr (GStrvBuilder) cand_builder = g_strv_builder_new ();
  g_assert (POS_IS_COMPLETER_UIM (ptr));

  g_debug ("%s: num: %d, limit: %d", __func__, num, limit);
  for (int i = 0; i < num; i++) {
    uim_candidate c;
    const char *str;
    const char *heading;

    c = uim_get_candidate (self->context, i, i);
    str = uim_candidate_get_cand_str (c);
    heading = uim_candidate_get_heading_label (c);

    g_debug (" %d%s%s%s| %s\n", i, (heading && heading[0]) ? "(" : "",
             (heading && heading[0]) ? heading : "", (heading && heading[0]) ? ")" : "", str);

    g_strv_builder_add (cand_builder, str);

    uim_candidate_free (c);
  }

  g_strfreev (self->completions);
  self->completions = g_strv_builder_end (cand_builder);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETIONS]);
}

/* Called when a candidate is selected */
static void
pos_completer_uim_candidate_select (void *ptr, int index)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (ptr);

  g_assert (POS_IS_COMPLETER_UIM (ptr));

  g_debug ("%s: %d", __func__, index);
  uim_set_candidate_index (self->context, index);
}


static void
pos_completer_uim_candidate_shift_page (void *ptr, int direction)
{
  g_assert (POS_IS_COMPLETER_UIM (ptr));

  g_critical ("%s - not implemented", __func__);
}

/* Called when candidate window should be deactivated */
static void
pos_completer_uim_candidate_deactivate (void *ptr)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (ptr);

  g_assert (POS_IS_COMPLETER_UIM (self));
  g_debug ("%s", __func__);

  g_clear_pointer (&self->completions, g_strfreev);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETIONS]);
}


static void
pos_completer_uim_commit (void *ptr, const char *str)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (ptr);

  g_assert (POS_IS_COMPLETER_UIM (self));
  g_debug ("%s: %s", __func__, str);

  g_signal_emit_by_name (self, "commit-string", str, 0, 0);
}


static void
pos_completer_uim_set_mode_name (PosCompleterUim *self, const char *mode_name)
{
  if (!g_set_str (&self->mode_name, mode_name))
    return;

  g_debug ("mode: %s", self->mode_name);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODE_NAME]);
}


/* See uim/doc/HELPER-PROTOCOL */
typedef struct  {
  gboolean leaf;

  char    *indication_id;
  char    *iconic_label;
  char    *label_string;

  /* Only for lines */
  char    *short_desc;
  char    *action_id;
  gboolean activity;
} PosUimPropLine;


static void
pos_uim_prop_line_free (PosUimPropLine *line)
{
  g_clear_pointer (&line->indication_id, g_free);
  g_clear_pointer (&line->iconic_label, g_free);
  g_clear_pointer (&line->label_string, g_free);
  g_clear_pointer (&line->short_desc, g_free);
  g_clear_pointer (&line->action_id, g_free);

  g_free (line);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PosUimPropLine, pos_uim_prop_line_free);

G_GNUC_WARN_UNUSED_RESULT
static PosUimPropLine *
parse_prop_line (const char *line, GError **err)
{
  g_auto (GStrv) parts = g_strsplit (line, "\t", -1);
  g_autoptr (PosUimPropLine) prop = g_new0 (PosUimPropLine, 1);

  g_assert (err == NULL || *err == NULL);

  if (g_strv_length (parts) < 4) {
    g_set_error (err,
                 pos_completer_error_quark (),
                 POS_COMPLETER_ERROR_LANG_INIT,
                 "Proplist line '%s' too short", line);
    return NULL;
  }

  if (g_str_equal (parts[0], "leaf")) {
    prop->leaf = TRUE;
  } else if (!g_str_equal (parts[0], "branch")) {
    g_set_error (err,
                 pos_completer_error_quark (),
                 POS_COMPLETER_ERROR_LANG_INIT,
                 "Invalid type in proplist line '%s'", line);
    return NULL;
  }

  prop->indication_id = g_strdup (parts[1]);
  prop->iconic_label = g_strdup (parts[2]);
  prop->label_string = g_strdup (parts[3]);

  if (!prop->leaf)
    return g_steal_pointer (&prop);

  if (g_strv_length (parts) < 7) {
    g_set_error (err,
                 pos_completer_error_quark (),
                 POS_COMPLETER_ERROR_LANG_INIT,
                 "Proplist line '%s' too short", line);
    return NULL;
  }

  prop->short_desc = g_strdup (parts[4]);
  prop->action_id = g_strdup (parts[5]);

  if (g_str_equal (parts[6], "*"))
    prop->activity = TRUE;

  return g_steal_pointer (&prop);
}

typedef enum {
  POS_UIM_PROP_PARSE_NONE,
  POS_UIM_PROP_PARSE_IM,
  POS_UIM_PROP_PARSE_OUTPUT,
  POS_UIM_PROP_PARSE_INPUT,
} PosUimPropParseState;

static void
prop_list_update (void *ptr, const char *str)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (ptr);
  g_autoptr (GString) prop_list = NULL;
  g_auto (GStrv) lines = NULL;
  PosUimPropParseState state = POS_UIM_PROP_PARSE_NONE;
  gboolean have_im = FALSE;

  g_assert (POS_IS_COMPLETER_UIM (self));

  lines = g_strsplit (str, "\n", -1);

  if (gm_strv_is_null_or_empty (lines)) {
    g_warning ("Got empty prop list");
    goto done;
  }

  self->uim->active = -1;
  pos_uim_input_method_free_all ();

  /* Find our actions */
  for (int i = 0; lines[i]; i++) {
    g_autoptr (GError) err = NULL;
    g_autoptr (PosUimPropLine) prop = NULL;

    if (gm_str_is_null_or_empty (lines[i]))
      continue;

    prop = parse_prop_line (lines[i], &err);
    if (!prop) {
      g_warning ("Failed to parse prop line '%s': %s", lines[i], err->message);
      continue;
    }

    if (state == POS_UIM_PROP_PARSE_NONE) {
      if (prop->leaf == FALSE) {
        /* First branch is list of IMs */
        state = POS_UIM_PROP_PARSE_IM;
      }
    } else if (state == POS_UIM_PROP_PARSE_IM) {
      if (prop->leaf) {
        /* Is this our current wanted IM ? */
        if (g_str_equal (self->uim->uim, prop->indication_id)) {
          g_debug ("Found our uim in proplist: %s", self->uim->uim);
          if (prop->activity) {
            g_debug ("Our IM is active");
            have_im = TRUE;
          } else {
            goto done;
          }
        }
      } else {
        state = POS_UIM_PROP_PARSE_OUTPUT;
      }
    } else if (state == POS_UIM_PROP_PARSE_OUTPUT) {
      PosUimInputMethod *uim = self->uim;

      if (!have_im) {
        g_warning ("Didn't find our IM, can't parse outputs");
        break;
      }

      if (prop->leaf) {
        for (int k = 0; uim->used_actions[k]; k++) {
          if (!g_str_equal (uim->used_actions[k], prop->action_id))
            continue;

          uim->names[k] = g_strdup (prop->label_string);
          uim->symbols[k] = g_strdup (prop->iconic_label);
          if (prop->activity)
            uim->active = k;
        }
      } else {
        state = POS_UIM_PROP_PARSE_INPUT;
      }
    }
  }

  for (int k = 0; self->uim->used_actions[k]; k++) {
    if (!self->uim->used_actions[k])
      break;

    if (!self->uim->names[k]) {
      g_warning ("Did not find name for action '%s'", self->uim->used_actions[k]);
      goto done;
    }

    if (!self->uim->symbols[k]) {
      g_warning ("Did not find symbol for action '%s'", self->uim->used_actions[k]);
      goto done;
    }
  }

  g_debug ("Found all needed actions for '%s'", self->uim->name);

  if (self->uim->active == -1) {
    g_warning ("Didn't find valid activity, forcing first one");
    self->uim->active = 0;
  }

  pos_completer_uim_set_mode_name (self, self->uim->names[self->uim->active]);
  /* TODO: Force a valid mode */

 done:
  prop_list = g_string_new ("");
  g_string_printf (prop_list, "prop_list_update\ncharset=UTF-8\n%s", str);
#ifdef POS_UIM_TRACE_PROPS
  g_debug ("Updating prop list: '%s'", str);
#endif
  uim_helper_send_message (uim_helper_fd, prop_list->str);
}


static gboolean
pos_completer_uim_initable_init (GInitable    *initable,
                                 GCancellable *cancelable,
                                 GError      **error)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (initable);

  if (uim_init () == -1) {
    g_set_error (error,
                 POS_COMPLETER_ERROR, POS_COMPLETER_ERROR_ENGINE_INIT,
                 "Failed to init uim engine");
    return FALSE;
  }

  return pos_completer_uim_set_language (POS_COMPLETER (self), "jp", NULL, error);
}


static void
pos_completer_uim_initable_interface_init (GInitableIface *iface)
{
  iface->init = pos_completer_uim_initable_init;
}


static int
symbol_to_key (const char *symbol, int *uim_state)
{
  int uim_ascii;

  g_assert (symbol);
  g_assert (*uim_state == 0);

  if (g_str_equal (symbol, "KEY_ENTER"))
    return UKey_Return;
  else if (g_str_equal (symbol, "KEY_BACKSPACE"))
    return UKey_Backspace;

  /* If we want to allow toggling via a key on the OSK:
   * g_str_equal (symbol, "KEY_ZENKAKUHANKAKU")){
   * return UKey_Zenkaku_Hankaku; */

  g_return_val_if_fail (strlen (symbol) == 1, ' ');

  uim_ascii = symbol[0];
  if (g_ascii_isupper (symbol[0])) {
    *uim_state |= UMod_Shift;
    uim_ascii = g_ascii_tolower (symbol[0]);
  }

  if (g_ascii_isalnum (uim_ascii) || g_ascii_isspace (uim_ascii))
    return uim_ascii;

  g_warning ("Unhandled symbol '%s'", symbol);
  return ' ';
}


static const char *
pos_completer_uim_get_name (PosCompleter *iface)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (iface);

  return self->name;
}


static gboolean
pos_completer_uim_feed_symbol (PosCompleter *iface, const char *symbol)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (iface);
  int processed, uim_symbol, uim_state = 0;

  uim_symbol = symbol_to_key (symbol, &uim_state);
  processed = feed_symbol (self, uim_symbol);

  return processed;
}


static gboolean
pos_completer_uim_set_language (PosCompleter *completer,
                                const char   *lang,
                                const char   *region,
                                GError      **error)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (completer);
  PosUimInputMethod *uim = NULL;

  g_return_val_if_fail (POS_IS_COMPLETER_UIM (self), FALSE);

  if (g_strcmp0 (self->lang, lang) == 0)
    return TRUE;

  g_clear_pointer (&self->lang, g_free);

  for (int i = 0; i < G_N_ELEMENTS (ims); i++) {
    if (g_str_equal (lang, ims[i].id)) {
      uim = &ims[i];
      break;
    }
  }

  if (uim == NULL) {
    g_set_error (error,
                 POS_COMPLETER_ERROR, POS_COMPLETER_ERROR_ENGINE_INIT,
                 "'%s' not supported by uim completer", lang);
    return FALSE;
  }

  self->uim = uim;
  pos_completer_uim_context_destroy (self);
  if (!pos_completer_uim_context_create (self, error))
    return FALSE;

  self->context = uim_create_context (self,
                                      "UTF-8",
                                      NULL,
                                      self->uim->uim,
                                      uim_iconv,
                                      pos_completer_uim_commit);
  if (self->context == NULL) {
    g_set_error (error,
                 POS_COMPLETER_ERROR, POS_COMPLETER_ERROR_ENGINE_INIT,
                 "Failed to create uim context for '%s'", self->uim->uim);
    return FALSE;
  }
  g_debug ("Uim completer inited with engine '%s'", self->uim->uim);

  uim_set_uim_fd (self->context, uim_helper_fd);
  uim_set_preedit_cb (self->context,
                      pos_completer_uim_preedit_clear,
                      pos_completer_uim_preedit_pushback,
                      pos_completer_uim_preedit_update);

  uim_set_candidate_selector_cb (self->context,
                                 pos_completer_uim_candidate_activate,
                                 pos_completer_uim_candidate_select,
                                 pos_completer_uim_candidate_shift_page,
                                 pos_completer_uim_candidate_deactivate);

  uim_set_prop_list_update_cb (self->context, prop_list_update);

  /* Trigger propery list update */
  uim_prop_list_update (self->context);

  g_debug ("Selected: %s", uim_get_current_im_name (self->context));

  self->lang = g_strdup (lang);

  return TRUE;
}


static char *
pos_completer_uim_get_display_name (PosCompleter *iface)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (iface);
  g_autofree char *lang = NULL, *upper = NULL;
  g_auto (GStrv) parts = NULL;

  const char *im_name;

  g_return_val_if_fail (self->context, NULL);

  upper = g_utf8_strup (self->lang, -1);
  lang = gnome_get_country_from_code (upper, NULL);

  im_name = uim_get_current_im_name (self->context);
  parts = g_strsplit (im_name, "-", -1);

  return g_strdup_printf ("%s (%s)", lang, parts[0]);
}


static gboolean
pos_completer_uim_set_selected (PosCompleter *completer, const char *selected)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (completer);

  for (int i = 0; i < g_strv_length (self->completions); i++) {
    if (g_strcmp0 (selected, self->completions[i]) == 0) {
      g_debug ("Matched completion: %d: %s", i, selected);
      uim_set_candidate_index (self->context, i);
      /* Submit the candidate */
      feed_symbol (self, UKey_Return);
      break;
    }
  }

  return TRUE;
}


static void
pos_completer_uim_toggle_mode (PosCompleter *completer)
{
  PosCompleterUim *self = POS_COMPLETER_UIM (completer);
  const char *action;

  action = self->uim->used_actions[self->uim->active + 1];
  if (action == NULL)
    action = self->uim->used_actions[0];

  /* Convert and submit anything pending content so it doesn't get lost */
  feed_symbol (self, UKey_Return);

  uim_prop_activate (self->context, action);
}


static void
pos_completer_uim_interface_init (PosCompleterInterface *iface)
{
  iface->get_name = pos_completer_uim_get_name;
  iface->feed_symbol = pos_completer_uim_feed_symbol;
  iface->get_preedit = pos_completer_uim_get_preedit;
  iface->set_preedit = pos_completer_uim_set_preedit;
  iface->set_language = pos_completer_uim_set_language;
  iface->get_display_name = pos_completer_uim_get_display_name;
  iface->set_selected = pos_completer_uim_set_selected;
  iface->toggle_mode = pos_completer_uim_toggle_mode;
}


static void
pos_completer_uim_init (PosCompleterUim *self)
{
  self->max_completions = MAX_COMPLETIONS;
  self->preedit = g_string_new (NULL);
  self->name = "uim";
}

/**
 * pos_completer_uim_new:
 * @err:(nullable): a GError location to store the error occurring, or NULL to ignore.
 *
 * Returns:(transfer full): A new uim based completer.
 */
PosCompleter *
pos_completer_uim_new (GError **err)
{
  return POS_COMPLETER (g_initable_new (POS_TYPE_COMPLETER_UIM, NULL, err, NULL));
}
