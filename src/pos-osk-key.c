/*
 * Copyright (C) 2022 Purism SPC
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-osk-key"

#include "pos-osk-key.h"
#include "pos-enums.h"

enum {
  PROP_0,
  PROP_USE,
  PROP_WIDTH,
  PROP_SYMBOL,
  PROP_SYMBOLS,
  PROP_LABEL,
  PROP_ICON,
  PROP_STYLE,
  PROP_LAYER,
  PROP_EXPAND,
  PROP_PRESSED,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

/**
 * PosOskKey:
 *
 * A key on the osk widget
 */
struct _PosOskKey {
  GObject           parent;

  PosOskKeyUse      use;
  double            width;
  char             *symbol;
  GStrv             symbols;
  char             *label;
  char             *icon;
  char             *style;
  PosOskWidgetLayer layer;
  GdkRectangle      box;
  gboolean          expand;
  gboolean          pressed;
};
G_DEFINE_TYPE (PosOskKey, pos_osk_key, G_TYPE_OBJECT)


static void
pos_osk_key_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  PosOskKey *self = POS_OSK_KEY (object);

  switch (property_id) {
  case PROP_USE:
    self->use = g_value_get_enum (value);
    break;
  case PROP_WIDTH:
    pos_osk_key_set_width (self, g_value_get_double (value));
    break;
  case PROP_SYMBOL:
    self->symbol = g_value_dup_string (value);
    break;
  case PROP_SYMBOLS:
    self->symbols = g_value_dup_boxed (value);
    break;
  case PROP_LABEL:
    self->label = g_value_dup_string (value);
    break;
  case PROP_ICON:
    self->icon = g_value_dup_string (value);
    break;
  case PROP_STYLE:
    self->style = g_value_dup_string (value);
    break;
  case PROP_LAYER:
    self->layer = g_value_get_enum (value);
    break;
  case PROP_EXPAND:
    self->expand = g_value_get_boolean (value);
    break;
  case PROP_PRESSED:
    pos_osk_key_set_pressed (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_osk_key_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  PosOskKey *self = POS_OSK_KEY (object);

  switch (property_id) {
  case PROP_USE:
    g_value_set_boolean (value, self->use);
    break;
  case PROP_WIDTH:
    g_value_set_double (value, self->width);
    break;
  case PROP_SYMBOL:
    g_value_set_string (value, self->symbol);
    break;
  case PROP_SYMBOLS:
    g_value_set_boxed (value, self->symbols);
    break;
  case PROP_LABEL:
    g_value_set_string (value, self->label);
    break;
  case PROP_ICON:
    g_value_set_string (value, self->icon);
    break;
  case PROP_STYLE:
    g_value_set_string (value, self->style);
    break;
  case PROP_LAYER:
    g_value_set_enum (value, self->layer);
    break;
  case PROP_EXPAND:
    g_value_set_boolean (value, self->expand);
    break;
  case PROP_PRESSED:
    g_value_set_boolean (value, self->pressed);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
pos_osk_key_finalize (GObject *object)
{
  PosOskKey *self = POS_OSK_KEY (object);

  g_clear_pointer (&self->symbols, g_strfreev);
  g_clear_pointer (&self->symbol, g_free);
  g_clear_pointer (&self->label, g_free);
  g_clear_pointer (&self->icon, g_free);
  g_clear_pointer (&self->style, g_free);

  G_OBJECT_CLASS (pos_osk_key_parent_class)->finalize (object);
}


static void
pos_osk_key_class_init (PosOskKeyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pos_osk_key_get_property;
  object_class->set_property = pos_osk_key_set_property;
  object_class->finalize = pos_osk_key_finalize;

  /**
   * PosOskKey:use
   *
   * What the key is used for.
   */
  props[PROP_USE] =
    g_param_spec_enum ("use", "", "",
                       POS_TYPE_OSK_KEY_USE,
                       FALSE,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:width
   *
   * Width of key in "key units" (smallest key width).
   */
  props[PROP_WIDTH] =
    g_param_spec_double ("width", "", "",
                         1.0, 10.0, 1.0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:symbol
   *
   * The symbol the key represents.
   */
  props[PROP_SYMBOL] =
    g_param_spec_string ("symbol", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:symbols
   *
   * Additional symbols the key represents (e.g. ä over a)
   */
  props[PROP_SYMBOLS] =
    g_param_spec_boxed ("symbols", "", "",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:label
   *
   * the label that is shown on the key. If unset the PosOskKey:symbol is used instead.
   */
  props[PROP_LABEL] =
    g_param_spec_string ("label", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:icon
   *
   * the icon that is shown on the key.
   */
  props[PROP_ICON] =
    g_param_spec_string ("icon", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:style
   *
   * Additional style classes
   */
  props[PROP_STYLE] =
    g_param_spec_string ("style", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:layer
   *
   * The layer the key toggles (if any)
   */
  props[PROP_LAYER] =
    g_param_spec_enum ("layer", "", "",
                       POS_TYPE_OSK_WIDGET_LAYER,
                       FALSE,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:expand
   *
   * Whether the key expands to use free space in available in a row
   */
  props[PROP_EXPAND] =
    g_param_spec_boolean ("expand", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PosOskKey:pressed
   *
   * Whether the key is currently pressed
   */
  props[PROP_PRESSED] =
    g_param_spec_boolean ("pressed", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
pos_osk_key_init (PosOskKey *self)
{
  self->use = POS_OSK_KEY_USE_KEY;
  self->width = 1.0;
  self->layer = POS_OSK_WIDGET_LAYER_NORMAL;
}


PosOskKey *
pos_osk_key_new (const char *symbol)
{
  return POS_OSK_KEY (g_object_new (POS_TYPE_OSK_KEY, "symbol", symbol, NULL));
}

/**
 * pos_osk_key_set_width:
 * @self: The key
 * @width: The key's width
 *
 * Sets the key's width in key units.
 */
void
pos_osk_key_set_width (PosOskKey *self, double width)
{
  g_return_if_fail (POS_IS_OSK_KEY (self));
  g_return_if_fail (width > 0.0);

  self->width = width;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WIDTH]);
}

/**
 * pos_osk_key_get_width:
 * @self: The key
 *
 * Gets the key's width in key units. See also [method@OskKey.get_box].
 *
 * Returns: The key's width
 */
double
pos_osk_key_get_width (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), 1.0);

  return self->width;
}

/**
 * pos_osk_key_get_use:
 * @self: The key
 *
 * Get what the key is used for (regular key, toggle another layer,
 * etc.)
 *
 * Return: The key's use
 */
PosOskKeyUse
pos_osk_key_get_use (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), 1.0);

  return self->use;
}

/**
 * pos_osk_key_get_pressed:
 * @self: The key
 *
 * Gets the key's pressed state.
 *
 * Returns: Whether the key is pressed
 */
gboolean
pos_osk_key_get_pressed (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), FALSE);

  return self->pressed;
}

/**
 * pos_osk_key_set_pressed:
 * @self: The key
 * @pressed: Whether the key is pressed
 *
 * Marks the key as currently pressed.
 */
void
pos_osk_key_set_pressed (PosOskKey *self, gboolean pressed)
{
  g_return_if_fail (POS_IS_OSK_KEY (self));

  if (pressed == self->pressed)
    return;

  self->pressed = pressed;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESSED]);
}

/**
 * pos_osk_key_get_label:
 * @self: The key
 *
 * Get the label to be rendered on the key. This can differ from the
 * key's primary symbol and it can be `NULL` if the key uses an icon.
 *
 * Returns: The key's label
 */
const char *
pos_osk_key_get_label (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), NULL);

  return self->label;
}

/**
 * pos_osk_key_get_icon:
 * @self: The key
 *
 * Get the icon to be rendered on the key. This can be `NULL` if the
 * key uses a label.
 *
 * Returns:(nullable): The icon
 */
const char *
pos_osk_key_get_icon (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), NULL);

  return self->icon;
}

