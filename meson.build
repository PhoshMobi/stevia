project(
  'phosh-osk-stevia',
  'c',
  version: '0.50.1',
  license: 'GPLv3+',
  meson_version: '>= 1.1',
  default_options: ['warning_level=1', 'buildtype=debugoptimized', 'c_std=gnu11'],
)


app_id = 'mobi.phosh.Stevia'

prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
localedir = join_paths(prefix, get_option('localedir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
desktopdir = join_paths(datadir, 'applications')
sessiondir = join_paths(datadir, 'gnome-session')
pkgdatadir = join_paths(datadir, meson.project_name())
pkglibdir = join_paths(libdir, meson.project_name())
schemasdir = datadir / 'glib-2.0' / 'schemas'

systemd_user_unit_dir = get_option('systemd_user_unit_dir')
systemd_dep = dependency('systemd', required: false)
if systemd_user_unit_dir == ''
  if systemd_dep.found()
    systemd_user_unit_dir = systemd_dep.get_variable('systemd_user_unit_dir')
  else
    systemd_user_unit_dir = prefix / libdir / 'systemd' / 'user'
  endif
endif

glib_ver = '2.80'
glib_ver_str = 'GLIB_VERSION_@0@'.format(glib_ver.replace('.', '_'))
glib_ver_cmp = '>=@0@'.format(glib_ver)

add_project_arguments(
  [
    '-DLIBFEEDBACK_USE_UNSTABLE_API',
    '-DGLIB_VERSION_MIN_REQUIRED=@0@'.format(glib_ver_str),
    '-DGLIB_VERSION_MAX_REQUIRED=@0@'.format(glib_ver_str),
    '-DG_LOG_USE_STRUCTURED',
    '-DGMOBILE_USE_UNSTABLE_API',
  ],
  language: 'c',
)

root_inc = include_directories('.')

cc = meson.get_compiler('c')

global_c_args = []
test_c_args = [
  '-Wcast-align',
  '-Wdate-time',
  '-Wdeclaration-after-statement',
  ['-Werror=format-security', '-Werror=format=2'],
  '-Wendif-labels',
  '-Werror=incompatible-pointer-types',
  '-Werror=missing-declarations',
  '-Werror=overflow',
  '-Werror=return-type',
  '-Werror=shift-count-overflow',
  '-Werror=shift-overflow=2',
  '-Werror=implicit-fallthrough=3',
  '-Werror=implicit-function-declaration',
  '-Wfloat-equal',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmaybe-uninitialized',
  '-Wmissing-field-initializers',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wnested-externs',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wtype-limits',
  '-Wundef',
  '-Wunused-function',
]
if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif

foreach arg : test_c_args
  if cc.has_multi_arguments(arg)
    global_c_args += arg
  endif
endforeach
add_project_arguments(global_c_args, language: 'c')

gnome = import('gnome')
i18n = import('i18n')

glib_dep = dependency('glib-2.0', version: glib_ver_cmp)
gio_dep = dependency('gio-2.0', version: glib_ver_cmp)
gobject_dep = dependency('gobject-2.0', version: glib_ver_cmp)
gmobile_dep = dependency('gmobile', version: '>=0.2.0')
gnome_desktop_dep = dependency('gnome-desktop-3.0', version: '>=3.26')
gsettings_desktop_schemas_dep = dependency('gsettings-desktop-schemas', version: '>= 47')
gtk_dep = dependency('gtk+-3.0', version: '>=3.22')
gtk_wayland_dep = dependency('gtk+-wayland-3.0', version: '>=3.22')
gdk_dep = dependency('gdk-3.0', version: '>=3.22')
gdk_wayland_dep = dependency('gdk-wayland-3.0', version: '>=3.22')
json_glib_dep = dependency('json-glib-1.0')
libfeedback_dep = dependency(
  'libfeedback-0.0',
  fallback: ['libfeedback', 'libfeedback_dep'],
  default_options: ['introspection=disabled', 'daemon=false', 'gtk_doc=false'],
)
libhandy_dep = dependency(
  'libhandy-1',
  version: '>=1.1.90',
  fallback: ['libhandy', 'libhandy_dep'],
  default_options: ['introspection=disabled'],
)
libsystemd_dep = dependency('libsystemd', 'libelogind', version: '>= 241')
wayland_client_dep = dependency('wayland-client', version: '>=1.14')
wayland_protos_dep = dependency('wayland-protocols', version: '>=1.12')
xkbcommon_dep = dependency('xkbcommon')

# For completion engines
hunspell_dep = dependency('hunspell', required: false)
fzf = find_program('fzf', required: false)
presage2_dep = dependency('presage', required: false, version: '>=2.0.0')
if presage2_dep.found()
  presage_dep = presage2_dep
else
  # presage did not have pkg-config files before version 2
  presage_dep = cc.find_library('presage', required: false)
endif
varnam_dep = dependency('govarnam', required: false)

completers = []
if presage_dep.found()
  default_completer = 'presage'
  completers += ['presage']
endif
if hunspell_dep.found()
  default_completer = 'hunspell'
  completers += ['hunspell']
endif

if completers.length() == 0
  error('No usable completer found')
endif

code = '''
#include <linux/rfkill.h>

struct rfkill_event_ext e;
'''
have_rfkill_event_ext = cc.compiles(code, name: 'Have rfkill_event_ext')
have_memfd_create = cc.has_function(
  'memfd_create',
  prefix: ['#define _GNU_SOURCE', '#include <sys/mman.h>'],
)

config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', 'phosh-osk-stevia')
config_h.set_quoted('LOCALEDIR', localedir)
config_h.set_quoted('PHOSH_OSK_STEVIA_VERSION', meson.project_version())
config_h.set('POS_HAVE_FZF', fzf.found())
config_h.set('POS_HAVE_HUNSPELL', hunspell_dep.found())
config_h.set('POS_HAVE_PRESAGE', presage_dep.found())
config_h.set('POS_HAVE_PRESAGE2', presage2_dep.found())
config_h.set('POS_HAVE_VARNAM', varnam_dep.found())
config_h.set(
  'POS_INPUT_SURFACE_DEFAULT_HEIGHT',
  200,
  description: 'Default height of the input surface',
)
config_h.set_quoted('POS_DEFAULT_COMPLETER', default_completer)
config_h.set(
  'PHOSH_HAVE_MEMFD_CREATE',
  have_memfd_create,
  description: 'Whether we have memdfd_create on Linux',
)

configure_file(output: 'pos-config.h', configuration: config_h)

run_data = configuration_data()
run_data.set('ABS_BUILDDIR', meson.current_build_dir())
run_data.set('ABS_SRCDIR', meson.current_source_dir())
configure_file(input: 'run.in', output: 'run', configuration: run_data)

subdir('data')
subdir('po')
subdir('protocol')
subdir('src')
subdir('tests')
subdir('doc')

summary(
  {
    'Tests': get_option('tests'),
    'Documentation': get_option('gtk_doc'),
    'Manpages': get_option('man'),
  },
  bool_yn: true,
  section: 'Build',
)

summary(
  {
    'Default': default_completer,
    'Presage': presage_dep.found(),
    'Fzf': fzf.found(),
    'Varnam': varnam_dep.found(),
    'Hunspell': hunspell_dep.found(),
  },
  bool_yn: true,
  section: 'Completers',
)

summary({'Use as default OSK': get_option('default_osk')}, bool_yn: true, section: 'Run')

# gnome.post_install() is available since meson 0.59.0
# Distributions use their own tooling (e.g. postinst, triggers, etc)
# so it is okay if the post_install() is not run on distro builds
if meson.version().version_compare('>=0.59.0')
  gnome.post_install(glib_compile_schemas: true)
endif
