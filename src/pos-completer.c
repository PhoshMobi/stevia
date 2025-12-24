/*
 * Copyright (C) 2023-2024 The Phosh Developers
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-completer"

#include "pos-config.h"

#include "pos-completer.h"
#include "pos-completer-priv.h"
#include "util.h"

#include "gmobile.h"

#include <ctype.h>

/**
 * PosCompleter:
 *
 * Interface for completion engines.
 *
 * Completion engines implement this interface so they can be used by
 * the OSK to complete or correct user input. Users of this interface
 * should fill the [property@Completer:preedit] with user input and
 * will get a list of possible completions in the `completions`
 * property. Note that this can happen asynchronously as getting the
 * completions can take time.
 *
 * The completer can also instruct the user of this interface to
 * commit a given text via the [signal@Completer::commit-string]
 * signal. Implementations should emit this signal on word breaking
 * characters to either take the user input as is or force
 * "aggressive" autocorrection (picking a correction on the users
 * behalf).
 */

G_DEFINE_INTERFACE (PosCompleter, pos_completer, G_TYPE_OBJECT)

/* TODO: all the brackets, also language dependent */
static const char * const completion_end_symbols[] = {
  /* whitespace */
  " ",
  "\t",
  "\n",
  /* non-whitespace */
  ".",
  ",",
  ";",
  ":",
  "?",
  "!",
  "(", ")",
  "{", "}",
  "[", "]",
  NULL,
};


GQuark
pos_completer_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("pos-completer");

  return quark;
}


void
pos_completer_default_init (PosCompleterInterface *iface)
{
  GType iface_type = G_TYPE_FROM_INTERFACE (iface);

  /**
   * PosCompleter:name:
   *
   * The name of this completer
   */
  g_object_interface_install_property (
    iface, g_param_spec_string ("name", "", "", NULL, G_PARAM_READABLE));

  /**
   * PosCompleter:preedit:
   *
   * The preedit is not yet submitted text at the current cursor position.
   */
  g_object_interface_install_property (
    iface, g_param_spec_string ("preedit", "", "", NULL, G_PARAM_READWRITE));

  /**
   * PosCompleter:completions:
   *
   * The list of completions for a given preedit.
   */
  g_object_interface_install_property (
    iface, g_param_spec_boxed ("completions", "", "", G_TYPE_STRV, G_PARAM_READABLE));

  /**
   * PosCompleter::commit-string:
   * @iface: The completer interface
   * @string: The commit string
   * @before: Number of bytes to delete before the cursor
   * @after: Number of bytes to delete after the cursor
   *
   * The completer wants the given text to be committed as is. This can
   * happen when the completer encounters a word separating character
   * (e.g. space). `preedit` will be set to empty in this case.
   */
  g_signal_new ("commit-string",
                iface_type,
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE,
                3,
                G_TYPE_STRING,
                G_TYPE_INT,
                G_TYPE_INT);
  /**
   * PosCompleter::update
   * @iface: The completer interface
   * @string: The new preedit string
   * @before: Number of bytes before the cursor to delete
   * @after: Number of bytes after the cursor to delete
   *
   * The completer changed it's preedit to the given string
   * and wants the given number of bytes before and after
   * the preedit removed.
   *
   * The completer must emit the signal before the corresponding `notify::preedit`
   * TODO: add marshallers.
   */
  g_signal_new ("update",
                iface_type,
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE,
                3,
                G_TYPE_STRING,
                G_TYPE_UINT,
                G_TYPE_UINT);
}

/**
 * pos_completer_get_name:
 * @self: the completer
 *
 * Returns the completers name
 *
 * Returns: (transfer none): the name
 */
const char *
pos_completer_get_name (PosCompleter *self)
{
  PosCompleterInterface *iface;

  g_return_val_if_fail (POS_IS_COMPLETER (self), NULL);

  iface = POS_COMPLETER_GET_IFACE (self);
  return iface->get_name (self);
}

/**
 * pos_completer_feed_symbol:
 * @self: The completer
 * @symbol: The symbol to process.
 *
 * Feeds a symbol to be processed by the completer.
 *
 * Returns: %TRUE if the symbol was processed, %FALSE otherwise.
 */
gboolean
pos_completer_feed_symbol (PosCompleter *self, const char *symbol)
{
  PosCompleterInterface *iface;

  g_return_val_if_fail (POS_IS_COMPLETER (self), FALSE);

  iface = POS_COMPLETER_GET_IFACE (self);
  g_return_val_if_fail (iface->feed_symbol != NULL, FALSE);
  return iface->feed_symbol (self, symbol);
}