/**
 * pos_osk_key_get_symbol:
 * @self: The key
 *
 * Get the key's primary symbol
 *
 * Returns: The key symbol
 */
const char *
pos_osk_key_get_symbol (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), NULL);

  return self->symbol;
}

/**
 * pos_osk_key_get_layer:
 * @self: The key
 *
 * Get the layer a key is in.
 *
 * Returns: The key's layer
 */
PosOskWidgetLayer
pos_osk_key_get_layer (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), POS_OSK_WIDGET_LAYER_NORMAL);

  return self->layer;
}

/**
 * pos_osk_key_get_symbols:
 * @self: The key
 *
 * Get the additional symbols. For the primary symbol see
 * [method@Pos.OskKey.get_symbol].
 *
 * Returns: (transfer none): The key's additional symbols
 */
GStrv
pos_osk_key_get_symbols (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), NULL);

  return self->symbols;
}

/**
 * pos_osk_key_set_box:
 * @self: The key
 * @box: The key's box
 *
 * Set the keys box. This specifies the key's position and size.
 */
void
pos_osk_key_set_box (PosOskKey *self, const GdkRectangle *box)
{
  memmove (&self->box, box, sizeof (self->box));
}

/**
 * pos_osk_key_get_box:
 * @self: The key
 *
 * Get the key's position and size.
 *
 * Returns: The key's box
 */
const GdkRectangle *
pos_osk_key_get_box (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), NULL);

  return &self->box;
}

/**
 * pos_osk_key_get_expand:
 * @self: The key
 *
 * Get whether the key expands to use free space in available in a
 * row.
 *
 * Returns: The key's expand value
 */
gboolean
pos_osk_key_get_expand (PosOskKey *self)
{
  g_return_val_if_fail (POS_IS_OSK_KEY (self), FALSE);

  return self->expand;
}
