#ifndef SHELL_HPP
#define SHELL_HPP

class Shell {
public:
    // 启动 Shell 阻塞监听循环
    static void run();

private:
    static void execute_command(const char* cmd);
    static bool strings_equal(const char* s1, const char* s2);
};

#endif
