#include "shell.hpp"
#include "vfs.hpp"

bool Shell::strings_equal(const char* s1, const char* s2) {
    while (*s1 && *s2 && *s1 == *s2) { s1++; s2++; }
    return (*s1 == '\0' && *s2 == '\0');
}

void Shell::execute_command(const char* cmd) {
    // 复用 VFS 接口，打开串口作为 stdout
    int stdout_fd = VfsManager::instance().open("/dev/tty0");
    if (stdout_fd < 0) return;

    auto print = [&](const char* str) {
        int len = 0; while(str[len]) len++;
        VfsManager::instance().write(stdout_fd, str, len);
    };

    if (strings_equal(cmd, "help")) {
        print("auroraOS built-in commands:\r\n");
        print("  help   - Show this message\r\n");
        print("  cat    - Read data from /tmp/log.txt\r\n");
        print("  about  - Show system information\r\n");
    } 
    else if (strings_equal(cmd, "cat")) {
        // 利用 VFS 尝试打开内存文件系统
        int file_fd = VfsManager::instance().open("/tmp/log.txt");
        if (file_fd >= 0) {
            char buf[128];
            // 确保游标回到文件头部
            VfsManager::instance().lseek(file_fd, 0);
            int bytes = VfsManager::instance().read(file_fd, buf, 127);
            if (bytes > 0) {
                buf[bytes] = '\0';
                print(buf);
                print("\r\n");
            }
            VfsManager::instance().close(file_fd);
        } else {
            print("[Error] Cannot open /tmp/log.txt\r\n");
        }
    } 
    else if (strings_equal(cmd, "about")) {
        print("auroraOS v0.1 - Microkernel RTOS\r\n");
        print("Architecture: ARM Cortex-M4\r\n");
    } 
    else if (cmd[0] != '\0') {
        print("aurorash: command not found: ");
        print(cmd);
        print("\r\n");
    }
    
    VfsManager::instance().close(stdout_fd);
}

void Shell::run() {
    int stdin_fd = VfsManager::instance().open("/dev/tty0");
    if (stdin_fd < 0) return;

    const char* prompt = "aurora> ";
    char cmd_buf[64];

    while (true) {
        // 1. 打印终端提示符
        int p_len = 0; while(prompt[p_len]) p_len++;
        VfsManager::instance().write(stdin_fd, prompt, p_len);

        // 2. 阻塞读取用户输入 (底层会自动 Yield 让出 CPU)
        VfsManager::instance().read(stdin_fd, cmd_buf, sizeof(cmd_buf));
        
        // 3. 执行对应的应用逻辑
        execute_command(cmd_buf);
    }
}
