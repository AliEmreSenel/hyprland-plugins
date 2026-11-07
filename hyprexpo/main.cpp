#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/config/lua/types/LuaConfigInt.hpp>
#include <hyprland/src/config/lua/types/LuaConfigBool.hpp>
#include <hyprland/src/config/lua/types/LuaConfigFloat.hpp>
#include <hyprland/src/config/lua/types/LuaConfigString.hpp>
#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/managers/input/trackpad/TrackpadGestures.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/keybinds/Resolver.hpp>
#include <lua.hpp>
#include <algorithm>
#include <string_view>

#include "globals.hpp"
#include "overview.hpp"
#include "Grid.hpp"
#include "ExpoGesture.hpp"
#include "SwishGesture.hpp"

// Methods
inline CFunctionHook* g_pRenderWorkspaceHook = nullptr;
inline CFunctionHook* g_pAddDamageHookA      = nullptr;
inline CFunctionHook* g_pAddDamageHookB      = nullptr;
typedef void (*origRenderWorkspace)(void*, Render::CRenderContext&, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&, const CBox&);
typedef void (*origAddDamageA)(void*, const CBox&);
typedef void (*origAddDamageB)(void*, const pixman_region32_t*);

void renderWorkspacePreview(PHLMONITOR monitor, PHLWORKSPACE workspace, const CBox& geometry) {
    reinterpret_cast<origRenderWorkspace>(g_pRenderWorkspaceHook->m_original)(g_pHyprRenderer.get(), g_pHyprRenderer->context(), monitor, workspace, Time::steadyNow(), geometry);
}

static bool g_unloading = false;

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static bool                renderingOverview = false;
static CHyprSignalListener preRenderListener, monitorRemovedListener, configReloadListener;

//
static void hkRenderWorkspace(void* thisptr, Render::CRenderContext& ctx, PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& now, const CBox& geometry) {
    if (!g_pOverview || renderingOverview || g_pOverview->blockOverviewRendering || g_pOverview->pMonitor != pMonitor)
        ((origRenderWorkspace)(g_pRenderWorkspaceHook->m_original))(thisptr, ctx, pMonitor, pWorkspace, now, geometry);
    else
        g_pOverview->render(ctx);
}

static void hkAddDamageA(void* thisptr, const CBox& box) {
    const auto PMONITOR = (Monitor::CMonitor*)thisptr;

    if (!g_pOverview || g_pOverview->pMonitor != PMONITOR->m_self || g_pOverview->blockDamageReporting) {
        ((origAddDamageA)g_pAddDamageHookA->m_original)(thisptr, box);
        return;
    }

    g_pOverview->onDamageReported();
}

static void hkAddDamageB(void* thisptr, const pixman_region32_t* rg) {
    const auto PMONITOR = (Monitor::CMonitor*)thisptr;

    if (!g_pOverview || g_pOverview->pMonitor != PMONITOR->m_self || g_pOverview->blockDamageReporting) {
        ((origAddDamageB)g_pAddDamageHookB->m_original)(thisptr, rg);
        return;
    }

    g_pOverview->onDamageReported();
}

static SDispatchResult onExpoDispatcher(std::string arg) {
    if (g_unloading)
        return {.success = false, .error = "plugin is unloading"};
    if (arg != "select" && arg != "toggle" && arg != "off" && arg != "close" && arg != "disable" && arg != "on" && arg != "enable")
        return {.success = false, .error = "unknown hyprexpo action: " + arg};
    if (g_pOverview && g_pOverview->m_isSwiping)
        return {.success = false, .error = "already swiping"};
    if (arg == "select") {
        if (g_pOverview) {
            g_pOverview->selectHoveredWorkspace();
            g_pOverview->close();
        }
        return {};
    }
    if (arg == "off" || arg == "close" || arg == "disable" || (arg == "toggle" && g_pOverview)) {
        if (g_pOverview)
            g_pOverview->close();
        return {};
    }
    if (g_pOverview)
        return {};
    const auto monitor = Desktop::focusState()->monitor();
    if (!monitor || !monitor->m_activeWorkspace)
        return {.success = false, .error = "no active monitor or workspace"};

    renderingOverview = true;
    Hyprutils::Utils::CScopeGuard restore([] { renderingOverview = false; });
    g_pOverview = std::make_unique<COverview>(monitor->m_activeWorkspace);
    return {};
}

static void clearOverview() {
    // clearGestures() does not release Hyprland's active gesture. End ours before unloading its vtable.
    if (CExpoGesture::isActive())
        g_pTrackpadGestures->gestureEnd(IPointer::SSwipeEndEvent{.cancelled = true});
    g_pHyprRenderer->context().m_pass.removeAllOfType("COverviewPassElement");
    if (g_pOverview)
        g_pOverview->damage();
    g_pOverview.reset();
}

