#include "fishhook.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

// 根据是否是64位环境，定义相应的结构体类型和命令
#ifdef __LP64__
typedef struct mach_header_64 mach_header_t;
typedef struct segment_command_64 segment_command_t;
typedef struct section_64 section_t;
typedef struct nlist_64 nlist_t;
#define LC_SEGMENT_ARCH_DEPENDENT LC_SEGMENT_64
#else
typedef struct mach_header mach_header_t;
typedef struct segment_command segment_command_t;
typedef struct section section_t;
typedef struct nlist nlist_t;
#define LC_SEGMENT_ARCH_DEPENDENT LC_SEGMENT
#endif

// 定义常量数据段名，如果未定义则定义为 "__DATA_CONST"
#ifndef SEG_DATA_CONST
#define SEG_DATA_CONST  "__DATA_CONST"
#endif

// 定义重绑定条目结构体，用于存储重绑定信息
struct rebindings_entry {
  struct rebinding *rebindings;          // 重绑定数组
  size_t rebindings_nel;                 // 重绑定数组的元素数量
  struct rebindings_entry *next;         // 指向下一个重绑定条目的指针
};

static struct rebindings_entry *_rebindings_head; // 重绑定链表的头指针

/**
 * @brief 在重绑定链表头部添加新的重绑定条目
 *
 * @param rebindings_head 当前重绑定链表的头指针
 * @param rebindings 新的重绑定数组
 * @param nel 新的重绑定数组的元素数量
 * @return int 成功返回0，失败返回-1
 */
static int prepend_rebindings(struct rebindings_entry **rebindings_head,
                              struct rebinding rebindings[],
                              size_t nel) {
  // 分配新的重绑定条目
  struct rebindings_entry *new_entry = (struct rebindings_entry *) malloc(sizeof(struct rebindings_entry));
  if (!new_entry) {
    return -1; // 分配失败
  }
  // 分配并复制重绑定数组
  new_entry->rebindings = (struct rebinding *) malloc(sizeof(struct rebinding) * nel);
  if (!new_entry->rebindings) {
    free(new_entry);
    return -1; // 分配失败
  }
  memcpy(new_entry->rebindings, rebindings, sizeof(struct rebinding) * nel);
  new_entry->rebindings_nel = nel;
  new_entry->next = *rebindings_head; // 将新条目插入链表头部
  *rebindings_head = new_entry;
  return 0; // 成功
}

#if 0
/**
 * @brief 获取指定地址的内存保护属性
 *
 * @param addr 目标地址
 * @param prot 存储当前保护属性的指针
 * @param max_prot 存储最大保护属性的指针
 * @return int 成功返回0，失败返回-1
 */
static int get_protection(void *addr, vm_prot_t *prot, vm_prot_t *max_prot) {
  mach_port_t task = mach_task_self(); // 获取当前任务
  vm_size_t size = 0;
  vm_address_t address = (vm_address_t)addr;
  memory_object_name_t object;
#ifdef __LP64__
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  vm_region_basic_info_data_64_t info;
  kern_return_t info_ret = vm_region_64(
      task, &address, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_64_t)&info, &count, &object);
#else
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT;
  vm_region_basic_info_data_t info;
  kern_return_t info_ret = vm_region(task, &address, &size, VM_REGION_BASIC_INFO, (vm_region_info_t)&info, &count, &object);
#endif
  if (info_ret == KERN_SUCCESS) {
    if (prot != NULL)
      *prot = info.protection;

    if (max_prot != NULL)
      *max_prot = info.max_protection;

    return 0;
  }

  return -1;
}
#endif

/**
 * @brief 对指定段中的符号进行重绑定
 *
 * @param rebindings 当前的重绑定条目
 * @param section 目标段的section
 * @param slide 地址滑动量
 * @param symtab 符号表
 * @param strtab 字符串表
 * @param indirect_symtab 间接符号表
 */
