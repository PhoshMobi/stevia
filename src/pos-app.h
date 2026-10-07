/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3-or-later
 */

#include "pos-osk-dbus.h"
#include <glib-object.h>

#pragma once

G_BEGIN_DECLS

#define POS_TYPE_APP (pos_app_get_type ())
G_DECLARE_FINAL_TYPE (PosApp, pos_app, POS, APP, GObject)

PosApp * pos_app_get_default (void);
int      pos_app_run (PosApp *self);
void     pos_app_quit (PosApp *self, int exit_status);
gboolean pos_app_setup_input_method (PosApp *self, PosOskDbus *osk_dbus);

G_END_DECLS
