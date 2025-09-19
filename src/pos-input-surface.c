/*
 * Copyright (C) 2021 Purism SPC
 *               2022-2024 The Phosh Developers
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-input-surface"

#include "pos-config.h"

#include "phosh-osk-enums.h"
#include "pos-emoji-picker.h"
#include "pos-input-method.h"
#include "pos-clipboard-manager.h"
#include "pos-completer.h"
#include "pos-completer-manager.h"
#include "pos-completion-bar.h"
#include "pos-input-surface.h"
#include "pos-keypad.h"
#include "pos-logind-session.h"
#include "pos-main.h"
#include "pos-osk-widget.h"
#include "pos-settings-panel.h"
#include "pos-shortcuts-bar.h"
#include "pos-style-manager.h"
#include "pos-vk-driver.h"
#include "pos-vk-driver.h"
#include "util.h"

#include <gmobile.h>

#include <handy.h>
#include <libfeedback.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-xkb-info.h>

#include <glib/gi18n-lib.h>

#define KEY_PRESS_EVENT "key-pressed"
#define BUTTON_PRESS_EVENT "button-pressed"

#define MIN_Y_VELOCITY 1000

#define BS_KEY_REPEAT_DELAY 700
#define BS_KEY_REPEAT_INTERVAL_CHAR 50

/**
 * POS_INPUT_SURFACE_IS_LANG_LAYOUT:
 * @layout: The layout to check
 *
 * Is this widget a "regular" language layout (not emoji, not terminal, …)?
 */
#define POS_INPUT_SURFACE_IS_LANG_LAYOUT(widget) \
  (POS_IS_OSK_WIDGET ((widget)) && GTK_WIDGET ((widget)) != self->osk_terminal)

/**
 * POS_INPUT_SURFACE_IS_TERMINAL_LAYOUT:
 * @layout: The layout to check
 *
 * Is this widget a terminal layout?
 */
#define POS_INPUT_SURFACE_IS_TERMINAL_LAYOUT(widget) \
  (POS_IS_OSK_WIDGET ((widget)) && GTK_WIDGET ((widget)) == self->osk_terminal)

enum {
  PROP_0,
  PROP_INPUT_METHOD,
  PROP_COMPLETER,
  PROP_COMPLETER_MANAGER,
  PROP_CLIPBOARD_MANAGER,
  PROP_SCREEN_KEYBOARD_ENABLED,
  PROP_KEYBOARD_DRIVER,
  PROP_SURFACE_VISIBLE,
  PROP_COMPLETER_ACTIVE,
  PROP_COMPLETION_ENABLED,
  PROP_OSK_FEATURES,
  PROP_MIN_HEIGHT,
  PROP_DEAD_ZONE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct {
  gboolean show;
  double   progress;
  gint64   last_frame;
  guint    id;
} PosInputSurfaceAnimation;

/**
 * PosInputSurface:
 *
 * Main surface that has all the widgets. Should not bother
 * how the OSK is driven.
 *
 * As toplevel widget it also implements #GActionMap so one
 * can easily add and remove actions.
 */
struct _PosInputSurface {
  PhoshLayerSurface        parent;

  gboolean                 surface_visible;
  PosInputSurfaceAnimation animation;
  guint                    min_height;
  guint                    dead_zone;

  /* GNOME settings */
  gboolean                 screen_keyboard_enabled;
  GSettings               *a11y_settings;
  GSettings               *input_settings;
  GSettings               *osk_settings;
  GnomeXkbInfo            *xkbinfo;

  PosLogindSession        *logind_session;

  /* Wayland input-method */
  PosInputMethod          *input_method;

  /* OSK */
  GHashTable              *osks;
  HdyDeck                 *deck;
  GtkWidget               *osk_terminal;
  GtkWidget               *emoji_picker;
  GtkWidget               *last_layout;
  GtkWidget               *keypad;
  PosShortcutsBar         *shortcuts_bar;
  PhoshOskFeatures         osk_features;
  GdkModifierType          latched_modifiers;

  /* TODO: this should be an interface for different keyboard drivers */
  PosVkDriver             *keyboard_driver;

  PosStyleManager         *style_manager;

  /* menu popover */
  GtkBox                  *menu_box_layouts;
  GtkPopover              *menu_popup;
  GSimpleActionGroup      *action_map;

  /* word completion */
  GtkWidget               *word_completion_btn;
  PosCompleter            *completer;
  PosCompleterManager     *completer_manager;
  GtkWidget               *completion_bar;
  gboolean                 completion_enabled;
  PhoshOskCompletionModeFlags completion_mode;

  /* Clipboard */
  PosClipboardManager    *clipboard_manager;

  /* Swipe gesture */
  GtkGesture              *swipe_down;

  /* emission hook for clicks */
  gulong                   clicked_id;

  /* Backspace handling */
  guint                    bs_repeat_id;
};


static void pos_input_surface_submit_symbol (PosInputSurface *self, const char *symbol);

static void pos_input_surface_action_group_iface_init (GActionGroupInterface *iface);
static void pos_input_surface_action_map_iface_init (GActionMapInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PosInputSurface, pos_input_surface, PHOSH_TYPE_LAYER_SURFACE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_GROUP, pos_input_surface_action_group_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_MAP, pos_input_surface_action_map_iface_init)
  )


static void
pos_input_surface_trigger_feedback (PosInputSurface *self, const char *event_name)
{
  g_autoptr (LfbEvent) event = NULL;

  g_assert (POS_IS_INPUT_SURFACE (self));

  event = lfb_event_new (event_name);
  lfb_event_set_important (event, TRUE);
  lfb_event_trigger_feedback_async (event, NULL, NULL, NULL);
}


static gboolean
on_bs_key_repeat (gpointer data)
{
  PosInputSurface *self = data;

  pos_input_surface_trigger_feedback (self, KEY_PRESS_EVENT);
  pos_input_surface_submit_symbol (self, "KEY_BACKSPACE");

  return G_SOURCE_CONTINUE;
}


static void
on_bs_long_press_timeout (gpointer data)
{
  PosInputSurface *self = data;
  guint interval;

  interval = BS_KEY_REPEAT_INTERVAL_CHAR;

  self->bs_repeat_id = g_timeout_add (interval, on_bs_key_repeat, self);
  g_source_set_name_by_id (self->bs_repeat_id, "[pos-bs-key-repeat]");
 }


static gboolean
pos_input_surface_set_backspace_pressed (PosInputSurface *self, const char *symbol)
{
  gboolean pressed;

  pressed = symbol && g_str_equal (symbol, "KEY_BACKSPACE");
  if (!pressed) {
    g_clear_handle_id (&self->bs_repeat_id, g_source_remove);
    return FALSE;
  }

  if (!self->bs_repeat_id) {
    self->bs_repeat_id = g_timeout_add_once (BS_KEY_REPEAT_DELAY,
                                             on_bs_long_press_timeout,
                                             self);
    g_source_set_name_by_id (self->bs_repeat_id, "[pos-bs-long-press-timeout]");
  }

  return TRUE;
}


static void
pos_input_surface_handle_backsapce (PosInputSurface *self)
{
  pos_vk_driver_key_down (self->keyboard_driver, "KEY_BACKSPACE", POS_KEYCODE_MODIFIER_NONE);
  pos_vk_driver_key_up (self->keyboard_driver, "KEY_BACKSPACE");
}


static void
on_swipe (GtkGestureSwipe *swipe, double velocity_x, double velocity_y, gpointer data)
{
  PosInputSurface *self = POS_INPUT_SURFACE (data);

  g_return_if_fail (GTK_IS_GESTURE_SWIPE (swipe));

  g_debug ("swipe with v_x: %f, v_y: %f", velocity_x, velocity_y);

  if (velocity_y > MIN_Y_VELOCITY && velocity_y > 2.0 * ABS (velocity_x)) {
    g_debug ("Hiding the keyboard on swipe down");
    pos_input_surface_set_visible (self, FALSE);
  } else {
    g_debug ("Swipe not downwards");
  }
}


static void
pos_input_surface_unlatch_modifiers (PosInputSurface *self)
{
  pos_shortcuts_bar_unlatch_modifiers (self->shortcuts_bar);
}


static void
on_shortcut_activated (PosInputSurface *self, PosShortcut *shortcut, PosShortcutsBar *bar)
{
  g_return_if_fail (POS_IS_INPUT_SURFACE (self));
  g_return_if_fail (POS_IS_SHORTCUTS_BAR (bar));

  pos_vk_driver_key_press_gdk (self->keyboard_driver,
                               pos_shortcut_get_key (shortcut),
                               pos_shortcut_get_modifiers (shortcut) | self->latched_modifiers);
  pos_input_surface_unlatch_modifiers (self);
}


static void
on_latched_modifiers_changed (PosInputSurface *self, GParamSpec *pspec, PosShortcutsBar *bar)
{
  g_return_if_fail (POS_IS_INPUT_SURFACE (self));
  g_return_if_fail (POS_IS_SHORTCUTS_BAR (bar));

  self->latched_modifiers = pos_shortcuts_bar_get_latched_modifiers (bar);

  g_debug ("Modifiers: 0x%x", self->latched_modifiers);
}


