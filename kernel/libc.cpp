#include <stddef.h>
#include <stdint.h>

extern "C" {

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dest);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(s);
    for (size_t i = 0; i < n; i++) {
        p[i] = static_cast<uint8_t>(c);
    }
    return s;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = static_cast<const uint8_t*>(s1);
    const uint8_t* p2 = static_cast<const uint8_t*>(s2);
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dest);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

int atoi(const char* str) {
    int res = 0;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res * sign;
}

int errno = 0;

// Dummy _ctype_ array to satisfy newlib's <ctype.h> macros if they are not overridden
extern const char _ctype_[257] = {
	0,
	0,0,0,0,0,0,0,0,
	0,0x28,0x28,0x28,0x28,0x28,0,0,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0x48,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
	0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
	0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,
	0x04,0x04,0x10,0x10,0x10,0x10,0x10,0x10,
	0x10,0x81,0x81,0x81,0x81,0x81,0x81,0x01,
	0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
	0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
	0x01,0x01,0x01,0x10,0x10,0x10,0x10,0x10,
	0x10,0x82,0x82,0x82,0x82,0x82,0x82,0x02,
	0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
	0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
	0x02,0x02,0x02,0x10,0x10,0x10,0x10,0x20
};

// Wrapper for sys_print to be callable from C code (like lwIP)
#include "syscall.hpp"
void sys_print_c(const char* str) {
    sys_print(str);
}

} // extern "C"
