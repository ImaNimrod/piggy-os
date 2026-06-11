json_c = dependency('json-c')

hog_sources = files(
  'hog.c',
)

hog_exe = executable(
  'hog',
  hog_sources,
  dependencies: json_c,
  install: true,
  install_dir: get_option('bindir')
)
