/******************************************************************************
 ** Copyright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: rtmq_worker.c
 ** 版本号: 1.0
 ** 描  述: 实时消息队列(REAL-TIME MESSAGE QUEUE)
 **         1. 主要用于异步系统之间数据消息的传输
 ** 作  者: # Qifeng.zou # 2015.05.18 #
 ******************************************************************************/

#include "xml_tree.h"
#include "rtmq_cmd.h"
#include "rtsd_send.h"
#include "thread_pool.h"

/* 静态函数 */
static rtmq_worker_t *rtsd_worker_get_curr(rtsd_cntx_t *ctx);
static int rtsd_worker_event_core_hdl(rtsd_cntx_t *ctx, rtmq_worker_t *worker);
static int rtsd_worker_cmd_proc_req_hdl(rtsd_cntx_t *ctx, rtmq_worker_t *worker, const rtmq_cmd_t *cmd);

/******************************************************************************
 **函数名称: rtsd_worker_routine
 **功    能: 运行工作线程
 **输入参数:
 **     _ctx: 全局对象
 **输出参数: NONE
 **返    回: VOID *
 **实现描述:
 **     1. 获取工作对象
 **     2. 等待事件通知
 **     3. 进行事件处理
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.18 #
 ******************************************************************************/
void *rtsd_worker_routine(void *_ctx)
{
    int ret, idx;
    rtmq_worker_t *worker;
    rtmq_cmd_proc_req_t *req;
    struct timeval timeout;
    rtsd_cntx_t *ctx = (rtsd_cntx_t *)_ctx;
    rtsd_conf_t *conf = (rtsd_conf_t *)&ctx->conf;

    /* 1. 获取工作对象 */
    worker = rtsd_worker_get_curr(ctx);
    if (NULL == worker)
    {
        log_fatal(ctx->log, "Get current worker failed!");
        abort();
        return (void *)-1;
    }

    nice(-20);

    for (;;)
    {
        /* 2. 等待事件通知 */
        FD_ZERO(&worker->rdset);

        FD_SET(worker->cmd_sck_id, &worker->rdset);

        worker->max = worker->cmd_sck_id;

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        ret = select(worker->max+1, &worker->rdset, NULL, NULL, &timeout);
        if (ret < 0)
        {
            if (EINTR == errno) { continue; }
            log_fatal(worker->log, "errmsg:[%d] %s", errno, strerror(errno));
            abort();
            return (void *)-1;
        }
        else if (0 == ret)
        {
            /* 超时: 模拟处理命令 */
            rtmq_cmd_t cmd;
            req = (rtmq_cmd_proc_req_t *)&cmd.param;

            for (idx=0; idx<conf->send_thd_num; ++idx)
            {
                memset(&cmd, 0, sizeof(cmd));

                cmd.type = RTMQ_CMD_PROC_REQ;
                req->num = -1;
                req->rqidx = idx;

                rtsd_worker_cmd_proc_req_hdl(ctx, worker, &cmd);
            }
            continue;
        }

        /* 3. 进行事件处理 */
        rtsd_worker_event_core_hdl(ctx, worker);
    }

    abort();
    return (void *)-1;
}

/******************************************************************************
 **函数名称: rtsd_worker_get_by_idx
 **功    能: 通过索引查找对象
 **输入参数:
 **     ctx: 全局对象
 **     idx: 索引号
 **输出参数: NONE
 **返    回: 工作对象
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.19 #
 ******************************************************************************/
rtmq_worker_t *rtsd_worker_get_by_idx(rtsd_cntx_t *ctx, int idx)
{
    return (rtmq_worker_t *)(ctx->worktp->data + idx * sizeof(rtmq_worker_t));
}

/******************************************************************************
 **函数名称: rtsd_worker_get_curr
 **功    能: 获取工作对象
 **输入参数:
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 工作对象
 **实现描述:
 **     1. 获取线程编号
 **     2. 返回工作对象
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.18 #
 ******************************************************************************/
static rtmq_worker_t *rtsd_worker_get_curr(rtsd_cntx_t *ctx)
{
    int id;

    /* > 获取线程编号 */
    id = thread_pool_get_tidx(ctx->worktp);
    if (id < 0)
    {
        log_fatal(ctx->log, "Get thread index failed!");
        return NULL;
    }

    /* > 返回工作对象 */
    return rtsd_worker_get_by_idx(ctx, id);
}

