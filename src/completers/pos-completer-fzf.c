/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-completer-fzf"

#include "pos-config.h"

#include "pos-completer-priv.h"
#include "pos-completer-fzf.h"

#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#include <sys/types.h>
#include <signal.h>

#define MAX_COMPLETIONS 3
#define WORD_LIST       "/usr/share/dict/words"
#define PROG_FZF        "fzf"

enum {
  PROP_0,
  PROP_NAME,
  PROP_PREEDIT,
  PROP_COMPLETIONS,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PosCompleterFzf:
 *
 * A completer using fzf.
 *
 * Uses [fzf](https://github.com/junegunn/fzf) and the systems
 * word list to suggest completions.
 *
 * This is mostly to demo a simple completer.
 */
struct _PosCompleterFzf {
  GObject               parent;

  char                 *name;
  GString              *preedit;
  GStrv                 completions;
  guint                 max_completions;

  GPid                  last_fzf_pid;
};


static void pos_completer_fzf_interface_init (PosCompleterInterface *iface);
static void pos_completer_fzf_initable_interface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PosCompleterFzf, pos_completer_fzf, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (POS_TYPE_COMPLETER,
                                                pos_completer_fzf_interface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                pos_completer_fzf_initable_interface_init))

typedef struct {
  GPid                  fzf_pid;
  char                  fzf_read_buf[256];
  gint                  fzf_child_watch_id;
  GString              *fzf_response;
  GInputStream         *fzf_stdout;
  GCancellable         *fzf_cancellable;
  gulong                fzf_cancellable_id;
  PosCompleterFzf      *completer;
} FzfData;


static void
fzf_data_free (FzfData *data)
{
  if (data->completer->last_fzf_pid == data->fzf_pid)
    data->completer->last_fzf_pid = 0;
  g_object_unref (data->completer);
  g_clear_signal_handler (&data->fzf_cancellable_id, data->fzf_cancellable);
  g_clear_object (&data->fzf_cancellable);
  g_clear_handle_id (&data->fzf_child_watch_id, g_source_remove);
  g_spawn_close_pid (data->fzf_pid);
  g_string_free (data->fzf_response, TRUE);
  g_clear_object (&data->fzf_stdout);
  g_free (data);
}


static void
pos_completer_fzf_set_completions (PosCompleter *iface, GStrv completions)
{
  PosCompleterFzf *self = POS_COMPLETER_FZF (iface);

  g_strfreev (self->completions);
  self->completions = g_strdupv (completions);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETIONS]);
}


static const char *
pos_completer_fzf_get_preedit (PosCompleter *iface)
{
  PosCompleterFzf *self = POS_COMPLETER_FZF (iface);

  return self->preedit->str;
}


static void
pos_completer_fzf_set_preedit (PosCompleter *iface, const char *preedit)
{
  PosCompleterFzf *self = POS_COMPLETER_FZF (iface);

  if (g_strcmp0 (self->preedit->str, preedit) == 0)
    return;

  g_string_truncate (self->preedit, 0);
  if (preedit)
    g_string_append (self->preedit, preedit);
  else {
    pos_completer_fzf_set_completions (POS_COMPLETER (self), NULL);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PREEDIT]);
}


