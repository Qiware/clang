#if !defined(__COMM_H__)
#define __COMM_H__

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <memory.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/shm.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/resource.h>

/* 宏定义 */
#define FILE_NAME_MAX_LEN   (256)           /* 文件名最大长度 */
#define FILE_PATH_MAX_LEN  FILE_NAME_MAX_LEN/* 文件路径最大长度 */
#define FILE_LINE_MAX_LEN   (1024)          /* 文件行最大长度 */
#define IP_ADDR_MAX_LEN     (32)            /* IP地址最大长度 */
#define CMD_LINE_MAX_LEN    (1024)          /* 命令行最大长度 */
#define UDP_MAX_LEN         (1472)          /* UDP最大承载长度 */
#define QUEUE_NAME_MAX_LEN  (64)            /* 队列名最大长度 */
#define TABLE_NAME_MAX_LEN  (64)            /* 表名最大长度 */
#define ERR_MSG_MAX_LEN     (1024)          /* 错误信息最大长度 */

#define MD5_SUM_CHK_LEN     (32)            /* MD5校验值长度 */

#define INVALID_FD          (-1)            /* 非法文件描述符 */
#define INVALID_PID         (-1)            /* 非法进程ID */

/* 内存单位 */
#define KB                  (1024)          /* KB */
#define MB                  (1024 * KB)     /* MB */
#define GB                  (1024 * MB)     /* GB */

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* 将秒折算成: D天H时M分S秒 */
#define TM_DAY(sec)  ((sec) / (24*60*60))                /* 天 */
#define TM_HOUR(sec) (((sec) % (24*60*60))/(60*60))      /* 时 */
#define TM_MIN(sec)  ((((sec) % (24*60*60))%(60*60))/60) /* 分 */
#define TM_SEC(sec)  ((((sec) % (24*60*60))%(60*60))%60) /* 秒 */

/* 内存对齐 */
#define mem_align(d, a)     (((d) + (a - 1)) & ~(a - 1))
#define mem_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))
#define PTR_ALIGNMENT   sizeof(unsigned long)

/******************************************************************************
 **函数名称: key_cb_t
 **功    能: 为唯一键产生KEY值
 **输入参数: 
 **     pkey: 主键(任意数据类型, 但该值必须是唯一的)
 **     pkey_len: 主键长度
 **输出参数: NONE
 **返    回: KEY值
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.09 #
 ******************************************************************************/
typedef uint32_t (*key_cb_t)(const void *pkey, size_t pkey_len);

/******************************************************************************
 **函数名称: mem_alloc_cb_t
 **功    能: 分配内存回调类型
 **输入参数: 
 **     pool: 内存池
 **     size: 分配空间
 **输出参数:
 **返    回: 内存地址
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.12.26 #
 ******************************************************************************/
typedef void * (*mem_alloc_cb_t)(void *pool, size_t size);

void *mem_alloc(void *pool, size_t size);

/******************************************************************************
 **函数名称: mem_dealloc_cb_t
 **功    能: 回收内存回调类型
 **输入参数: 
 **     pool: 内存池
 **     p: 内存地址
 **输出参数:
 **返    回: VOID
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.12.26 #
 ******************************************************************************/
typedef void (*mem_dealloc_cb_t)(void *pool, void *p);

void mem_dealloc(void *pool, void *p);

#endif /*__COMM_H__*/