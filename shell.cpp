#include "shell.hpp"
#include "vfs.hpp"
#include "syscall.hpp"
#include "elf_loader.hpp"

bool Shell::strings_equal(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2) return false;
        s1++; s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

void Shell::execute_command(const char* cmd) {
    auto print = [&](const char* str) { sys_print(str); };

    if (cmd[0] == '\0') return;

    if (strings_equal(cmd, "help")) {
        print("auroraOS built-in commands:\r\n");
        print("  help   - Show this message\r\n");
        print("  cat    - Read data from /tmp/log.txt\r\n");
        print("  about  - Show system information\r\n");
        print("  exec   - Launch dynamic ELF app\r\n");
    } else if (strings_equal(cmd, "cat")) {
        int fd = VfsManager::instance().open("/tmp/log.txt");
        if (fd >= 0) {
            char buf[64];
            VfsManager::instance().lseek(fd, 0);
            int bytes = VfsManager::instance().read(fd, buf, sizeof(buf)-1);
            if (bytes > 0) {
                buf[bytes] = '\0';
                print(buf);
                print("\r\n");
            }
            VfsManager::instance().close(fd);
        } else {
            print("Failed to open /tmp/log.txt\r\n");
        }
    } else if (strings_equal(cmd, "about")) {
        print("auroraOS v0.1 - Microkernel RTOS\r\n");
        print("Architecture: ARM Cortex-M4\r\n");
    } else if (strings_equal(cmd, "exec")) {
        print("Launching dynamic application from /tmp/app.elf...\r\n");
        bool success = ElfLoader::load_and_exec("/tmp/app.elf");
        if (success) {
            print(">> Dynamic application loaded into Scheduler successfully!\r\n");
        } else {
            print(">> Failed to load application.\r\n");
        }
    } else {
        print("Unknown command: ");
        print(cmd);
        print("\r\n");
    }
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
