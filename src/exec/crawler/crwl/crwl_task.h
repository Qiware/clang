#if !defined(__CRWL_TASK_H__)
#define __CRWL_TASK_H__

#include <stdint.h>
#include <pthread.h>

#include "queue.h"

/* 爬虫任务类型 */
typedef enum
{
    CRWL_TASK_TYPE_UNKNOWN                  /* 未知任务 */
    , CRWL_TASK_LOAD_PAGE                   /* 加载页面的任务 */

    , CRWL_TASK_TYPE_TOTAL                  /* 任务总数 */
} crwl_task_type_e;

/* 爬虫任务头信息 */
typedef struct
{
    uint32_t type;                          /* 任务类型 */
    uint32_t length;                        /* 数据长度(头+体) */
} crwl_task_header_t;

/* 爬虫任务 */
typedef struct
{
    pthread_rwlock_t lock;                  /* 任务队列锁 */
    Queue_t queue;                          /* 任务队列 */
} crwl_task_t;

#endif /*__CRWL_TASK_H__*/
