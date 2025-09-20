/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define POS_TYPE_COMPLETER_BASE (pos_completer_base_get_type ())

G_DECLARE_DERIVABLE_TYPE (PosCompleterBase, pos_completer_base, POS, COMPLETER_BASE, GObject)

struct _PosCompleterBaseClass {
  GObjectClass parent_class;
};

PosCompleterBase *      pos_completer_base_new (void);
GStrv                   pos_completer_base_get_additional_results (PosCompleterBase *self,
                                                                   const char       *match,
                                                                   guint             max_results);
void                    pos_completer_base_set_surrounding_text (PosCompleterBase *iface,
                                                                 const char       *before_text,
                                                                 const char       *after_text);
const char *            pos_completer_base_get_before_text (PosCompleterBase *self);
const char *            pos_completer_base_get_after_text (PosCompleterBase *self);

G_END_DECLS
