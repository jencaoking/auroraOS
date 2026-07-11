#ifndef RAMFS_HPP
#define RAMFS_HPP

#include "vfs.hpp"

// 继承自 VNode 的内存常规文件
class RamFile : public VNode {
private:
    char* data_;
    int capacity_;
    int file_size_;

public:
    // 初始化时在堆上开辟指定容量的内存
    RamFile(int capacity = 512);
    ~RamFile();

    int read(char* buf, int len, int offset) override;
    int write(const char* buf, int len, int offset) override;
    
    int get_size() const { return file_size_; }
};

#endif
