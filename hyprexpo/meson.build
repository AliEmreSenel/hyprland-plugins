project('hyprexpo', 'cpp',
  version: '0.1',
  default_options: ['buildtype=release', 'cpp_std=c++23'],
)

src = files('main.cpp', 'overview.cpp', 'ExpoGesture.cpp', 'OverviewPassElement.cpp')

shared_module(meson.project_name(), src,
  dependencies: [
    dependency('hyprland'),
    dependency('pixman-1'),
    dependency('libdrm'),
    dependency('pangocairo'),
    dependency('libinput'),
    dependency('libudev'),
    dependency('wayland-server'),
    dependency('xkbcommon'),
  ],
  install: true,
)

grid_test = executable('grid-test', 'tests/grid.cpp')
test('grid', grid_test)
preview_test = executable('preview-test', 'tests/preview.cpp')
test('workspace-preview', preview_test)
