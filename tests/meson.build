if not get_option('tests')
  subdir_done()
endif

test_env = environment()
test_env.set('G_DEBUG', 'gc-friendly,fatal-warnings')
test_env.set('MALLOC_CHECK_', '2')
test_env.set('NO_AT_BRIDGE', '1')
test_env.set('GSETTINGS_BACKEND', 'memory')
test_env.set('GSETTINGS_SCHEMA_DIR', meson.project_build_root() / 'data')

# For -Db_sanitize=address
test_env.set(
  'LSAN_OPTIONS',
  'suppressions=@0@/data/leak-suppress.txt'.format(meson.project_source_root()),
)
# as our libraries lack frame pointers since otherwise back traces are incomplete
test_env.set('ASAN_OPTIONS', 'fast_unwind_on_malloc=0')
# Useful when running locally and glib hits an assertion:
#test_env.set('ASAN_OPTIONS', 'fast_unwind_on_malloc=0:disable_coredump=0:unmap_shadow_on_exit=1:abort_on_error=1')

tests = [
  'completer',
  'completer-base',
  'completer-hunspell',
  'load-layouts',
  'osk-widget',
  'completers',
  'emoji-db',
  'size-manager',
]
foreach name : tests
  name = 'test-@0@'.format(name)
  test = executable(name, '@0@.c'.format(name), pie: true, dependencies: libpos_dep)
  test('@0@'.format(name), test, depends: compile_schemas, env: test_env)
endforeach

if presage_dep.found()
  # Create a dummy db for testing
  presage_test_db = custom_target(
    'presage-test-db',
    input: meson.current_source_dir() / 'data' / 'input.txt',
    output: 'database_en.db',
    command: ['mk-presage-db', '@INPUT@', '@OUTPUT@'],
  )

  name = 'test-completer-presage'
  test(
    name,
    executable(
      name,
      f'@name@.c',
      pie: true,
      c_args: '-DPOS_PRESAGE_TEST_DB_DIR="@0@"'.format(meson.current_build_dir()),
      dependencies: libpos_dep,
    ),
    depends: [compile_schemas, presage_test_db],
    env: test_env,
  )
endif
