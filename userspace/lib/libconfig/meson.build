includes = include_directories('include')

sources = files(
  'src/config.c',
  'src/parser.c',
  'src/tokenizer.c'
)

libconfig = shared_library(
    'config',
    sources,
    include_directories: includes,
    version: '1.0.0',
    soversion: '1',
    install: true,
    install_dir: get_option('libdir')
)

install_headers(
  'include/config.h',
  install_dir: get_option('includedir')
)

libconfig_dep = declare_dependency(
  link_with: libconfig,
  include_directories: includes
)
