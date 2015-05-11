#if !defined(__SLOT_H__)
#define __SLOT_H__

#include "comm.h"
#include "ring.h"

typedef struct
{
    int max;                    /* 内存块数 */
    int size;                   /* 内存块大小 */

    void *addr;                 /* 内存地址 */
    ring_t *ring;               /* 环形队列 */
} slot_t;

slot_t *slot_creat(int num, size_t size);
void *slot_alloc(slot_t *slot);
void slot_dealloc(slot_t *slot, void *p);
void slot_destroy(slot_t *slot);

#endif /*__SLOT_H__*/
