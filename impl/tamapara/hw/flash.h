#ifndef FLASH_H
#define FLASH_H
#include "io.h"
void flash_prog4k(u32 addr, const void *src);
void flash_read(u32 addr, void *dst, u32 len);
int  flash_same(u32 addr, const void *src, u32 len);
int  flash_update4k(u32 addr, const void *src);
void flash_erase4k(u32 addr);
void flash_prog(u32 addr, const void *src, u32 len);
#endif
