/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-completer-pipe"

#include "pos-config.h"

#include "pos-completer-priv.h"
#include "pos-completer-pipe.h"

#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#include <sys/types.h>
#include <signal.h>


enum {
  PROP_0,
  PROP_NAME,
  PROP_PREEDIT,
  PROP_COMPLETIONS,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PosCompleterPipe:
 *
 * A completer using a Unix pipe like approach.
 *
 * This completer feeds the preedit to standard input
 * of the given executable and reads the possible completioins
 * from standard output.
 */
struct _PosCompleterPipe {
  GObject       parent;

  char         *name;
  GString      *preedit;
  GStrv         completions;

  GSettings    *settings;
  GStrv         command;
  GSubprocess  *proc;
  GCancellable *cancel;
};


static void pos_completer_pipe_interface_init (PosCompleterInterface *iface);
static void pos_completer_pipe_initable_interface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PosCompleterPipe, pos_completer_pipe, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (POS_TYPE_COMPLETER,
                                                pos_completer_pipe_interface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                pos_completer_pipe_initable_interface_init))


static void
pos_completer_pipe_set_completions (PosCompleter *iface, GStrv completions)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (iface);

  g_strfreev (self->completions);
  self->completions = g_strdupv (completions);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETIONS]);
}


static const char *
pos_completer_pipe_get_preedit (PosCompleter *iface)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (iface);

  return self->preedit->str;
}


static void
pos_completer_pipe_set_preedit (PosCompleter *iface, const char *preedit)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (iface);

  if (g_strcmp0 (self->preedit->str, preedit) == 0)
    return;

  g_string_truncate (self->preedit, 0);
  if (preedit)
    g_string_append (self->preedit, preedit);
  else {
    pos_completer_pipe_set_completions (POS_COMPLETER (self), NULL);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PREEDIT]);
}


static void
pos_completer_pipe_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (object);

  switch (property_id) {
  case PROP_PREEDIT:
    pos_completer_pipe_set_preedit (POS_COMPLETER (self), g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_completer_pipe_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (object);

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
pos_completer_pipe_finalize (GObject *object)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (object);

  g_clear_object (&self->settings);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->proc);
  g_clear_pointer (&self->command, g_strfreev);
  g_clear_pointer (&self->completions, g_strfreev);
  g_string_free (self->preedit, TRUE);

  G_OBJECT_CLASS (pos_completer_pipe_parent_class)->finalize (object);
}


static void
pos_completer_pipe_class_init (PosCompleterPipeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_completer_pipe_get_property;
  object_class->set_property = pos_completer_pipe_set_property;
  object_class->finalize = pos_completer_pipe_finalize;

  g_object_class_override_property (object_class, PROP_NAME, "name");
  props[PROP_NAME] = g_object_class_find_property (object_class, "name");

  g_object_class_override_property (object_class, PROP_PREEDIT, "preedit");
  props[PROP_PREEDIT] = g_object_class_find_property (object_class, "preedit");

  g_object_class_override_property (object_class, PROP_COMPLETIONS, "completions");
  props[PROP_COMPLETIONS] = g_object_class_find_property (object_class, "completions");
}


static gboolean
pos_completer_pipe_initable_init (GInitable    *initable,
                                  GCancellable *cancelable,
                                  GError      **error)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (initable);
  g_autoptr (GError) err = NULL;
  g_autofree char *path = NULL;
  gboolean found;
  g_autofree char *command = NULL;

  command = g_settings_get_string (self->settings, "command");
  if (g_shell_parse_argv (command, NULL, &self->command, &err) == FALSE) {
    g_set_error (error,
                 G_IO_ERROR,
                 G_IO_ERROR_INVALID_FILENAME,
                 "Failed to parse command '%s'", command);
    return FALSE;
  }

  if (g_strv_length (self->command) == 0) {
    g_set_error (error,
                 G_IO_ERROR,
                 G_IO_ERROR_INVALID_FILENAME,
                 "Invalid command '%s'", command);
    return FALSE;
  }

  found = g_file_test (self->command[0], G_FILE_TEST_EXISTS);
  if (!found) {
    path = g_find_program_in_path (self->command[0]);

    /* Avoid G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP so we can use pre 2.72 glib */
    if (path) {
      g_free (self->command[0]);
      self->command[0] = g_steal_pointer (&path);
      found = TRUE;
    }
  }

  if (!found) {
    g_set_error (error,
                 G_IO_ERROR,
                 G_IO_ERROR_NOT_FOUND,
                 "Command '%s' not found", self->command[0]);
    return FALSE;
  }

  g_debug ("Using command '%s'", self->command[0]);
  return TRUE;
}


