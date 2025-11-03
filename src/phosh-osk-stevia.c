/*
 * Copyright (C) 2018 Purism SPC
 *               2022-2024 The Phosh Developers
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-osk-stevia"

#include "pos-config.h"
#include "pos.h"

#include <gio/gio.h>
#include <glib-unix.h>

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include <libfeedback.h>

#define GNOME_SESSION_DBUS_NAME      "org.gnome.SessionManager"
#define GNOME_SESSION_DBUS_OBJECT    "/org/gnome/SessionManager"
#define GNOME_SESSION_DBUS_INTERFACE "org.gnome.SessionManager"
#define GNOME_SESSION_CLIENT_PRIVATE_DBUS_INTERFACE "org.gnome.SessionManager.ClientPrivate"

#define APP_ID "sm.puri.OSK0"

/**
 * PosDebugFlags:
 * @POS_DEBUG_FLAG_FORCE_SHOW: Ignore the `screen-keyboard-enabled` GSetting and always enable the OSK
 * @POS_DEBUG_FLAG_FORCE_COMPLETEION: Force text completion to on
 * @POS_DEBUG_FLAG_DEBUG_SURFACE: Enable the debug surface
 */
typedef enum _PosDebugFlags {
  POS_DEBUG_FLAG_NONE              = 0,
  POS_DEBUG_FLAG_FORCE_SHOW        = 1 << 0,
  POS_DEBUG_FLAG_FORCE_COMPLETEION = 1 << 1,
  POS_DEBUG_FLAG_DEBUG_SURFACE     = 1 << 2,
} PosDebugFlags;

typedef struct _PosApp {
  GObject              parent_instance;

  PosInputSurface     *input_surface;

  GMainLoop           *loop;
  GDBusProxy          *session_proxy;
  PosOskDbus          *osk_dbus;
  PosActivationFilter *activation_filter;
  PosHwTracker        *hw_tracker;
  PosEmojiDb          *emoji_db;
  PosSizeManager      *size_manager;
} PosApp;

G_DEFINE_TYPE (PosApp, pos_app, G_TYPE_OBJECT)


/* TODO: allow to force virtual-keyboard instead of input-method */
static PosDebugFlags _debug_flags;


static void G_GNUC_NORETURN
print_version (void)
{
  g_message ("Stevia %s\n", PHOSH_OSK_STEVIA_VERSION);
  exit (0);
}


static gboolean
quit_cb (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_info ("Caught signal, shutting down...");

  g_main_loop_quit (loop);
  return FALSE;
}


static void
respond_to_end_session (GDBusProxy *proxy)
{
  /* we must answer with "EndSessionResponse" */
  g_dbus_proxy_call (proxy, "EndSessionResponse",
                     g_variant_new ("(bs)", TRUE, ""),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1, NULL, NULL, NULL);
}


static void
client_proxy_signal_cb (GDBusProxy *proxy,
                        char       *sender_name,
                        char       *signal_name,
                        GVariant   *parameters,
                        gpointer    user_data)
{
  GMainLoop *loop = user_data;

  if (g_strcmp0 (signal_name, "QueryEndSession") == 0) {
    g_debug ("Got QueryEndSession signal");
    respond_to_end_session (proxy);
  } else if (g_strcmp0 (signal_name, "EndSession") == 0) {
    g_debug ("Got EndSession signal");
    respond_to_end_session (proxy);
  } else if (g_strcmp0 (signal_name, "Stop") == 0) {
    g_debug ("Got Stop signal");
    quit_cb (loop);
  }
}


static void
on_client_registered (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GMainLoop *loop = user_data;
  GDBusProxy *client_proxy;
  g_autoptr (GVariant) variant = NULL;
  g_autoptr (GError) err = NULL;
  g_autofree char *object_path = NULL;

  variant = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &err);
  if (!variant) {
    g_warning ("Unable to register client: %s", err->message);
    return;
  }

  g_variant_get (variant, "(o)", &object_path);

  g_debug ("Registered client at path %s", object_path);

  client_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION, 0, NULL,
                                                GNOME_SESSION_DBUS_NAME,
                                                object_path,
                                                GNOME_SESSION_CLIENT_PRIVATE_DBUS_INTERFACE,
                                                NULL,
                                                &err);
  if (!client_proxy) {
    g_warning ("Unable to get the session client proxy: %s", err->message);
    return;
  }

  g_signal_connect (client_proxy, "g-signal",
                    G_CALLBACK (client_proxy_signal_cb), loop);
}


