cargs = ['-DG_LOG_DOMAIN="FuPluginMntPico"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('mnt-pico.quirk')
plugin_builtins += static_library('fu_plugin_mnt_pico',
  rustgen.process('fu-mnt-pico.rs'),
  sources: [
    'fu-mnt-pico-common.c',
    'fu-mnt-pico-device.c',
    'fu-mnt-pico-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