/**
 * pos_completer_get_completions:
 * @self: the completer
 *
 * Returns the current possible completions.
 *
 * Returns: (transfer full): the completions
 */
GStrv
pos_completer_get_completions (PosCompleter *self)
{
  GStrv completions;

  g_return_val_if_fail (POS_IS_COMPLETER (self), NULL);
  g_object_get (self, "completions", &completions, NULL);

  return completions;
}

/**
 * pos_completer_lookup_completion:
 * @self: the completer
 * @completion: The completion to lookup
 *
 * Given a given completion string lookup what should actually
 * be completed. This allows completers to e.g. provide the full
 * word as completion "handle" and fill in just the last chars as
 * actual completions.
 *
 * Returns: the actual completion string
 */
const char *
pos_completer_lookup_completion (PosCompleter *self, const char *completion)
{
  PosCompleterInterface *iface;

  g_return_val_if_fail (POS_IS_COMPLETER (self), NULL);

  iface = POS_COMPLETER_GET_IFACE (self);
  if (!iface->lookup_completion)
    return NULL;

  return (iface->lookup_completion)(self, completion);
}

/**
 * pos_completer_get_preedit:
 * @self: the completer
 *
 * Returns the current preedit
 *
 * Returns: (transfer none): the current preedit
 */
const char *
pos_completer_get_preedit (PosCompleter *self)
{
  PosCompleterInterface *iface;

  g_return_val_if_fail (POS_IS_COMPLETER (self), NULL);

  iface = POS_COMPLETER_GET_IFACE (self);
  g_return_val_if_fail (iface->get_preedit != NULL, NULL);

  return iface->get_preedit (self);
}

/**
 * pos_completer_set_preedit:
 * @self: the completer
 * @preedit: the preedit text to set
 *
 * Sets the current preedit. The preedit is the current word under
 * completion.
 */
void
pos_completer_set_preedit (PosCompleter *self, const char *preedit)
{
  PosCompleterInterface *iface;

  g_return_if_fail (POS_IS_COMPLETER (self));

  iface = POS_COMPLETER_GET_IFACE (self);
  g_return_if_fail (iface->set_preedit != NULL);

  return iface->set_preedit (self, preedit);
}


/**
 * pos_completer_set_surrounding_text:
 * @self: the completer
 * @before_text: the text before the cursor
 * @after_text: the text after the cursor
 *
 * Set the text before and after the current cursor position. This can
 * be used by the completer to improve the prediction.
 */
void
pos_completer_set_surrounding_text (PosCompleter *self,
                                    const char   *before_text,
                                    const char   *after_text)
{
  PosCompleterInterface *iface;

  g_return_if_fail (POS_IS_COMPLETER (self));

  iface = POS_COMPLETER_GET_IFACE (self);
  /* optional */
  if (iface->set_surrounding_text == NULL)
    return;

  return iface->set_surrounding_text (self, before_text, after_text);
}

/**
 * pos_completer_set_language:
 * @self: The completer
 * @lang:  The language
 * @region: The region
 * @error (nullable): The error location
 *
 * Let the completer pick a language based on the given language code
 * and region. If an error occurs %FALSE is returned and @error set
 * to the error.
 *
 * For a locale of `de_AT` language would be `de` and region `at`.
 *
 * Returns: %TRUE on success. On error %FALSE.
 */
gboolean
pos_completer_set_language (PosCompleter *self,
                            const char   *lang,
                            const char   *region,
                            GError      **error)
{
  PosCompleterInterface *iface;

  g_return_val_if_fail (POS_IS_COMPLETER (self), FALSE);

  iface = POS_COMPLETER_GET_IFACE (self);
  /* optional */
  if (iface->set_language == NULL)
    return TRUE;

  g_return_val_if_fail (lang, FALSE);

  return iface->set_language (self, lang, region, error);
}

/* Used by completers to simplify implementations */

/**
 * pos_completer_add_preedit:
 * @self: the completer
 * @preedit: The current preedit
 * @symbol: the symbol to add
 *
 * Processes a symbol and appends it to preedit if possible and
 * indicates whether the resulting `preedit` should be submitted.
 *
 * If `symbol` is `KEY_BACKSPACE` the last character of the the
 * preedit is automatically deleted.
 *
 * Since `KEY_ENTER` is often used to toggle actions like "submit" we
 * handle it in a special way: `TRUE` is returned but no `\n` is
 * appended so the completer can submit the raw `KEY_ENTER` and
 * actions can still trigger.
 *
 * Returns: %TRUE if preedit should be submitted as is. %FALSE otherwise.
 */
