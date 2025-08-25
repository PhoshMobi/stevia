/*
 * Copyright (C) 2022 Purism SPC
 *               2023-2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-vk-driver"

#include "pos-config.h"

#include "pos-vk-driver.h"

#include <xkbcommon/xkbcommon.h>

#include <linux/input-event-codes.h>

enum {
  PROP_0,
  PROP_VIRTUAL_KEYBOARD,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PosVkDriver:
 *
 * Processes input events and drives a virtual keyboard
 * using the wayland virtual keyboard protocol. The input
 * events can either be based on kernel input event codes
 * or GDK keycodes.
 */
struct _PosVkDriver {
  GObject             parent;

  GHashTable         *keycodes;
  GHashTable         *gdk_keycodes;
  PosVirtualKeyboard *virtual_keyboard;

  char               *layout_id;
};
G_DEFINE_TYPE (PosVkDriver, pos_vk_driver, G_TYPE_OBJECT)

typedef struct {
  char *key;
  guint keycode;
  guint modifiers;
} PosKeycode;

static const PosKeycode keycodes_common[] = {
  /* special keys */
  { "KEY_LEFT", KEY_LEFT, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_RIGHT", KEY_RIGHT, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_UP", KEY_UP, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_DOWN", KEY_DOWN, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_ENTER", KEY_ENTER, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_TAB", KEY_TAB, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_BACKSPACE" ,KEY_BACKSPACE, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_ESC", KEY_ESC, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F1", KEY_F1, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F2", KEY_F2, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F3", KEY_F3, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F4", KEY_F4, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F5", KEY_F5, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F6", KEY_F6, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F7", KEY_F7, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F8", KEY_F8, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F9", KEY_F9, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F10", KEY_F10, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F11", KEY_F11, POS_KEYCODE_MODIFIER_NONE },
  { "KEY_F12", KEY_F12, POS_KEYCODE_MODIFIER_NONE },
  /* common keys */
  { " ", KEY_SPACE, POS_KEYCODE_MODIFIER_NONE},
  { "0", KEY_0, POS_KEYCODE_MODIFIER_NONE },
  { "1", KEY_1, POS_KEYCODE_MODIFIER_NONE },
  { "2", KEY_2, POS_KEYCODE_MODIFIER_NONE },
  { "3", KEY_3, POS_KEYCODE_MODIFIER_NONE },
  { "4", KEY_4, POS_KEYCODE_MODIFIER_NONE },
  { "5", KEY_5, POS_KEYCODE_MODIFIER_NONE },
  { "6", KEY_6, POS_KEYCODE_MODIFIER_NONE },
  { "7", KEY_7, POS_KEYCODE_MODIFIER_NONE },
  { "8", KEY_8, POS_KEYCODE_MODIFIER_NONE },
  { "9", KEY_9, POS_KEYCODE_MODIFIER_NONE },
};

