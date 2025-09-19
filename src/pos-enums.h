/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * PosOskWidgetLayer:
 * @POS_OSK_WIDGET_LAYER_NORMAL: lower case letters
 * @POS_OSK_WIDGET_LAYER_CAPS: capital letters
 * @POS_OSK_WIDGET_LAYER_SYMBOLS: additional symbols / numbers
 * @POS_OSK_WIDGET_LAYER_EMOJI: emojis
 *
 * The currently displayed keyboard layer.
 */
typedef enum {
  POS_OSK_WIDGET_LAYER_NORMAL = 0,
  POS_OSK_WIDGET_LAYER_CAPS   = 1,
  POS_OSK_WIDGET_LAYER_SYMBOLS = 2,
  POS_OSK_WIDGET_LAYER_SYMBOLS2 = 3,
  POS_OSK_WIDGET_LAST_LAYER = POS_OSK_WIDGET_LAYER_SYMBOLS2,
} PosOskWidgetLayer;


/**
 * PosOskWidgetMode:
 * @POS_OSK_WIDGET_MODE_KEYBOARD: Act as keyboard
 * @POS_OSK_WIDGET_MODE_CURSOR: Add as "touch pad" to move cursor
 *
 * The mode the #PosOskWidget is in
 */
typedef enum {
  POS_OSK_WIDGET_MODE_KEYBOARD = 0,
  POS_OSK_WIDGET_MODE_CURSOR   = 1,
} PosOskWidgetMode;


/**
 * PosOskKeyUse:
 * @POS_OSK_KEY_USE_KEY: a regular key (e.g. letter)
 * @POS_OSK_KEY_USE_DELETE: A key to delete text
 * @POS_OSK_KEY_USE_TOGGLE: a key that toggles another layer
 * @POS_OSK_KEY_USE_MENU: a key that opens a popup menu
 *
 * The use (purpose) of a key.
 */
typedef enum  {
  POS_OSK_KEY_USE_KEY,
  POS_OSK_KEY_USE_DELETE,
  POS_OSK_KEY_USE_TOGGLE,
  POS_OSK_KEY_USE_MENU,
} PosOskKeyUse;

/**
 * PosInputMethodTextChangeCause:
 * POS_INPUT_METHOD_TEXT_CHANGE_CAUSE_IM: the input method cause the change
 * POS_INPUT_METHOD_TEXT_CHANGE_CAUSE_NOT_IM: s.th. else caused the change
 */
typedef enum {
  POS_INPUT_METHOD_TEXT_CHANGE_CAUSE_IM = 0,
  POS_INPUT_METHOD_TEXT_CHANGE_CAUSE_NOT_IM = 1,
} PosInputMethodTextChangeCause;

/**
 * PosInputMethodPurpose:
 *
 * Input purpose as specified by text input protocol.
 */
typedef enum {
  POS_INPUT_METHOD_PURPOSE_NORMAL = 0,
  POS_INPUT_METHOD_PURPOSE_ALPHA,
  POS_INPUT_METHOD_PURPOSE_DIGITS,
  POS_INPUT_METHOD_PURPOSE_NUMBER,
  POS_INPUT_METHOD_PURPOSE_PHONE,
  POS_INPUT_METHOD_PURPOSE_URL,
  POS_INPUT_METHOD_PURPOSE_EMAIL,
  POS_INPUT_METHOD_PURPOSE_NAME,
  POS_INPUT_METHOD_PURPOSE_PASSWORD,
  POS_INPUT_METHOD_PURPOSE_PIN,
  POS_INPUT_METHOD_PURPOSE_DATE,
  POS_INPUT_METHOD_PURPOSE_TIME,
  POS_INPUT_METHOD_PURPOSE_DATETIME,
  POS_INPUT_METHOD_PURPOSE_TERMINAL,
} PosInputMethodPurpose;

/**
 * PosInputMethodHint:
 *
 * Input hint as specified by text input protocol.
 */
typedef enum {
  POS_INPUT_METHOD_HINT_NONE = 0,
  POS_INPUT_METHOD_HINT_COMPLETION,
  POS_INPUT_METHOD_HINT_SPELLCHECK,
  POS_INPUT_METHOD_HINT_AUTO_CAPITALIZATION,
  POS_INPUT_METHOD_HINT_LOWERCASE,
  POS_INPUT_METHOD_HINT_UPPERCASE,
  POS_INPUT_METHOD_HINT_TITLECASE,
  POS_INPUT_METHOD_HINT_HIDDEN_TEXT,
  POS_INPUT_METHOD_HINT_SENSITIVE_DATA,
  POS_INPUT_METHOD_HINT_LATIN,
  POS_INPUT_METHOD_HINT_MULTILINE,
} PosInputMethodHint;

/**
 * PosBackspaceMode:
 * @POS_BACKSPACE_MODE_CHAR: Delete on character
 * @POS_BACKSPACE_MODE_WORD: Delete one wordx
 *
 * How much a press of backspace should remove
 */
typedef enum {
  POS_BACKSPACE_MODE_CHAR = 1,
  POS_BACKSPACE_MODE_WORD = 2,
} PosBackspaceMode;

G_END_DECLS