static void
pos_input_surface_toggle_shortcuts_bar (PosInputSurface *self)
{
  GtkWidget *child;
  gboolean shortcuts_visible = FALSE;

  child = hdy_deck_get_visible_child (self->deck);

  /* shortcuts bar is only for terminal and when we have shortcuts defined */
  if (POS_INPUT_SURFACE_IS_TERMINAL_LAYOUT (child))
    shortcuts_visible = !!pos_shortcuts_bar_get_num_shortcuts (self->shortcuts_bar);

  gtk_widget_set_visible (GTK_WIDGET (self->shortcuts_bar), shortcuts_visible);
}


static void
on_num_shortcuts_changed (PosInputSurface *self)
{
  pos_input_surface_toggle_shortcuts_bar (self);
}


static gboolean
on_click_hook (GSignalInvocationHint *ihint,
               guint                  n_param_values,
               const GValue          *param_values,
               gpointer               user_data)
{
  PosInputSurface *self = POS_INPUT_SURFACE (user_data);

  pos_input_surface_trigger_feedback (self, BUTTON_PRESS_EVENT);
  return TRUE;
}


/* This is a bit more strict than is_completer_active so it can be used
   with active completer but also takes the OSK's mode into account */
static gboolean
pos_input_surface_is_completion_mode (PosInputSurface *self)
{
  GtkWidget *osk_widget;

  if (pos_input_surface_is_completer_active (self) == FALSE)
    return FALSE;

  /* no completion in cursor mode */
  osk_widget = hdy_deck_get_visible_child (self->deck);
  if (POS_IS_OSK_WIDGET (osk_widget) == FALSE)
    return FALSE;

  return pos_osk_widget_get_mode (POS_OSK_WIDGET (osk_widget)) == POS_OSK_WIDGET_MODE_KEYBOARD;
}


static void
on_completion_selected (PosInputSurface *self, const char *completion)
{
  g_autofree char *send = NULL;

  g_return_if_fail (POS_IS_INPUT_SURFACE (self));
  g_return_if_fail (completion != NULL);

  g_debug ("completion: %s", completion);
  send = g_strdup_printf ("%s ", completion);

  pos_input_method_send_string (self->input_method, send, TRUE);

  if (pos_input_surface_is_completer_active (self)) {
    pos_completer_learn_accepted (self->completer, send);
    pos_completer_set_preedit (self->completer, NULL);
  }

}


static void
on_completer_preedit_changed (PosInputSurface *self)
{
  const char *preedit = NULL;
  int pos;

  /* Only update preedit when in cursor mode as this updates preedit too */
  if (pos_input_surface_is_completion_mode (self) == FALSE)
    return;

  preedit = pos_completer_get_preedit (self->completer);
  pos = preedit ? strlen (preedit) : 0;

  pos_input_method_send_preedit (self->input_method, preedit, pos, pos, TRUE);
}


static void
on_completer_completions_changed (PosInputSurface *self)
{
  g_auto (GStrv) completions = pos_completer_get_completions (self->completer);

  pos_completion_bar_set_completions (POS_COMPLETION_BAR (self->completion_bar),
                                      completions);
}


static void
on_completer_commit_string (PosInputSurface *self, const char *text)
{
  g_debug ("%s: %s", __func__, text);
  pos_input_method_send_string (self->input_method, text, TRUE);
}


static void
on_completer_update (PosInputSurface *self, const char *preedit, guint before, guint after)
{
  guint pos = strlen (preedit);

  /* In cursor mode we reset preedit, make sure to break the cycle */
  if (pos_input_surface_is_completion_mode (self) == FALSE)
    return;

  pos_input_method_delete_surrounding_text (self->input_method, before, after, FALSE);
  pos_input_method_send_preedit (self->input_method, preedit, pos, pos, TRUE);
}


static void
pos_input_surface_submit_current_preedit (PosInputSurface *self)
{
  g_autofree char *preedit = NULL;

  if (pos_input_surface_is_completer_active (self) == FALSE)
    return;

  preedit = g_strdup (pos_completer_get_preedit (self->completer));
  if (gm_str_is_null_or_empty (preedit))
    return;

  g_debug ("%s: Submitting %s", __func__, preedit);
  pos_completer_set_preedit (self->completer, NULL);
  pos_input_method_send_preedit (self->input_method, "", 0, 0, FALSE);
  pos_input_method_send_string (self->input_method, preedit, TRUE);
}


static void
on_osk_key_down (PosInputSurface *self, const char *symbol, GtkWidget *osk_widget)
{
  g_return_if_fail (POS_IS_INPUT_SURFACE (self));
  g_return_if_fail (POS_IS_OSK_WIDGET (osk_widget));

  pos_input_surface_trigger_feedback (self, KEY_PRESS_EVENT);

  pos_input_surface_set_backspace_pressed (self, symbol);
}


static void
on_osk_key_up (PosInputSurface *self)
{
  g_assert (POS_IS_INPUT_SURFACE (self));

  pos_input_surface_set_backspace_pressed (self, NULL);
}


static void
on_osk_key_cancelled (PosInputSurface *self)
{
  g_assert (POS_IS_INPUT_SURFACE (self));

  pos_input_surface_set_backspace_pressed (self, NULL);
}


static void
on_osk_key_symbol (PosInputSurface *self, const char *symbol)
{
  g_assert (POS_IS_INPUT_SURFACE (self));

  pos_input_surface_submit_symbol (self, symbol);
}


static void
pos_input_surface_submit_symbol (PosInputSurface *self, const char *symbol)
{
  gboolean handled, is_bs;

  g_debug ("Key: '%s' symbol", symbol);

  is_bs = pos_input_surface_set_backspace_pressed (self, symbol);

  /* Latched modifiers, send as virtual-keyboard */
  if (self->latched_modifiers) {
    PosKeycodeModifier modifier;

    modifier = pos_vk_driver_convert_modifiers (self->keyboard_driver, self->latched_modifiers);
    pos_vk_driver_key_down (self->keyboard_driver, symbol, modifier);
    pos_vk_driver_key_up (self->keyboard_driver, symbol);
    pos_input_surface_unlatch_modifiers (self);
    return;
  }
  /* virtual-keyboard, no input method */
  if (!pos_input_method_get_active (self->input_method)) {
    pos_vk_driver_key_down (self->keyboard_driver, symbol, POS_KEYCODE_MODIFIER_NONE);
    pos_vk_driver_key_up (self->keyboard_driver, symbol);
    return;
  }

  if (pos_input_surface_is_completion_mode (self)) {
    handled = pos_completer_feed_symbol (self->completer, symbol);
    if (handled)
      return;
  }

  if (is_bs) {
    pos_input_surface_handle_backsapce (self);
  } else if (g_str_has_prefix (symbol, "KEY_")) {
    pos_vk_driver_key_down (self->keyboard_driver, symbol, POS_KEYCODE_MODIFIER_NONE);
    pos_vk_driver_key_up (self->keyboard_driver, symbol);
  } else {
    pos_input_method_send_string (self->input_method, symbol, TRUE);
  }

  if (pos_input_surface_is_completer_active (self))
    pos_completer_set_preedit (self->completer, NULL);
}


static void
set_keymap (PosInputSurface *self)
{
  GtkWidget *child;
  PosOskWidget *osk;

  /* If the object isn't fully constructed */
  if (self->keyboard_driver == NULL)
    return;

  child = hdy_deck_get_visible_child (self->deck);
  /* Do nothing on e.g. debug surface */
  if (!POS_IS_OSK_WIDGET (child))
    return;

  osk = POS_OSK_WIDGET (child);
  if (osk == POS_OSK_WIDGET (self->osk_terminal)) {
    pos_vk_driver_set_terminal_keymap (self->keyboard_driver);
  } else {
    pos_vk_driver_set_keymap_symbols (self->keyboard_driver,
                                      pos_osk_widget_get_layout_id (osk),
                                      pos_osk_widget_get_symbols (osk));
  }
}


static void
set_keymap_delayed (PosInputSurface *self)
{
  /*
   * Add a slight delay before switching back the keymap. Otherwise an
   * X11 client might apply the symbol sent to the popup to the new
   * keymap.
   */
  g_timeout_add_once (25, (GSourceOnceFunc)set_keymap, self);
}


static void
on_osk_mode_changed (PosInputSurface *self, GParamSpec *pspec, GtkWidget *osk_widget)
{
  g_return_if_fail (POS_IS_INPUT_SURFACE (self));
  g_return_if_fail (POS_IS_OSK_WIDGET (osk_widget));

  /* We only want to clear preedit when entering cursor mode */
  if (pos_input_surface_is_completion_mode (self) == TRUE)
    return;

  pos_input_surface_submit_current_preedit (self);
}


static void
on_osk_popover_shown (PosInputSurface *self, GStrv *symbols, GtkWidget *osk_widget)
{
  g_return_if_fail (POS_IS_INPUT_SURFACE (self));
  g_return_if_fail (POS_IS_OSK_WIDGET (osk_widget));

  pos_vk_driver_set_overlay_keymap (self->keyboard_driver, (const char * const *)symbols);
}