static const PosKeycode keycodes_terminal[] = {
  { "!", KEY_1, POS_KEYCODE_MODIFIER_SHIFT },
  { "#", KEY_3, POS_KEYCODE_MODIFIER_SHIFT },
  { "$", KEY_4, POS_KEYCODE_MODIFIER_SHIFT },
  { "%", KEY_5, POS_KEYCODE_MODIFIER_SHIFT },
  { "&", KEY_7, POS_KEYCODE_MODIFIER_SHIFT },
  { "(", KEY_9, POS_KEYCODE_MODIFIER_SHIFT },
  { ")", KEY_0, POS_KEYCODE_MODIFIER_SHIFT },
  { "*", KEY_8, POS_KEYCODE_MODIFIER_SHIFT },
  { "+", KEY_EQUAL, POS_KEYCODE_MODIFIER_SHIFT },
  { ",", KEY_COMMA, POS_KEYCODE_MODIFIER_NONE },
  { "-", KEY_MINUS, POS_KEYCODE_MODIFIER_NONE },
  { ".", KEY_DOT, POS_KEYCODE_MODIFIER_NONE },
  { "/", KEY_SLASH, POS_KEYCODE_MODIFIER_NONE },
  { ":", KEY_SEMICOLON, POS_KEYCODE_MODIFIER_SHIFT },
  { ";", KEY_SEMICOLON, POS_KEYCODE_MODIFIER_NONE },
  { "=", KEY_EQUAL, POS_KEYCODE_MODIFIER_NONE },
  { "?", KEY_SLASH, POS_KEYCODE_MODIFIER_SHIFT },
  { "@", KEY_2, POS_KEYCODE_MODIFIER_SHIFT },
  { "A", KEY_A, POS_KEYCODE_MODIFIER_SHIFT },
  { "B", KEY_B, POS_KEYCODE_MODIFIER_SHIFT },
  { "C", KEY_C, POS_KEYCODE_MODIFIER_SHIFT },
  { "D", KEY_D, POS_KEYCODE_MODIFIER_SHIFT },
  { "E", KEY_E, POS_KEYCODE_MODIFIER_SHIFT },
  { "F", KEY_F, POS_KEYCODE_MODIFIER_SHIFT },
  { "G", KEY_G, POS_KEYCODE_MODIFIER_SHIFT },
  { "H", KEY_H, POS_KEYCODE_MODIFIER_SHIFT },
  { "I", KEY_I, POS_KEYCODE_MODIFIER_SHIFT },
  { "J", KEY_J, POS_KEYCODE_MODIFIER_SHIFT },
  { "K", KEY_K, POS_KEYCODE_MODIFIER_SHIFT },
  { "L", KEY_L, POS_KEYCODE_MODIFIER_SHIFT },
  { "M", KEY_M, POS_KEYCODE_MODIFIER_SHIFT },
  { "N", KEY_N, POS_KEYCODE_MODIFIER_SHIFT },
  { "O", KEY_O, POS_KEYCODE_MODIFIER_SHIFT },
  { "P", KEY_P, POS_KEYCODE_MODIFIER_SHIFT },
  { "Q", KEY_Q, POS_KEYCODE_MODIFIER_SHIFT },
  { "R", KEY_R, POS_KEYCODE_MODIFIER_SHIFT },
  { "S", KEY_S, POS_KEYCODE_MODIFIER_SHIFT },
  { "T", KEY_T, POS_KEYCODE_MODIFIER_SHIFT },
  { "U", KEY_U, POS_KEYCODE_MODIFIER_SHIFT },
  { "V", KEY_V, POS_KEYCODE_MODIFIER_SHIFT },
  { "W", KEY_W, POS_KEYCODE_MODIFIER_SHIFT },
  { "X", KEY_X, POS_KEYCODE_MODIFIER_SHIFT },
  { "Y", KEY_Y, POS_KEYCODE_MODIFIER_SHIFT },
  { "Z", KEY_Z, POS_KEYCODE_MODIFIER_SHIFT },
  { "[", KEY_LEFTBRACE, POS_KEYCODE_MODIFIER_NONE },
  { "\"", KEY_APOSTROPHE, POS_KEYCODE_MODIFIER_SHIFT },
  { "\'", KEY_GRAVE, POS_KEYCODE_MODIFIER_NONE },
  { "\\", KEY_BACKSLASH, POS_KEYCODE_MODIFIER_NONE },
  { "]", KEY_RIGHTBRACE, POS_KEYCODE_MODIFIER_NONE },
  { "^", KEY_6, POS_KEYCODE_MODIFIER_SHIFT },
  { "_", KEY_MINUS, POS_KEYCODE_MODIFIER_SHIFT },
  { "a", KEY_A, POS_KEYCODE_MODIFIER_NONE },
  { "b", KEY_B, POS_KEYCODE_MODIFIER_NONE },
  { "c", KEY_C, POS_KEYCODE_MODIFIER_NONE },
  { "d", KEY_D, POS_KEYCODE_MODIFIER_NONE },
  { "e", KEY_E, POS_KEYCODE_MODIFIER_NONE },
  { "f", KEY_F, POS_KEYCODE_MODIFIER_NONE },
  { "g", KEY_G, POS_KEYCODE_MODIFIER_NONE },
  { "h", KEY_H, POS_KEYCODE_MODIFIER_NONE },
  { "i", KEY_I, POS_KEYCODE_MODIFIER_NONE },
  { "j", KEY_J, POS_KEYCODE_MODIFIER_NONE },
  { "k", KEY_K, POS_KEYCODE_MODIFIER_NONE },
  { "l", KEY_L, POS_KEYCODE_MODIFIER_NONE },
  { "m", KEY_M, POS_KEYCODE_MODIFIER_NONE },
  { "n", KEY_N, POS_KEYCODE_MODIFIER_NONE },
  { "o", KEY_O, POS_KEYCODE_MODIFIER_NONE },
  { "p", KEY_P, POS_KEYCODE_MODIFIER_NONE },
  { "q", KEY_Q, POS_KEYCODE_MODIFIER_NONE },
  { "r", KEY_R, POS_KEYCODE_MODIFIER_NONE },
  { "s", KEY_S, POS_KEYCODE_MODIFIER_NONE },
  { "t", KEY_T, POS_KEYCODE_MODIFIER_NONE },
  { "u", KEY_U, POS_KEYCODE_MODIFIER_NONE },
  { "v", KEY_V, POS_KEYCODE_MODIFIER_NONE },
  { "w", KEY_W, POS_KEYCODE_MODIFIER_NONE },
  { "x", KEY_X, POS_KEYCODE_MODIFIER_NONE },
  { "y", KEY_Y, POS_KEYCODE_MODIFIER_NONE },
  { "z", KEY_Z, POS_KEYCODE_MODIFIER_NONE },
  { "{", KEY_LEFTBRACE, POS_KEYCODE_MODIFIER_SHIFT },
  { "|", KEY_BACKSLASH, POS_KEYCODE_MODIFIER_SHIFT },
  { "}", KEY_RIGHTBRACE, POS_KEYCODE_MODIFIER_SHIFT },
  { "~", KEY_GRAVE, POS_KEYCODE_MODIFIER_SHIFT },


  { "℅", KEY_1, POS_KEYCODE_MODIFIER_ALTGR },
  { "®", KEY_2, POS_KEYCODE_MODIFIER_ALTGR },
  { "©", KEY_3, POS_KEYCODE_MODIFIER_ALTGR },
  { "¢", KEY_4, POS_KEYCODE_MODIFIER_ALTGR },
  { "€", KEY_5, POS_KEYCODE_MODIFIER_ALTGR },
  { "¥", KEY_6, POS_KEYCODE_MODIFIER_ALTGR },
  { "™", KEY_7, POS_KEYCODE_MODIFIER_ALTGR },

  { NULL },
};

