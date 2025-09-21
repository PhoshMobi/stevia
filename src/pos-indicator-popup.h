/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "pos-osk-key.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define POS_TYPE_INDICATOR_POPUP (pos_indicator_popup_get_type ())

G_DECLARE_FINAL_TYPE (PosIndicatorPopup, pos_indicator_popup, POS, INDICATOR_POPUP,
                      GtkPopover)

PosIndicatorPopup *pos_indicator_popup_new (void);
void               pos_indicator_popup_show_key (PosIndicatorPopup *self, PosOskKey *key);
void               pos_indicator_popup_hide (PosIndicatorPopup *self, gboolean now);

G_END_DECLS
