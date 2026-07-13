#include "lua_ui_binding.hpp"
#include "../ui/ui_manager.hpp"
#include "../ui/view_group.hpp"
#include "../ui/widgets/text_view.hpp"
#include "../ui/widgets/arc_progress.hpp"
#include "../kernel/memory.hpp"

using namespace UI;

struct ViewUserData {
    View* view;
};

static View* check_view(lua_State* L, int index) {
    void* ud = luaL_checkudata(L, index, "aurora.ui.View");
    luaL_argcheck(L, ud != nullptr, index, "`View` expected");
    return static_cast<ViewUserData*>(ud)->view;
}

static int text_view_new(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    int fg = luaL_checkinteger(L, 4);
    int bg = luaL_optinteger(L, 5, 0);
    int scale = luaL_optinteger(L, 6, 2);

    TextView* tv = new TextView(x, y, text, fg, bg, scale);
    
    ViewUserData* ud = static_cast<ViewUserData*>(lua_newuserdata(L, sizeof(ViewUserData)));
    ud->view = tv;

    luaL_getmetatable(L, "aurora.ui.View");
    lua_setmetatable(L, -2);
    return 1;
}

static int view_group_new(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);

    ViewGroup* vg = new ViewGroup(x, y, w, h);
    
    ViewUserData* ud = static_cast<ViewUserData*>(lua_newuserdata(L, sizeof(ViewUserData)));
    ud->view = vg;

    luaL_getmetatable(L, "aurora.ui.View");
    lua_setmetatable(L, -2);
    return 1;
}

static int arc_progress_new(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int r = luaL_checkinteger(L, 3);
    int val = luaL_checkinteger(L, 4);
    int color = luaL_checkinteger(L, 5);

    ArcProgress* arc = new ArcProgress(x, y, r, val, color);

    ViewUserData* ud = static_cast<ViewUserData*>(lua_newuserdata(L, sizeof(ViewUserData)));
    ud->view = arc;

    luaL_getmetatable(L, "aurora.ui.View");
    lua_setmetatable(L, -2);
    return 1;
}

static int view_add_child(lua_State* L) {
    View* parent = check_view(L, 1);
    View* child = check_view(L, 2);
    
    ViewGroup* vg = static_cast<ViewGroup*>(parent);
    vg->add_child(child);
    return 0;
}

static int text_view_set_text(lua_State* L) {
    View* v = check_view(L, 1);
    const char* text = luaL_checkstring(L, 2);
    static_cast<TextView*>(v)->set_text(text);
    return 0;
}

static int arc_progress_set_percentage(lua_State* L) {
    View* v = check_view(L, 1);
    int percent = luaL_checkinteger(L, 2);
    static_cast<ArcProgress*>(v)->set_percentage(percent);
    return 0;
}

static int set_root_view(lua_State* L) {
    View* v = check_view(L, 1);
    UiManager::instance().set_root_view(static_cast<ViewGroup*>(v));
    return 0;
}

static const struct luaL_Reg ui_funcs[] = {
    {"TextView", text_view_new},
    {"ViewGroup", view_group_new},
    {"ArcProgress", arc_progress_new},
    {"set_root_view", set_root_view},
    {NULL, NULL}
};

static const struct luaL_Reg view_methods[] = {
    {"add_child", view_add_child},
    {"set_text", text_view_set_text},
    {"set_percentage", arc_progress_set_percentage},
    {NULL, NULL}
};

void luaopen_aurora_ui(lua_State* L) {
    luaL_newmetatable(L, "aurora.ui.View");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, view_methods, 0);
    lua_pop(L, 1);

    lua_getglobal(L, "aurora");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "aurora");
        lua_getglobal(L, "aurora");
    }

    lua_newtable(L);
    luaL_setfuncs(L, ui_funcs, 0);
    lua_setfield(L, -2, "ui"); 

    lua_pop(L, 1); 
}