typedef struct {
  guint gdk_keycode;
  guint keycode;
} PosGdkKeycode;

static const PosGdkKeycode keycodes_gdk_us[] = {
  { GDK_KEY_grave, KEY_GRAVE },
  { GDK_KEY_0, KEY_0 },
  { GDK_KEY_1, KEY_1 },
  { GDK_KEY_2, KEY_2 },
  { GDK_KEY_3, KEY_3 },
  { GDK_KEY_4, KEY_4 },
  { GDK_KEY_5, KEY_5 },
  { GDK_KEY_6, KEY_6 },
  { GDK_KEY_7, KEY_7 },
  { GDK_KEY_8, KEY_8 },
  { GDK_KEY_9, KEY_9 },
  { GDK_KEY_minus, KEY_MINUS },
  { GDK_KEY_equal, KEY_EQUAL },

  { GDK_KEY_q, KEY_Q },
  { GDK_KEY_w, KEY_W },
  { GDK_KEY_e, KEY_E },
  { GDK_KEY_r, KEY_R },
  { GDK_KEY_t, KEY_T },
  { GDK_KEY_y, KEY_Y },
  { GDK_KEY_u, KEY_U },
  { GDK_KEY_i, KEY_I },
  { GDK_KEY_o, KEY_O },
  { GDK_KEY_p, KEY_P },
  { GDK_KEY_bracketleft, KEY_LEFTBRACE },
  { GDK_KEY_bracketright, KEY_RIGHTBRACE },
  { GDK_KEY_backslash, KEY_BACKSLASH },

  { GDK_KEY_a, KEY_A },
  { GDK_KEY_s, KEY_S },
  { GDK_KEY_d, KEY_D },
  { GDK_KEY_f, KEY_F },
  { GDK_KEY_g, KEY_G },
  { GDK_KEY_h, KEY_H },
  { GDK_KEY_j, KEY_J },
  { GDK_KEY_k, KEY_K },
  { GDK_KEY_l, KEY_L },
  { GDK_KEY_semicolon, KEY_SEMICOLON },
  { GDK_KEY_apostrophe, KEY_APOSTROPHE },

  { GDK_KEY_z, KEY_Z },
  { GDK_KEY_x, KEY_X },
  { GDK_KEY_c, KEY_C },
  { GDK_KEY_v, KEY_V },
  { GDK_KEY_b, KEY_B },
  { GDK_KEY_n, KEY_N },
  { GDK_KEY_m, KEY_M },
  { GDK_KEY_comma, KEY_COMMA },
  { GDK_KEY_period, KEY_DOT },
  { GDK_KEY_slash, KEY_SLASH },
  { GDK_KEY_space, KEY_SPACE },
};


