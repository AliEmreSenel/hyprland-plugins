# HyprExpo

HyprExpo provides a workspace overview and a two-dimensional workspace swipe (Swish).

![HyprExpo](https://github.com/user-attachments/assets/e89df9d2-9800-4268-9929-239ad9bc3a54)

## Build

Build against the headers and dependencies of the **running Hyprland version**. This code targets Hyprland 0.56.2; plugin initialization checks the Hyprland commit to prevent incompatible hooks.

```sh
make -C hyprexpo
make -C hyprexpo test
```

CMake and Meson are also supported:

```sh
cmake -S hyprexpo -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

```sh
meson setup build hyprexpo
meson compile -C build
meson test -C build --print-errorlogs
```

Use separate build directories when trying both CMake and Meson. CMake generates a compilation database in its build directory; Meson provides one too. With Nix, `nix build .#hyprexpo` builds against the pinned Hyprland input and `nix flake check` builds and tests the package. Override that input to match your compositor when integrating the flake into an existing configuration.

## Configuration

The plugin exposes Lua functions under `hl.plugin.hyprexpo`. After loading the plugin, add the following to your Hyprland Lua configuration. Guard plugin configuration so the file also works before the plugin is loaded and after it is unloaded.

```lua
if hl.plugin.hyprexpo then
    hl.config({
        plugin = {
            hyprexpo = {
                columns = 3,
                gap_size = 5,
                bg_col = "rgb(111111)",
                workspace_method = "center current",
                skip_empty = 0,
                gesture_distance = 200,
            },
            hyprswish = {
                zoom_scale = 0.9,
                gesture_distance = 200,
            },
        },
    })

    hl.bind("SUPER + G", function()
        hl.plugin.hyprexpo.expo("toggle")
    end)

    hl.plugin.hyprexpo.gesture({ fingers = 3, direction = "vertical", action = "expo" })
    hl.plugin.hyprexpo.gesture({ fingers = 4, direction = "swipe", action = "swish" })
end
```

| Property | Description | Default |
| --- | --- | --- |
| `hyprexpo.columns` | Tiles per row and column, from 1 to 10 | `3` |
| `hyprexpo.gap_size` | Gap between tiles; nonnegative and limited to fit the monitor | `5` |
| `hyprexpo.bg_col` | Background color between tiles | `rgb(111111)` |
| `hyprexpo.workspace_method` | `center <workspace>` or `first <workspace>`; selectors such as `current`, `1`, and `m+1` are supported | `center current` |
| `hyprexpo.skip_empty` | `0` uses sequential monitor-relative IDs (`r`); `1` uses existing monitor-relative workspaces (`m`) | `0` |
| `hyprexpo.gesture_distance` | Positive swipe distance for Expo | `200` |
| `hyprswish.zoom_scale` | Workspace scale during Swish, from 0.1 to 1 | `0.9` |
| `hyprswish.gesture_distance` | Positive swipe distance for Swish | `200` |

The grid has `columns * columns` tiles. With `skip_empty = 1`, selecting an unused tile opens the next empty workspace.

Workspaces belong to a single monitor. If an explicit `first` or `center` anchor belongs to another monitor, the grid uses the current workspace on this monitor as its anchor instead. Thumbnail rendering preserves workspace visibility and animation state on both monitors.

## Actions and gestures

`hl.plugin.hyprexpo.expo()` defaults to `"toggle"`. The `hyprexpo:expo` dispatcher accepts the same actions:

| Action | Behavior |
| --- | --- |
| `toggle` | Open or close the overview |
| `select` | Select the workspace under the pointer and close |
| `off`, `close`, `disable` | Close and keep the starting workspace |
| `on`, `enable` | Open the overview |

`hl.plugin.hyprexpo.gesture()` requires `fingers`, `direction`, and `action`. Actions are `expo`, `swish`, and `unset`; optional fields are `mods`, `scale`, and `disable_inhibit`. Expo uses vertical swipe movement; Swish uses both axes. Pinch directions are rejected. Gesture `scale` multiplies movement, and cancellation returns to the workspace where the gesture began.
