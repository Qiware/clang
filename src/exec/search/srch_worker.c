#include "syscall.h"
#include "srch_mesg.h"
#include "srch_worker.h"

/******************************************************************************
 **函数名称: srch_worker_self
 **功    能: 获取Worker对象
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: Agent对象
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.12.20 #
 ******************************************************************************/
static srch_worker_t *srch_worker_self(srch_cntx_t *ctx)
{
    int tidx;
    srch_worker_t *worker;

    tidx = thread_pool_get_tidx(ctx->worker_pool);
    worker = thread_pool_get_args(ctx->worker_pool);

    return worker + tidx;
}

/******************************************************************************
 **函数名称: srch_worker_routine
 **功    能: 运行Worker线程
 **输入参数:
 **     _ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项: TODO: 后续改成事件触发机制 
 **作    者: # Qifeng.zou # 2014.12.20 #
 ******************************************************************************/
void *srch_worker_routine(void *_ctx)
{
    int rqid; /* 接收队列ID */
    void *addr;
    srch_reg_t *reg;
    srch_worker_t *worker;
    srch_mesg_header_t *head;
    srch_cntx_t *ctx = (srch_cntx_t *)_ctx;

    worker = srch_worker_self(ctx);

    while (1)
    {
        rqid = rand() % ctx->conf->agent_num;

        log_trace(worker->log, "widx:%d rqid:%d", worker->tidx, rqid);

        /* 1. 从队列中取数据 */
        addr = queue_pop(ctx->recvq[rqid]);
        if (NULL == addr)
        {
            usleep(500);
            continue;
        }

        /* 2. 对数据进行处理 */
        head = (srch_mesg_header_t *)(addr + sizeof(srch_flow_t));

        reg = &ctx->reg[head->type];

        reg->proc(head->type, addr, head->length + sizeof(srch_flow_t), reg->args, worker->log);

        /* 3. 释放内存空间 */
        queue_dealloc(ctx->recvq[rqid], addr);
    }
    return NULL;
}

/******************************************************************************
 **函数名称: srch_worker_init
 **功    能: 初始化Worker线程
 **输入参数:
 **     ctx: 全局信息
 **     worker: Worker对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.18 #
 ******************************************************************************/
int srch_worker_init(srch_cntx_t *ctx, srch_worker_t *worker, int idx)
{
    worker->log = ctx->log;
    worker->tidx = idx;
    return SRCH_OK;
}

/******************************************************************************
 **函数名称: srch_worker_destroy
 **功    能: 销毁Worker线程
 **输入参数:
 **     worker: Worker对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.18 #
 ******************************************************************************/
int srch_worker_destroy(srch_worker_t *worker)
{
    return SRCH_OK;
}