static void
on_osk_popover_hidden (PosInputSurface *self)
{
  g_return_if_fail (POS_IS_INPUT_SURFACE (self));

  set_keymap_delayed (self);
}


static void
clipboard_paste_activated (GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       data)
{
  PosInputSurface *self = POS_INPUT_SURFACE (data);
  const char *text;

  text = pos_clipboard_manager_get_text (self->clipboard_manager);
  if (gm_str_is_null_or_empty (text))
    return;

  if (pos_input_method_get_active (self->input_method)) {
    pos_input_surface_submit_current_preedit (self);
    pos_input_method_send_string (self->input_method, text, TRUE);
  } else {
    /* TODO */
    g_warning_once ("Pasting via vk-driver not yet supported");
  }

  /* Close popover in case we pasted from there */
  gtk_popover_popdown (self->menu_popup);
}


static void
settings_activated (GSimpleAction *action, GVariant *parameter, gpointer data)
{
  PosInputSurface *self = POS_INPUT_SURFACE (data);

  /* popdown popover right away to avoid flicker when OSK goes away */
  gtk_popover_popdown (self->menu_popup);
  pos_open_settings_panel ("osk");
}


/* Emoji picker */

static void
send_emoji_via_vk (PosInputSurface *self, const char *emoji)
{
  g_autoptr (GPtrArray) syms_array = g_ptr_array_new_with_free_func (g_free);
  g_autofree gunichar *items = NULL;

  /* To send an emoji via the vk driver we split it into unicode symbols… */
  items = g_utf8_to_ucs4_fast (emoji, -1, NULL);
  for (int i = 0; items[i]; i++) {
    char *symbol = g_new0 (char, 7);

    g_unichar_to_utf8 (items[i], symbol);
    /* TODO: Can use g_strv_builder_take with glib 2.80 */
    g_ptr_array_add (syms_array, symbol);
  }
  g_ptr_array_add (syms_array, NULL);

  /* …add a keymap that contains the emoji, combining characters and emoji modifiers */
  pos_vk_driver_set_overlay_keymap (self->keyboard_driver, (const char * const*)syms_array->pdata);

  /* … and type each of these symbols one by one */
  for (int i = 0; syms_array->pdata[i]; i++) {
    const char *symbol = syms_array->pdata[i];

    pos_vk_driver_key_down (self->keyboard_driver, symbol, POS_KEYCODE_MODIFIER_NONE);
    pos_vk_driver_key_up (self->keyboard_driver, symbol);
  }

  set_keymap_delayed (self);
}


static void
on_emoji_picked (PosInputSurface *self, const char *emoji, PosEmojiPicker *emoji_picker)
{
  g_assert (POS_IS_INPUT_SURFACE (self));
  g_assert (POS_IS_EMOJI_PICKER (emoji_picker));

  if (pos_input_method_get_active (self->input_method)) {
    pos_input_surface_submit_current_preedit (self);
    pos_input_method_send_string (self->input_method, emoji, TRUE);
  } else {
    send_emoji_via_vk (self, emoji);
  }

  pos_input_surface_trigger_feedback (self, KEY_PRESS_EVENT);
}


static void
on_emoji_picker_done (PosInputSurface *self)
{
  hdy_deck_set_visible_child (self->deck, self->last_layout);
}


static void
on_emoji_picker_delete_last (PosInputSurface *self)
{
  g_debug ("Deleting last emoji");
  pos_input_surface_submit_symbol (self, "KEY_BACKSPACE");
}

/* Keypads */

static void
on_keypad_symbol_pressed (PosInputSurface *self, const char *symbol, PosKeypad *keypad)
{
  g_assert (POS_IS_INPUT_SURFACE (self));
  g_assert (POS_IS_KEYPAD (keypad));

  if (pos_input_method_get_active (self->input_method)) {
    pos_input_surface_submit_current_preedit (self);
    pos_input_method_send_string (self->input_method, symbol, TRUE);
  } else {
    pos_vk_driver_key_down (self->keyboard_driver, symbol, POS_KEYCODE_MODIFIER_NONE);
    pos_vk_driver_key_up (self->keyboard_driver, symbol);
  }

  pos_input_surface_trigger_feedback (self, KEY_PRESS_EVENT);
}


static void
on_keypad_done (PosInputSurface *self)
{
  hdy_deck_set_visible_child (self->deck, self->last_layout);
}


/* menu button */


static const char *
pos_osk_get_display_name (PosOskWidget *osk_widget)
{
  PosCompletionInfo *info = g_object_get_data (G_OBJECT (osk_widget), "pos-completion-info");

  if (info)
    return info->display_name;

  return pos_osk_widget_get_display_name (osk_widget);
}


static void
menu_add_layout (gpointer key, gpointer value, gpointer data)
{
  GtkWidget *button;
  const char *name = key;
  PosOskWidget *osk_widget = POS_OSK_WIDGET (value);
  PosInputSurface *self = POS_INPUT_SURFACE (data);
  const char *display_name = pos_osk_get_display_name (osk_widget);

  button = g_object_new (GTK_TYPE_MODEL_BUTTON,
                         "visible", TRUE,
                         "label", display_name,
                         "action-name", "win.select-layout",
                         "action-target", g_variant_new_string (name),
                         NULL);
  gtk_box_pack_start (self->menu_box_layouts, button, FALSE, FALSE, 0);
}


static void
menu_add_emoji_picker (PosInputSurface *self)
{
  GtkWidget *button;

  button = g_object_new (GTK_TYPE_MODEL_BUTTON,
                         "visible", TRUE,
                         "label", _("Emoji"),
                         "action-name", "win.select-layout",
                         "action-target", g_variant_new_string ("emoji"),
                         NULL);
  gtk_box_pack_start (self->menu_box_layouts, button, FALSE, FALSE, 0);
}


static void
menu_activated (GSimpleAction *action, GVariant *parameter, gpointer data)
{
  PosInputSurface *self = POS_INPUT_SURFACE (data);
  GdkRectangle rect = {};
  GtkWidget *osk_widget;
  GAction *layout_action;
  const char *osk_name;

  osk_widget = hdy_deck_get_visible_child (self->deck);
  osk_name = pos_osk_widget_get_name (POS_OSK_WIDGET (osk_widget));
  g_variant_get (parameter, "(ii)", &rect.x, &rect.y);
  g_debug ("Menu popup activated at %d %d, current: '%s'", rect.x, rect.y, osk_name);

  layout_action = g_action_map_lookup_action (G_ACTION_MAP (self->action_map), "select-layout");
  g_simple_action_set_state (G_SIMPLE_ACTION (layout_action), g_variant_new ("s", osk_name));

  gtk_container_foreach (GTK_CONTAINER (self->menu_box_layouts),
                         (GtkCallback) gtk_widget_destroy,
                         NULL);

  g_hash_table_foreach (self->osks, menu_add_layout, self);
  menu_add_layout ((gpointer)pos_osk_widget_get_name (POS_OSK_WIDGET (self->osk_terminal)),
                   self->osk_terminal,
                   self);
  menu_add_emoji_picker (self);

  gtk_widget_set_visible (self->word_completion_btn,
                          self->completer &&
                          !(self->completion_mode == PHOSH_OSK_COMPLETION_MODE_NONE));

  gtk_popover_set_relative_to (self->menu_popup, osk_widget);
  gtk_popover_set_pointing_to (self->menu_popup, &rect);

  gtk_popover_popup (self->menu_popup);
}


static void
select_layout_change_state (GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       data)
{
  PosInputSurface *self = POS_INPUT_SURFACE (data);
  const char *layout = NULL;
  GtkWidget *osk_widget;

  /* popdown popover right away to avoid flicker when switching layouts */
  gtk_popover_popdown (self->menu_popup);

  /* reset all letched modifiers */
  pos_input_surface_unlatch_modifiers (self);

  g_variant_get (parameter, "&s", &layout);
  g_debug ("Layout '%s' selected", layout);

  osk_widget = g_hash_table_lookup (self->osks, layout);
  if (osk_widget == NULL) {
    if (g_str_equal (layout, "terminal")) {
      osk_widget = self->osk_terminal;
      pos_input_surface_submit_current_preedit (self);
    } else if (g_str_equal (layout, "emoji")) {
      osk_widget = self->emoji_picker;
      pos_input_surface_submit_current_preedit (self);
    } else {
      g_warning ("Failed to find layout '%s'", layout);
      return;
    }
  }

  hdy_deck_set_visible_child (self->deck, GTK_WIDGET (osk_widget));
  g_simple_action_set_state (action, parameter);
}


static void pos_input_surface_set_completer (PosInputSurface *self, PosCompleter *completer);

