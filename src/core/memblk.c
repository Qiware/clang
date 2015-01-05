/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: memblk.c
 ** 版本号: 1.0
 ** 描  述: 内存块的实现(也属于内存池算法)
 **         所有数据块大小都一致.
 **         此内存池主要用于存储大小一致的数据块的使用场景
 ** 作  者: # Qifeng.zou # 2014.12.18 #
 ******************************************************************************/
#include "log.h"
#include "common.h"
#include "syscall.h"
#include "memblk.h"


/******************************************************************************
 **函数名称: memblk_creat
 **功    能: 创建内存池
 **输入参数: 
 **     num: 内存块数
 **     size: 内存块大小
 **输出参数: NONE
 **返    回: 内存池对象
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.12.18 #
 ******************************************************************************/
memblk_t *memblk_creat(int num, size_t size)
{
    int i, m, idx;
    uint32_t *bitmap;
    memblk_t *blk;
    memblk_page_t *page;

    /* 1. 创建对象 */
    blk = (memblk_t *)calloc(1, sizeof(memblk_t));
    if (NULL == blk)
    {
        return NULL;
    }

    blk->num = num;
    blk->size = size;

    /* 2. 计算页数, 并分配页空间 */
    blk->pages = math_ceiling(num, MEMBLK_PAGE_SLOT_NUM);

    blk->page = (memblk_page_t *)calloc(blk->pages, sizeof(memblk_page_t));
    if (NULL == blk->page)
    {
        free(blk);
        return NULL;
    }

    /* 3 分配总内存空间 */
    blk->addr = (char *)calloc(num, size);
    if (NULL == blk->addr)
    {
        free(blk->page);
        free(blk);
        return NULL;
    }

    /* 4. 设置位图信息 */
    for (idx=0; idx<blk->pages; ++idx)
    {
        page = &blk->page[idx];

        spin_lock_init(&page->lock);

        /* 3.1 设置bitmap */
        if (idx == (blk->pages - 1))
        {
            m = (num - idx*MEMBLK_PAGE_SLOT_NUM) % 32;

            page->bitmaps = math_ceiling(num - idx*MEMBLK_PAGE_SLOT_NUM, 32);
        }
        else
        {
            m = MEMBLK_PAGE_SLOT_NUM % 32;

            page->bitmaps = math_ceiling(MEMBLK_PAGE_SLOT_NUM, 32);
        }

        page->bitmap = (uint32_t *)calloc(page->bitmaps, sizeof(uint32_t));
        if (NULL == page->bitmap)
        {
            free(blk->page);
            free(blk->addr);
            free(blk);
            return NULL;
        }

        if (m)
        {
            bitmap = &page->bitmap[page->bitmaps - 1];

            for (i=m; i<32; i++)
            {
                *bitmap |= (1 << i);
            }
        }

        /* 3.2 设置数据空间 */
        page->addr = blk->addr + idx * MEMBLK_PAGE_SLOT_NUM * size;
    }

    return blk;
}

/******************************************************************************
 **函数名称: memblk_alloc
 **功    能: 申请空间
 **输入参数: 
 **     blk: 内存块对象
 **输出参数: NONE
 **返    回: 内存地址
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.12.18 #
 ******************************************************************************/
void *memblk_alloc(memblk_t *blk)
{
    uint32_t i, j, n, p;
    uint32_t *bitmap;
    memblk_page_t *page;

    n = rand(); /* 随机选择页 */

    for (p=0; p<blk->pages; ++p, ++n)
    {
        n %= blk->pages;
        page = &blk->page[n];

        spin_lock(&page->lock);

        for (i=0; i<page->bitmaps; ++i)
        {
            if (0xFFFFFFFF == page->bitmap[i])
            {
                continue;
            }

            bitmap = page->bitmap + i;
            for (j=0; j<32; ++j)
            {
                if (!((*bitmap >> j) & 1))
                {
                    *bitmap |= (1 << j);

                    spin_unlock(&page->lock);

                    return page->addr + (i*32 + j)*blk->size;
                }
            }
        }

        spin_unlock(&page->lock);
    }
    return NULL;
}

/******************************************************************************
 **函数名称: memblk_dealloc
 **功    能: 回收空间
 **输入参数: 
 **     blk: 内存块对象
 **     p: 需要释放的空间地址
 **输出参数: NONE
 **返    回: VOID
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.12.18 #
 ******************************************************************************/
void memblk_dealloc(memblk_t *blk, void *p)
{
    int i, j, n;

    n = (p - blk->addr) / (MEMBLK_PAGE_SLOT_NUM * blk->size);        /* 计算页号 */
    i = (p - blk->page[n].addr) / (32 * blk->size);                     /* 计算页内bitmap索引 */
    j = (p - (blk->page[n].addr + i * (32 * blk->size))) / blk->size; /* 计算bitmap内偏移 */

    spin_lock(&blk->page[n].lock);
    blk->page[n].bitmap[i] &= ~(1 << j);
    spin_unlock(&blk->page[n].lock);
}

/******************************************************************************
 **函数名称: memblk_destroy
 **功    能: 销毁内存块
 **输入参数: 
 **     blk: 内存块对象
 **输出参数: NONE
 **返    回: VOID
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.12.18 #
 ******************************************************************************/
void memblk_destroy(memblk_t *blk)
{
    int i;
    memblk_page_t *page;

    for (i=0; i<blk->pages; ++i)
    {
        page = &blk->page[i];

        free(page->bitmap);
        spin_lock_destroy(&page->lock);
    }

    free(blk->page);
    free(blk->addr);
    free(blk);
}