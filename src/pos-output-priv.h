/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3-or-later
 */
#pragma once

#include "pos-output.h"

G_BEGIN_DECLS

void pos_output_set_logical_width (PosOutput *self, guint width);
void pos_output_set_logical_height (PosOutput *self, guint height);
void pos_output_set_physical_width (PosOutput *self, guint width);
void pos_output_set_physical_height (PosOutput *self, guint height);
void pos_output_set_transform (PosOutput *self, enum wl_output_transform transform);

G_END_DECLS