static GDBusProxy *
pos_session_register (const char *client_id, GMainLoop *loop)
{
  GDBusProxy *proxy;
  const char *startup_id;
  g_autoptr (GError) err = NULL;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                         G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
                                         NULL,
                                         GNOME_SESSION_DBUS_NAME,
                                         GNOME_SESSION_DBUS_OBJECT,
                                         GNOME_SESSION_DBUS_INTERFACE,
                                         NULL,
                                         &err);
  if (proxy == NULL) {
    g_debug ("Failed to contact gnome-session: %s", err->message);
    return NULL;
  }

  startup_id = g_getenv ("DESKTOP_AUTOSTART_ID");
  g_dbus_proxy_call (proxy,
                     "RegisterClient",
                     g_variant_new ("(ss)", client_id, startup_id ? startup_id : ""),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     on_client_registered,
                     loop);
  g_unsetenv ("DESKTOP_AUTOSTART_ID");

  return proxy;
}


static gboolean
set_surface_prop_surface_visible (GBinding     *binding,
                                  const GValue *from_value,
                                  GValue       *to_value,
                                  gpointer      user_data)
{
  PosApp *self = POS_APP (user_data);
  gboolean enabled, visible = g_value_get_boolean (from_value);

  if (_debug_flags & POS_DEBUG_FLAG_FORCE_SHOW) {
    g_value_set_boolean (to_value, TRUE);
    return TRUE;
  }

  enabled = pos_input_surface_get_screen_keyboard_enabled (self->input_surface);

  if (self->activation_filter && !pos_activation_filter_allow_active (self->activation_filter))
    enabled = FALSE;

  if (self->hw_tracker && !pos_hw_tracker_get_allow_active (self->hw_tracker))
    enabled = FALSE;

  g_debug ("active: %d, enabled: %d", visible, enabled);
  if (enabled == FALSE)
    visible = FALSE;

  g_value_set_boolean (to_value, visible);

  return TRUE;
}


static void
on_screen_keyboard_enabled_changed (PosInputSurface *input_surface)
{
  gboolean enabled;

  if (pos_input_surface_get_visible (input_surface) == FALSE)
    return;

  enabled = pos_input_surface_get_screen_keyboard_enabled (input_surface);
  pos_input_surface_set_visible (input_surface, enabled);
}


static void
on_hw_tracker_allow_active_changed (PosHwTracker *hw_tracker, GParamSpec *pspec, PosInputMethod *im)
{
  /* Revalidate whether to show the OSK when attached hw changed */
  g_object_notify (G_OBJECT (im), "active");
}


static void on_input_surface_gone (gpointer data, GObject *unused);
static void on_has_dbus_name_changed (PosOskDbus *dbus, GParamSpec *pspec, gpointer unused);

static void
dispose_input_surface (PosApp *self)
{
  g_assert (POS_IS_APP (self));
  g_assert (POS_IS_INPUT_SURFACE (self->input_surface));

  /* Remove weak ref so input-surface doesn't get recreated */
  g_object_weak_unref (G_OBJECT (self->input_surface), on_input_surface_gone, self);
  gtk_widget_destroy (GTK_WIDGET (self->input_surface));
}



