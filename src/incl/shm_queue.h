#if !defined(__SHM_QUEUE_H__)
#define __SHM_QUEUE_H__

#include "shm_ring.h"
#include "shm_slot.h"

/* 队列对象 */
typedef struct
{
    shm_ring_t *ring;       /* 环形队列 */
    shm_slot_t *slot;       /* 内存池对象 */
} shm_queue_t;

shm_queue_t *shm_queue_creat(const char *path, int max, int size);
shm_queue_t *shm_queue_creat_ex(int key, int max, int size);
shm_queue_t *shm_queue_attach(const char *path);
shm_queue_t *shm_queue_attach_ex(int key);
#define shm_queue_malloc(shmq) shm_slot_alloc((shmq)->slot)
#define shm_queue_dealloc(shmq, p) shm_slot_dealloc((shmq)->slot, p)
int shm_queue_push(shm_queue_t *shmq, void *p);
void *shm_queue_pop(shm_queue_t *shmq);

#define shm_queue_print(shmq) shm_ring_print((shmq)->ring)
#define shm_queue_size(shmq) shm_slot_get_size((shmq)->slot)

#endif /*__SHM_QUEUE_H__*/
