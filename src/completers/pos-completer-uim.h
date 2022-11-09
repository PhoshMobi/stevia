/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "pos-completer.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define POS_TYPE_COMPLETER_UIM (pos_completer_uim_get_type ())

G_DECLARE_FINAL_TYPE (PosCompleterUim, pos_completer_uim, POS, COMPLETER_UIM, GObject)

PosCompleter *pos_completer_uim_new (GError **err);

G_END_DECLS