static gboolean
is_valid_for_electron_apps (int eventcode)
{
  /*
   * Electron / Chromium assumes it can just use the raw event code for some keys
   * Make sure we don't put keymap symbols there
   */
  switch (eventcode) {
    /* 10 */
  case KEY_BACKSPACE:
    /* 50 */
  case KEY_LEFTALT:
  case KEY_CAPSLOCK:
  case KEY_F1:
    /* 60 */
  case KEY_F2:
  case KEY_F3:
  case KEY_F4:
  case KEY_F5:
  case KEY_F6:
  case KEY_F7:
  case KEY_F8:
  case KEY_F9:
  case KEY_F10:
    /* 80 */
  case 84 /* unused */:
  case KEY_F11:
  case KEY_F12:
    /* 90 */
  case KEY_KPJPCOMMA:
  case KEY_RIGHTCTRL:
    /* 100 */
  case KEY_RIGHTALT:
  case KEY_LINEFEED:
  case KEY_HOME:
  case KEY_UP:
  case KEY_LEFT:
  case KEY_RIGHT:
  case KEY_END:
  case KEY_DOWN:
    /* 110 */
  case KEY_DELETE:
  case KEY_MACRO:
    /* 120 */
  case KEY_COMPOSE:
    /* 130 */
  case KEY_PROPS:
  case KEY_HELP:
  case KEY_MENU:
  case KEY_SETUP:
    /* 140 */
  case KEY_SLEEP:
  case KEY_SENDFILE:
  case KEY_DELETEFILE:
  case KEY_XFER:
  case KEY_PROG1:
  case KEY_PROG2:
    /* 150 */
  case KEY_MSDOS:
  case KEY_ROTATE_DISPLAY:
  case KEY_CYCLEWINDOWS:
  case KEY_BOOKMARKS:
  case KEY_COMPUTER:
  case KEY_BACK:
  case KEY_FORWARD:
    /* 160 */
  case KEY_CLOSECD:
  case KEY_EJECTCLOSECD:
    /* 170 */
  case KEY_ISO:
  case KEY_HOMEPAGE:
  case KEY_REFRESH:
  case KEY_MOVE:
  case KEY_EDIT:
  case KEY_SCROLLDOWN:
  case KEY_SCROLLUP:
    /* 180 */
  case KEY_NEW:
    return FALSE;
  default:
    return TRUE;
  }
}


static int
get_next_valid_keycode (int keycode)
{
  while (!is_valid_for_electron_apps (keycode)) {
    keycode++;
  }

  return keycode;
}

typedef struct _PosKeysym {
  char *key;
  char *keysym;
} PosKeysym;


static const char *
get_keysym (char *key, PosKeysym keysyms[])
{
  if (keysyms == NULL)
    return NULL;

  for (int i = 0; keysyms[i].key; i++) {
    if (g_strcmp0 (keysyms[i].key, key) == 0)
      return keysyms[i].keysym;
  }

  return NULL;
}


