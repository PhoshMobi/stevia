/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-speech-input"

#include "pos-config.h"

#include "pos-speech-input.h"

#include <gmobile.h>

#include <libsoup/soup.h>

#include <json-glib/json-glib.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

/**
 * PosSpeechInput:
 *
 * Speech input via vosk-server
 *
 * TODO: Move the vosk bits to a separate class using an interface
 */

#define VOSK_SERVER_URL "http://127.0.0.1:2700"
#define SAMPLE_RATE "16000"
#define VOSK_CHUNK_SIZE 4000

// #define VOSK_TRACE

enum {
  PROP_0,
  PROP_PRESENT,
  PROP_ENABLED,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  TEXT,
  PARTIAL,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

struct _PosSpeechInput {
  GObject       parent;

  GstElement   *sink;
  GstElement   *pipeline;

  JsonParser   *parser;

  SoupSession  *session;
  SoupWebsocketConnection *conn;
  gboolean      connecting;
  GCancellable *cancel;
  GByteArray   *buffer;
  char         *partial;

  gboolean      enabled;
};

G_DEFINE_TYPE (PosSpeechInput, pos_speech_input, G_TYPE_OBJECT)


static void
vosk_websocket_send_text (PosSpeechInput *self, const char *str)
{
  g_return_if_fail (self->conn);

  soup_websocket_connection_send_text (self->conn, str);
}


static GstFlowReturn
on_new_sample (GstAppSink *sink, gpointer user_data)
{
  PosSpeechInput *self = POS_SPEECH_INPUT (user_data);
  g_autoptr (GstSample) sample = gst_app_sink_pull_sample (sink);
  GstBuffer *buffer = gst_sample_get_buffer (sample);
  GstMapInfo map;
  gboolean silence = FALSE;

#ifdef VOSK_TRACE
  gsize size = gst_buffer_get_size (buffer);
  g_debug ("Received audio buffer: %lu bytes\n", size);

  GstCaps *caps = gst_sample_get_caps (sample);
  g_print ("caps: %s\n", gst_caps_to_string (caps));
#endif

  if (!self->conn)
    return GST_FLOW_OK;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  g_byte_array_append (self->buffer, map.data, map.size);
  gst_buffer_unmap (buffer, &map);

  if (self->buffer->len >= VOSK_CHUNK_SIZE && !silence) {
    soup_websocket_connection_send_binary (self->conn, self->buffer->data, self->buffer->len);
#ifdef VOSK_TRACE
    g_debug ("Sending buffer of %u bytes\n", self->buffer->len);
#endif
    g_byte_array_set_size (self->buffer, 0);
  }

  return GST_FLOW_OK;
}


static gboolean
setup_gst (PosSpeechInput *self)
{
  g_autoptr (GError) err = NULL;

  self->pipeline =
    gst_parse_launch_full ("pulsesrc name=mic !"
                           "audioconvert !"
                           "audioresample !"
                           "audio/x-raw,format=S16LE,channels=1,rate=" SAMPLE_RATE " !"
                           "appsink name=sink emit-signals=true max-buffers=2 drop=true sync=false",
                           NULL,
                           GST_PARSE_FLAG_FATAL_ERRORS,
                           &err);
  if (!self->pipeline) {
    g_critical ("Failed to create audio source pipeline: %s", err->message);
    return FALSE;
  }

  self->sink = gst_bin_get_by_name (GST_BIN (self->pipeline), "sink");
  if (!self->sink) {
    g_critical ("Failed to get audio sink element");
    return FALSE;
  }

  g_signal_connect (self->sink, "new-sample", G_CALLBACK (on_new_sample), self);

  return TRUE;
}


static void
on_vosk_websocket_message (PosSpeechInput          *self,
                           SoupWebsocketDataType    type,
                           GBytes                  *message,
                           SoupWebsocketConnection *conn)
{
  g_autoptr (GError) err = NULL;
  const gchar *data;
  JsonNode *root;
  JsonObject *obj;
  gsize size;

  if (type != SOUP_WEBSOCKET_DATA_TEXT) {
    g_warning ("Unexpected data type %d", type);
    return;
  }

  data = g_bytes_get_data (message, &size);

  if (!json_parser_load_from_data (self->parser, data, size, &err)) {
    g_warning ("Failed to parse JSON: %s", err->message);
    return;
  }

  root = json_parser_get_root (self->parser);
  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    g_warning ("JSON root is not an object");
    return;
  }