static void perform_rebinding_with_section(struct rebindings_entry *rebindings,
                                           section_t *section,
                                           intptr_t slide,
                                           nlist_t *symtab,
                                           char *strtab,
                                           uint32_t *indirect_symtab) {
  // 获取间接符号索引数组
  uint32_t *indirect_symbol_indices = indirect_symtab + section->reserved1;
  // 获取间接符号绑定数组的实际地址
  void **indirect_symbol_bindings = (void **)((uintptr_t)slide + section->addr);

  // 遍历所有符号指针
  for (uint i = 0; i < section->size / sizeof(void *); i++) {
    uint32_t symtab_index = indirect_symbol_indices[i];
    // 跳过绝对符号和本地符号
    if (symtab_index == INDIRECT_SYMBOL_ABS || symtab_index == INDIRECT_SYMBOL_LOCAL ||
        symtab_index == (INDIRECT_SYMBOL_LOCAL   | INDIRECT_SYMBOL_ABS)) {
      continue;
    }
    // 获取符号名
    uint32_t strtab_offset = symtab[symtab_index].n_un.n_strx;
    char *symbol_name = strtab + strtab_offset;
    bool symbol_name_longer_than_1 = symbol_name[0] && symbol_name[1];
    struct rebindings_entry *cur = rebindings;
    // 遍历所有重绑定条目
    while (cur) {
      for (uint j = 0; j < cur->rebindings_nel; j++) {
        // 比较符号名，跳过第一个字符（通常是前缀下划线）
        if (symbol_name_longer_than_1 && strcmp(&symbol_name[1], cur->rebindings[j].name) == 0) {
          kern_return_t err;

          // 如果有替换函数且当前绑定不等于替换函数，则保存原函数地址
          if (cur->rebindings[j].replaced != NULL && indirect_symbol_bindings[i] != cur->rebindings[j].replacement)
            *(cur->rebindings[j].replaced) = indirect_symbol_bindings[i];

          /**
           * 1. 将修改vm保护的代码移动到这里，以减少更改范围。
           * 2. 无条件添加VM_PROT_WRITE模式，因为某些iOS/Mac上的vm_region API报告的vm保护属性不匹配。
           * -- Lianfu Hao 2021年6月16日
           **/
          err = vm_protect (mach_task_self (), (uintptr_t)indirect_symbol_bindings, section->size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
          if (err == KERN_SUCCESS) {
            /**
             * 一旦无法更改vm保护，我们
             * 必须不要继续下面的写操作！
             * iOS 15已经修正了const段的保护。
             * -- Lionfore Hao 2021年6月11日
             **/
            indirect_symbol_bindings[i] = cur->rebindings[j].replacement;
          }
          goto symbol_loop; // 跳出当前符号的处理
        }
      }
      cur = cur->next; // 处理下一个重绑定条目
    }
  symbol_loop:;
  }
}

/**
 * @brief 对Mach-O镜像中的符号进行重绑定
 *
 * @param rebindings 当前的重绑定链表
 * @param header Mach-O镜像的头部
 * @param slide 地址滑动量
 */
static void rebind_symbols_for_image(struct rebindings_entry *rebindings,
                                     const struct mach_header *header,
                                     intptr_t slide) {
  Dl_info info;
  // 获取镜像的信息，如果失败则返回
  if (dladdr(header, &info) == 0) {
    return;
  }

  segment_command_t *cur_seg_cmd;
  segment_command_t *linkedit_segment = NULL; // LINKEDIT段
  struct symtab_command* symtab_cmd = NULL;    // 符号表命令
  struct dysymtab_command* dysymtab_cmd = NULL; // 动态符号表命令

  uintptr_t cur = (uintptr_t)header + sizeof(mach_header_t);
  // 遍历所有加载命令，查找LINKEDIT段和符号表相关命令
  for (uint i = 0; i < header->ncmds; i++, cur += cur_seg_cmd->cmdsize) {
    cur_seg_cmd = (segment_command_t *)cur;
    if (cur_seg_cmd->cmd == LC_SEGMENT_ARCH_DEPENDENT) {
      if (strcmp(cur_seg_cmd->segname, SEG_LINKEDIT) == 0) {
        linkedit_segment = cur_seg_cmd;
      }
    } else if (cur_seg_cmd->cmd == LC_SYMTAB) {
      symtab_cmd = (struct symtab_command*)cur_seg_cmd;
    } else if (cur_seg_cmd->cmd == LC_DYSYMTAB) {
      dysymtab_cmd = (struct dysymtab_command*)cur_seg_cmd;
    }
  }

  // 如果缺少必要的命令或间接符号数为0，则返回
  if (!symtab_cmd || !dysymtab_cmd || !linkedit_segment ||
      !dysymtab_cmd->nindirectsyms) {
    return;
  }

  // 计算LINKEDIT段的基地址
  uintptr_t linkedit_base = (uintptr_t)slide + linkedit_segment->vmaddr - linkedit_segment->fileoff;
  nlist_t *symtab = (nlist_t *)(linkedit_base + symtab_cmd->symoff); // 符号表
  char *strtab = (char *)(linkedit_base + symtab_cmd->stroff); // 字符串表

  // 获取间接符号表（指向符号表索引的uint32_t数组）
  uint32_t *indirect_symtab = (uint32_t *)(linkedit_base + dysymtab_cmd->indirectsymoff);

  cur = (uintptr_t)header + sizeof(mach_header_t);
  // 再次遍历所有加载命令，查找DATA和DATA_CONST段
  for (uint i = 0; i < header->ncmds; i++, cur += cur_seg_cmd->cmdsize) {
    cur_seg_cmd = (segment_command_t *)cur;
    if (cur_seg_cmd->cmd == LC_SEGMENT_ARCH_DEPENDENT) {
      if (strcmp(cur_seg_cmd->segname, SEG_DATA) != 0 &&
          strcmp(cur_seg_cmd->segname, SEG_DATA_CONST) != 0) {
        continue; // 只处理DATA和DATA_CONST段
      }
      // 遍历段中的所有section
      for (uint j = 0; j < cur_seg_cmd->nsects; j++) {
        section_t *sect =
          (section_t *)(cur + sizeof(segment_command_t)) + j;
        // 处理延迟符号指针段
        if ((sect->flags & SECTION_TYPE) == S_LAZY_SYMBOL_POINTERS) {
          perform_rebinding_with_section(rebindings, sect, slide, symtab, strtab, indirect_symtab);
        }
        // 处理非延迟符号指针段
        if ((sect->flags & SECTION_TYPE) == S_NON_LAZY_SYMBOL_POINTERS) {
          perform_rebinding_with_section(rebindings, sect, slide, symtab, strtab, indirect_symtab);
        }
      }
    }
  }
}

/**
 * @brief 内部函数，用于对单个镜像进行符号重绑定
 *
 * @param header Mach-O镜像的头部
 * @param slide 地址滑动量
 */
static void _rebind_symbols_for_image(const struct mach_header *header,
                                      intptr_t slide) {
    rebind_symbols_for_image(_rebindings_head, header, slide);
}

/**
 * @brief 对指定的Mach-O镜像进行符号重绑定
 *
 * @param header 镜像的头部指针
 * @param slide 地址滑动量
 * @param rebindings 重绑定数组
 * @param rebindings_nel 重绑定数组的元素数量
 * @return int 成功返回0，失败返回-1
 */
int rebind_symbols_image(void *header,
                         intptr_t slide,
                         struct rebinding rebindings[],
                         size_t rebindings_nel) {
    struct rebindings_entry *rebindings_head = NULL;
    // 在本地链表头部添加新的重绑定条目
    int retval = prepend_rebindings(&rebindings_head, rebindings, rebindings_nel);
    // 对指定镜像进行符号重绑定
    rebind_symbols_for_image(rebindings_head, (const struct mach_header *) header, slide);
    // 释放临时分配的重绑定链表
    if (rebindings_head) {
      free(rebindings_head->rebindings);
    }
    free(rebindings_head);
    return retval;
}

/**
 * @brief 对所有已加载的Mach-O镜像进行符号重绑定
 *
 * @param rebindings 重绑定数组
 * @param rebindings_nel 重绑定数组的元素数量
 * @return int 成功返回0，失败返回-1
 */
int rebind_symbols(struct rebinding rebindings[], size_t rebindings_nel) {
  // 在全局重绑定链表头部添加新的重绑定条目
  int retval = prepend_rebindings(&_rebindings_head, rebindings, rebindings_nel);
  if (retval < 0) {
    return retval; // 添加失败
  }
  // 如果这是第一次调用，注册回调函数以处理新加载的镜像
  if (!_rebindings_head->next) {
    _dyld_register_func_for_add_image(_rebind_symbols_for_image);
  } else {
    // 对已存在的所有镜像进行符号重绑定
    uint32_t c = _dyld_image_count();
    for (uint32_t i = 0; i < c; i++) {
      _rebind_symbols_for_image(_dyld_get_image_header(i), _dyld_get_image_vmaddr_slide(i));
    }
  }
  return retval; // 返回添加结果
}