/* Switch the completion engine and it's configuration */
static void
pos_input_surface_switch_completion (PosInputSurface *self, PosOskWidget *osk)
{
  PosCompletionInfo *info;
  gboolean success;
  PosCompleter *default_completer;
  g_autoptr (GError) err = NULL;

  default_completer = pos_completer_manager_get_default_completer (self->completer_manager);

  info = g_object_get_data (G_OBJECT (osk), "pos-completion-info");
  if (info) {
    /* Layout with completion info */
    pos_input_surface_set_completer (self, info->completer);
    success = pos_completer_set_language (self->completer, info->lang, info->region, &err);
    if (!success)
      g_warning ("Failed to switch completer: %s", err->message);
  } else if (default_completer) {
    /* Layout without completion info - use default completer */
    const char *lang = pos_osk_widget_get_lang (osk);
    const char *region = pos_osk_widget_get_region (osk);

    pos_input_surface_set_completer (self, default_completer);
    success = pos_completer_set_language (self->completer, lang, region, &err);
    if (!success) {
      g_warning ("Failed to set completion language: %s-%s: %s, switching to '%s-%s' instead",
                 lang, region, err->message, POS_COMPLETER_DEFAULT_LANG,
                 POS_COMPLETER_DEFAULT_REGION);
      g_clear_error (&err);
      if (!pos_completer_set_language (self->completer,
                                       POS_COMPLETER_DEFAULT_LANG,
                                       POS_COMPLETER_DEFAULT_REGION,
                                       &err)) {
        g_warning ("Failed to set completion language '%s-%s': %s",
                   POS_COMPLETER_DEFAULT_LANG, POS_COMPLETER_DEFAULT_REGION, err->message);
      }
    }
  }

  pos_completion_bar_set_completions (POS_COMPLETION_BAR (self->completion_bar), NULL);
}


static void
on_visible_child_changed (PosInputSurface *self)
{
  GtkWidget *child;
  PosOskWidget *osk;

  child = hdy_deck_get_visible_child (self->deck);

  pos_input_surface_toggle_shortcuts_bar (self);

  if (!POS_IS_OSK_WIDGET (child))
    return;

  osk = POS_OSK_WIDGET (child);
  g_debug ("Switched to layout '%s'", pos_osk_widget_get_display_name (osk));
  pos_osk_widget_set_layer (osk, POS_OSK_WIDGET_LAYER_NORMAL);

  set_keymap (self);

  /* Remember last layout */
  if (POS_INPUT_SURFACE_IS_LANG_LAYOUT (osk)) {
    pos_input_surface_switch_completion (self, osk);
    self->last_layout = GTK_WIDGET (osk);
  }

  /* Recheck completion bar visibility */
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETER_ACTIVE]);
}

static void
pos_screen_keyboard_set_enabled (PosInputSurface *self, gboolean enable)
{
  const char *msg = enable ? "enabled" : "disabled";

  g_debug ("Screen keyboard enable: %s", msg);

  if (enable == self->screen_keyboard_enabled)
    return;

  self->screen_keyboard_enabled = enable;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SCREEN_KEYBOARD_ENABLED]);
}


static void
pos_input_surface_move (PosInputSurface *self)
{
  int margin;
  int height;
  double progress = hdy_ease_out_cubic (self->animation.progress);

  if (self->animation.show)
    progress = 1.0 - progress;

  g_object_get (self, "configured-height", &height, NULL);
  margin = -height * progress;

  phosh_layer_surface_set_margins (PHOSH_LAYER_SURFACE (self), 0, 0, margin, 0);

  if (self->animation.progress >= 1.0 &&  self->animation.show) {
    /* On unfold adjust the exclusive zone at the very end to avoid flickering */
    phosh_layer_surface_set_exclusive_zone (PHOSH_LAYER_SURFACE (self), height);
  } else if (self->animation.progress < 1.0 && !self->animation.show) {
    /* On fold adjust the exclusive zone at the start to avoid flickering */
    phosh_layer_surface_set_exclusive_zone (PHOSH_LAYER_SURFACE (self), 0);
  }

  if (self->animation.show) {
    gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
  } else if (self->animation.progress >= 1.0 && !self->animation.show) {
    GtkWidget *widget;

    gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
    widget = hdy_deck_get_visible_child (self->deck);
    if (POS_IS_OSK_WIDGET (widget))
      pos_osk_widget_set_layer (POS_OSK_WIDGET (widget), POS_OSK_WIDGET_LAYER_NORMAL);
  }

  phosh_layer_surface_wl_surface_commit (PHOSH_LAYER_SURFACE (self));
}


static gboolean
animate_cb (GtkWidget     *widget,
            GdkFrameClock *frame_clock,
            gpointer       user_data)
{
  PosInputSurface *self = POS_INPUT_SURFACE (widget);
  gint64 time;
  gboolean finished = FALSE;

  time = gdk_frame_clock_get_frame_time (frame_clock) - self->animation.last_frame;
  if (self->animation.last_frame < 0)
    time = 0;

  self->animation.progress += 0.06666 * time / 16666.00;
  self->animation.last_frame = gdk_frame_clock_get_frame_time (frame_clock);

  if (self->animation.progress >= 1.0) {
    finished = TRUE;
    self->animation.progress = 1.0;
  }

  pos_input_surface_move (self);

  if (finished)
    return G_SOURCE_REMOVE;

  return G_SOURCE_CONTINUE;
}

static void
pos_input_surface_set_completer (PosInputSurface *self, PosCompleter *completer)
{
  if (self->completer == completer)
    return;

  if (self->completer)
    g_signal_handlers_disconnect_by_data (self->completer, self);

  g_set_object (&self->completer, completer);

  if (self->completer != NULL) {
    g_debug ("Adding completer '%s'", G_OBJECT_CLASS_NAME (G_OBJECT_GET_CLASS (self->completer)));
    g_object_connect (self->completer,
                      "swapped-signal::notify::completions",
                      G_CALLBACK (on_completer_completions_changed), self,
                      "swapped-signal::notify::preedit",
                      G_CALLBACK (on_completer_preedit_changed), self,
                      "swapped-signal::commit-string",
                      G_CALLBACK (on_completer_commit_string), self,
                      "swapped-signal::update",
                      G_CALLBACK (on_completer_update), self,
                      NULL);
  } else {
    g_debug ("Removing completer");
  }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETER]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETER_ACTIVE]);
}


static void
on_default_completer_changed (PosInputSurface *self)
{
  /* Handle completer change as layout switch as it will recheck the
   * completer to be used */
  g_debug ("Default completer changed, updating layout");
  on_visible_child_changed (self);
}


static void
pos_input_surface_set_completer_manager (PosInputSurface     *self,
                                         PosCompleterManager *completer_manager)
{
  if (self->completer_manager == completer_manager)
    return;

  if (self->completer_manager)
    g_signal_handlers_disconnect_by_data (self->completer_manager, self);

  g_set_object (&self->completer_manager, completer_manager);

  if (self->completer_manager) {
    g_signal_connect_swapped (self->completer_manager,
                              "notify::default",
                              G_CALLBACK (on_default_completer_changed),
                              self);
  }

  /* Switch completion */
  if (self->last_layout && self->completer_manager)
    pos_input_surface_switch_completion (self, POS_OSK_WIDGET (self->last_layout));
}


static void
pos_input_surface_set_clipboard_manager (PosInputSurface     *self,
                                         PosClipboardManager *clipboard_manager)
{
  GAction *paste_action;

  if (self->clipboard_manager == clipboard_manager)
    return;

  g_set_object (&self->clipboard_manager, clipboard_manager);

  paste_action = g_action_map_lookup_action (G_ACTION_MAP (self), "clipboard-paste");
  g_assert (paste_action);

  g_object_bind_property (clipboard_manager, "has-text",
                          paste_action, "enabled",
                          G_BINDING_SYNC_CREATE);
}


static double
reverse_ease_out_cubic (double t)
{
  return cbrt (t - 1) + 1;
}


static void
pos_input_surface_set_completion_enabled (PosInputSurface *self, gboolean enable)
{
  if (self->completion_enabled == enable)
    return;

  /* popdown popover right away */
  gtk_popover_popdown (self->menu_popup);

  self->completion_enabled = enable;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETION_ENABLED]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETER_ACTIVE]);
}


static void
update_osk_features (gpointer key, gpointer value, gpointer data)
{
  PosOskWidget *osk_widget = POS_OSK_WIDGET (value);
  PosInputSurface *self = POS_INPUT_SURFACE (data);

  pos_osk_widget_set_features (osk_widget, self->osk_features);
}


static void
pos_input_surface_set_osk_features (PosInputSurface *self, PhoshOskFeatures osk_features)
{
  if (self->osk_features == osk_features)
    return;

  self->osk_features = osk_features;
  g_hash_table_foreach (self->osks, update_osk_features, self);
  pos_osk_widget_set_features (POS_OSK_WIDGET (self->osk_terminal), self->osk_features);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_OSK_FEATURES]);
}


static void
update_osk_key_height (gpointer key, gpointer value, gpointer data)
{
  PosOskWidget *osk_widget = POS_OSK_WIDGET (value);
  PosInputSurface *self = POS_INPUT_SURFACE (data);
  guint n_rows;

  n_rows = pos_osk_widget_max_rows (osk_widget);
  pos_osk_widget_set_key_height (osk_widget, self->min_height / n_rows);
}


static void
pos_input_surface_set_min_height (PosInputSurface *self, guint min_height)
{
  if (self->min_height == min_height)
    return;

  self->min_height = min_height;
  g_debug ("Minimum keyboard height: %d", self->min_height);

  g_hash_table_foreach (self->osks, update_osk_key_height, self);
  update_osk_key_height (NULL, self->osk_terminal, self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MIN_HEIGHT]);
}


