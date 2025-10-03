/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-clipboard-manager"

#include "pos-config.h"

#include "pos-clipboard-manager.h"

#include <glib-unix.h>
#include <gio/gunixinputstream.h>

#define MAX_TEXTS 5

/**
 * PosClipboardManager:
 *
 * Handle copy/paste
 */

typedef enum {
  POS_CLIPBOARD_DATA_NONE = 0,
  POS_CLIPBOARD_DATA_TEXT = 1,
} PosClipboardDataType;


typedef enum {
  POS_CLIPBOARD_DEFAULT = 0,
  POS_CLIPBOARD_PRIMARY = 1,
} PosClipboardType;

enum {
  PROP_0,
  PROP_DATA_CONTROL_MANAGER,
  PROP_WL_SEAT,
  PROP_HAS_TEXT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PosClipboardManager {
  GObject  parent;

  struct wl_seat                         *wl_seat;
  struct zwlr_data_control_manager_v1    *data_control_manager;
  struct zwlr_data_control_device_v1     *data_control_device;

  char                                   *mime_type;
  PosClipboardDataType                    data_type;
  GPtrArray                              *texts;
  GCancellable                           *cancel[2];
};
G_DEFINE_TYPE (PosClipboardManager, pos_clipboard_manager, G_TYPE_OBJECT)


static void
pos_clipboard_manager_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (object);

  switch (property_id) {
  case PROP_DATA_CONTROL_MANAGER:
    self->data_control_manager = g_value_get_pointer (value);
    break;
  case PROP_WL_SEAT:
    self->wl_seat = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_clipboard_manager_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (object);

  switch (property_id) {
  case PROP_HAS_TEXT:
    g_value_set_boolean (value, !!self->texts->len);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


#define BUFFER_SIZE 1023
typedef struct  {
  char                               buffer[BUFFER_SIZE+1];
  char                              *text;
  struct zwlr_data_control_offer_v1 *offer;
  PosClipboardType                   clipboard_type;
  PosClipboardDataType               data_type;
  PosClipboardManager               *manager;
} RequestData;


static void
pos_clipboard_manager_destroy_request_data (RequestData *request_data)
{
  g_free (request_data->text);
  zwlr_data_control_offer_v1_destroy (request_data->offer);
  g_free (request_data);
}


static void
pos_clipboard_manager_offer_request_text (GObject      *source_object,
                                          GAsyncResult *res,
                                          gpointer      data)
{
  RequestData *request_data = data;
  GInputStream *stream = G_INPUT_STREAM (source_object);
  g_autoptr (GError) error = NULL;
  gboolean success;
  gsize size;
  PosClipboardManager *self;

  success = g_input_stream_read_all_finish (stream, res, &size, &error);
  if (!success) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to get text from pipe: %s", error->message);
    pos_clipboard_manager_destroy_request_data (request_data);
    return;
  }

  self = request_data->manager;
  if (size > 0) {
    char *text;

    request_data->buffer[size] = '\0';
    text = g_strconcat (request_data->text, request_data->buffer, NULL);
    g_free (request_data->text);
    request_data->text = text;

    if (size == BUFFER_SIZE) {
      g_input_stream_read_all_async (stream, request_data->buffer, BUFFER_SIZE, G_PRIORITY_DEFAULT,
                                     self->cancel[request_data->clipboard_type],
                                     pos_clipboard_manager_offer_request_text,
                                     request_data);
      return;
    }
  }

  if (g_utf8_validate (request_data->text, -1, NULL)) {

    g_debug ("Got %s", request_data->text);
    g_ptr_array_add (self->texts, g_steal_pointer (&request_data->text));
    if (self->texts->len == 1)
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_TEXT]);
  } else {
    g_warning_once ("Invalid utf-8 text received");
  }

  pos_clipboard_manager_destroy_request_data (request_data);
}


