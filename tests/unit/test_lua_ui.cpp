#include <gtest/gtest.h>
#include "apps/lua_ui_binding.hpp"
#include "ui/ui_manager.hpp"
#include "ui/view_group.hpp"

TEST(LuaUIBindingTest, LoadAndInstantiateViews) {
    lua_State* L = luaL_newstate();
    ASSERT_NE(L, nullptr);
    luaL_openlibs(L);
    luaopen_aurora_ui(L);

    const char* script = R"(
        local vg = aurora.ui.ViewGroup(0, 0, 100, 100)
        local tv = aurora.ui.TextView(10, 10, "Hello", 65535)
        local arc = aurora.ui.ArcProgress(50, 50, 20, 75, 63488)
        vg:add_child(tv)
        vg:add_child(arc)
        aurora.ui.set_root_view(vg)
    )";

    int result = luaL_dostring(L, script);
    if (result != LUA_OK) {
        printf("Lua error: %s\n", lua_tostring(L, -1));
    }
    EXPECT_EQ(result, LUA_OK);

    UI::ViewGroup* root = UI::UiManager::instance().get_root_view();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->get_width(), 100);

    // Clean up manually since Lua GC doesn't delete the C++ objects in our binding
    delete root;
    UI::UiManager::instance().set_root_view(nullptr);

    lua_close(L);
}