static void
pos_input_surface_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PosInputSurface *self = POS_INPUT_SURFACE (object);

  switch (property_id) {
  case PROP_INPUT_METHOD:
    self->input_method = g_value_dup_object (value);
    break;
  case PROP_COMPLETER_MANAGER:
    pos_input_surface_set_completer_manager (self, g_value_get_object (value));
    break;
  case PROP_CLIPBOARD_MANAGER:
    pos_input_surface_set_clipboard_manager (self, g_value_get_object (value));
    break;
  case PROP_SCREEN_KEYBOARD_ENABLED:
    pos_screen_keyboard_set_enabled (self, g_value_get_boolean (value));
    break;
  case PROP_KEYBOARD_DRIVER:
    self->keyboard_driver = g_value_dup_object (value);
    break;
  case PROP_SURFACE_VISIBLE:
    pos_input_surface_set_visible (self, g_value_get_boolean (value));
    break;
  case PROP_COMPLETION_ENABLED:
    pos_input_surface_set_completion_enabled (self, g_value_get_boolean (value));
    break;
  case PROP_OSK_FEATURES:
    pos_input_surface_set_osk_features (self, g_value_get_flags (value));
    break;
  case PROP_MIN_HEIGHT:
    pos_input_surface_set_min_height (self, g_value_get_uint (value));
    break;
  case PROP_DEAD_ZONE:
    self->dead_zone = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_input_surface_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  PosInputSurface *self = POS_INPUT_SURFACE (object);

  switch (property_id) {
  case PROP_COMPLETER:
    g_value_set_object (value, self->completer);
    break;
  case PROP_COMPLETER_MANAGER:
    g_value_set_object (value, self->completer_manager);
    break;
  case PROP_CLIPBOARD_MANAGER:
    g_value_set_object (value, self->clipboard_manager);
    break;
  case PROP_SCREEN_KEYBOARD_ENABLED:
    g_value_set_boolean (value, pos_input_surface_get_screen_keyboard_enabled (self));
    break;
  case PROP_SURFACE_VISIBLE:
    g_value_set_boolean (value, pos_input_surface_get_visible (self));
    break;
  case PROP_COMPLETER_ACTIVE:
    g_value_set_boolean (value, pos_input_surface_is_completer_active (self));
    break;
  case PROP_COMPLETION_ENABLED:
    g_value_set_boolean (value, self->completion_enabled);
    break;
  case PROP_OSK_FEATURES:
    g_value_set_flags (value, self->osk_features);
    break;
  case PROP_MIN_HEIGHT:
    g_value_set_uint (value, self->min_height);
    break;
  case PROP_DEAD_ZONE:
    g_value_set_uint (value, self->dead_zone);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
on_im_purpose_changed (PosInputSurface *self, GParamSpec *pspec, PosInputMethod *im)
{
  GtkWidget *widget = NULL;
  PosOskWidgetLayer layer = POS_OSK_WIDGET_LAYER_NORMAL;
  PosInputMethodPurpose purpose;

  g_assert (POS_IS_INPUT_SURFACE (self));
  g_assert (POS_IS_INPUT_METHOD (im));

  /* We only have completer active on `normal` input purpose */
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETER_ACTIVE]);

  purpose = pos_input_method_get_purpose (im);
  switch (purpose) {
  case POS_INPUT_METHOD_PURPOSE_ALPHA:
  case POS_INPUT_METHOD_PURPOSE_EMAIL:
  case POS_INPUT_METHOD_PURPOSE_NAME:
  case POS_INPUT_METHOD_PURPOSE_NORMAL:
  case POS_INPUT_METHOD_PURPOSE_PASSWORD:
  case POS_INPUT_METHOD_PURPOSE_URL:
    layer = POS_OSK_WIDGET_LAYER_NORMAL;
    break;
  case POS_INPUT_METHOD_PURPOSE_DATE:
  case POS_INPUT_METHOD_PURPOSE_DATETIME:
  case POS_INPUT_METHOD_PURPOSE_TIME:
    layer = POS_OSK_WIDGET_LAYER_SYMBOLS;
    break;
  case POS_INPUT_METHOD_PURPOSE_NUMBER:
    pos_keypad_set_decimal_separator_visible (POS_KEYPAD (self->keypad), TRUE);
    pos_keypad_set_symbols_visible (POS_KEYPAD (self->keypad), FALSE);
    pos_keypad_set_letters_visible (POS_KEYPAD (self->keypad), FALSE);
    widget = self->keypad;
    break;
  case POS_INPUT_METHOD_PURPOSE_DIGITS:
    pos_keypad_set_decimal_separator_visible (POS_KEYPAD (self->keypad), FALSE);
    pos_keypad_set_symbols_visible (POS_KEYPAD (self->keypad), FALSE);
    pos_keypad_set_letters_visible (POS_KEYPAD (self->keypad), FALSE);
    widget = self->keypad;
    break;
  case POS_INPUT_METHOD_PURPOSE_PIN:
    pos_keypad_set_decimal_separator_visible (POS_KEYPAD (self->keypad), FALSE);
    pos_keypad_set_symbols_visible (POS_KEYPAD (self->keypad), FALSE);
    pos_keypad_set_letters_visible (POS_KEYPAD (self->keypad), TRUE);
    widget = self->keypad;
    break;
  case POS_INPUT_METHOD_PURPOSE_PHONE:
    pos_keypad_set_decimal_separator_visible (POS_KEYPAD (self->keypad), FALSE);
    pos_keypad_set_symbols_visible (POS_KEYPAD (self->keypad), TRUE);
    pos_keypad_set_letters_visible (POS_KEYPAD (self->keypad), TRUE);
    widget = self->keypad;
    break;
  case POS_INPUT_METHOD_PURPOSE_TERMINAL:
    widget = self->osk_terminal;
    break;
  default:
    g_return_if_reached ();
  }

  if (widget == NULL) {
    widget = hdy_deck_get_visible_child (self->deck);
    /* If no "special" layout, Switch back to the last language layer */
    if (!POS_INPUT_SURFACE_IS_LANG_LAYOUT (widget))
      widget = self->last_layout;
  } else if (POS_IS_OSK_WIDGET (widget)) {
    pos_osk_widget_set_layer (POS_OSK_WIDGET (widget), layer);
    g_debug ("Layout: %s, purpose: %d",
             pos_osk_widget_get_name (POS_OSK_WIDGET (widget)),
             purpose);
  }

  hdy_deck_set_visible_child (self->deck, widget);
}


static void
on_im_text_change_cause_changed (PosInputSurface *self, GParamSpec *pspec, PosInputMethod *im)
{
  g_assert (POS_IS_INPUT_SURFACE (self));
  g_assert (POS_IS_INPUT_METHOD (im));

  if (!pos_input_surface_is_completer_active (self))
    return;

  if (pos_input_method_get_text_change_cause (im) != POS_INPUT_METHOD_TEXT_CHANGE_CAUSE_IM)
    pos_completer_set_preedit (self->completer, NULL);
}


static void
on_im_hint_changed (PosInputSurface *self, GParamSpec *pspec, PosInputMethod *im)
{
  gboolean enable;

  g_assert (POS_IS_INPUT_SURFACE (self));
  g_assert (POS_IS_INPUT_METHOD (im));

  g_debug ("Hint changed: 0x%.2x", pos_input_method_get_hint (im));
  if ((self->completion_mode & PHOSH_OSK_COMPLETION_MODE_HINT) == 0)
    return;

  enable = !!(pos_input_method_get_hint (im) & POS_INPUT_METHOD_HINT_COMPLETION);

  pos_input_surface_set_completion_enabled (self, enable);
}


static void
on_im_surrounding_text_changed (PosInputSurface *self, GParamSpec *pspec, PosInputMethod *im)
{
  const char *text;
  guint anchor, cursor;
  g_autofree char *before = NULL;
  g_autofree char *after = NULL;

  g_assert (POS_IS_INPUT_SURFACE (self));
  g_assert (POS_IS_INPUT_METHOD (im));

  text = pos_input_method_get_surrounding_text (im, &anchor, &cursor);

  if (!pos_input_surface_is_completion_mode (self))
    return;

  before = g_strndup (text, cursor);

  if (text)
    after = g_strdup (&(text[cursor]));

  pos_completer_set_surrounding_text (POS_COMPLETER (self->completer), before, after);
}


static void
on_im_active_changed (PosInputSurface *self, GParamSpec *pspec, PosInputMethod *im)
{
  gboolean active;

  g_assert (POS_IS_INPUT_SURFACE (self));
  g_assert (POS_IS_INPUT_METHOD (im));

  active = pos_input_method_get_active (im);

  if (active) {
    /* TODO: Reset buffered commit_string, delete_surrounding_text */
    if (pos_input_surface_is_completer_active (self)) {
      pos_completer_set_preedit (self->completer, NULL);
    }
  }

  /* Completer can only be active with input method, not vk */
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETER_ACTIVE]);
}

static PosOskWidget *insert_xkb_layout (PosInputSurface *self, const char *type, const char *id);
static void on_input_setting_changed (PosInputSurface *self, const char *key, GSettings *settings);