static void
pos_completer_pipe_initable_interface_init (GInitableIface *iface)
{
  iface->init = pos_completer_pipe_initable_init;
}


static const char *
pos_completer_pipe_get_name (PosCompleter *iface)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (iface);

  return self->name;
}


static void
on_communicate_finish (GObject *source, GAsyncResult *res, gpointer user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;
  PosCompleterPipe *self = POS_COMPLETER_PIPE (user_data);
  g_autofree char *stdout_buf = NULL;
  g_autofree char *stderr_buf = NULL;
  g_auto (GStrv) completions = NULL;
  char *last_char;

  g_return_if_fail (POS_IS_COMPLETER_PIPE (self));

  success = g_subprocess_communicate_utf8_finish (G_SUBPROCESS (source),
                                                  res,
                                                  &stdout_buf,
                                                  &stderr_buf,
                                                  &err);
  if (!success) {
    /* Canceled on shutdown */
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;

    g_warning ("Failed to communicate with %s: %s", self->command[0], err->message);
    goto out;
  }

  if (stdout_buf == NULL)
    goto out;

  /* Avoid empty string ('') at end of list */
  last_char = &stdout_buf[strlen(stdout_buf)-1];
  if (*last_char == '\n')
    *last_char = '\0';

  completions = g_strsplit (stdout_buf, "\n", -1);

out:
  if (stderr_buf)
    g_warning ("%s: %s", self->command[0], stderr_buf);

  pos_completer_pipe_set_completions (POS_COMPLETER (self), completions);
}


static gboolean
pos_completer_pipe_feed_symbol (PosCompleter *iface, const char *symbol)
{
  PosCompleterPipe *self = POS_COMPLETER_PIPE (iface);
  g_autofree char *preedit = g_strdup (self->preedit->str);
  g_autoptr (GError) err = NULL;

  if (pos_completer_add_preedit (POS_COMPLETER (self), self->preedit, symbol)) {
    g_signal_emit_by_name (self, "commit-string", self->preedit->str);
    pos_completer_pipe_set_preedit (POS_COMPLETER (self), NULL);

    /* Make sure enter is processed as raw keystroke */
    if (g_strcmp0 (symbol, "KEY_ENTER") == 0)
      return FALSE;

    return TRUE;
  }

  /* preedit didn't change and wasn't committed so we didn't handle it */
  if (g_strcmp0 (self->preedit->str, preedit) == 0)
    return FALSE;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PREEDIT]);
  g_debug ("Looking up string '%s'", self->preedit->str);

  if (self->proc && g_subprocess_get_if_exited (self->proc) == FALSE) {
    g_debug ("Killing slow %s", g_subprocess_get_identifier (self->proc));
    g_subprocess_force_exit (self->proc);
  }
  g_clear_object (&self->proc);
  self->proc = g_subprocess_newv ((const char * const *)self->command,
                                  G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                  G_SUBPROCESS_FLAGS_STDIN_PIPE,
                                  &err);
  if (self->proc == NULL) {
    g_warning ("Failed to spawn pipe: %s", err->message);
    return FALSE;
  }

  g_subprocess_communicate_utf8_async (self->proc,
                                       self->preedit->str,
                                       self->cancel,
                                       on_communicate_finish,
                                       self);
  return TRUE;
}


static void
pos_completer_pipe_interface_init (PosCompleterInterface *iface)
{
  iface->get_name = pos_completer_pipe_get_name;
  iface->feed_symbol = pos_completer_pipe_feed_symbol;
  iface->get_preedit = pos_completer_pipe_get_preedit;
  iface->set_preedit = pos_completer_pipe_set_preedit;
}


static void
pos_completer_pipe_init (PosCompleterPipe *self)
{
  self->preedit = g_string_new (NULL);
  self->cancel = g_cancellable_new ();
  self->name = "pipe";

  self->settings = g_settings_new ("sm.puri.phosh.osk.Completers.Pipe");
}

/**
 * pos_completer_pipe_new:
 * err: An error location
 *
 * Returns:(transfer full): A new completer
 */
PosCompleter *
pos_completer_pipe_new (GError **err)
{
  return POS_COMPLETER (g_initable_new (POS_TYPE_COMPLETER_PIPE, NULL, err, NULL));
}
