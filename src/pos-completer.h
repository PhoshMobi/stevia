/*
 * Copyright (C) 2022 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define POS_COMPLETER_DEFAULT_LANG "en"
#define POS_COMPLETER_DEFAULT_REGION "us"

GQuark pos_completer_error_quark(void);

/**
 * POS_COMPLETER_ERROR:
 *
 * Error domain for completers. Errors in this domain will be from the
 * #PosCompleterError enumeration.  See #GError for more information on
 * error domains.
 **/
#define POS_COMPLETER_ERROR pos_completer_error_quark()

/**
 * PosCompleterError:
 * @POS_COMPLETER_ERROR_ENGINE_INIT: The completer engine failed to init
 * @POS_COMPLETER_ERROR_LANG_INIT: The completer engine failed to setup the language
 *
 * Errors emitted by the completion engines.
 */
typedef enum {
  POS_COMPLETER_ERROR_ENGINE_INIT = 1,
  POS_COMPLETER_ERROR_LANG_INIT = 2,
} PosCompleterError;

#define POS_TYPE_COMPLETER (pos_completer_get_type())
G_DECLARE_INTERFACE (PosCompleter, pos_completer, POS, COMPLETER, GObject)

struct _PosCompleterInterface
{
  GTypeInterface parent_iface;

  const char *   (*get_name)    (PosCompleter *self);
  gboolean       (*feed_symbol) (PosCompleter *self, const char *symbol);
  const char *   (*get_preedit) (PosCompleter *self);
  void           (*set_preedit) (PosCompleter *self, const char *preedit);
  const char *   (*get_before_text) (PosCompleter *self);
  const char *   (*get_after_text) (PosCompleter *self);
  void           (*set_surrounding_text) (PosCompleter *self,
                                          const char *before_text,
                                          const char *after_text);
  gboolean       (*set_language) (PosCompleter  *self,
                                  const char    *lang,
                                  const char    *region,
                                  GError       **error);
  char *         (*get_display_name) (PosCompleter *self);
  void           (*learn_accepted) (PosCompleter *self, const char *word);
};

/* Used by completion users */
const char    *pos_completer_get_name (PosCompleter *self);
gboolean       pos_completer_feed_symbol (PosCompleter *self, const char *symbol);
GStrv          pos_completer_get_completions (PosCompleter *self);
const char    *pos_completer_get_preedit (PosCompleter *self);
void           pos_completer_set_preedit (PosCompleter *self, const char *preedit);
const char    *pos_completer_get_before_text (PosCompleter *self);
const char    *pos_completer_get_after_text (PosCompleter *self);
void           pos_completer_set_surrounding_text (PosCompleter *self,
                                                   const char *before_text,
                                                   const char *after_text);
gboolean       pos_completer_set_language (PosCompleter  *self,
                                           const char    *lang,
                                           const char    *region,
                                           GError       **error);
char          *pos_completer_get_display_name (PosCompleter *self);
void           pos_completer_learn_accepted (PosCompleter *self, const char *word);

GStrv          pos_completer_capitalize_by_template (const char *template,
                                                     const GStrv completions);

glong          pos_completer_find_prev_word_break (const char *text);

G_END_DECLS
