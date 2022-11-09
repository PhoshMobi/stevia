libpos_completer_includes = include_directories('.')

if presage_dep.found()
  # Presage based completer
  libpos_completer_presage_sources = files('pos-completer-presage.c', 'pos-completer-presage.h')

  libpos_completer_presage_deps = [gio_dep, glib_dep, gmobile_dep, gtk_dep, presage_dep]

  libpos_completer_presage_lib = static_library(
    'pos-completer-presage',
    libpos_completer_presage_sources,
    include_directories: pos_includes,
    install: false,
    dependencies: libpos_completer_presage_deps,
    c_args: ['-DPRESAGE_DICT_DIR="@0@"'.format(datadir / 'phosh' / 'osk' / 'presage')],
    override_options: ['cpp_std=c++11'],
  )

  libpos_completer_presage_dep = declare_dependency(
    include_directories: libpos_completer_includes,
    link_with: libpos_completer_presage_lib,
  )
else
  libpos_completer_presage_sources = []
  libpos_completer_presage_lib = []
  libpos_completer_presage_dep = declare_dependency()
endif

#  pipe like completer
libpos_completer_pipe_sources = files('pos-completer-pipe.c', 'pos-completer-pipe.h')

libpos_completer_pipe_deps = [gio_dep, glib_dep, gtk_dep]

libpos_completer_pipe_lib = static_library(
  'pos-completer-pipe',
  libpos_completer_pipe_sources,
  include_directories: pos_includes,
  install: false,
  dependencies: libpos_completer_pipe_deps,
)

libpos_completer_pipe_dep = declare_dependency(
  include_directories: libpos_completer_includes,
  link_with: libpos_completer_pipe_lib,
)


if fzf.found()
  #  fzf based completer
  libpos_completer_fzf_sources = files('pos-completer-fzf.c', 'pos-completer-fzf.h')

  libpos_completer_fzf_deps = [gio_dep, glib_dep, gtk_dep]

  libpos_completer_fzf_lib = static_library(
    'pos-completer-fzf',
    libpos_completer_fzf_sources,
    include_directories: pos_includes,
    install: false,
    dependencies: libpos_completer_fzf_deps,
  )

  libpos_completer_fzf_dep = declare_dependency(
    include_directories: libpos_completer_includes,
    link_with: libpos_completer_fzf_lib,
  )
else
  libpos_completer_fzf_sources = []
  libpos_completer_fzf_lib = []
  libpos_completer_fzf_dep = declare_dependency()
endif

if hunspell_dep.found()
  #  hunspell based completer
  libpos_completer_hunspell_sources = files('pos-completer-hunspell.c', 'pos-completer-hunspell.h')

  libpos_completer_hunspell_deps = [gio_dep, glib_dep, gtk_dep, hunspell_dep]

  libpos_completer_hunspell_lib = static_library(
    'pos-completer-hunspell',
    libpos_completer_hunspell_sources,
    include_directories: pos_includes,
    c_args: ['-DPOS_HUNSPELL_DICT_PATH="@0@:@1@"'.format(datadir / 'hunspell', datadir / 'myspell')],
    install: false,
    dependencies: libpos_completer_hunspell_deps,
  )

  libpos_completer_hunspell_dep = declare_dependency(
    include_directories: libpos_completer_includes,
    link_with: libpos_completer_hunspell_lib,
  )
else
  libpos_completer_hunspell_sources = []
  libpos_completer_hunspell_lib = []
  libpos_completer_hunspell_dep = declare_dependency()
endif

if uim_dep.found()
  # Uim based completer
  libpos_completer_uim_sources = files('pos-completer-uim.c', 'pos-completer-uim.h')

  libpos_completer_uim_deps = [gio_dep, glib_dep, gmobile_dep, gnome_desktop_dep, gtk_dep, uim_dep]

  libpos_completer_uim_lib = static_library(
    'pos-completer-uim',
    libpos_completer_uim_sources,
    include_directories: pos_includes,
    install: false,
    dependencies: libpos_completer_uim_deps,
    override_options: ['cpp_std=c++11'],
  )

  libpos_completer_uim_dep = declare_dependency(
    include_directories: libpos_completer_includes,
    link_with: libpos_completer_uim_lib,
    dependencies: libpos_deps,
  )

  libpos_completer_uim_dep = declare_dependency(
    include_directories: libpos_completer_includes,
    link_with: libpos_completer_uim_lib,
  )
else
  libpos_completer_uim_sources = []
  libpos_completer_uim_lib = []
  libpos_completer_uim_dep = declare_dependency()
endif

if varnam_dep.found()
  #  varnam based completer
  libpos_completer_varnam_sources = files('pos-completer-varnam.c', 'pos-completer-varnam.h')

  libpos_completer_varnam_deps = [gio_dep, glib_dep, gtk_dep, varnam_dep]

  libpos_completer_varnam_lib = static_library(
    'pos-completer-varnam',
    libpos_completer_varnam_sources,
    include_directories: pos_includes,
    install: false,
    dependencies: libpos_completer_varnam_deps,
  )

  libpos_completer_varnam_dep = declare_dependency(
    include_directories: libpos_completer_includes,
    link_with: libpos_completer_varnam_lib,
  )
else
  libpos_completer_varnam_sources = []
  libpos_completer_varnam_lib = []
  libpos_completer_varnam_dep = declare_dependency()
endif

libpos_completers_sources = [
  libpos_completer_fzf_sources,
  libpos_completer_hunspell_sources,
  libpos_completer_pipe_sources,
  libpos_completer_presage_sources,
  libpos_completer_uim_sources,
  libpos_completer_varnam_sources,
]

libpos_completer_libs = [
  libpos_completer_fzf_lib,
  libpos_completer_hunspell_lib,
  libpos_completer_pipe_lib,
  libpos_completer_presage_lib,
  libpos_completer_uim_lib,
  libpos_completer_varnam_lib,
]

libpos_completers_dep = declare_dependency(
  dependencies: [
    libpos_completer_fzf_dep,
    libpos_completer_hunspell_dep,
    libpos_completer_pipe_dep,
    libpos_completer_presage_dep,
    libpos_completer_uim_dep,
    libpos_completer_varnam_dep,
  ],
)
