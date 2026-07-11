#ifndef ELF_HPP
#define ELF_HPP

#include <stdint.h>

// ELF 魔数标识
#define EI_NIDENT 16
#define ELFMAG0   0x7f
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'

// 架构与目标指令集
#define EM_ARM    40      // ARM 架构
#define PT_LOAD   1       // 可加载的程序段 (Program Header Loadable Segment)

// 1. ELF 32位 文件头
struct Elf32_Ehdr {
    uint8_t  e_ident[EI_NIDENT]; // 魔数与硬件标识
    uint16_t e_type;             // 文件类型 (可执行文件 2)
    uint16_t e_machine;          // 目标架构 (EM_ARM)
    uint32_t e_version;          // 版本
    uint32_t e_entry;            // 【绝对核心】程序执行的入口虚拟地址
    uint32_t e_phoff;            // 程序段表在文件中的偏移量
    uint32_t e_shoff;            // 节区表偏移量
    uint32_t e_flags;            // 处理器特定标志
    uint16_t e_ehsize;           // ELF 头部大小
    uint16_t e_phentsize;        // 程序段头部每个条目的大小
    uint16_t e_phnum;            // 程序段的个数
    uint16_t e_shentsize;        // 节区头部大小
    uint16_t e_shnum;            // 节区个数
    uint16_t e_shstrndx;         // 节区名称字符串表索引
};

// 2. ELF 32位 程序段头 (Program Header)
struct Elf32_Phdr {
    uint32_t p_type;             // 段类型 (必须寻找 PT_LOAD)
    uint32_t p_offset;           // 该段在 ELF 文件中的位置
    uint32_t p_vaddr;            // 期望映射到内存中的虚拟地址
    uint32_t p_paddr;            // 物理地址
    uint32_t p_filesz;           // 在 ELF 文件中所占的实际字节数 (.text + .data)
    uint32_t p_memsz;            // 在内存中所需占用字节数 (多出来的部分就是未初始化的 .bss)
    uint32_t p_flags;            // 读/写/执行 权限
    uint32_t p_align;            // 内存对齐
};

#endif
