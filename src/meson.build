subdir('dbus')
subdir('layouts')

libpos_enum_headers = files('phosh-osk-enums.h', 'pos-enums.h')
libpos_enum_sources = gnome.mkenums_simple('pos-enum-types', sources: libpos_enum_headers)

libpos_deps = [
  gio_dep,
  gmobile_dep,
  gnome_desktop_dep,
  gtk_dep,
  gtk_wayland_dep,
  gdk_dep,
  gdk_wayland_dep,
  json_glib_dep,
  libfeedback_dep,
  libhandy_dep,
  libsystemd_dep,
  wayland_client_dep,
  xkbcommon_dep,
  cc.find_library('m', required: false),
  cc.find_library('rt', required: false),
]

pos_resources = gnome.compile_resources(
  'pos-resources',
  'phosh-osk-stevia.gresources.xml',
  extra_args: '--manual-register',
  c_name: 'pos',
)

libpos_sources = files(
  'pos-activation-filter.c',
  'pos-activation-filter.h',
  'pos-char-popup.c',
  'pos-char-popup.h',
  'pos-clipboard-manager.c',
  'pos-clipboard-manager.h',
  'pos-completer-base.c',
  'pos-completer-base.h',
  'pos-completer-manager.c',
  'pos-completer-manager.h',
  'pos-completer.c',
  'pos-completer.h',
  'pos-completion-bar.c',
  'pos-completion-bar.h',
  'pos-completions-box.c',
  'pos-completions-box.h',
  'pos-emoji-db.c',
  'pos-emoji-db.h',
  'pos-emoji-picker.c',
  'pos-emoji-picker.h',
  'pos-enums.h',
  'pos-hw-tracker.c',
  'pos-hw-tracker.h',
  'pos-indicator-popup.c',
  'pos-indicator-popup.h',
  'pos-input-method.c',
  'pos-input-method.h',
  'pos-input-surface.c',
  'pos-input-surface.h',
  'pos-keypad-button.c',
  'pos-keypad-button.h',
  'pos-keypad.c',
  'pos-keypad.h',
  'pos-logind-session.c',
  'pos-logind-session.h',
  'pos-main.c',
  'pos-main.h',
  'pos-osk-key.c',
  'pos-osk-key.h',
  'pos-osk-widget.c',
  'pos-osk-widget.h',
  'pos-output.c',
  'pos-output.h',
  'pos-settings-panel.c',
  'pos-settings-panel.h',
  'pos-shortcuts-bar.c',
  'pos-shortcuts-bar.h',
  'pos-size-manager.c',
  'pos-size-manager.h',
  'pos-style-manager.c',
  'pos-style-manager.h',
  'pos-virtual-keyboard.c',
  'pos-virtual-keyboard.h',
  'pos-vk-driver.c',
  'pos-vk-driver.h',
  'pos-wayland.c',
  'pos-wayland.h',
  'pos.h',
)

libpos_generated_sources = [
  generated_dbus_sources,
  libpos_enum_sources,
  pos_resources,
  wl_proto_headers,
  wl_proto_sources,
]

# copied verbatim from phoh
phosh_contrib_sources = files(
  'contrib/layersurface.c',
  'contrib/layersurface.h',
  'contrib/util.c',
  'contrib/util.h',
)

pos_inc = include_directories(['.', 'contrib/'])
pos_includes = [pos_inc, root_inc, dbus_inc, proto_inc]

subdir('completers')

pos_lib = static_library(
  'pos',
  libpos_sources,
  libpos_generated_sources,
  phosh_contrib_sources,
  dependencies: libpos_deps,
  include_directories: pos_includes,
  install: false,
)

libpos_dep = declare_dependency(
  sources: libpos_generated_sources,
  include_directories: pos_includes,
  link_with: pos_lib,
  dependencies: [libpos_deps, libpos_completers_dep],
)

executable(
  'phosh-osk-stevia',
  'phosh-osk-stevia.c',
  'pos-osk-dbus.c',
  include_directories: pos_includes,
  install: true,
  dependencies: libpos_dep,
)

if get_option('gtk_doc')
  pos_gir_extra_args = ['--quiet']

  pos_gir = gnome.generate_gir(
    [pos_lib, libpos_completer_libs],
    sources: [libpos_sources, libpos_completers_sources],
    nsversion: '0',
    namespace: 'Pos',
    export_packages: 'pos-0',
    symbol_prefix: 'pos',
    identifier_prefix: 'Pos',
    includes: ['Gio-2.0', 'Gtk-3.0', 'Handy-1'],
    install: false,
    extra_args: pos_gir_extra_args,
    dependencies: [libpos_dep, libpos_completers_dep],
  )
endif
