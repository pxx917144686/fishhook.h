#ifndef fishhook_h
#define fishhook_h

#include <stddef.h>
#include <stdint.h>

// 定义符号可见性
#if !defined(FISHHOOK_EXPORT)
#define FISHHOOK_VISIBILITY __attribute__((visibility("hidden")))
#else
#define FISHHOOK_VISIBILITY __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*
 * 重绑定结构体，包含符号名称、替换函数和原函数指针
 */
struct rebinding {
  const char *name;    // 符号名称
  void *replacement;   // 替换函数
  void **replaced;     // 原函数指针
};

/*
 * 重绑定符号函数
 * 将指定的符号重绑定到替换函数
 */
FISHHOOK_VISIBILITY
int rebind_symbols(struct rebinding rebindings[], size_t rebindings_nel);

/*
 * 针对特定镜像重绑定符号
 * 仅对指定的Mach-O镜像进行重绑定
 */
FISHHOOK_VISIBILITY
int rebind_symbols_image(void *header,
                         intptr_t slide,
                         struct rebinding rebindings[],
                         size_t rebindings_nel);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // fishhook_h