static gboolean
pos_clipboard_manager_offer_request_data (PosClipboardManager               *self,
                                          struct zwlr_data_control_offer_v1 *offer,
                                          PosClipboardType                   clipboard_type)
{
  RequestData *request_data;
  g_autoptr (GInputStream) stream = NULL;
  g_autoptr (GError) error = NULL;
  int fds[2];

  if (!g_unix_open_pipe (fds, O_NONBLOCK, &error)) {
    g_warning ("Failed to open pipe: %s", error->message);
    return FALSE;
  }
  zwlr_data_control_offer_v1_receive (offer, self->mime_type, fds[G_UNIX_PIPE_END_WRITE]);
  close (fds[G_UNIX_PIPE_END_WRITE]);

  stream = g_unix_input_stream_new (fds[G_UNIX_PIPE_END_READ], TRUE);
  self->cancel[clipboard_type] = g_cancellable_new ();

  if (self->data_type != POS_CLIPBOARD_DATA_TEXT) {
    close (G_UNIX_PIPE_END_READ);
    g_return_val_if_reached (FALSE);
  }

  request_data = g_new0 (RequestData, 1);
  request_data->offer = offer;
  request_data->manager = self;
  request_data->clipboard_type = clipboard_type;
  request_data->data_type = self->data_type;
  request_data->text = g_strdup ("");

  g_input_stream_read_all_async (stream, request_data->buffer, BUFFER_SIZE, G_PRIORITY_DEFAULT,
                                 self->cancel[request_data->clipboard_type],
                                 pos_clipboard_manager_offer_request_text,
                                 request_data);
  return TRUE;
}


static void
handle_zwlr_data_control_offer_offer (void                              *data,
                                      struct zwlr_data_control_offer_v1 *offer,
                                      const char                        *mime_type)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (data);

  /* we already match */
  if (self->data_type != POS_CLIPBOARD_DATA_NONE)
    return;

  if (g_strcmp0 (mime_type, "text/plain;charset=utf-8") == 0) {
    g_debug ("Found utf8 text in offer");
    self->data_type = POS_CLIPBOARD_DATA_TEXT;

    g_free (self->mime_type);
    self->mime_type = g_strdup (mime_type);
  }
}


static const struct zwlr_data_control_offer_v1_listener zwlr_data_control_offer_v1_listener = {
  handle_zwlr_data_control_offer_offer,
};


static void
handle_zwlr_data_control_device_data_offer (void                               *data,
                                            struct zwlr_data_control_device_v1 *device,
                                            struct zwlr_data_control_offer_v1  *offer)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (data);

  g_assert (POS_IS_CLIPBOARD_MANAGER (self));

  self->data_type = POS_CLIPBOARD_DATA_NONE;
  zwlr_data_control_offer_v1_add_listener (offer,
                                           &zwlr_data_control_offer_v1_listener,
                                           self);
}


static void
handle_zwlr_data_control_device_selection (void                               *data,
                                           struct zwlr_data_control_device_v1 *device,
                                           struct zwlr_data_control_offer_v1  *offer)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (data);

  g_cancellable_cancel (self->cancel[POS_CLIPBOARD_DEFAULT]);
  g_clear_object (&self->cancel[POS_CLIPBOARD_DEFAULT]);

  if (offer == NULL)
    return;

  if (self->data_type == POS_CLIPBOARD_DATA_NONE) {
    zwlr_data_control_offer_v1_destroy (offer);
    return;
  }

  if (!pos_clipboard_manager_offer_request_data (self, offer, POS_CLIPBOARD_DEFAULT))
    zwlr_data_control_offer_v1_destroy (offer);
}


static void
handle_zwlr_data_control_device_primary_selection (void                               *data,
                                                   struct zwlr_data_control_device_v1 *device,
                                                   struct zwlr_data_control_offer_v1  *offer)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (data);

  g_cancellable_cancel (self->cancel[POS_CLIPBOARD_PRIMARY]);
  g_clear_object (&self->cancel[POS_CLIPBOARD_PRIMARY]);

  if (offer == NULL)
    return;

  if (self->data_type == POS_CLIPBOARD_DATA_NONE) {
    zwlr_data_control_offer_v1_destroy (offer);
    return;
  }

  if (!pos_clipboard_manager_offer_request_data (self, offer, POS_CLIPBOARD_PRIMARY))
    zwlr_data_control_offer_v1_destroy (offer);
}