static void failNotif(const std::string& reason) {
    HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Failure in initialization: " + reason, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

static int luaExpo(lua_State* L) {
    const auto RESULT = onExpoDispatcher(luaL_optstring(L, 1, "toggle"));
    if (!RESULT.success)
        return luaL_error(L, "%s", RESULT.error.c_str());
    return 0;
}

static void addConfigValue(SP<Config::Values::IValue> value) {
    if (!HyprlandAPI::addConfigValueV2(PHANDLE, value)) {
        LOG(Log::ERR, "[hyprexpo] failed to register plugin value {}", value->name());
        throw std::runtime_error("[hyprexpo] Failed to register configuration");
    }
}

static int expoGesture(lua_State* L) {
    if (g_unloading)
        return 0;
    if (!lua_istable(L, 1))
        return luaL_error(L, R"(hyprexpo.gesture: expected a table, e.g. { fingers = 3, direction = "vertical", action = "expo" })");

    Config::Lua::CLuaConfigInt fingersParser(0, 2, 9);
    auto                       fingersErr = Config::Lua::Bindings::Internal::parseTableField(L, 1, "fingers", fingersParser);
    if (fingersErr.errorCode != Config::Lua::PARSE_ERROR_OK)
        return luaL_error(L, "hyprexpo.gesture: %s", fingersErr.message.c_str());

    size_t                        fingerCount = fingersParser.parsed();

    Config::Lua::CLuaConfigString dirParser("");
    auto                          dirErr = Config::Lua::Bindings::Internal::parseTableField(L, 1, "direction", dirParser);
    if (dirErr.errorCode != Config::Lua::PARSE_ERROR_OK)
        return luaL_error(L, "hyprexpo.gesture: %s", dirErr.message.c_str());

    const auto direction = g_pTrackpadGestures->dirForString(dirParser.parsed());
    if (direction == TRACKPAD_GESTURE_DIR_NONE)
        return luaL_error(L, "hyprexpo.gesture: invalid direction \"%s\"", dirParser.parsed().c_str());

    Input::ModifierMask modMask{Input::eKeyboardModifiers::HL_MODIFIER_NONE};
    lua_getfield(L, 1, "mods");
    if (!lua_isnil(L, -1)) {
        Config::Lua::CLuaConfigString modsParser("");
        auto                          modsErr = modsParser.parse(L);
        if (modsErr.errorCode != Config::Lua::PARSE_ERROR_OK) {
            lua_pop(L, 1);
            return luaL_error(L, "hyprexpo.gesture: field \"mods\": %s", modsErr.message.c_str());
        }
        modMask = Keybinds::modMaskFromString(modsParser.parsed());
    }
    lua_pop(L, 1);

    float deltaScale = 1.F;
    lua_getfield(L, 1, "scale");
    if (!lua_isnil(L, -1)) {
        Config::Lua::CLuaConfigFloat scaleParser(1.F, 0.1F, 10.F);
        auto                         scaleErr = scaleParser.parse(L);
        if (scaleErr.errorCode != Config::Lua::PARSE_ERROR_OK) {
            lua_pop(L, 1);
            return luaL_error(L, "hyprexpo.gesture: field \"scale\": %s", scaleErr.message.c_str());
        }
        deltaScale = scaleParser.parsed();
    }
    lua_pop(L, 1);

    bool disableInhibit = false;
    lua_getfield(L, 1, "disable_inhibit");
    if (!lua_isnil(L, -1)) {
        Config::Lua::CLuaConfigBool disableInhibitParser(false);
        auto                        disableInhibitErr = disableInhibitParser.parse(L);
        if (disableInhibitErr.errorCode != Config::Lua::PARSE_ERROR_OK) {
            lua_pop(L, 1);
            return luaL_error(L, "hyprexpo.gesture: field \"disable_inhibit\": %s", disableInhibitErr.message.c_str());
        }
        disableInhibit = disableInhibitParser.parsed();
    }
    lua_pop(L, 1);

    std::expected<void, std::string> result;

    Config::Lua::CLuaConfigString    actionParser("");
    auto                             actionErr = Config::Lua::Bindings::Internal::parseTableField(L, 1, "action", actionParser);
    if (actionErr.errorCode != Config::Lua::PARSE_ERROR_OK)
        return luaL_error(L, "hyprexpo.gesture: %s", actionErr.message.c_str());

    const auto& action = actionParser.parsed();
    if (action != "unset" && (direction == TRACKPAD_GESTURE_DIR_PINCH || direction == TRACKPAD_GESTURE_DIR_PINCH_IN || direction == TRACKPAD_GESTURE_DIR_PINCH_OUT))
        return luaL_error(L, "hyprexpo.gesture: expo and swish require a swipe direction");

    if (action == "expo")
        result = g_pTrackpadGestures->addGesture(makeUnique<CExpoGesture>(), fingerCount, direction, modMask, deltaScale, disableInhibit);
    else if (action == "swish")
        result = g_pTrackpadGestures->addGesture(makeUnique<CSwishGesture>(), fingerCount, direction, modMask, deltaScale, disableInhibit);
    else if (action == "unset")
        result = g_pTrackpadGestures->removeGesture(fingerCount, direction, modMask, deltaScale, disableInhibit);
    else
        return luaL_error(L, "hyprexpo.gesture: unknown action \"%s\"", action.c_str());

    if (!result)
        return luaL_error(L, "hyprexpo.gesture: %s", result.error().c_str());

    return 0;
}

static CFunctionHook* createHook(const std::string& name, std::string_view signature, void* replacement) {
    const auto matches = HyprlandAPI::findFunctionsByName(PHANDLE, name);
    const auto match   = std::ranges::find_if(matches, [&](const auto& candidate) { return candidate.demangled.starts_with(signature); });
    if (match == matches.end()) {
        failNotif("no matching function for hook " + name);
        throw std::runtime_error("[hyprexpo] No matching function for hook " + name);
    }
    const auto hook = HyprlandAPI::createFunctionHook(PHANDLE, match->address, replacement);
    if (!hook) {
        failNotif("could not create hook " + name);
        throw std::runtime_error("[hyprexpo] Could not create hook " + name);
    }
    return hook;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE     = handle;
    g_unloading = false;
    if (HyprlandAPI::getHyprlandVersion(PHANDLE).hash != GIT_COMMIT_HASH) {
        failNotif("Hyprland version mismatch; rebuild the plugin against the running version");
        throw std::runtime_error("[hyprexpo] Hyprland version mismatch");
    }

    g_pRenderWorkspaceHook = createHook("renderWorkspace", "Render::IHyprRenderer::renderWorkspace(Render::CRenderContext&, Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>,",
                                        reinterpret_cast<void*>(hkRenderWorkspace));
    g_pAddDamageHookA      = createHook("addDamage", "Monitor::CMonitor::addDamage(Hyprutils::Math::CBox const&)", reinterpret_cast<void*>(hkAddDamageA));
    g_pAddDamageHookB      = createHook("addDamage", "Monitor::CMonitor::addDamage(pixman_region32 const*)", reinterpret_cast<void*>(hkAddDamageB));

    bool success = g_pRenderWorkspaceHook->hook();
    success      = success && g_pAddDamageHookA->hook();
    success      = success && g_pAddDamageHookB->hook();

    if (!success) {
        failNotif("Failed initializing hooks");
        throw std::runtime_error("[hyprexpo] Failed initializing hooks");
    }

    preRenderListener      = Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
        if (!g_pOverview)
            return;
        if (g_pOverview->isFinished()) {
            clearOverview();
            return;
        }
        if (g_pOverview->pMonitor == monitor)
            g_pOverview->onPreRender();
    });
    monitorRemovedListener = Event::bus()->m_events.monitor.preRemoved.listen([](PHLMONITOR monitor) {
        if (g_pOverview && g_pOverview->pMonitor == monitor)
            clearOverview();
    });
    configReloadListener   = Event::bus()->m_events.config.preReload.listen([] { clearOverview(); });

    if (!HyprlandAPI::addLuaFunction(PHANDLE, "hyprexpo", "expo", ::luaExpo) || !HyprlandAPI::addLuaFunction(PHANDLE, "hyprexpo", "gesture", ::expoGesture))
        throw std::runtime_error("[hyprexpo] Failed to register plugin functions");

    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:columns", "columns", 3, Config::Values::SIntValueOptions{.min = 1, .max = Grid::MAX_COLUMNS}));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:gap_size", "gap size", 5, Config::Values::SIntValueOptions{.min = 0}));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:bg_col", "background color", 0xFF111111));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:workspace_method", "workspace method", "center current"));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:skip_empty", "skip empty workspaces", 0));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:gesture_distance", "gesture distance", 200, Config::Values::SIntValueOptions{.min = 1}));
    addConfigValue(makeShared<Config::Values::CFloatValue>("plugin:hyprswish:zoom_scale", "zoom scale", 0.9f, Config::Values::SFloatValueOptions{.min = 0.1f, .max = 1.0f}));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprswish:gesture_distance", "gesture distance", 200, Config::Values::SIntValueOptions{.min = 1}));
    HyprlandAPI::reloadConfig();

    return {"hyprexpo", "A plugin for an overview and swipe", "Ali Emre Senel", "2.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_unloading = true;
    preRenderListener.reset();
    monitorRemovedListener.reset();
    configReloadListener.reset();
    clearOverview();
    // Keep entry points available as no-ops while reload clears our registered gestures.
    Config::mgr()->reload();
    HyprlandAPI::removeLuaFunction(PHANDLE, "hyprexpo", "expo");
    HyprlandAPI::removeLuaFunction(PHANDLE, "hyprexpo", "gesture");
}
