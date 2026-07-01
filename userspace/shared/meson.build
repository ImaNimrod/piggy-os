shared_src = files(
  'file.c',
  'util.c',
)

shared_lib = static_library(
  'userspace_shared',
  shared_src,
  include_directories: include_directories('.')
)

shared_dep = declare_dependency(
  link_with: shared_lib,
  include_directories: include_directories('.')
)
