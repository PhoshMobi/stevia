/*
 * Copyright (C) 2022-2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define POS_TYPE_SHORTCUT (pos_shortcut_get_type ())
GType   pos_shortcut_get_type      (void);

typedef struct     _PosShortcut PosShortcut;
PosShortcut        *pos_shortcut_ref (PosShortcut *shortcut);
void                pos_shortcut_unref (PosShortcut *shortcut);
guint               pos_shortcut_get_key (PosShortcut *shortcut);
GdkModifierType     pos_shortcut_get_modifiers (PosShortcut *shortcut);
const char         *pos_shortcut_get_label (PosShortcut *shortcut);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PosShortcut, pos_shortcut_unref);


#define POS_TYPE_SHORTCUTS_BAR (pos_shortcuts_bar_get_type ())

G_DECLARE_FINAL_TYPE (PosShortcutsBar, pos_shortcuts_bar, POS, SHORTCUTS_BAR, GtkBox)

PosShortcutsBar    *pos_shortcuts_bar_new (void);
guint               pos_shortcuts_bar_get_num_shortcuts (PosShortcutsBar *self);
GdkModifierType     pos_shortcuts_bar_get_latched_modifiers (PosShortcutsBar *self);
void                pos_shortcuts_bar_unlatch_modifiers (PosShortcutsBar *self);

G_END_DECLS