gboolean
pos_completer_add_preedit (PosCompleter *self, GString *preedit, const char *symbol)
{
  gboolean is_ws;

  g_return_val_if_fail (POS_IS_COMPLETER (self), FALSE);
  g_return_val_if_fail (symbol, FALSE);

  if (g_strcmp0 (symbol, "KEY_BACKSPACE") == 0 && preedit->len) {
    /* Remove last utf-8 character */
    const char *last = &preedit->str[preedit->len];
    const char *prev = g_utf8_find_prev_char (preedit->str, last);
    int n = last - prev;
    g_string_truncate (preedit, preedit->len - n);
    return FALSE;
  }

  /* Return/Enter is special, see above. */
  if (g_strcmp0 (symbol, "KEY_ENTER") == 0)
    return TRUE;

  /* Ignore all other special keys */
  if (g_str_has_prefix (symbol, "KEY_"))
    return FALSE;

  g_string_append (preedit, symbol);

  if (pos_completer_symbol_is_word_separator (symbol, &is_ws)) {
    if (is_ws == FALSE)
      g_string_append (preedit, " ");
    return TRUE;
  }

  return FALSE;
}

/**
 * pos_completer_get_display_name:
 * @self: The completer
 *
 * Get the completers display name
 *
 * Returns:(transfer full): The display name
 */
char *
pos_completer_get_display_name (PosCompleter *self)
{
  PosCompleterInterface *iface;

  g_return_val_if_fail (POS_IS_COMPLETER (self), NULL);

  iface = POS_COMPLETER_GET_IFACE (self);
  if (iface->get_display_name == NULL)
    return NULL;

  return iface->get_display_name (self);
}

/**
 * pos_completer_varnam_learn_accepted:
 * @iface: The completer iface
 * @word: The word to learn
 *
 * The completer should learn the given word and offer it for
 * completions in the future.
 */
void
pos_completer_learn_accepted (PosCompleter *self, const char *word)
{
  PosCompleterInterface *iface;

  g_return_if_fail (POS_IS_COMPLETER (self));

  iface = POS_COMPLETER_GET_IFACE (self);
  if (iface->learn_accepted == NULL)
    return;

  return iface->learn_accepted (self, word);
}

/**
 * pos_completer_symbol_is_word_separator:
 * @symbol: the symbol to check
 * @is_ws:(out) (nullable): whether symbol is a whitespace
 *
 * Checks if the given symbol is a word separator like a full stop,
 * exclamation mark, etc. `is_ws` will be set to %TRUE if it's a
 * whitespace character.
 *
 * Returns: %TRUE if the sumbol is a word separator. %FALSE otherwise.
 */
gboolean
pos_completer_symbol_is_word_separator (const char *symbol, gboolean *is_ws)
{
  static int nl = -1;

  if (nl < 0) {
    for (nl = 0; completion_end_symbols[nl]; nl++) {
      if (!isspace (completion_end_symbols[nl][0]))
        break;
    }
  }
  if (is_ws != NULL)
    *is_ws = FALSE;

  /* TODO: use hash table - or rather just use isalnum? */
  for (int i = 0; completion_end_symbols[i]; i++) {
    if (strcmp (symbol, completion_end_symbols[i]) == 0) {
      if (i < nl && is_ws != NULL)
        *is_ws = TRUE;
      return TRUE;
    }
  }
  return FALSE;
}

/**
 * pos_completer_grab_last_word:
 * @text: the text to grab the last word from
 * @new_text:(out): The new text with the last word removed
 * @word: The last word of text
 *
 * Scans `text` from the end and returns the last word. If `text` ends
 * with whitespace the last word is considered empty and `new_text`
 * and `word` remain unchanged.
 *
 * Returns: %TRUE `new_text` and `word` were filled.
 */