static void
delayed_init (PosInputSurface *self)
{
  pos_vk_driver_key_press_gdk (self->keyboard_driver, GDK_KEY_BackSpace, 0);
}


static void
pos_input_surface_constructed (GObject *object)
{
  static gboolean input_serial_sent = FALSE;
  PosInputSurface *self = POS_INPUT_SURFACE (object);
  const char *test_layout = g_getenv ("POS_TEST_LAYOUT");

  G_OBJECT_CLASS (pos_input_surface_parent_class)->constructed (object);

  if (test_layout) {
    PosOskWidget *osk_widget = insert_xkb_layout (self, "xkb", test_layout);
    hdy_deck_set_visible_child (self->deck, GTK_WIDGET (osk_widget));
  } else {
    g_object_connect (self->input_settings,
                      "swapped-signal::changed::sources",
                      G_CALLBACK (on_input_setting_changed), self,
                      "swapped-signal::changed::xkb-options",
                      G_CALLBACK (on_input_setting_changed), self,
                      NULL);
    on_input_setting_changed (self, NULL, self->input_settings);
  }

  g_assert (POS_IS_INPUT_METHOD (self->input_method));
  g_object_connect (self->input_method,
                    "swapped-object-signal::notify::active", on_im_active_changed, self,
                    "swapped-object-signal::notify::purpose", on_im_purpose_changed, self,
                    "swapped-object-signal::notify::hint", on_im_hint_changed, self,
                    "swapped-object-signal::notify::text-change-cause",
                    on_im_text_change_cause_changed, self,
                    "swapped-object-signal::notify::surrounding-text",
                    on_im_surrounding_text_changed, self,
                    NULL);

  set_keymap (self);

  /* Work around https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/5628 by
     sending at least one key press to the shell so we have a serial */
  if (!input_serial_sent) {
    g_timeout_add_seconds_once (1, (GSourceOnceFunc)delayed_init, self);
    input_serial_sent = TRUE;
  }
}


static void
pos_input_surface_destroy (GtkWidget *widget)
{
  PosInputSurface *self = POS_INPUT_SURFACE (widget);

  g_clear_object (&self->action_map);
  /* Remove hash table early since this also destroys the osks in the deck */
  g_clear_pointer (&self->osks, g_hash_table_destroy);

  GTK_WIDGET_CLASS (pos_input_surface_parent_class)->destroy (widget);
}


static void
pos_input_surface_finalize (GObject *object)
{
  PosInputSurface *self = POS_INPUT_SURFACE (object);

  g_signal_remove_emission_hook (g_signal_lookup ("clicked", GTK_TYPE_BUTTON), self->clicked_id);
  self->clicked_id = 0;

  g_clear_handle_id (&self->animation.id, g_source_remove);
  g_clear_handle_id (&self->bs_repeat_id, g_source_remove);

  g_clear_object (&self->logind_session);
  g_clear_object (&self->keyboard_driver);
  g_clear_object (&self->input_method);
  g_clear_object (&self->a11y_settings);
  g_clear_object (&self->input_settings);
  g_clear_object (&self->osk_settings);
  g_clear_object (&self->xkbinfo);
  g_clear_object (&self->clipboard_manager);
  g_clear_object (&self->completer);
  pos_input_surface_set_completer_manager (self, NULL);
  g_clear_object (&self->swipe_down);
  g_clear_object (&self->style_manager);
  g_clear_pointer (&self->osks, g_hash_table_destroy);

  G_OBJECT_CLASS (pos_input_surface_parent_class)->finalize (object);
}


static char **
pos_input_surface_list_actions (GActionGroup *group)
{
  PosInputSurface *self = POS_INPUT_SURFACE (group);

  /* may be NULL after dispose has run */
  if (!self->action_map)
    return g_new0 (char *, 0 + 1);

  return g_action_group_list_actions (G_ACTION_GROUP (self->action_map));
}

static gboolean
pos_input_surface_query_action (GActionGroup        *group,
                                const char          *action_name,
                                gboolean            *enabled,
                                const GVariantType **parameter_type,
                                const GVariantType **state_type,
                                GVariant           **state_hint,
                                GVariant           **state)
{
  PosInputSurface *self = POS_INPUT_SURFACE (group);

  if (!self->action_map)
    return FALSE;

  return g_action_group_query_action (G_ACTION_GROUP (self->action_map),
                                      action_name, enabled, parameter_type, state_type, state_hint, state);
}


static void
pos_input_surface_activate_action (GActionGroup *group,
                                   const char   *action_name,
                                   GVariant     *parameter)
{
  PosInputSurface *self = POS_INPUT_SURFACE (group);

  if (!self->action_map)
    return;

  g_action_group_activate_action (G_ACTION_GROUP (self->action_map), action_name, parameter);
}


static void
pos_input_surface_change_action_state (GActionGroup *group,
                                       const char   *action_name,
                                       GVariant     *state)
{
  PosInputSurface *self = POS_INPUT_SURFACE (group);

  if (!self->action_map)
    return;

  g_action_group_change_action_state (G_ACTION_GROUP (self->action_map), action_name, state);
}


static void
pos_input_surface_action_group_iface_init (GActionGroupInterface *iface)
{
  iface->list_actions = pos_input_surface_list_actions;
  iface->query_action = pos_input_surface_query_action;
  iface->activate_action = pos_input_surface_activate_action;
  iface->change_action_state = pos_input_surface_change_action_state;
}


static GAction *
pos_input_surface_lookup_action (GActionMap  *action_map, const char *action_name)
{
  PosInputSurface *self = POS_INPUT_SURFACE (action_map);

  if (!self->action_map)
    return NULL;

  return g_action_map_lookup_action (G_ACTION_MAP (self->action_map), action_name);
}

static void
pos_input_surface_add_action (GActionMap *action_map, GAction *action)
{
  PosInputSurface *self = POS_INPUT_SURFACE (action_map);

  if (!self->action_map)
    return;

  g_action_map_add_action (G_ACTION_MAP (self->action_map), action);
}

static void
pos_input_surface_remove_action (GActionMap *action_map, const char *action_name)
{
  PosInputSurface *self = POS_INPUT_SURFACE (action_map);

  if (!self->action_map)
    return;

  g_action_map_remove_action (G_ACTION_MAP (self->action_map), action_name);
}


static void pos_input_surface_action_map_iface_init (GActionMapInterface *iface)
{
  iface->lookup_action = pos_input_surface_lookup_action;
  iface->add_action = pos_input_surface_add_action;
  iface->remove_action = pos_input_surface_remove_action;
}


static void
pos_input_surface_check_resize (GtkContainer *container)
{
  PosInputSurface *self = POS_INPUT_SURFACE (container);
  GtkRequisition min;
  int height;

  g_return_if_fail (GTK_IS_CONTAINER (container));

  gtk_widget_get_preferred_size (GTK_WIDGET (self), &min, NULL);
  g_object_get (self, "height", &height, NULL);

  min.height = MAX (min.height, self->min_height);

  if (gtk_widget_get_mapped (GTK_WIDGET (self)) && min.height != height) {
    phosh_layer_surface_set_size (PHOSH_LAYER_SURFACE (self), -1, min.height);
    /* Don't interfere with animation */
    if (self->animation.progress >= 1.0) {
      phosh_layer_surface_set_exclusive_zone (PHOSH_LAYER_SURFACE (self), min.height);
    }
  }

  GTK_CONTAINER_CLASS (pos_input_surface_parent_class)->check_resize (container);
}