static void
create_input_surface (PosApp *self)
{
  g_autoptr (PosVirtualKeyboard) virtual_keyboard = NULL;
  g_autoptr (PosVkDriver) vk_driver = NULL;
  g_autoptr (PosInputMethod) im = NULL;
  g_autoptr (PosCompleterManager) completer_manager = NULL;
  g_autoptr (PosClipboardManager) clipboard_manager = NULL;
  PosWayland *wayland = pos_wayland_get_default ();
  gboolean force_completion;

  g_assert (POS_IS_APP (self));
  g_assert (POS_IS_OSK_DBUS (self->osk_dbus));

  virtual_keyboard =
    pos_virtual_keyboard_new (pos_wayland_get_zwp_virtual_keyboard_manager_v1 (wayland),
                              pos_wayland_get_wl_seat (wayland));
  vk_driver = pos_vk_driver_new (virtual_keyboard);
  completer_manager = pos_completer_manager_new ();
  clipboard_manager =
    pos_clipboard_manager_new (pos_wayland_get_zwlr_data_control_manager_v1 (wayland),
                               pos_wayland_get_wl_seat (wayland));

  im =
    pos_input_method_new (pos_wayland_get_zwp_input_method_manager_v2 (wayland),
                          pos_wayland_get_wl_seat (wayland));

  force_completion = !!(_debug_flags & POS_DEBUG_FLAG_FORCE_COMPLETEION);
  self->input_surface = g_object_new (POS_TYPE_INPUT_SURFACE,
                                      "min-height", POS_INPUT_SURFACE_DEFAULT_HEIGHT,
                                      /* layer-surface */
                                      "layer-shell", pos_wayland_get_zwlr_layer_shell_v1 (wayland),
                                      "height", POS_INPUT_SURFACE_DEFAULT_HEIGHT,
                                      "anchor", ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                      ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
                                      "layer", ZWLR_LAYER_SHELL_V1_LAYER_TOP,
                                      "kbd-interactivity", FALSE,
                                      "exclusive-zone", POS_INPUT_SURFACE_DEFAULT_HEIGHT,
                                      "namespace", "osk",
                                      /* pos-input-surface */
                                      "input-method", im,
                                      "keyboard-driver", vk_driver,
                                      "completer-manager", completer_manager,
                                      "completion-enabled", force_completion,
                                      "clipboard-manager", clipboard_manager,
                                      NULL);

  g_object_bind_property (self->input_surface,
                          "surface-visible",
                          self->osk_dbus,
                          "visible",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property_full (im, "active",
                               self->input_surface, "surface-visible",
                               G_BINDING_SYNC_CREATE,
                               set_surface_prop_surface_visible,
                               NULL,
                               self,
                               NULL);
  g_object_bind_property (self->size_manager,
                          "height",
                          self->input_surface,
                          "min-height",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (self->size_manager,
                          "dead-zone",
                          self->input_surface,
                          "dead-zone",
                          G_BINDING_SYNC_CREATE);

  g_signal_connect_object (self->hw_tracker, "notify::allow-active",
                           G_CALLBACK (on_hw_tracker_allow_active_changed),
                           im,
                           G_CONNECT_DEFAULT);

  if (_debug_flags & POS_DEBUG_FLAG_FORCE_SHOW) {
    pos_input_surface_set_visible (self->input_surface, TRUE);
  } else {
    g_signal_connect (self->input_surface, "notify::screen-keyboard-enabled",
                      G_CALLBACK (on_screen_keyboard_enabled_changed), NULL);
  }

  if (_debug_flags & POS_DEBUG_FLAG_DEBUG_SURFACE)
    pos_input_surface_set_layout_swipe (self->input_surface, TRUE);

  gtk_window_present (GTK_WINDOW (self->input_surface));

  g_object_weak_ref (G_OBJECT (self->input_surface), on_input_surface_gone, self);
}


static void
maybe_create_input_surface (PosApp *self)
{
  if (self->input_surface)
    return;

  if (!pos_wayland_has_wl_protcols (pos_wayland_get_default ())) {
    g_debug ("Wayland not yet ready, skipping input surface creation");
    return;
  }

  if (!self->osk_dbus) {
    g_debug ("Don't have DBus name yet, skipping input surface creation");
    return;
  }

  g_debug ("Creating new input surface");
  create_input_surface (self);
}


static void
on_input_surface_gone (gpointer data, GObject *unused)
{
  PosApp *self = POS_APP (data);

  g_assert (POS_IS_APP (self));

  g_debug ("Input surface gone, recreating");
  maybe_create_input_surface (self);
}


static void
on_has_dbus_name_changed (PosOskDbus *dbus, GParamSpec *pspec, gpointer data)
{
  PosApp *self = POS_APP (data);
  gboolean has_name;

  has_name = pos_osk_dbus_has_name (dbus);
  g_debug ("Has DBus name: %d", has_name);

  if (has_name == FALSE) {
    dispose_input_surface (self);
    self->input_surface = NULL;
  } else {
    maybe_create_input_surface (self);
  }
}


static void
on_wayland_ready (PosApp *self, PosWayland *wayland)
{
  g_assert (POS_IS_APP (self));
  g_assert (POS_IS_WAYLAND (wayland));
  g_assert (pos_wayland_has_wl_protcols (wayland));

  self->activation_filter =
    pos_activation_filter_new (pos_wayland_get_zwlr_foreign_toplevel_manager_v1 (wayland));
  self->hw_tracker =
    pos_hw_tracker_new (pos_wayland_get_zphoc_device_state_v1 (wayland));

  maybe_create_input_surface (self);
}


/* TODO: this could happen in constructed */
static gboolean
pos_app_setup_input_method (PosApp *self, PosOskDbus *osk_dbus)
{
  g_assert (POS_IS_APP (self));
  g_assert (POS_IS_OSK_DBUS (osk_dbus));

  self->osk_dbus = g_object_ref (osk_dbus);
  g_signal_connect (osk_dbus, "notify::has-name", G_CALLBACK (on_has_dbus_name_changed), self);

  return TRUE;
}


static GDebugKey debug_keys[] =
{
  { .key = "force-show",
    .value = POS_DEBUG_FLAG_FORCE_SHOW,},
  { .key = "force-completion",
    .value = POS_DEBUG_FLAG_FORCE_COMPLETEION,},
  { .key = "debug-surface",
    .value = POS_DEBUG_FLAG_DEBUG_SURFACE,},
};

static PosDebugFlags
parse_debug_env (void)
{
  const char *debugenv;
  PosDebugFlags flags = POS_DEBUG_FLAG_NONE;

  debugenv = g_getenv ("POS_DEBUG");
  if (!debugenv)
    return flags;

  return g_parse_debug_string (debugenv, debug_keys, G_N_ELEMENTS (debug_keys));
}


static void
pos_input_surface_finalize (GObject *object)
{
  PosApp *self = POS_APP (object);

  g_clear_object (&self->osk_dbus);
  g_clear_object (&self->activation_filter);
  g_clear_object (&self->hw_tracker);

  g_clear_object (&self->session_proxy);

  if (self->input_surface)
    dispose_input_surface (self);

  g_clear_object (&self->emoji_db);
  g_clear_object (&self->size_manager);

  G_OBJECT_CLASS (pos_app_parent_class)->finalize (object);
}


static void
pos_app_class_init (PosAppClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = pos_input_surface_finalize;
}


void
pos_app_init (PosApp *self)
{
  PosWayland *wayland = pos_wayland_get_default ();

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (), "/mobi/phosh/stevia/icons");

  self->loop = g_main_loop_new (NULL, FALSE);
  self->emoji_db = pos_emoji_db_get_default ();

  g_unix_signal_add (SIGTERM, quit_cb, self->loop);
  g_unix_signal_add (SIGINT, quit_cb, self->loop);

  self->session_proxy = pos_session_register (APP_ID, self->loop);
  self->size_manager = pos_size_manager_new ();

  g_signal_connect_object (wayland,
                           "ready",
                           G_CALLBACK (on_wayland_ready),
                           self,
                           G_CONNECT_SWAPPED);
  if (pos_wayland_has_wl_protcols (wayland))
    on_wayland_ready (self, wayland);
}


static void
pos_app_run (PosApp *self)
{
  g_main_loop_run (self->loop);
}


int
main (int argc, char *argv[])
{
  g_autoptr (GOptionContext) opt_context = NULL;
  g_autoptr (GError) err = NULL;
  PosApp *stevia;
  g_autoptr (PosOskDbus) osk_dbus = NULL;
  gboolean version = FALSE, replace = FALSE, allow_replace = FALSE;
  GBusNameOwnerFlags flags;
  g_autoptr (PosWayland) wayland = NULL;

  const GOptionEntry options [] = {
    {"replace", 0, 0, G_OPTION_ARG_NONE, &replace,
     "Replace DBus service", NULL},
    {"allow-replacement", 0, 0, G_OPTION_ARG_NONE, &allow_replace,
     "Allow replacement of DBus service", NULL},
    {"version", 0, 0, G_OPTION_ARG_NONE, &version,
     "Show version information", NULL},
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
  };

  opt_context = g_option_context_new ("- A OSK for phosh");
  g_option_context_add_main_entries (opt_context, options, NULL);
  if (!g_option_context_parse (opt_context, &argc, &argv, &err)) {
    g_warning ("%s", err->message);
    return EXIT_FAILURE;
  }

  if (version)
    print_version ();

  gdk_set_allowed_backends ("wayland");
  pos_init ();
  lfb_init (APP_ID, NULL);
  _debug_flags = parse_debug_env ();
  gtk_init (&argc, &argv);

  wayland = pos_wayland_get_default ();
  stevia = g_object_new (POS_TYPE_APP, NULL);

  flags = (allow_replace ? G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT : 0) |
    (replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0);
  osk_dbus = pos_osk_dbus_new (flags);

  if (!pos_app_setup_input_method (stevia, osk_dbus))
    return EXIT_FAILURE;

  pos_app_run (stevia);

  g_clear_object (&stevia);
  pos_uninit ();

  return EXIT_SUCCESS;
}
