vip_sources = files(
  'editor.c',
  'motions.c',
  'vip.c',
)

vip_exe = executable(
  'vip',
  vip_sources,
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'vip.1',
  install_dir: get_option('mandir') / 'man1'
)