static void
pos_input_surface_class_init (PosInputSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->get_property = pos_input_surface_get_property;
  object_class->set_property = pos_input_surface_set_property;
  object_class->constructed = pos_input_surface_constructed;
  object_class->finalize = pos_input_surface_finalize;

  widget_class->destroy = pos_input_surface_destroy;

  container_class->check_resize = pos_input_surface_check_resize;

  /* Make sure resources are available when building git */
  pos_init ();

  g_type_ensure (POS_TYPE_COMPLETION_BAR);
  g_type_ensure (POS_TYPE_EMOJI_PICKER);
  g_type_ensure (POS_TYPE_KEYPAD);
  g_type_ensure (POS_TYPE_OSK_WIDGET);
  g_type_ensure (POS_TYPE_SHORTCUTS_BAR);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/stevia/ui/input-surface.ui");
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, completion_bar);
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, deck);
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, emoji_picker);
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, keypad);
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, menu_box_layouts);
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, menu_popup);
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, osk_terminal);
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, shortcuts_bar);
  gtk_widget_class_bind_template_child (widget_class, PosInputSurface, word_completion_btn);
  gtk_widget_class_bind_template_callback (widget_class, on_completion_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_emoji_picked);
  gtk_widget_class_bind_template_callback (widget_class, on_emoji_picker_done);
  gtk_widget_class_bind_template_callback (widget_class, on_emoji_picker_delete_last);

  gtk_widget_class_bind_template_callback (widget_class, on_keypad_symbol_pressed);
  gtk_widget_class_bind_template_callback (widget_class, on_keypad_done);

  gtk_widget_class_bind_template_callback (widget_class, on_latched_modifiers_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_num_shortcuts_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_osk_key_cancelled);
  gtk_widget_class_bind_template_callback (widget_class, on_osk_key_down);
  gtk_widget_class_bind_template_callback (widget_class, on_osk_key_up);
  gtk_widget_class_bind_template_callback (widget_class, on_osk_key_symbol);
  gtk_widget_class_bind_template_callback (widget_class, on_osk_mode_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_osk_popover_shown);
  gtk_widget_class_bind_template_callback (widget_class, on_osk_popover_hidden);
  gtk_widget_class_bind_template_callback (widget_class, on_shortcut_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_visible_child_changed);

  /**
   * PosInputSurface:input-method:
   *
   * A zwp_input_method_v2
   */
  props[PROP_INPUT_METHOD] =
    g_param_spec_object ("input-method", "", "",
                         POS_TYPE_INPUT_METHOD,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:completer:
   *
   * A completer implementing the #PosCompleter interface.
   *
   */
  props[PROP_COMPLETER] =
    g_param_spec_object ("completer", "", "",
                         POS_TYPE_COMPLETER,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:completer-manager:
   *
   * A completer manager to use. It allows to query which implementation of
   * [iface@Completer] to use.
   */
  props[PROP_COMPLETER_MANAGER] =
    g_param_spec_object ("completer-manager", "", "",
                         POS_TYPE_COMPLETER_MANAGER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:clipboard-manager:
   *
   * A clipboard manager to use. The clipboard manager is used to get text to paste.
   */
  props[PROP_CLIPBOARD_MANAGER] =
    g_param_spec_object ("clipboard-manager", "", "",
                         POS_TYPE_CLIPBOARD_MANAGER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:enable
   *
   * Whether an on screen keyboard should be enabled. This is the
   * global toggle that enables screen keyboards and maps the a11y
   * settings.
   */
  props[PROP_SCREEN_KEYBOARD_ENABLED] =
    g_param_spec_boolean ("screen-keyboard-enabled", "", "", FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:surface-visible
   *
   * If this is %TRUE the input surface will be shown, otherwise hidden.
   */
  props[PROP_SURFACE_VISIBLE] =
    g_param_spec_boolean ("surface-visible", "", "", FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:completer-active
   *
   * %TRUE if the there is a completer set and active
   */
  props[PROP_COMPLETER_ACTIVE] =
    g_param_spec_boolean ("completer-active", "", "", FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:completion-enabled
   *
   * %TRUE if the user enabled completion. That does not imply that completion is actually active
   * as this also depends on an input-method being present, a completer configured, etc. This
   * setting merely reflects the users intent.
   */
  props[PROP_COMPLETION_ENABLED] =
    g_param_spec_boolean ("completion-enabled", "", "", FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:keyboard-driver:
   *
   * Keyboard driver for submitting keycodes and handling associated keymaps.
   */
  props[PROP_KEYBOARD_DRIVER] =
    g_param_spec_object ("keyboard-driver", "", "",
                         /* TODO: should be an interface */
                         POS_TYPE_VK_DRIVER,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:osk-features:
   *
   * Features to enable on all OSKs.
   */
  props[PROP_OSK_FEATURES] =
    g_param_spec_flags ("osk-features", "", "",
                        PHOSH_TYPE_OSK_FEATURES,
                        PHOSH_OSK_FEATURE_DEFAULT,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:min-height:
   *
   * Minimal height in pixels this OSK should have
   */
  props[PROP_MIN_HEIGHT] =
    g_param_spec_uint ("min-height", "", "",
                       0, G_MAXUINT,
                       0,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosInputSurface:dead-zone:
   *
   * Empty space at the bottom of the keyboard
   */
  props[PROP_DEAD_ZONE] =
    g_param_spec_uint ("dead-zone", "", "",
                       0, G_MAXUINT,
                       0,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_css_name (widget_class, "pos-input-surface");
}


static char *
build_layout_name (const char *engine, const char *layout, const char *variant)
{
  char *name;

  if (STR_IS_NULL_OR_EMPTY (variant))
    name = g_strdup_printf ("%s:%s", engine, layout);
  else
    name = g_strdup_printf ("%s:%s+%s", engine, layout, variant);

  return name;
}


static PosOskWidget *
insert_osk (PosInputSurface   *self,
            const char        *name,
            const char        *layout_id,
            const char        *display_name,
            const char        *layout,
            const char        *variant,
            PosCompletionInfo *info)
{
  g_autoptr (GError) err = NULL;
  PosOskWidget *osk_widget;

  osk_widget = g_hash_table_lookup (self->osks, name);
  if (osk_widget)
    return osk_widget;

  osk_widget = pos_osk_widget_new (self->osk_features);
  if (!pos_osk_widget_set_layout (POS_OSK_WIDGET (osk_widget),
                                  name,
                                  layout_id,
                                  display_name,
                                  layout,
                                  variant,
                                  &err)) {
    g_warning ("Failed to load osk layout for %s: %s", name, err->message);

    gtk_widget_destroy (g_object_ref_sink (GTK_WIDGET (osk_widget)));
    return NULL;
  }

  g_object_set_data_full (G_OBJECT (osk_widget),
                          "pos-completion-info",
                          info,
                          (GDestroyNotify)pos_completion_info_free);

  g_debug ("Adding osk for layout '%s'", name);
  gtk_widget_set_visible (GTK_WIDGET (osk_widget), TRUE);
  g_object_connect (osk_widget,
                    "swapped-signal::key-cancelled", G_CALLBACK (on_osk_key_cancelled), self,
                    "swapped-signal::key-down", G_CALLBACK (on_osk_key_down), self,
                    "swapped-signal::key-symbol", G_CALLBACK (on_osk_key_symbol), self,
                    "swapped-signal::key-up", G_CALLBACK (on_osk_key_up), self,
                    "swapped-signal::notify::mode", G_CALLBACK (on_osk_mode_changed), self,
                    "swapped-signal::popover-shown", G_CALLBACK (on_osk_popover_shown), self,
                    "swapped-signal::popover-hidden", G_CALLBACK (on_osk_popover_hidden), self,
                    NULL);

  hdy_deck_insert_child_after (self->deck, GTK_WIDGET (osk_widget), NULL);
  g_hash_table_insert (self->osks, g_strdup (name), osk_widget);

  if (self->last_layout == NULL && POS_INPUT_SURFACE_IS_LANG_LAYOUT (osk_widget))
    self->last_layout = GTK_WIDGET (osk_widget);

  return osk_widget;
}


static PosOskWidget *
insert_xkb_layout (PosInputSurface *self, const char *type, const char *layout_id)
{
  g_autofree char *name = NULL;
  const char *layout = NULL;
  const char *variant = NULL;
  const char *display_name = NULL;

  if (g_strcmp0 (type, "xkb")) {
    g_debug ("Not a xkb layout: '%s' - ignoring", layout_id);
    return NULL;
  }

  if (!gnome_xkb_info_get_layout_info (self->xkbinfo, layout_id, &display_name, NULL,
                                       &layout, &variant)) {
    g_warning ("Failed to get layout info for %s", layout_id);
    return NULL;
  }
  name = build_layout_name ("xkb", layout, variant);

  return insert_osk (self, name, layout_id, display_name, layout, variant, NULL);
}

static PosOskWidget *
insert_ibus_layout (PosInputSurface *self, const char *type, const char *id)
{
  const char *engine_name, *lang;
  g_autofree char *name = NULL;
  g_autoptr (GError) err = NULL;
  g_auto (GStrv) parts = NULL;
  PosCompletionInfo *info;

  /* We don't actually do ibus bus but try to match these to completers */
  if (g_strcmp0 (type, "ibus")) {
    g_debug ("Not an ibus layout: '%s' - ignoring", id);
    return NULL;
  }

  parts = g_strsplit (id, ":", -1);
  if (g_strv_length (parts) > 3) {
    g_warning ("ibus layout '%s' not parsable - ignoring", id);
    return NULL;
  }

  if (g_strv_length (parts) < 2) {
    g_warning ("ibus layout '%s' has no language - ignoring", id);
    return NULL;
  }

  engine_name = parts[0];
  lang = parts[1];

  info = pos_completer_manager_get_info (self->completer_manager, engine_name, lang, NULL, &err);
  if (!info) {
    g_warning ("ibus layout '%s': engine '%s' not usable for '%s': %s - ignoring",
               id,
               engine_name,
               lang,
               err->message);
    return NULL;
  }

  name = build_layout_name ("ibus", lang, NULL);

  /* TODO: allow for other base layouts than "us" */
  return insert_osk (self, name, id, info->display_name, "us", NULL, info);
}


static void
on_input_setting_changed (PosInputSurface *self, const char *key, GSettings *settings)
{
  g_autoptr (GVariant) sources = NULL;
  g_autoptr (GHashTable) new = NULL;
  g_autofree GStrv old = NULL;
  g_autofree GStrv old_keys = NULL;
  GVariantIter iter;
  const char *id = NULL;
  const char *type = NULL;
  gboolean first_set = FALSE;

  g_debug ("Setting changed, reloading input settings");

  sources = g_settings_get_value (settings, "sources");
  g_variant_iter_init (&iter, sources);

  /* Get us a copy of the keys since we remove elements while iterating */
  old_keys = (GStrv)g_hash_table_get_keys_as_array (self->osks, NULL);
  old = g_strdupv (old_keys);
  new = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  self->last_layout = NULL;
  while (g_variant_iter_next (&iter, "(&s&s)", &type, &id)) {
    PosOskWidget *osk_widget;

    osk_widget = insert_xkb_layout (self, type, id);
    if (osk_widget == NULL)
      osk_widget = insert_ibus_layout (self, type, id);

    if (osk_widget == NULL)
      continue;

    g_hash_table_add (new, g_strdup (pos_osk_widget_get_name (osk_widget)));
    if (!first_set) {
      first_set = TRUE;
      hdy_deck_set_visible_child (self->deck, GTK_WIDGET (osk_widget));
    }
    update_osk_key_height (NULL, osk_widget, self);
  }

  if (old) {
    /* Drop removed layouts */
    for (int i = 0; old[i]; i++) {
      if (!g_hash_table_contains (new, old[i])) {
        g_debug ("Removing layout %s", old[i]);
        g_hash_table_remove (self->osks, old[i]);
      }
    }
  }

  /* If nothing is left add a default */
  if (g_hash_table_size (self->osks) == 0) {
    insert_osk (self, "us", "us", "English (USA)", "us", NULL, NULL);
  }

  set_keymap (self);
}


static void
on_completion_mode_changed (PosInputSurface *self, const char *key, GSettings *settings)
{
  PhoshOskCompletionModeFlags mode;

  mode = g_settings_get_flags (settings, "completion-mode");

  if (mode == self->completion_mode)
    return;

  self->completion_mode = mode;

  /* In manual mode we don't interfere with the user's choice */
  if (self->completion_mode & PHOSH_OSK_COMPLETION_MODE_MANUAL)
    return;

  /* In hint mode catch up with the input method */
  if ((self->completion_mode & PHOSH_OSK_COMPLETION_MODE_HINT) && self->input_method) {
    gboolean enable;

    enable = pos_input_method_get_hint (self->input_method) & POS_INPUT_METHOD_HINT_COMPLETION;
    pos_input_surface_set_completion_enabled (self, enable);
    return;
  }

  /* no completion wanted */
  pos_input_surface_set_completion_enabled (self, FALSE);
}


static GActionEntry entries[] =
{
  { .name = "clipboard-paste", .activate = clipboard_paste_activated },
  { .name = "settings", .activate = settings_activated },
  { .name = "select-layout", .parameter_type = "s", .state = "\"terminal\"",
    .change_state = select_layout_change_state },
  { .name = "menu", .parameter_type = "(ii)", .activate = menu_activated },
};


static void
pos_input_surface_init (PosInputSurface *self)
{
  g_autoptr (GPropertyAction) completion_action = NULL;
  g_autoptr (GError) err = NULL;
  GAction *action;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->style_manager = pos_style_manager_new ();
  self->action_map = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->action_map),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "win",
                                  G_ACTION_GROUP (self->action_map));
  completion_action = g_property_action_new ("word-completion", self, "completion-enabled");
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (completion_action));

  /* Ensure initial sync */
  self->screen_keyboard_enabled = -1;
  self->surface_visible = -1;
  self->animation.progress = 1.0;

  self->a11y_settings = g_settings_new ("org.gnome.desktop.a11y.applications");
  g_settings_bind (self->a11y_settings, "screen-keyboard-enabled",
                   self, "screen-keyboard-enabled", G_SETTINGS_BIND_GET);

  self->osks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                      (GDestroyNotify)gtk_widget_destroy);
  self->xkbinfo = gnome_xkb_info_new ();
  self->input_settings = g_settings_new ("org.gnome.desktop.input-sources");
  self->osk_settings = g_settings_new ("sm.puri.phosh.osk");
  g_signal_connect_swapped (self->osk_settings, "changed::completion-mode",
                            G_CALLBACK (on_completion_mode_changed),
                            self);
  on_completion_mode_changed (self, NULL, self->osk_settings);
  g_settings_bind (self->osk_settings, "osk-features", self, "osk-features", G_SETTINGS_BIND_GET);

  if (!pos_osk_widget_set_layout (POS_OSK_WIDGET (self->osk_terminal),
                                  "terminal",
                                  "terminal",
                                  _("Terminal"),
                                  "terminal",
                                  NULL,
                                  &err)) {
    g_warning ("Failed to set terminal layout: %s", err->message);
  }

  self->clicked_id = g_signal_add_emission_hook (g_signal_lookup ("clicked", GTK_TYPE_BUTTON), 0,
                                                 on_click_hook, self, NULL);

  /* Disable swipe gestures to change layouts by default */
  pos_input_surface_set_layout_swipe (self, FALSE);

  self->swipe_down = g_object_new (GTK_TYPE_GESTURE_SWIPE,
                                   "widget", self,
                                   "propagation-phase", GTK_PHASE_CAPTURE,
                                   "touch-only", TRUE,
                                   NULL);
  g_signal_connect (self->swipe_down, "swipe", G_CALLBACK (on_swipe), self);

  self->logind_session = pos_logind_session_new ();
  action = g_action_map_lookup_action (G_ACTION_MAP (self->action_map), "settings");
  g_object_bind_property (self->logind_session, "locked",
                          action, "enabled",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
}


gboolean
pos_input_surface_get_active (PosInputSurface *self)
{
  g_return_val_if_fail (POS_IS_INPUT_SURFACE (self), FALSE);

  return pos_input_method_get_active (self->input_method) && self->completer;
}


static gboolean
animation_timeout_cb (gpointer data)
{
  PosInputSurface *self = POS_INPUT_SURFACE (data);

  if (self->animation.progress < 1.0) {
    g_warning ("Animation did not finish in time: %f", self->animation.progress);
    self->animation.progress = 1.0;
    pos_input_surface_move (self);
  }

  self->animation.id = 0;
  return FALSE;
}


void
pos_input_surface_set_visible (PosInputSurface *self, gboolean visible)
{
  g_return_if_fail (POS_IS_INPUT_SURFACE (self));

  g_debug ("Showing keyboard: %d, %d", visible, self->surface_visible);
  if (visible == self->surface_visible)
    return;

  self->surface_visible = visible;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SURFACE_VISIBLE]);

  self->animation.show = visible;
  self->animation.last_frame = -1;
  self->animation.progress =
    reverse_ease_out_cubic (1.0 - hdy_ease_out_cubic (self->animation.progress));

  if (self->animation.id)
    g_source_remove (self->animation.id);

  self->animation.id = g_timeout_add_seconds (1, animation_timeout_cb, self);
  gtk_widget_add_tick_callback (GTK_WIDGET (self), animate_cb, NULL, NULL);
}


gboolean
pos_input_surface_get_visible (PosInputSurface *self)
{
  g_return_val_if_fail (POS_IS_INPUT_SURFACE (self), FALSE);

  return self->surface_visible;
}


gboolean
pos_input_surface_get_screen_keyboard_enabled (PosInputSurface *self)
{
  g_return_val_if_fail (POS_IS_INPUT_SURFACE (self), FALSE);

  return self->screen_keyboard_enabled;
}

gboolean
pos_input_surface_is_completer_active (PosInputSurface *self)
{
  GtkWidget *child;

  g_return_val_if_fail (POS_IS_INPUT_SURFACE (self), FALSE);

  if (self->completer == NULL)
    return FALSE;

  if (pos_input_method_get_active (self->input_method) == FALSE)
    return FALSE;

  /* Layout has an "implicit" completer (e.g. varnam) */
  child = hdy_deck_get_visible_child (self->deck);
  if (POS_IS_OSK_WIDGET (child) &&
      g_object_get_data (G_OBJECT (child), "pos-completion-info")) {
      return TRUE;
  }

  if (self->completion_enabled == FALSE)
    return FALSE;

  /* Completion should only be used on "regular" layouts */
  if (!POS_INPUT_SURFACE_IS_LANG_LAYOUT (child))
    return FALSE;

  /* We only complete input purpose `normal` */
  return (pos_input_method_get_purpose (self->input_method) == POS_INPUT_METHOD_PURPOSE_NORMAL);
}

/**
 * pos_input_surface_set_layout_swipe:
 * @self: The input surface
 * @enable: Passing %TRUE will enable layout change via swipe gestures.
 *
 * Controls whether layout changes are possible using swipe gestures.
 */
void
pos_input_surface_set_layout_swipe (PosInputSurface *self, gboolean enable)
{
  g_return_if_fail (POS_IS_INPUT_SURFACE (self));

  hdy_deck_set_can_swipe_forward (self->deck, enable);
  hdy_deck_set_can_swipe_back (self->deck, enable);
}

/**
 * pos_input_surface_get_layout_swipe:
 * @self: The input surface
 *
 * Whether layouts can be changed using a swipe gesture.
 *
 * Returns %TRUE is layout swiping is enabled, otherwise %FALSE.
 */
gboolean
pos_input_surface_get_layout_swipe (PosInputSurface *self)
{
  g_return_val_if_fail (POS_IS_INPUT_SURFACE (self), FALSE);

  return hdy_deck_get_can_swipe_forward (self->deck);
}
