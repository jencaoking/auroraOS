#ifndef ELF_LOADER_HPP
#define ELF_LOADER_HPP

class ElfLoader {
public:
    // 从 VFS 路径中加载并创建一个全新的动态后台进程
    static bool load_and_exec(const char* filepath);
};

#endif