static void
pos_completer_fzf_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PosCompleterFzf *self = POS_COMPLETER_FZF (object);

  switch (property_id) {
  case PROP_PREEDIT:
    pos_completer_fzf_set_preedit (POS_COMPLETER (self), g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_completer_fzf_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  PosCompleterFzf *self = POS_COMPLETER_FZF (object);

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
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_completer_fzf_finalize (GObject *object)
{
  PosCompleterFzf *self = POS_COMPLETER_FZF(object);

  g_clear_pointer (&self->completions, g_strfreev);
  g_string_free (self->preedit, TRUE);

  G_OBJECT_CLASS (pos_completer_fzf_parent_class)->finalize (object);
}


static void
pos_completer_fzf_class_init (PosCompleterFzfClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_completer_fzf_get_property;
  object_class->set_property = pos_completer_fzf_set_property;
  object_class->finalize = pos_completer_fzf_finalize;

  g_object_class_override_property (object_class, PROP_NAME, "name");
  props[PROP_NAME] = g_object_class_find_property (object_class, "name");

  g_object_class_override_property (object_class, PROP_PREEDIT, "preedit");
  props[PROP_PREEDIT] = g_object_class_find_property (object_class, "preedit");

  g_object_class_override_property (object_class, PROP_COMPLETIONS, "completions");
  props[PROP_COMPLETIONS] = g_object_class_find_property (object_class, "completions");
}


static void
on_fzf_exited (GPid pid, int status, gpointer user_data)
{
  FzfData *data = user_data;

  if (status != 0 && status != SIGTERM) {
    g_warning ("fzf exited with %d", status);
    return;
  }

  if (data->fzf_response->len) {
    g_auto (GStrv) completions = g_strsplit (data->fzf_response->str, "\n", -1);
    pos_completer_fzf_set_completions (POS_COMPLETER (data->completer), completions);
  } else {
    pos_completer_fzf_set_completions (POS_COMPLETER (data->completer), NULL);
  }
  fzf_data_free (data);
}


static void
on_fzf_cancelled (GObject *object, gpointer user_data)
{
  fzf_data_free (user_data);
}


static void
on_fzf_read_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GInputStream *fzf_out = G_INPUT_STREAM (source_object);
  FzfData *data = user_data;
  gssize read_size;
  g_autoptr (GError) error = NULL;

  read_size = g_input_stream_read_finish (fzf_out, res, &error);
  switch (read_size) {
  case -1:
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to read from fzf: %s", error->message);
    break;
  case 0:
    data->fzf_child_watch_id = g_child_watch_add (data->fzf_pid, on_fzf_exited, data);
    break;
  default:
    g_string_append_len (data->fzf_response, data->fzf_read_buf, read_size);
    g_input_stream_read_async (fzf_out,
                               data->fzf_read_buf,
                               sizeof(data->fzf_read_buf),
                               G_PRIORITY_DEFAULT,
                               data->fzf_cancellable,
                               on_fzf_read_done,
                               data);
    return;
  }

  g_input_stream_close (fzf_out, NULL, NULL);
}


static gboolean
pos_completer_fzf_initable_init (GInitable    *initable,
                                 GCancellable *cancelable,
                                 GError      **error)
{
  if (g_file_test (WORD_LIST, G_FILE_TEST_EXISTS) == FALSE) {
    g_set_error_literal (error,
                         G_IO_ERROR,
                         G_IO_ERROR_NOT_FOUND,
                         "Wordlist " WORD_LIST " does not exist");
    return FALSE;
  }

  return TRUE;
}


static void
pos_completer_fzf_initable_interface_init (GInitableIface *iface)
{
  iface->init = pos_completer_fzf_initable_init;
}

static const char *
pos_completer_fzf_get_name (PosCompleter *iface)
{
  PosCompleterFzf *self = POS_COMPLETER_FZF (iface);

  return self->name;
}

static gboolean
pos_completer_fzf_feed_symbol (PosCompleter *iface, const char *symbol)
{
  PosCompleterFzf *self = POS_COMPLETER_FZF (iface);
  g_autoptr (GPtrArray) fzf_argv = g_ptr_array_new ();
  g_autofree char *cmd = NULL;
  g_autofree char *preedit = g_strdup (self->preedit->str);
  g_autoptr (GError) err = NULL;
  FzfData *data;
  GInputStream *fzf_stdout;
  GPid fzf_pid;
  int fzf_stdout_fd;

  if (pos_completer_add_preedit (POS_COMPLETER (self), self->preedit, symbol)) {
    g_signal_emit_by_name (self, "commit-string", self->preedit->str, 0, 0);
    pos_completer_fzf_set_preedit (POS_COMPLETER (self), NULL);

    /* Make sure enter is processed as raw keystroke */
    if (g_strcmp0 (symbol, "KEY_ENTER") == 0)
      return FALSE;

    return TRUE;
  }

  /* preedit didn't change and wasn't committed so we didn't handle it */
  if (g_strcmp0 (self->preedit->str, preedit) == 0)
    return FALSE;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PREEDIT]);

  if (self->last_fzf_pid) {
    g_debug ("Killing slow %d", self->last_fzf_pid);
    kill (self->last_fzf_pid, SIGTERM);
    self->last_fzf_pid = 0;
  }

  g_debug ("Looking up string '%s'", self->preedit->str);
  /* TODO: This is obviously just an experiment. wordlists can be changed
   * via select-default-wordlist
   */
  cmd = g_strdup_printf ("cat " WORD_LIST " | " PROG_FZF " -f '%s' -0 | head -%d",
                         self->preedit->str, MAX_COMPLETIONS);

  g_ptr_array_add (fzf_argv, "/bin/sh");
  g_ptr_array_add (fzf_argv, "-c");
  g_ptr_array_add (fzf_argv, cmd);
  g_ptr_array_add (fzf_argv, NULL);

  if (!g_spawn_async_with_pipes (NULL,
                                 (char **) fzf_argv->pdata,
                                 NULL, /* envp */
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL, /* setup-func */
                                 NULL, /* user_data */
                                 &fzf_pid,
                                 NULL,
                                 &fzf_stdout_fd,
                                 NULL,
                                 &err)) {
    g_warning ("Failed to spawn fzf: %s", err->message);
    return FALSE;
  }

  self->last_fzf_pid = fzf_pid;
  fzf_stdout = g_unix_input_stream_new (fzf_stdout_fd, TRUE);
  data  = g_new0 (FzfData, 1);
  *data = (FzfData){
    .fzf_pid = fzf_pid,
    .fzf_stdout = fzf_stdout,
    .fzf_response = g_string_new (NULL),
    .fzf_cancellable = g_cancellable_new (),
    .completer = g_object_ref (self),
  };

  data->fzf_cancellable_id = g_cancellable_connect (data->fzf_cancellable,
                                                    G_CALLBACK (on_fzf_cancelled), data, NULL);
  g_input_stream_read_async (data->fzf_stdout,
                             data->fzf_read_buf,
                             sizeof(data->fzf_read_buf),
                             G_PRIORITY_DEFAULT,
                             data->fzf_cancellable,
                             on_fzf_read_done,
                             data);
  return TRUE;
}


static void
pos_completer_fzf_interface_init (PosCompleterInterface *iface)
{
  iface->get_name = pos_completer_fzf_get_name;
  iface->feed_symbol = pos_completer_fzf_feed_symbol;
  iface->get_preedit = pos_completer_fzf_get_preedit;
  iface->set_preedit = pos_completer_fzf_set_preedit;
}


static void
pos_completer_fzf_init (PosCompleterFzf *self)
{
  self->max_completions = MAX_COMPLETIONS;
  self->preedit = g_string_new (NULL);
  self->name = "fzf";
}

/**
 * pos_completer_fzf_new:
 * err: An error location
 *
 * Returns:(transfer full): A new completer
 */
PosCompleter *
pos_completer_fzf_new (GError **err)
{
  return POS_COMPLETER (g_initable_new (POS_TYPE_COMPLETER_FZF, NULL, err, NULL));
}