/******************************************************************************
 **函数名称: rtsd_worker_init
 **功    能: 初始化工作服务
 **输入参数:
 **     ctx: 全局对象
 **     worker: 工作对象
 **     id: 工作对象编号
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 创建命令套接字
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.18 #
 ******************************************************************************/
int rtsd_worker_init(rtsd_cntx_t *ctx, rtmq_worker_t *worker, int id)
{
    char path[FILE_PATH_MAX_LEN];
    rtsd_conf_t *conf = &ctx->conf;

    worker->id = id;
    worker->log = ctx->log;

    /* 1. 创建命令套接字 */
    rtsd_worker_usck_path(conf, path, worker->id);

    worker->cmd_sck_id = unix_udp_creat(path);
    if (worker->cmd_sck_id < 0)
    {
        log_error(worker->log, "Create unix-udp socket failed!");
        return RTMQ_ERR;
    }

    return RTMQ_OK;
}

/******************************************************************************
 **函数名称: rtsd_worker_event_core_hdl
 **功    能: 核心事件处理
 **输入参数:
 **     ctx: 全局对象
 **     worker: 工作对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 创建命令套接字
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.18 #
 ******************************************************************************/
static int rtsd_worker_event_core_hdl(rtsd_cntx_t *ctx, rtmq_worker_t *worker)
{
    rtmq_cmd_t cmd;

    if (!FD_ISSET(worker->cmd_sck_id, &worker->rdset))
    {
        return RTMQ_OK; /* 无数据 */
    }

    if (unix_udp_recv(worker->cmd_sck_id, (void *)&cmd, sizeof(cmd)) < 0)
    {
        log_error(worker->log, "errmsg:[%d] %s", errno, strerror(errno));
        return RTMQ_ERR_RECV_CMD;
    }

    switch (cmd.type)
    {
        case RTMQ_CMD_PROC_REQ:
        {
            return rtsd_worker_cmd_proc_req_hdl(ctx, worker, &cmd);
        }
        default:
        {
            log_error(worker->log, "Received unknown type! %d", cmd.type);
            return RTMQ_ERR_UNKNOWN_CMD;
        }
    }

    return RTMQ_ERR_UNKNOWN_CMD;
}

/******************************************************************************
 **函数名称: rtsd_worker_cmd_proc_req_hdl
 **功    能: 处理请求的处理
 **输入参数:
 **     ctx: 全局对象
 **     worker: 工作对象
 **     cmd: 命令信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.18 #
 ******************************************************************************/
static int rtsd_worker_cmd_proc_req_hdl(rtsd_cntx_t *ctx, rtmq_worker_t *worker, const rtmq_cmd_t *cmd)
{
#define RTSD_WORK_POP_NUM   (1024)
    int idx, num;
    void *addr[RTSD_WORK_POP_NUM];
    queue_t *rq;
    rtmq_reg_t *reg;
    rtmq_header_t *head;
    const rtmq_cmd_proc_req_t *work_cmd = (const rtmq_cmd_proc_req_t *)&cmd->param;

    /* 1. 获取接收队列 */
    rq = ctx->recvq[work_cmd->rqidx];

    while (1)
    {
        /* > 从接收队列获取数据 */
        num = MIN(queue_used(rq), RTSD_WORK_POP_NUM);
        if (0 == num)
        {
            return RTMQ_OK;
        }

        num = queue_mpop(rq, addr, num);
        if (0 == num)
        {
            continue;
        }

        log_trace(worker->log, "Multi-pop num:%d!", num);

        for (idx=0; idx<num; ++idx)
        {
            /* > 执行回调函数 */
            head = (rtmq_header_t *)addr[idx];

            reg = &ctx->reg[head->type];
            if (NULL == reg->proc)
            {
                ++worker->drop_total;   /* 丢弃计数 */
                queue_dealloc(rq, addr[idx]);
                continue;
            }

            if (reg->proc(head->type, head->nodeid,
                addr[idx] + sizeof(rtmq_header_t), head->length, reg->param))
            {
                ++worker->err_total;    /* 错误计数 */
            }
            else
            {
                ++worker->proc_total;   /* 处理计数 */
            }

            /* > 释放内存空间 */
            queue_dealloc(rq, addr[idx]);
        }
    }

    return RTMQ_OK;
}