static char *
pos_vk_driver_build_keymap (PosVkDriver *self, PosKeysym extra_keysms[])
{
  char *keymap_str;
  GHashTableIter iter;
  gpointer key, value;
  GString *keymap;
  g_autoptr (GString) keycodes = g_string_new (NULL);
  g_autoptr (GString) symbols = g_string_new (NULL);

  g_hash_table_iter_init (&iter, self->keycodes);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    char *symbol = key;
    PosKeycode *keycode = value;
    gunichar val;

    /*
     * Add a keycode for each symbol/key. The keycodes are arbitrary but must match
     * what we emit via `pos_vk_driver_key_down()`. As usual with xkb keymaps the keycodes
     * have an offset of `8`.
     */
    g_string_append_printf (keycodes, "    <I%.3d>         = %d;\n",
                            keycode->keycode + 8,
                            keycode->keycode + 8);
    /*
     * For characters map each symbol as 'U<UCS4>' to the keycodes added above
     * See /usr/include/X11/keysymdef.h
     */
    if (!g_str_has_prefix (symbol, "KEY_")) {
      val = g_utf8_get_char (symbol);

      if ((val >= 0x20 && val <= 0x7E) ||
          (val >= 0xa0 && val <= 0x10ffff)) {
        g_string_append_printf (symbols,
                                "    key <I%.3d> { [ U%.4X ] };\n", keycode->keycode + 8, val);
      } else {
        g_warning ("Can't convert '%s' to keysym", symbol);
      }
    } else {
      /* For non characters like cursor keys look up the keysym name */
      const char *keysym;

      keysym = get_keysym (symbol, extra_keysms);
      if (keysym == NULL) {
        g_warning ("Key '%s' has no mapping", symbol);
        continue;
      }
      g_string_append_printf (symbols,
                              "    key <I%.3d> { [ %s ] };\n", keycode->keycode + 8, keysym);
    }
  }

  /* Put everything into the the keymap */
  keymap = g_string_new (  "xkb_keymap {\n"
                           "  xkb_keycodes \"pos\" {\n"
                           "    minimum = 8;\n"
                           "    maximum = 255;\n");
  g_string_append (keymap, keycodes->str);
  g_string_append (keymap, "    indicator 1 = \"Caps Lock\";\n");
  g_string_append (keymap, "  };\n"
                           "  xkb_types \"pos\" {\n"
                           "    virtual_modifiers Pos;\n"
                           "    type \"ONE_LEVEL\" {\n"
                           "      modifiers= none;\n"
                           "      level_name[Level1]= \"Any\";\n"
                           "    };\n"
                           "    type \"TWO_LEVEL\" {\n"
                           "      level_name[Level1]= \"Base\";\n"
                           "    };\n"
                           "      type \"ALPHABETIC\" {\n"
                           "      level_name[Level1]= \"Base\";\n"
                           "    };\n"
                           "      type \"KEYPAD\" {\n"
                           "      level_name[Level1]= \"Base\";\n"
                           "    };\n"
                           "      type \"SHIFT+ALT\" {\n"
                           "      level_name[Level1]= \"Base\";\n"
                           "    };\n"
                           "  };\n"
                           "\n"
                           "  xkb_compatibility \"pos\" {\n"
                           "    interpret Any+AnyOf(all) {\n"
                           "       action= SetMods(modifiers=modMapMods,clearLocks);\n"
                           "     };\n"
                           "  };\n"
                           "\n"
                           "  xkb_symbols \"pos\" {\n"
                           "    name[group1]=\"English (US)\";\n");
  g_string_append (keymap, symbols->str);
  g_string_append (keymap, "  };\n"
                           "};\n");
  keymap_str = g_string_free (keymap, FALSE);
  g_debug ("keymap: %s", keymap_str);

  return keymap_str;
}


static void
pos_vk_driver_update_terminal_keycodes (PosVkDriver *self)
{
  g_clear_pointer (&self->keycodes, g_hash_table_destroy);

  self->keycodes = g_hash_table_new (g_str_hash, g_str_equal);
  for (int i = 0; i < G_N_ELEMENTS (keycodes_common); i++)
    g_hash_table_insert (self->keycodes, keycodes_common[i].key,  (gpointer)&keycodes_common[i]);

  for (int i = 0; keycodes_terminal[i].key != NULL; i++)
    g_hash_table_insert (self->keycodes, keycodes_terminal[i].key,  (gpointer)&keycodes_terminal[i]);
}


