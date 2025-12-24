/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <pos-virtual-keyboard.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define POS_TYPE_VK_DRIVER (pos_vk_driver_get_type ())

G_DECLARE_FINAL_TYPE (PosVkDriver, pos_vk_driver, POS, VK_DRIVER, GObject)

typedef enum {
  POS_KEYCODE_MODIFIER_NONE =  0,
  POS_KEYCODE_MODIFIER_SHIFT = 1 << 0,
  POS_KEYCODE_MODIFIER_CTRL =  1 << 1,
  POS_KEYCODE_MODIFIER_ALT  =  1 << 2,
  POS_KEYCODE_MODIFIER_ALTGR = 1 << 3,
} PosKeycodeModifier;

PosVkDriver *pos_vk_driver_new (PosVirtualKeyboard *self);
void         pos_vk_driver_key_down (PosVkDriver        *self,
                                     const char         *key,
                                     PosKeycodeModifier  modifiers);
void        pos_vk_driver_key_up (PosVkDriver *self, const char *key);
void        pos_vk_driver_key_press_gdk (PosVkDriver    *self,
                                         guint           gdk_keycode,
                                         GdkModifierType modifiers);
void        pos_vk_driver_set_terminal_keymap (PosVkDriver       *self);
void        pos_vk_driver_set_keymap_symbols (PosVkDriver        *self,
                                              const char         *layout_id,
                                              const char * const *symbols);
void        pos_vk_driver_set_overlay_keymap (PosVkDriver *self, const char * const *symbols);
PosKeycodeModifier
            pos_vk_driver_convert_modifiers (PosVkDriver *self, GdkModifierType modifier);

G_END_DECLS