  obj = json_node_get_object (root);

  if (json_object_has_member (obj, "partial")) {
    const gchar *partial_text = json_object_get_string_member (obj, "partial");
    g_debug ("Partial: %s", partial_text);

    if (g_set_str (&self->partial, partial_text))
      g_signal_emit (self, signals[PARTIAL], 0, self->partial);

    return;
  }

  if (json_object_has_member (obj, "result")) {
    JsonArray *result_array = json_object_get_array_member (obj, "result");
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_auto (GStrv) words = NULL;

    for (guint i = 0; i < json_array_get_length (result_array); i++) {
      JsonObject *word_obj = json_array_get_object_element (result_array, i);
      const gchar *word = json_object_get_string_member (word_obj, "word");
      gdouble start = json_object_get_double_member (word_obj, "start");
      gdouble end = json_object_get_double_member (word_obj, "end");

      g_debug ("Final: %s (%.2f-%.2f)\n", word, start, end);
      g_strv_builder_add (builder, word);
    }

    words = g_strv_builder_end (builder);
    if (!gm_strv_is_null_or_empty (words)) {
      g_autofree char *text = g_strjoinv (" ", words);

      /* We have text, clear partial */
      g_clear_pointer (&self->partial, g_free);
      g_signal_emit (self, signals[TEXT], 0, text);
    }
  }
}


static void
on_vosk_websocket_closed (PosSpeechInput *self, SoupWebsocketConnection *conn)
{
  g_debug ("Vosk websocket connection closed");

  g_clear_object (&self->conn);
  g_byte_array_set_size (self->buffer, 0);
  g_clear_pointer (&self->partial, g_free);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
}


static void
on_vosk_websocket_error (PosSpeechInput          *self,
                         GError                  *error,
                         SoupWebsocketConnection *conn)
{
  g_warning ("Vosk websocket error: %s\n", error->message);
}


static void
on_vosk_websocket_connected (GObject      *source,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  PosSpeechInput *self;
  g_autoptr (GError) error = NULL;
  SoupWebsocketConnection *conn;

  conn = soup_session_websocket_connect_finish (SOUP_SESSION (source),
                                                result,
                                                &error);
  /* Cancelled on dispose, `self` is gone */
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  self = POS_SPEECH_INPUT (user_data);
  g_assert (POS_IS_SPEECH_INPUT (self));
  self->connecting = FALSE;

  if (error) {
    g_debug ("Connection to vosk server failed: %s\n", error->message);
    return;
  }

  self->conn = conn;

  g_debug ("Connected to vosk server via WebSocket");

  vosk_websocket_send_text (self, "{ \"config\" : { \"sample_rate\" : " SAMPLE_RATE " } }");

  g_object_connect (self->conn,
                    "swapped-object-signal::message", on_vosk_websocket_message, self,
                    "swapped-object-signal::closed", on_vosk_websocket_closed, self,
                    "swapped-object-signal::error", on_vosk_websocket_error, self,
                    NULL);

  g_debug ("Speech input available");
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
}


static void
open_vosk_websocket (PosSpeechInput *self)
{
  g_autoptr (SoupMessage) msg = NULL;

  if (self->conn || self->connecting)
    return;

  self->connecting = TRUE;
  msg = soup_message_new ("GET", VOSK_SERVER_URL);
  soup_session_websocket_connect_async (self->session,
                                        msg,
                                        NULL,
                                        NULL,
                                        G_PRIORITY_DEFAULT,
                                        self->cancel,
                                        on_vosk_websocket_connected,
                                        self);
}


/* Ask the server for the result of what it has heard so far, keeps the connection open */
static void
flush_vosk_websocket (PosSpeechInput *self)
{
  if (!self->conn)
    return;

  vosk_websocket_send_text (self, "{\"reset\" : 1}");
}


