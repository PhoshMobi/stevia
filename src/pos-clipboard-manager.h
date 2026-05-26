/*
 * Copyright (C) The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ext-data-control-v1-client-protocol.h"

#include <gdk/gdkwayland.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define POS_TYPE_CLIPBOARD_MANAGER (pos_clipboard_manager_get_type ())

G_DECLARE_FINAL_TYPE (PosClipboardManager, pos_clipboard_manager, POS, CLIPBOARD_MANAGER, GObject)

PosClipboardManager *pos_clipboard_manager_new (struct ext_data_control_manager_v1 *manager,
                                                struct wl_seat                     *seat);
const char          *pos_clipboard_manager_get_text (PosClipboardManager *self);
GStrv                pos_clipboard_manager_get_texts (PosClipboardManager *self);

G_END_DECLS