gboolean
pos_completer_grab_last_word (const char *text, char **new_text, char **word)
{
  gsize len;
  g_autofree char *symbol = NULL;

  g_return_val_if_fail (new_text && *new_text == NULL, FALSE);
  g_return_val_if_fail (word && *word == NULL, FALSE);

  /* Nothing to parse */
  if (gm_str_is_null_or_empty (text))
    return FALSE;

  /* text ends with whitespace */
  len = g_utf8_strlen (text, -1);
  symbol = g_utf8_substring (text, len - 1, len);
  if (pos_completer_symbol_is_word_separator (symbol, NULL))
    return FALSE;

  /* Get last word in text */
  for (glong start = len - 1; start >= 0; start--) {
    g_free (symbol);
    symbol = g_utf8_substring (text, start, start + 1);

    if (pos_completer_symbol_is_word_separator (symbol, NULL)) {
      *word = g_strdup (g_utf8_offset_to_pointer (text, start + 1));
      *new_text = g_utf8_substring (text, 0, start + 1);
      return TRUE;
    }
  }

  /* No whitespace in text */
  *new_text = NULL;
  *word = g_strdup (text);

  return TRUE;
}

/**
 * pos_completer_find_prev_word_break:
 * @text: the text to find the word break in
 *
 * Scans `text` from the end and returns the last word break in bytes
 * counted from the end of the string. The word break is the position
 * of the last two words separated by `completion_end_symbols`.
 *
 * If the string has trailing whitespace the amount of whitespace at the
 * end is returned instead.
 *
 * Returns: The position of the last word break or `-1` if none was found.
 */
long
pos_completer_find_prev_word_break (const char *text)
{
  char *addr;
  long len, pos, byte_len;
  g_autofree char *symbol = NULL;

  /* Nothing to parse */
  if (gm_str_is_null_or_empty (text))
    return -1;

  len = g_utf8_strlen (text, -1);
  if (!len)
    return -1;

  /* Check trailing whitespace */
  for (pos = len - 1; pos >= 0; pos--) {
    g_free (symbol);
    symbol = g_utf8_substring (text, pos, pos + 1);

    if (!pos_completer_symbol_is_word_separator (symbol, NULL))
      break;
  }

  if (pos != len - 1)
    goto out;

  for (; pos >= 0; pos--) {
    g_free (symbol);
    symbol = g_utf8_substring (text, pos, pos + 1);

    g_debug ("%s", symbol);

    if (pos_completer_symbol_is_word_separator (symbol, NULL))
      break;
  }

 out:
  byte_len = strlen (text);
  addr = g_utf8_offset_to_pointer (text, pos);
  return &(text[byte_len]) - addr - 1;
}

/**
 * pos_completer_capitalize_by_template:
 * @template: String with some characters in upper case
 * @completions: List of completion strings
 *
 * Returns a copy of completions with the capitalization of the strings set to
 * match the capitalization in the template. Use with the preedit as template
 * with completers which return only lower case completions.
 *
 * Returns: (transfer full): copy of completions with changed capitalization
 */
GStrv
pos_completer_capitalize_by_template (const char *template, const GStrv completions)
{
  gboolean has_caps;
  glong templ_len;
  g_autoptr (GStrvBuilder) builder = NULL;
  g_autofree gunichar *utemplate = NULL;

  if (completions == NULL || gm_str_is_null_or_empty (template))
    return g_strdupv (completions);

  utemplate = g_utf8_to_ucs4_fast (template, -1, &templ_len);

  has_caps = FALSE;
  for (int i = 0; i < templ_len; i++) {
    if (g_unichar_isupper (utemplate[i])) {
      has_caps = TRUE;
      break;
    }
  }
  if (!has_caps)
    return g_strdupv (completions);

  builder = g_strv_builder_new ();

  for (int i = 0; i < g_strv_length (completions); i++) {
    glong read_len, compl_len, read, written;
    g_autofree gunichar *ucompletion = NULL;
    g_autofree char *new_completion = NULL;
    g_autoptr (GError) err = NULL;

    ucompletion = g_utf8_to_ucs4 (completions[i], -1, &read_len, &compl_len, &err);
    if (ucompletion == NULL && err != NULL) {
      g_warning ("Failed to convert '%s': %s", completions[i], err->message);
      g_strv_builder_add (builder, completions[i]);
      continue;
    }

    for (int c = 0; c < MIN (templ_len, compl_len); c++) {
      if (g_unichar_isupper (utemplate[c]))
        ucompletion[c] = g_unichar_toupper (ucompletion[c]);
    }

    new_completion = g_ucs4_to_utf8 (ucompletion, compl_len, &read, &written, NULL);
    g_strv_builder_add (builder, new_completion);
  }

  return g_strv_builder_end (builder);
}
