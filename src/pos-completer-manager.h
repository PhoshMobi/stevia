/*
 * Copyright (C) 2023 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "pos-completer.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _PosCompletionInfo {
  PosCompleter *completer;
  char         *lang;
  char         *region;
  char         *display_name;
  char         *base_layout;
} PosCompletionInfo;

#define POS_TYPE_COMPLETER_MANAGER (pos_completer_manager_get_type ())

G_DECLARE_FINAL_TYPE (PosCompleterManager, pos_completer_manager, POS, COMPLETER_MANAGER, GObject)

PosCompleterManager *pos_completer_manager_new (void);
PosCompleter        *pos_completer_manager_get_default_completer (PosCompleterManager *self);
PosCompletionInfo   *pos_completer_manager_get_info              (PosCompleterManager *self,
                                                                  const char          *engine,
                                                                  const char          *lang,
                                                                  const char          *region,
                                                                  GError             **err);

void                 pos_completion_info_free                    (PosCompletionInfo   *info);

G_END_DECLS