static void
pos_vk_driver_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  PosVkDriver *self = POS_VK_DRIVER (object);

  switch (property_id) {
  case PROP_VIRTUAL_KEYBOARD:
    self->virtual_keyboard = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_vk_driver_finalize (GObject *object)
{
  PosVkDriver *self = POS_VK_DRIVER (object);

  g_hash_table_destroy (self->keycodes);
  g_clear_pointer (&self->layout_id, g_free);
  g_clear_object (&self->virtual_keyboard);

  G_OBJECT_CLASS (pos_vk_driver_parent_class)->finalize (object);
}


static void
pos_vk_driver_class_init (PosVkDriverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = pos_vk_driver_set_property;
  object_class->finalize = pos_vk_driver_finalize;

  props[PROP_VIRTUAL_KEYBOARD] =
    g_param_spec_object ("virtual-keyboard", "", "",
                         POS_TYPE_VIRTUAL_KEYBOARD,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
pos_vk_driver_init (PosVkDriver *self)
{
  self->gdk_keycodes = g_hash_table_new (g_direct_hash, g_direct_equal);
  for (int i = 0; i < G_N_ELEMENTS (keycodes_gdk_us); i++)
    g_hash_table_insert (self->gdk_keycodes, GUINT_TO_POINTER (keycodes_gdk_us[i].gdk_keycode),
                         GUINT_TO_POINTER (keycodes_gdk_us[i].keycode));
}


PosVkDriver *
pos_vk_driver_new (PosVirtualKeyboard *virtual_keyboard)
{
  g_return_val_if_fail (POS_IS_VIRTUAL_KEYBOARD (virtual_keyboard), NULL);

  return POS_VK_DRIVER (g_object_new (POS_TYPE_VK_DRIVER,
                                      "virtual-keyboard", virtual_keyboard,
                                      NULL));
}

/**
 * pos_vk_driver_key_down:
 * @self: The virtual keyboard driver
 * @key: The key to press
 * @modifiers: Additional modifiers
 *
 * Submits a key via the virtual keyboard protocol. This handles
 * capital letters implicitly by adding the correctmodifier. Same is true for several
 * special letters on the terminal layout that require AltGr.
 *
 * One can pass additional modifiers to trigger e.g. <ctrl>+<character> compbos.
 */
void
pos_vk_driver_key_down (PosVkDriver *self, const char *key, PosKeycodeModifier modifiers)
{
  PosKeycode *keycode;
  guint vk_modifiers = 0;

  g_return_if_fail (POS_IS_VK_DRIVER (self));

  keycode = g_hash_table_lookup (self->keycodes, key);
  g_return_if_fail (keycode);

  modifiers |= keycode->modifiers;
  if (modifiers & POS_KEYCODE_MODIFIER_SHIFT)
    vk_modifiers |= POS_VIRTUAL_KEYBOARD_MODIFIERS_SHIFT;
  if (modifiers & POS_KEYCODE_MODIFIER_CTRL)
    vk_modifiers |= POS_VIRTUAL_KEYBOARD_MODIFIERS_CTRL;
  if (modifiers & POS_KEYCODE_MODIFIER_ALT)
    vk_modifiers |= POS_VIRTUAL_KEYBOARD_MODIFIERS_ALT;
  if (modifiers & POS_KEYCODE_MODIFIER_ALTGR)
    vk_modifiers |= POS_VIRTUAL_KEYBOARD_MODIFIERS_ALTGR;

  /* FIXME: preserve current modifiers */
  pos_virtual_keyboard_set_modifiers (self->virtual_keyboard,
                                      vk_modifiers,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE);

  pos_virtual_keyboard_press (self->virtual_keyboard, keycode->keycode);
}

void
pos_vk_driver_key_up (PosVkDriver *self, const char *key)
{
  PosKeycode *keycode;

  g_return_if_fail (POS_IS_VK_DRIVER (self));

  keycode = g_hash_table_lookup (self->keycodes, key);
  g_return_if_fail (keycode);

  pos_virtual_keyboard_release (self->virtual_keyboard, keycode->keycode);
  pos_virtual_keyboard_set_modifiers (self->virtual_keyboard,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE);
}

/**
 * pos_vk_driver_key_press_gdk:
 * @self: The virtual keyboard driver
 * @gdk_keycode: The keycode as used by GDKeventkey
 * @modifiers: The modifiers as used by GDK
 *
 * Given a GDK keycode and modifier simulate a press of that key.
 * We only handle the US layout. Improvements are welcome.
 */
void
pos_vk_driver_key_press_gdk (PosVkDriver *self, guint gdk_keycode, GdkModifierType modifiers)
{
  PosVirtualKeyboardModifierFlags flags = POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE;
  guint key;

  g_return_if_fail (POS_IS_VK_DRIVER (self));

  if (modifiers & GDK_SHIFT_MASK)
    flags |= POS_VIRTUAL_KEYBOARD_MODIFIERS_SHIFT;
  if (modifiers & GDK_CONTROL_MASK)
    flags |= POS_VIRTUAL_KEYBOARD_MODIFIERS_CTRL;
  if (modifiers & GDK_META_MASK)
    flags |= POS_VIRTUAL_KEYBOARD_MODIFIERS_ALT;
  if (modifiers & GDK_MOD1_MASK)
    flags |= POS_VIRTUAL_KEYBOARD_MODIFIERS_ALT;
  if (modifiers & GDK_SUPER_MASK)
    flags |= POS_VIRTUAL_KEYBOARD_MODIFIERS_SUPER;

  key = GPOINTER_TO_UINT (g_hash_table_lookup (self->gdk_keycodes, GUINT_TO_POINTER (gdk_keycode)));

  if (!key) {
    GdkKeymap *gdk_keymap = gdk_keymap_get_for_display (gdk_display_get_default ());
    g_autofree GdkKeymapKey *keys = NULL;
    int n_keys = 0;

    if (!gdk_keymap_get_entries_for_keyval (gdk_keymap, gdk_keycode, &keys, &n_keys) || !n_keys) {
      g_warning ("Couldn't convert keycode 0x%x", gdk_keycode);
      return;
    }
    key = keys[0].keycode - 8;
  }

  /* FIXME: preserve current modifiers */
  pos_virtual_keyboard_set_modifiers (self->virtual_keyboard,
                                      flags,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE);

  pos_virtual_keyboard_press (self->virtual_keyboard, key);
  pos_virtual_keyboard_release (self->virtual_keyboard, key);

  pos_virtual_keyboard_set_modifiers (self->virtual_keyboard,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE,
                                      POS_VIRTUAL_KEYBOARD_MODIFIERS_NONE);
}

/**
 * pos_vk_driver_set_terminal_keymap:
 * @self: The vk driver
 *
 * Sets the terminal keymap.
 */
void
pos_vk_driver_set_terminal_keymap (PosVkDriver *self)
{
  const char *layout_id = "terminal";
  const char *keymap;
  g_autoptr (GBytes) data = NULL;
  gsize size;

  g_return_if_fail (POS_IS_VK_DRIVER (self));

  if (g_strcmp0 (layout_id, self->layout_id) == 0)
    return;

  g_debug ("Setting terminal keymap");
  g_clear_pointer (&self->layout_id, g_free);
  self->layout_id = g_strdup (layout_id);
  data = g_resources_lookup_data ("/mobi/phosh/stevia/keymap.txt", 0, NULL);
  g_assert (data);
  keymap = (char*) g_bytes_get_data (data, &size);

  pos_virtual_keyboard_set_keymap (self->virtual_keyboard, keymap);

  pos_vk_driver_update_terminal_keycodes (self);
}

/**
 * pos_vk_driver_set_keymap_symbols:
 * @self: The vk driver
 * @layout_id: The layout_id that identifies this keymap
 * @symbols: The symbols for the keymap
 *
 * Generates and installs a keymap based on the given symbols.
 */
void
pos_vk_driver_set_keymap_symbols (PosVkDriver *self, const char *layout_id, const char * const *symbols)
{
  g_autofree char *keymap_str = NULL;
  int keycode = KEY_1;
  /* Extra keysyms to add to each keymap */
  /* TODO: make dynamic */
  PosKeysym extra_keysyms[] = {
    { "KEY_ENTER", "Return" },
    { "KEY_BACKSPACE", "BackSpace" },
    { "KEY_LEFT", "Left" },
    { "KEY_RIGHT", "Right" },
    { "KEY_UP", "Up" },
    { "KEY_DOWN", "Down"},
    { NULL, NULL } };

  g_return_if_fail (POS_IS_VK_DRIVER (self));
  g_return_if_fail (layout_id);
  g_return_if_fail (symbols);

  if (g_strcmp0 (layout_id, self->layout_id) == 0)
    return;

  g_debug ("Switching to %s", layout_id);
  g_clear_pointer (&self->keycodes, g_hash_table_destroy);
  self->keycodes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  for (int n = 0; symbols[n]; n++, keycode++) {
    PosKeycode *pos_keycode = g_new0 (PosKeycode, 1);
    const char *symbol = symbols[n];

    keycode = get_next_valid_keycode (keycode);

    pos_keycode->keycode = keycode;
    g_hash_table_insert (self->keycodes, g_strdup (symbol), pos_keycode);
  }

  for (int n = 0; extra_keysyms[n].key; n++, keycode++) {
    PosKeycode *pos_keycode = g_new0 (PosKeycode, 1);

    keycode = get_next_valid_keycode (keycode);

    pos_keycode->keycode = keycode;
    g_hash_table_insert (self->keycodes, g_strdup (extra_keysyms[n].key), pos_keycode);
  }

  keymap_str = pos_vk_driver_build_keymap (self, extra_keysyms);
  pos_virtual_keyboard_set_keymap (self->virtual_keyboard, keymap_str);

  g_clear_pointer (&self->layout_id, g_free);
  self->layout_id = g_strdup (layout_id);
}

/**
 * pos_vk_driver_set_overlay_keymap:
 * @self: The virtual keyboard driver
 * @symbols: The symbols to create a keymap for or %NULL
 *
 * Installs a temporary overlay keymap with the given symbols. If called multiple times
 * the current overlay keymap will be replaced.
 *
 * This is very similar to `pos_vk_driver_set_keymap_symbols` but does not require
 * a layout-id nor does it add any extra keys.
 */
void
pos_vk_driver_set_overlay_keymap (PosVkDriver *self, const char *const *symbols)
{
  g_autofree char *keymap_str = NULL;
  int keycode = KEY_1;

  g_return_if_fail (POS_IS_VK_DRIVER (self));
  g_return_if_fail (symbols);

  g_clear_pointer (&self->keycodes, g_hash_table_destroy);
  self->keycodes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  for (int n = 0; symbols[n]; n++, keycode++) {
    PosKeycode *pos_keycode = g_new0 (PosKeycode, 1);
    const char *symbol = symbols[n];

    keycode = get_next_valid_keycode (keycode);

    pos_keycode->keycode = keycode;
    g_hash_table_insert (self->keycodes, g_strdup (symbol), pos_keycode);
  }
  keymap_str = pos_vk_driver_build_keymap (self, NULL);

  g_clear_pointer (&self->layout_id, g_free);
  pos_virtual_keyboard_set_keymap (self->virtual_keyboard, keymap_str);
}


PosKeycodeModifier
pos_vk_driver_convert_modifiers (PosVkDriver *self, GdkModifierType gdk_modifier)
{
  PosKeycodeModifier modifier = POS_KEYCODE_MODIFIER_NONE;

  if (gdk_modifier & GDK_CONTROL_MASK)
    modifier |= POS_KEYCODE_MODIFIER_CTRL;

  if (gdk_modifier & GDK_MOD1_MASK)
    modifier |= POS_KEYCODE_MODIFIER_ALT;

  return modifier;
}