static void
handle_zwlr_data_control_device_finished (void                               *data,
                                          struct zwlr_data_control_device_v1 *device)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (data);

  g_clear_pointer (&self->data_control_device, zwlr_data_control_device_v1_destroy);
}


static const struct zwlr_data_control_device_v1_listener zwlr_data_control_device_v1_listener  = {
  handle_zwlr_data_control_device_data_offer,
  handle_zwlr_data_control_device_selection,
  handle_zwlr_data_control_device_finished,
  handle_zwlr_data_control_device_primary_selection,
};


static void
pos_clipboard_manager_constructed (GObject *object)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (object);

  G_OBJECT_CLASS (pos_clipboard_manager_parent_class)->constructed (object);

  self->data_control_device =
    zwlr_data_control_manager_v1_get_data_device (self->data_control_manager,
                                                  self->wl_seat);
  zwlr_data_control_device_v1_add_listener (self->data_control_device,
                                            &zwlr_data_control_device_v1_listener,
                                            self);
}


static void
pos_clipboard_manager_finalize (GObject *object)
{
  PosClipboardManager *self = POS_CLIPBOARD_MANAGER (object);

  g_cancellable_cancel (self->cancel[POS_CLIPBOARD_PRIMARY]);
  g_clear_object (&self->cancel[POS_CLIPBOARD_PRIMARY]);

  g_cancellable_cancel (self->cancel[POS_CLIPBOARD_DEFAULT]);
  g_clear_object (&self->cancel[POS_CLIPBOARD_DEFAULT]);

  g_clear_pointer (&self->texts, g_ptr_array_unref);
  g_clear_pointer (&self->mime_type, g_free);
  g_clear_pointer (&self->data_control_device, zwlr_data_control_device_v1_destroy);

  G_OBJECT_CLASS (pos_clipboard_manager_parent_class)->finalize (object);
}


static void
pos_clipboard_manager_class_init (PosClipboardManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_clipboard_manager_get_property;
  object_class->set_property = pos_clipboard_manager_set_property;
  object_class->constructed = pos_clipboard_manager_constructed;
  object_class->finalize = pos_clipboard_manager_finalize;

  props[PROP_DATA_CONTROL_MANAGER] =
    g_param_spec_pointer ("wlr-data-control-manager", "", "",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_WL_SEAT] =
    g_param_spec_pointer ("wl-seat", "", "",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_HAS_TEXT] =
    g_param_spec_boolean ("has-text", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
pos_clipboard_manager_init (PosClipboardManager *self)
{
  self->texts = g_ptr_array_new_full (MAX_TEXTS, g_free);
}


PosClipboardManager *
pos_clipboard_manager_new (struct zwlr_data_control_manager_v1 *manager,
                           struct wl_seat                      *seat)
{
  return POS_CLIPBOARD_MANAGER (g_object_new (POS_TYPE_CLIPBOARD_MANAGER,
                                              "wlr-data-control-manager", manager,
                                              "wl-seat", seat,
                                              NULL));
}

/**
 * pos_clipboard_manager_get_text:
 * @self: The clipboard manager
 *
 * Get the most recently copied text
 *
 * Returns: The text
 */
const char *
pos_clipboard_manager_get_text (PosClipboardManager *self)
{
  g_assert (POS_IS_CLIPBOARD_MANAGER (self));

  if (self->texts->len == 0)
    return NULL;

  return g_ptr_array_index (self->texts, self->texts->len - 1);
}

/**
 * pos_clipboard_manager_get_texts:
 * @self: The clipboard manager
 *
 * Get all texts currently in the clipboard manager
 *
 * Returns:(transfer full): The texts
 */
GStrv
pos_clipboard_manager_get_texts (PosClipboardManager *self)
{
  GStrvBuilder *builder;

  g_assert (POS_IS_CLIPBOARD_MANAGER (self));

  builder = g_strv_builder_new ();
  for (int i = 0; i < self->texts->len; i++)
    g_strv_builder_add (builder, g_ptr_array_index (self->texts, i));

  return g_strv_builder_end (builder);
}
