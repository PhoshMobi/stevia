/*
 * Copyright (C) 2022-2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pos-shortcuts-bar"

#include "pos-shortcuts-bar.h"

/* TODO: Relocatable schema for different OSKs ? */
#define SHORTCUTS_SCHEMA_ID "mobi.phosh.osk.Terminal"
#define SHORTCUTS_KEY       "shortcuts"

enum {
  PROP_0,
  PROP_LATCHED_MODIFIERS,
  PROP_NUM_SHORTCUTS,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  SHORTCUT_ACTIVATED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

typedef struct _PosShortcut
{
  char           *name;
  guint           key;
  GdkModifierType modifiers;
} PosShortcut;
G_DEFINE_BOXED_TYPE (PosShortcut, pos_shortcut, pos_shortcut_ref, pos_shortcut_unref);

/**
 * PosShortcutsBar:
 *
 * The shortcuts bar contains buttons that emit key combinations (like `<ctrl>c`). These
 * can be configured via GSettings.
 *
 * If a combination is only a modifier like `<ctrl>` or `<alt>` then a toggle button is
 * used to allow for latched modifiers.
 */

typedef struct _PosShortcutsBar
{
  GtkBox           parent;

  GtkFlowBox      *shortcuts_box;
  guint            n_shortcuts;
  GdkModifierType  latched;

  GSettings       *settings;
} PosShortcutsBar;

G_DEFINE_TYPE (PosShortcutsBar, pos_shortcuts_bar, GTK_TYPE_BOX);

static void
pos_shortcut_free (gpointer data)
{
  PosShortcut *shortcut = data;

  g_free (shortcut->name);
}

PosShortcut *
pos_shortcut_ref (PosShortcut *shortcut)
{
  return g_atomic_rc_box_acquire (shortcut);
}

void
pos_shortcut_unref (PosShortcut *shortcut)
{
  g_atomic_rc_box_release_full (shortcut, (GDestroyNotify) pos_shortcut_free);
}


const char *
pos_shortcut_get_label (PosShortcut *shortcut)
{
  return shortcut->name;
}


guint
pos_shortcut_get_key (PosShortcut *shortcut)
{
  return shortcut->key;
}


GdkModifierType
pos_shortcut_get_modifiers (PosShortcut *shortcut)
{
  return shortcut->modifiers;
}


static PosShortcut *
pos_shortcut_new (void)
{
  return g_atomic_rc_box_new0 (PosShortcut);
}


static void
on_btn_clicked (PosShortcutsBar *self, GtkButton *btn)
{
  PosShortcut *shortcut;

  g_assert (POS_IS_SHORTCUTS_BAR (self));

  shortcut = g_object_get_data (G_OBJECT (btn), "pos-shortcut");
  g_assert (shortcut);
  g_signal_emit (self, signals[SHORTCUT_ACTIVATED], 0, shortcut);
}


static void
on_toggle_btn_active_changed (PosShortcutsBar *self, GParamSpec *pspec, GtkToggleButton *btn)
{
  PosShortcut *shortcut;
  gboolean active;

  g_assert (POS_IS_SHORTCUTS_BAR (self));

  active = gtk_toggle_button_get_active (btn);
  shortcut = g_object_get_data (G_OBJECT (btn), "pos-shortcut");
  g_assert (shortcut);

  if (active)
    self->latched |= shortcut->modifiers;
  else
    self->latched &= ~shortcut->modifiers;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LATCHED_MODIFIERS]);
}


static char *
pos_accelerator_get_label (PosShortcut *shortcut)
{
  char *label = NULL;

  if (shortcut->key == 0) {
    if (shortcut->modifiers == GDK_CONTROL_MASK)
      label = "Ctrl";
    else if (shortcut->modifiers == GDK_MOD1_MASK)
      label = "Alt";
    else if (shortcut->modifiers == GDK_SHIFT_MASK)
      label = "Shift";
    else if (shortcut->modifiers == GDK_SUPER_MASK)
      label = "Super";

    if (label)
      return g_strdup (label);
  }

  if (shortcut->modifiers)
    return NULL;

  switch (shortcut->key) {
  case GDK_KEY_Down:
    label = "↓";
    break;
  case GDK_KEY_Up:
    label = "↑";
    break;
  case GDK_KEY_Left:
    label = "←";
    break;
  case GDK_KEY_Right:
    label = "→";
    break;
  case GDK_KEY_Page_Up:
    label = "PgUp";
    break;
  case GDK_KEY_Page_Down:
    label = "PgDn";
    break;
  default:
    return NULL;
  }

  return g_strdup (label);
}