static gboolean
pos_speech_input_get_present (PosSpeechInput *self)
{
  return self->sink && self->conn;
}


static void
pos_speech_input_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  PosSpeechInput *self = POS_SPEECH_INPUT (object);

  switch (property_id) {
  case PROP_ENABLED:
    pos_speech_input_set_enabled (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_speech_input_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  PosSpeechInput *self = POS_SPEECH_INPUT (object);

  switch (property_id) {
  case PROP_PRESENT:
    g_value_set_boolean (value, pos_speech_input_get_present (self));
    break;
  case PROP_ENABLED:
    g_value_set_boolean (value, self->enabled);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_speech_input_dispose (GObject *object)
{
  PosSpeechInput *self = POS_SPEECH_INPUT (object);

  /* Stop the streaming thread so it can't call into us anymore */
  if (self->pipeline)
    gst_element_set_state (self->pipeline, GST_STATE_NULL);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  if (self->conn) {
    g_signal_handlers_disconnect_by_data (self->conn, self);
    if (soup_websocket_connection_get_state (self->conn) == SOUP_WEBSOCKET_STATE_OPEN)
      soup_websocket_connection_close (self->conn, SOUP_WEBSOCKET_CLOSE_NORMAL, NULL);
    g_clear_object (&self->conn);
  }

  g_clear_object (&self->session);
  g_clear_object (&self->pipeline);
  g_clear_object (&self->sink);
  g_clear_object (&self->parser);

  g_clear_pointer (&self->buffer, g_byte_array_unref);
  g_clear_pointer (&self->partial, g_free);

  G_OBJECT_CLASS (pos_speech_input_parent_class)->dispose (object);
}


static void
pos_speech_input_class_init (PosSpeechInputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_speech_input_get_property;
  object_class->set_property = pos_speech_input_set_property;
  object_class->dispose = pos_speech_input_dispose;

  props[PROP_PRESENT] =
    g_param_spec_boolean ("present", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[TEXT] = g_signal_new ("text",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                0, NULL, NULL, NULL,
                                G_TYPE_NONE,
                                1,
                                G_TYPE_STRING);

  signals[PARTIAL] = g_signal_new ("partial",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL,
                                   G_TYPE_NONE,
                                   1,
                                   G_TYPE_STRING);
}


static void
pos_speech_input_init (PosSpeechInput *self)
{
  self->buffer = g_byte_array_new ();
  self->parser = json_parser_new ();

  gst_init (NULL, NULL);

  if (!setup_gst (self))
    return;

  self->cancel = g_cancellable_new ();
  self->session = soup_session_new ();

  open_vosk_websocket (self);
}


PosSpeechInput *
pos_speech_input_new (void)
{
  return g_object_new (POS_TYPE_SPEECH_INPUT, NULL);
}


void
pos_speech_input_set_enabled (PosSpeechInput *self, gboolean enable)
{
  GstState state = GST_STATE_NULL;

  if (self->enabled == enable)
    return;

  self->enabled = enable;

  if (self->enabled)
    state = GST_STATE_PLAYING;

  g_debug ("Enabling speech input: %d\n", self->enabled);
  if (gst_element_set_state (self->pipeline, state) == GST_STATE_CHANGE_FAILURE)
    g_warning ("Failed to set audio pipeline to %s", gst_element_state_get_name (state));

  if (enable)
    open_vosk_websocket (self);
  else
    flush_vosk_websocket (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENABLED]);
}


/**
 * pos_speech_input_connect:
 * @self: The speech input
 *
 * Connect to the speech recognition server, Does nothing if already
 * connected.
 */
void
pos_speech_input_connect (PosSpeechInput *self)
{
  g_return_if_fail (POS_IS_SPEECH_INPUT (self));

  /* Setting up the audio pipeline failed */
  if (!self->session) {
    g_warning_once ("No audio pipeline, not connecting to speech input");
    return;
  }

  open_vosk_websocket (self);
}
