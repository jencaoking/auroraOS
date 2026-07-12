#ifndef AURORA_MINI_PROGRAM_ENGINE_HPP
#define AURORA_MINI_PROGRAM_ENGINE_HPP

#include <stdint.h>
#include "posix.hpp"
#include "../drivers/sensor/sensor_framework.hpp"
#include "../drivers/display/framebuffer.hpp"

// 引入第三方 Lua 虚拟机 C 接口
extern "C" {
    #include "../3rdparty/lua/lua.h"
    #include "../3rdparty/lua/lualib.h"
    #include "../3rdparty/lua/lauxlib.h"
}

// 声明外部全局的图形缓冲引擎 (用于供 Lua 脚本调用画图)
extern FrameBuffer<128, 128> g_fb;

class MiniProgramEngine {
private:
    lua_State* L_;
    bool       is_loaded_;

    // ========================================================
    // 系统 API 绑定 1：暴露获取心率的 C++ 函数给 Lua
    // ========================================================
    static int api_get_heart_rate(lua_State* L) {
        extern HealthSensor g_health_sensor;
        SensorDevice* hr_sensor = &g_health_sensor;
        if (hr_sensor) {
            hr_sensor->sample_fetch();
            SensorValue val;
            hr_sensor->channel_get(SensorChannel::HEART_RATE, &val);
            lua_pushinteger(L, val.val1); // 将心率值压入 Lua 栈返回
            return 1;
        }
        lua_pushinteger(L, 0);
        return 1;
    }

    // ========================================================
    // 系统 API 绑定 2：暴露 UI 绘制函数给 Lua
    // ========================================================
    static int api_fill_rect(lua_State* L) {
        // 从 Lua 获取 5 个参数: x, y, width, height, color_rgb565
        int x = luaL_checkinteger(L, 1);
        int y = luaL_checkinteger(L, 2);
        int w = luaL_checkinteger(L, 3);
        int h = luaL_checkinteger(L, 4);
        int color = luaL_checkinteger(L, 5);

        g_fb.fill_rect(x, y, w, h, static_cast<ColorRGB565>(color));
        return 0; // 无返回值
    }

    static int api_print(lua_State* L) {
        const char* str = luaL_checkstring(L, 1);
        int fd = open("/dev/uart0", 0);
        write(fd, "[Lua App] ", 10);
        
        int len = 0;
        while (str[len]) len++;
        
        write(fd, str, len);
        write(fd, "\r\n", 2);
        close(fd);
        return 0;
    }

public:
    MiniProgramEngine() : L_(nullptr), is_loaded_(false) {}

    ~MiniProgramEngine() {
        if (L_) lua_close(L_);
    }

    // 初始化虚拟机并注册 API 命名空间
    bool init() {
        L_ = luaL_newstate(); // 创建隔离的沙盒虚拟机
        if (!L_) return false;
        
        // 我们裁剪了标准的 linit.c，只手动加载最核心的 base 库
        luaL_requiref(L_, "_G", luaopen_base, 1);
        lua_pop(L_, 1);

        // 将底层的 C++ 函数注册为 Lua 全局命名空间 aurora 的方法
        lua_newtable(L_);
        
        lua_pushcfunction(L_, api_get_heart_rate);
        lua_setfield(L_, -2, "get_heart_rate");

        lua_pushcfunction(L_, api_fill_rect);
        lua_setfield(L_, -2, "fill_rect");

        lua_pushcfunction(L_, api_print);
        lua_setfield(L_, -2, "print");

        lua_setglobal(L_, "aurora"); // 注册全局变量 aurora

        return true;
    }

    // 从字符串 (或 LittleFS 文件) 加载应用脚本代码
    bool load_app(const char* script_code) {
        if (!L_) return false;
        if (luaL_dostring(L_, script_code) != LUA_OK) {
            int fd = open("/dev/uart0", 0);
            write(fd, "Lua Load Error!\r\n", 17);
            close(fd);
            return false;
        }
        is_loaded_ = true;
        return true;
    }

    // 触发脚本的生命周期钩子函数
    void call_hook(const char* hook_name) {
        if (!is_loaded_ || !L_) return;

        lua_getglobal(L_, hook_name);
        if (lua_isfunction(L_, -1)) {
            // 安全调用 Lua 函数，0个参数，0个返回值
            if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
                lua_pop(L_, 1); // 弹出错误信息
            }
        } else {
            lua_pop(L_, 1); // 如果不是函数则出栈
        }
    }
};

#endif
