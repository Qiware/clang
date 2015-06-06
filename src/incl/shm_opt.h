#if !defined(__SHM_OPT_H__)
#define __SHM_OPT_H__

#include <sys/shm.h>
#include <sys/ipc.h>

/* SHM数据 */
typedef struct
{
    key_t key;                  /* KEY值 */
    size_t size;                /* 总大小 */
#define SHM_CHECK_SUM   (0x12345678)
    int checksum;               /* 校验值 */
} shm_data_t;

#define SHM_DATA_INVALID(shm)     /* 验证合法性 */\
    ((0 == (shm)->key) \
        || (0 == (shm)->size) \
        || (SHM_CHECK_SUM != (shm)->checksum))

key_t shm_ftok(const char *path, int id);
void *shm_creat(const char *path, size_t size);
void *shm_attach(const char *path, size_t size);

#endif /*__SHM_OPT_H__*/