static void
on_shortcuts_changed (PosShortcutsBar *self,
                      const char      *key,
                      GSettings       *settings)
{
  g_auto (GStrv) accelerators = NULL;
  guint n_shortcuts;

  g_return_if_fail (POS_IS_SHORTCUTS_BAR (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  g_debug ("Shortcuts changed");
  gtk_container_foreach (GTK_CONTAINER (self->shortcuts_box),
                         (GtkCallback) gtk_widget_destroy, NULL);

  accelerators = g_settings_get_strv (self->settings, SHORTCUTS_KEY);
  n_shortcuts = g_strv_length (accelerators);
  for (int i = 0; i < n_shortcuts; i++) {
    g_autoptr (PosShortcut) shortcut = pos_shortcut_new ();
    GtkWidget *btn;
    GtkWidget *child;

    gtk_accelerator_parse (accelerators[i], &shortcut->key, &shortcut->modifiers);
    shortcut->name = pos_accelerator_get_label (shortcut);
    if (!shortcut->name) {
      if (gtk_accelerator_valid (shortcut->key, shortcut->modifiers)) {
        shortcut->name = gtk_accelerator_get_label (shortcut->key, shortcut->modifiers);
      } else {
        g_warning ("Invalid shortcut '%s': (0x%x/0x%x)",
                   accelerators[i],
                   shortcut->key,
                   shortcut->modifiers);
        continue;
      }
    }

    child = gtk_flow_box_child_new ();
    if (shortcut->key) {
      g_debug ("Adding shortcut: '%s'", shortcut->name);
      btn = gtk_button_new_with_label (shortcut->name);
      g_object_set_data_full (G_OBJECT (btn), "pos-shortcut",
                              g_steal_pointer (&shortcut),
                              (GDestroyNotify)pos_shortcut_unref);
      g_signal_connect_swapped (btn, "clicked", G_CALLBACK (on_btn_clicked), self);
      gtk_container_add (GTK_CONTAINER (child), btn);
    } else {
      g_debug ("Adding modifier: '%s', mod: 0x%x", shortcut->name, shortcut->modifiers);
      btn = gtk_toggle_button_new_with_label (shortcut->name);
      g_object_set_data_full (G_OBJECT (btn), "pos-shortcut",
                              g_steal_pointer (&shortcut),
                              (GDestroyNotify)pos_shortcut_unref);
      g_signal_connect_swapped (btn,
                                "notify::active",
                                G_CALLBACK (on_toggle_btn_active_changed),
                                self);
      gtk_container_add (GTK_CONTAINER (child), btn);
    }
    gtk_widget_show_all (child);
    gtk_flow_box_insert (self->shortcuts_box, child, -1);
  }

  if (self->n_shortcuts == n_shortcuts)
    return;

  self->n_shortcuts = n_shortcuts;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NUM_SHORTCUTS]);
}


static void
pos_shortcuts_bar_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  PosShortcutsBar *self = POS_SHORTCUTS_BAR (object);

  switch (property_id) {
  case PROP_NUM_SHORTCUTS:
    g_value_set_uint (value, pos_shortcuts_bar_get_num_shortcuts (self));
    break;
  case PROP_LATCHED_MODIFIERS:
    g_value_set_flags (value, self->latched);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pos_shortcuts_bar_finalize (GObject *object)
{
  PosShortcutsBar *self = POS_SHORTCUTS_BAR (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (pos_shortcuts_bar_parent_class)->finalize (object);
}


static void
pos_shortcuts_bar_class_init (PosShortcutsBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = pos_shortcuts_bar_finalize;
  object_class->get_property = pos_shortcuts_bar_get_property;

  /**
   * PosShortcutsBar:num-shortcuts
   *
   * The current number of shortcuts defined
   */
  props[PROP_NUM_SHORTCUTS] =
    g_param_spec_uint ("num-shortcuts", "", "",
                       0,
                       G_MAXUINT,
                       0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PosShortcutsBar:latched-modifiers
   *
   * Currently latched modifiers. This only takes effect when there are modifier keys
   * like plain `Ctrl` or `Alt` added to the shortcuts bar.
   */
  props[PROP_LATCHED_MODIFIERS] =
    g_param_spec_flags ("latched-modifiers", "", "",
                        GDK_TYPE_MODIFIER_TYPE,
                        0,
                        G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PosShortcutsbar::shortcut-activated:
   * @self: The shortcuts bar
   * @shortcut: The selected shortcut
   *
   * Emitted when a shortcut in the shortcut bar was activated.
   */
  signals[SHORTCUT_ACTIVATED] =
    g_signal_new ("shortcut-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  POS_TYPE_SHORTCUT);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/stevia/ui/shortcuts-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, PosShortcutsBar, shortcuts_box);

  gtk_widget_class_set_css_name (widget_class, "pos-shortcuts-bar");
}


static void
pos_shortcuts_bar_init (PosShortcutsBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new (SHORTCUTS_SCHEMA_ID);
  g_signal_connect_swapped (self->settings, "changed::" SHORTCUTS_KEY,
                            G_CALLBACK (on_shortcuts_changed),
                            self);
  on_shortcuts_changed (self, NULL, self->settings);
}


PosShortcutsBar *
pos_shortcuts_bar_new (void)
{
  return g_object_new (POS_TYPE_SHORTCUTS_BAR, NULL);
}


guint
pos_shortcuts_bar_get_num_shortcuts (PosShortcutsBar *self)
{
  g_return_val_if_fail (POS_IS_SHORTCUTS_BAR (self), 0);

  return self->n_shortcuts;
}

GdkModifierType
pos_shortcuts_bar_get_latched_modifiers (PosShortcutsBar *self)
{
  g_return_val_if_fail (POS_IS_SHORTCUTS_BAR (self), 0);

  return self->latched;
}


static void
unlatch_modifiers (GtkWidget *widget)
{
  PosShortcut *shortcut;
  GtkWidget *child;

  g_return_if_fail (GTK_IS_FLOW_BOX_CHILD (widget));

  child = gtk_bin_get_child (GTK_BIN (widget));
  if (!GTK_IS_TOGGLE_BUTTON (child))
    return;

  shortcut = g_object_get_data (G_OBJECT (child), "pos-shortcut");
  g_return_if_fail (shortcut);
  g_return_if_fail (!shortcut->key);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (child), FALSE);
}


void
pos_shortcuts_bar_unlatch_modifiers (PosShortcutsBar *self)
{
  g_return_if_fail (POS_IS_SHORTCUTS_BAR (self));

  gtk_container_foreach (GTK_CONTAINER (self->shortcuts_box),
                         (GtkCallback) unlatch_modifiers,
                         NULL);
}
