/******************************************************************************
 ** Copyright(C) 2014-2024 Toushi technology Co., Ltd
 **
 ** 文件名: kw_tree.c
 ** 版本号: 1.0
 ** 描  述: 键树的实现
 ** 作  者: # Qifeng.zou # 2015.05.12 #
 ******************************************************************************/
#include "comm.h"
#include "kw_tree.h"

static void kwt_node_free(kwt_tree_t *kwt, kwt_node_t *node, void *mempool, mem_dealloc_cb_t dealloc);

/******************************************************************************
 **函数名称: kwt_creat
 **功    能: 创建键树
 **输入参数:
 **     opt: 选项
 **输出参数: NONE
 **返    回: 键树
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.12 #
 ******************************************************************************/
kwt_tree_t *kwt_creat(kwt_opt_t *opt)
{
    int max = 256;
    kwt_tree_t *kwt;

    if (!ISPOWEROF2(max)) { return NULL; }

    /* 1. 创建对象 */
    kwt = (kwt_tree_t *)opt->alloc(opt->pool, sizeof(kwt_tree_t));
    if (NULL == kwt)
    {
        return NULL;
    }

    kwt->pool = opt->pool;
    kwt->alloc = opt->alloc;
    kwt->dealloc = opt->dealloc;

    /* 2. 创建结点 */
    kwt->max = max;
    kwt->root = (kwt_node_t *)kwt->alloc(kwt->pool, max * sizeof(kwt_node_t));
    if (NULL == kwt->root)
    {
        kwt->dealloc(kwt->pool, kwt);
        return NULL;
    }

    memset(kwt->root, 0, sizeof(kwt_node_t));

    return kwt;
}

/******************************************************************************
 **函数名称: kwt_insert
 **功    能: 插入字符串
 **输入参数:
 **     kwt: 键树
 **     str: 字串(各字符取值：0x00~0XFF)
 **     len: 字串长度
 **     data: 附加数据
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.12 #
 ******************************************************************************/
int kwt_insert(kwt_tree_t *kwt, const u_char *str, int len, void *data)
{
    int i, max = len - 1;
    kwt_node_t *node = kwt->root;

    if (len <= 0) { return -1; }

    for (i=0; i<len; ++i)
    {
        node += str[i];
        node->key = str[i];
        if ((i < max) && (NULL == node->child))
        {
            node->child = (kwt_node_t *)kwt->alloc(kwt->pool, kwt->max * sizeof(kwt_node_t));
            if (NULL == node->child)
            {
                return -1;
            }

            memset(node->child, 0, sizeof(kwt_node_t));
        }
        else if (i == max)
        {
            node->data = data;
            return 0;
        }
        node = node->child;
    }

    return 0;
}

/******************************************************************************
 **函数名称: kwt_query
 **功    能: 搜索字符串
 **输入参数:
 **     kwt: 键树
 **     str: 字串(各字符取值：0x00~0XFF)
 **     len: 字串长度
 **输出参数:
 **     data: 附加参数
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.12 #
 ******************************************************************************/
int kwt_query(kwt_tree_t *kwt, const u_char *str, int len, void **data)
{
    int i, max = len - 1;
    kwt_node_t *node = kwt->root;

    /* 1. 搜索键树 */
    for (i=0; i<len; ++i)
    {
        node += str[i];
        if (node->key != str[i])
        {
            *data = NULL;
            return -1;
        }
        else if (i == max)
        {
            *data = node->data;
            return 0;
        }

        node = node->child;
    }

    *data = NULL;
    return -1;
}

/******************************************************************************
 **函数名称: _kwt_print
 **功    能: 打印键树
 **输入参数:
 **     kwt: 键树
 **输出参数:
 **返    回: VOID
 **实现描述: 遍历打印树中所有结点
 **注意事项:
 **作    者: # Qifeng.zou # 2015.06.01 17:00:37 #
 ******************************************************************************/
void _kwt_print(kwt_tree_t *kwt, kwt_node_t *node, int depth)
{
    int i, n;
    kwt_node_t *item = node;

    for (i=0; i<kwt->max; ++i, ++item)
    {
        if (0 == item->key)
        {
            continue;
        }

        for (n=0; n<depth; ++n)
        {
            fprintf(stderr, "| ");
        }

        fprintf(stderr, "|%02X\n", item->key);

        if (NULL == item->child)
        {
            continue;
        }

        _kwt_print(kwt, item->child, depth+1);
    }
}

/******************************************************************************
 **函数名称: kwt_print
 **功    能: 打印键树
 **输入参数:
 **     kwt: 键树
 **输出参数:
 **返    回: VOID
 **实现描述: 遍历打印树中所有结点
 **注意事项:
 **作    者: # Qifeng.zou # 2015.06.01 17:00:37 #
 ******************************************************************************/
void kwt_print(kwt_tree_t *kwt)
{
    int i;
    kwt_node_t *node = kwt->root;

    fprintf(stdout, "\n\n");

    for (i=0; i<kwt->max; ++i, ++node)
    {
        if (0 == node->key)
        {
            continue;
        }

        fprintf(stdout, " %02X\n", node->key);

        if (NULL == node->child)
        {
            continue;
        }

        _kwt_print(kwt, node->child, 1);
    }
}

/******************************************************************************
 **函数名称: kwt_destroy
 **功    能: 销毁键树
 **输入参数:
 **     kwt: 键树
 **     mempool: 附加数据的内存池
 **     dealloc: 释放附加数据空间的毁掉函数
 **输出参数: NONE
 **返    回: VOID
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.12 #
 ******************************************************************************/
void kwt_destroy(kwt_tree_t *kwt, void *mempool, mem_dealloc_cb_t dealloc)
{
    if (NULL != kwt->root)
    {
        kwt_node_free(kwt, kwt->root, mempool, dealloc);
    }

    kwt->dealloc(kwt->pool, kwt);
}

/******************************************************************************
 **函数名称: kwt_node_free
 **功    能: 销毁键树结点
 **输入参数:
 **     kwt: 键树
 **     node: 键树结点
 **     mempool: 附加数据的内存池
 **     dealloc: 释放附加数据空间的毁掉函数
 **输出参数: NONE
 **返    回: VOID
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.12 #
 ******************************************************************************/
static void kwt_node_free(kwt_tree_t *kwt, kwt_node_t *node, void *mempool, mem_dealloc_cb_t dealloc)
{
    int i;

    for (i=0;i<kwt->max; ++i)
    {   
        if (NULL == node[i].child)
        {
            continue;
        }
        dealloc(mempool, node[i].data);
        kwt_node_free(kwt, node[i].child, mempool, dealloc);
        node[i].child = NULL;
    }

    kwt->dealloc(kwt->pool, node);
}
