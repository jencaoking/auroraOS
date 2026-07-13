#include <gtest/gtest.h>
#include "../../apps/watch/screens/dynamic_watch_face_screen.hpp"
#include "../../kernel/memory.hpp"
#include "../../vfs/vfs.hpp"
#include "../../vfs/ramfs.hpp"

using namespace aurora;
using namespace aurora::watch;

void aurora_get_time(uint32_t& h, uint32_t& m) {
    h = 10;
    m = 9;
}

// Test fixture for Lua dynamic watch faces
class DynamicWatchFaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        KernelHeap::instance().init();
        VfsManager::instance().init();

        // Mount a RAMFS to simulate LittleFS
        ramfs_ = new RamFs();
        VfsManager::instance().mount("/lfs", ramfs_);

        // Write a valid Lua watch face script to VFS
        const char* lua_script = 
            "function create_ui()\n"
            "    local vg = aurora.ui.ViewGroup(0, 0, 192, 490)\n"
            "    local tv = aurora.ui.TextView(20, 100, \"Lua Time\", 65535, 0, 4)\n"
            "    vg:add_child(tv)\n"
            "    return vg\n"
            "end\n"
            "function on_create()\n"
            "    aurora.print(\"WF created\")\n"
            "end\n";

        int fd = VfsManager::instance().open("/lfs/test_wf.lua", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            VfsManager::instance().write(fd, lua_script, strlen(lua_script));
            VfsManager::instance().close(fd);
        }
    }

    void TearDown() override {
        delete ramfs_;
    }

    RamFs* ramfs_;
};

TEST_F(DynamicWatchFaceTest, LoadsAndCreatesLuaUI) {
    DynamicWatchFaceScreen screen("/lfs/test_wf.lua");
    
    // on_create will load the Lua script, run create_ui(), and add the returned ViewGroup as a child.
    screen.on_create();

    // The screen itself is a ViewGroup. If the Lua script successfully returned a ViewGroup,
    // the screen should now have 1 child.
    const auto& children = screen.get_children();
    EXPECT_EQ(children.size(), 1);

    // Get the child (the Lua ViewGroup)
    UI::View* lua_vg = children[0];
    EXPECT_NE(lua_vg, nullptr);

    // Call on_show, it should call Lua's on_show (though not defined, it shouldn't crash)
    screen.on_show();
}

TEST_F(DynamicWatchFaceTest, FailsGracefullyOnInvalidFile) {
    DynamicWatchFaceScreen screen("/lfs/non_existent.lua");
    
    // Shouldn't crash
    screen.on_create();

    // No children should be added
    EXPECT_EQ(screen.get_children().size(), 0);
}
