/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3-or-later
 */


#include <glib-object.h>

#pragma once

G_BEGIN_DECLS

#define POS_TYPE_APP (pos_app_get_type ())
G_DECLARE_FINAL_TYPE (PosApp, pos_app, POS, APP, GObject)

PosApp *pos_app_get_default (void);
void    pos_app_quit (PosApp *self, int exit_status);

G_END_DECLS
