/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: smtc.c
 ** 版本号: 1.0
 ** 描  述: 共享消息传输通道(Sharing Message Transaction Channel)
 **         1. 主要用于异步系统之间数据消息的传输
 ** 作  者: # Qifeng.zou # 2014.12.29 #
 ******************************************************************************/
#include <memory.h>
#include <assert.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "smtc.h"
#include "xml_tree.h"
#include "smtc_cmd.h"
#include "smtc_comm.h"
#include "smtc_recv.h"
#include "thread_pool.h"

/* 静态函数 */
static smtc_rsvr_t *smtc_rsvr_get_curr(smtc_cntx_t *ctx);
static int smtc_rsvr_init(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, int tidx);
static int smtc_rsvr_event_core_hdl(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr);
static int smtc_rsvr_event_timeout_hdl(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr);

static int smtc_rsvr_trav_recv(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr);
static int smtc_rsvr_trav_send(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr);

static int smtc_rsvr_recv_proc(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static int smtc_rsvr_read_init(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static int smtc_rsvr_recv_header(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static int smtc_rsvr_recv_body(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static int smtc_rsvr_read_post(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);

static int smtc_rsvr_check_header(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static void smtc_rsvr_read_release(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);

static int smtc_rsvr_proc_sys_mesg(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static int smtc_rsvr_proc_exp_mesg(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);

static int smtc_rsvr_keepalive_req_hdl(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static int smtc_rsvr_cmd_send_proc_req(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static void smtc_rsvr_cmd_send_proc_all_req(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr);
static int smtc_rsvr_cmd_resend_proc_req(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr);

static int smtc_rsvr_add_conn_hdl(smtc_rsvr_t *rsvr, smtc_cmd_add_sck_t *req);
static int smtc_rsvr_del_conn_hdl(smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static void smtc_rsvr_del_all_conn_hdl(smtc_rsvr_t *rsvr);

static int smtc_rsvr_add_mesg(smtc_rsvr_t *rsvr, smtc_sck_t *sck, void *addr);
static void *smtc_rsvr_fetch_mesg(smtc_rsvr_t *rsvr, smtc_sck_t *sck);
static int smtc_rsvr_clear_mesg(smtc_rsvr_t *rsvr, smtc_sck_t *sck);

/* 随机选择接收线程 */
#define smtc_rand_recv(ctx) ((ctx)->listen.total++ % (ctx->recvtp->num))

/* 随机选择工作线程 */
#define smtc_rand_work(ctx) (rand() % (ctx->worktp->num))

/******************************************************************************
 **函数名称: smtc_rsvr_set_rdset
 **功    能: 设置可读集合
 **输入参数: 
 **     rsvr: 接收服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **     如果超时未接收或发送数据，则关闭连接!
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
#define smtc_rsvr_set_rdset(rsvr) \
{ \
    smtc_sck_t *curr, *next; \
    \
    FD_ZERO(&rsvr->rdset); \
    \
    FD_SET(rsvr->cmd_sck_id, &rsvr->rdset); \
    rsvr->max = rsvr->cmd_sck_id; \
    \
    curr = rsvr->sck; \
    while (NULL != curr) \
    { \
        if ((rsvr->ctm - curr->rtm > 30) \
            && (rsvr->ctm - curr->wtm > 30)) \
        { \
            log_trace(rsvr->log, "Didn't active for along time! fd:%d ip:%s", \
                    curr->fd, curr->ipaddr); \
            \
            next = curr->next; \
            smtc_rsvr_del_conn_hdl(rsvr, curr); \
            \
            curr = next; \
            continue; \
        } \
        \
        FD_SET(curr->fd, &rsvr->rdset); \
        rsvr->max = (rsvr->max > curr->fd)? rsvr->max : curr->fd; \
        \
        curr = curr->next; \
    } \
}

/******************************************************************************
 **函数名称: smtc_rsvr_set_wrset
 **功    能: 设置可写集合
 **输入参数: 
 **     rsvr: 接收服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     只有发送链表中存在数据时，才将该套接字加入到可写侦听集合!
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
#define smtc_rsvr_set_wrset(rsvr) \
{ \
    smtc_sck_t *curr; \
    \
    FD_ZERO(&rsvr->wrset); \
    \
    curr = rsvr->sck; \
    while (NULL != curr) \
    { \
        if (NULL == curr->mesg_list \
            && NULL == curr->send.addr) \
        { \
            curr = curr->next; \
            continue; \
        } \
        \
        FD_SET(curr->fd, &rsvr->wrset); \
        \
        curr = curr->next; \
    } \
}

/******************************************************************************
 **函数名称: smtc_rsvr_routine
 **功    能: 运行接收服务线程
 **输入参数: 
 **     _ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 获取接收服务
 **     2. 等待事件通知
 **     3. 进行事件处理
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
void *smtc_rsvr_routine(void *_ctx)
{
    int ret;
    smtc_rsvr_t *rsvr;
    struct timeval timeout;
    smtc_cntx_t *ctx = (smtc_cntx_t *)_ctx;

    /* 1. 获取接收服务 */
    rsvr = smtc_rsvr_get_curr(ctx);
    if (NULL == rsvr)
    {
        log_fatal(rsvr->log, "Get recv server failed!");
        abort();
        return (void *)SMTC_ERR;
    }

    for (;;)
    {
        /* 2. 等待事件通知 */
        smtc_rsvr_set_rdset(rsvr);
        smtc_rsvr_set_wrset(rsvr);

        timeout.tv_sec = SMTC_TMOUT_SEC;
        timeout.tv_usec = SMTC_TMOUT_USEC;
        ret = select(rsvr->max+1, &rsvr->rdset, &rsvr->wrset, NULL, &timeout);
        if (ret < 0)
        {
            if (EINTR == errno)
            {
                continue;
            }

            log_fatal(rsvr->log, "errmsg:[%d] %s", errno, strerror(errno));
            abort();
            return (void *)SMTC_ERR;
        }
        else if (0 == ret)
        {
            smtc_rsvr_event_timeout_hdl(ctx, rsvr);
            continue;
        }

        /* 3. 进行事件处理 */
        smtc_rsvr_event_core_hdl(ctx, rsvr);
    }

    log_fatal(rsvr->log, "errmsg:[%d] %s", errno, strerror(errno));
    abort();
    return (void *)-1;
}

/******************************************************************************
 **函数名称: smtc_rsvr_get_curr
 **功    能: 获取当前线程对应的接收服务
 **输入参数: 
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 当前接收服务
 **实现描述: 
 **     1. 获取当前线程的索引
 **     2. 返回当前线程对应的接收服务
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static smtc_rsvr_t *smtc_rsvr_get_curr(smtc_cntx_t *ctx)
{
    int tidx;

    /* 1. 获取当前线程的索引 */
    tidx = thread_pool_get_tidx(ctx->recvtp);
    if (tidx < 0)
    {
        log_error(rsvr->log, "Get index of current thread failed!");
        return NULL;
    }

    /* 2. 返回当前线程对应的接收服务 */
    return (smtc_rsvr_t *)(ctx->recvtp->data + tidx * sizeof(smtc_rsvr_t));
}

/******************************************************************************
 **函数名称: smtc_rsvr_init
 **功    能: 初始化接收服务
 **输入参数: 
 **     ctx: 全局对象
 **     tidx: 接收服务编号
 **输出参数:
 **     rsvr: 接收服务
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 获取当前线程的索引
 **     2. 返回当前线程对应的接收服务
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_init(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, int tidx)
{
    char path[FILE_PATH_MAX_LEN];
    smtc_conf_t *conf = &ctx->conf;

    rsvr->tidx = tidx;
    rsvr->log = ctx->log;
    rsvr->ctm = time(NULL);

    /* 1. 创建各队列滞留条数数组 */
    rsvr->delay_total = calloc(ctx->conf.rqnum, sizeof(uint64_t));
    if (NULL == rsvr->delay_total)
    {
        log_fatal(rsvr->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SMTC_ERR;
    }

    /* 2. 创建CMD套接字 */
    smtc_rsvr_usck_path(conf, path, rsvr->tidx);
    
    rsvr->cmd_sck_id = usck_udp_creat(path);
    if (rsvr->cmd_sck_id < 0)
    {
        log_error(rsvr->log, "Create unix-udp socket failed!");
        return SMTC_ERR;
    }

    /* 3. 创建SLAB内存池 */
    rsvr->pool = slab_init(SMTC_MEM_POOL_SIZE)
    if (NULL == rsvr->pool)
    {
        log_error(rsvr->log, "Initialize slab mem-pool failed!");
        return SMTC_ERR;
    }

    rsvr->sck = NULL;
    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_recv_cmd
 **功    能: 接收命令数据
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 接收命令数据
 **     2. 进行命令处理
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_recv_cmd(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr)
{
    smtc_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));

    /* 1. 接收命令数据 */
    if (usck_udp_recv(rsvr->cmd_sck_id, (void *)&cmd, sizeof(cmd)) < 0)
    {
        log_error(rsvr->log, "Recv command failed!");
        return SMTC_ERR_RECV_CMD;
    }

    /* 2. 进行命令处理 */
    switch (cmd.type)
    {
        case SMTC_CMD_ADD_SCK:
        {
            return smtc_rsvr_add_conn_hdl(rsvr, (smtc_cmd_add_sck_t *)&cmd.args);
        }
        default:
        {
            log_error(rsvr->log, "Unknown command! type:%d", cmd.type);
            return SMTC_ERR_UNKNOWN_CMD;
        }
    }

    return SMTC_ERR_UNKNOWN_CMD;
}

/******************************************************************************
 **函数名称: smtc_rsvr_trav_recv
 **功    能: 遍历接收数据
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     遍历判断套接字是否可读，并接收数据!
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_trav_recv(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr)
{
    smtc_sck_t *curr, *next;

    rsvr->ctm = time(NULL);

    curr = rsvr->sck;
    while (NULL != curr)
    {
        if (FD_ISSET(curr->fd, &rsvr->rdset))
        {
            curr->rtm = rsvr->ctm;

            /* Recv data */
            if (smtc_rsvr_recv_proc(ctx, recv, curr))
            {
                log_error(rsvr->log, "Read proc failed! fd:%d ip:%s", curr->fd, curr->ipaddr);
                next = curr->next;
                smtc_rsvr_del_conn_hdl(rsvr, curr);
                curr = next;
                continue;
            }
        }

        curr = curr->next;
    }

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_trav_send
 **功    能: 遍历发送数据
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     遍历判断套接字是否可写，并发送数据!
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_trav_send(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr)
{
    int n;
    smtc_header_t *head;
    smtc_sck_t *curr;
    socket_snap_t *send;

    rsvr->ctm = time(NULL);
    curr = rsvr->sck;

    while (NULL != curr)
    {
        if (FD_ISSET(curr->fd, &rsvr->wrset))
        {
            curr->wtm = rsvr->ctm;
            send = &curr->send;

            for (;;)
            {
                /* 1. 获取需要发送的数据 */
                if (NULL == send->addr)
                {
                    send->addr = smtc_rsvr_fetch_mesg(rsvr, curr);
                    if (NULL == send->addr)
                    {
                        break;
                    }

                    head = (smtc_header_t *)send->addr;

                    send->loc = SMTC_DATA_LOC_SLAB;
                    send->off = 0;
                    send->total = head->length + sizeof(smtc_header_t);
                    send->left = send->total;
                }

                /* 2. 发送数据 */
                n = Writen(curr->fd, send->addr+send->off, send->left);
                if (n != (int) send->left)
                {
                    if (n > 0)
                    {
                        send->off += n;
                        send->left -= n;
                        break;
                    }

                    log_error(rsvr->log, "errmsg:[%d] %s!", errno, strerror(errno));

                    if (SMTC_DATA_LOC_SLAB == send->loc)
                    {
                        slab_dealloc(rsvr->pool, send->addr);
                    }

                    smtc_reset_send_snap(send);

                    /* 关闭套接字　并清空发送队列 */
                    smtc_rsvr_del_conn_hdl(rsvr, curr);
                    return SMTC_ERR;
                }

                /* 3. 释放空间 */
                if (SMTC_DATA_LOC_SLAB == send->loc)
                {
                    slab_dealloc(rsvr->pool, send->addr);
                }

                smtc_reset_send_snap(send);
            }
        }

        curr = curr->next;
    }

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_recv_proc
 **功    能: 接收数据并做相应处理
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 被操作的套接字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 初始化接收
 **     2. 接收数据头
 **     3. 接收数据体
 **     4. 进行数据处理
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_recv_proc(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    int ret;
    smtc_header_t *head;
    socket_snap_t *read = &sck->read;

    switch (read->phase)
    {
        /* 1. 初始化接收 */
        case SMTC_PHASE_READ_INIT:
        {
            if (smtc_rsvr_read_init(ctx, recv, sck))
            {
                log_error(rsvr->log, "Init read failed!");
                return SMTC_ERR;
            }

            /* 注意: 继续后续处理, 不执行break语句... */
        }
        /* 2. 接收数据头 */
        case SMTC_PHASE_READ_HEAD:
        {
            ret = smtc_rsvr_recv_header(ctx, recv, sck);
            if (SMTC_DONE == ret)
            {
                head = (smtc_header_t *)read->addr;
                if (head->length > 0)
                {
                    smtc_set_read_phase(read, SMTC_PHASE_READ_BODY);
                    return SMTC_OK;
                }

                smtc_set_read_phase(read, SMTC_PHASE_READ_POST);
                goto PHASE_READ_POST;
            }
            else if (SMTC_AGAIN == ret)  /* incomplete */
            {
                /* Note: Continue rsvr head at next loop */
                return SMTC_OK;
            }
            else if (SMTC_SCK_CLOSED == ret)
            {
                log_debug(rsvr->log, "Client disconnect!");
                break;
            }
            else
            {
                log_error(rsvr->log, "Recv head failed!");
                break; /* error */
            }
            /* 注意: 继续后续处理, 不执行break语句... */
        }
        /* 3. 接收数据体 */
        case SMTC_PHASE_READ_BODY:
        {
            ret = smtc_rsvr_recv_body(ctx, recv, sck);
            if (SMTC_DONE == ret)
            {
                /* NULL;  Note: Continue handle */
            }
            else if (SMTC_AGAIN == ret)
            {
                /* Note: Continue rsvr body at next loop */
                return SMTC_OK;
            }
            else if (SMTC_HDL_DISCARD == ret)
            {
                smtc_rsvr_read_release(ctx, recv, sck);
                return SMTC_OK;
            }
            else if (SMTC_SCK_CLOSED == ret)
            {
                log_debug(rsvr->log, "Client disconnect!");
                break;
            }
            else
            {
                log_error(rsvr->log, "Recv body failed!");
                break; /* error */
            }
            /* 注意: 继续后续处理, 不执行break语句... */
        }
        /* 4. 进行数据处理 */
        case SMTC_PHASE_READ_POST:
        {
        PHASE_READ_POST:
            ret = smtc_rsvr_read_post(ctx, recv, sck);
            if (SMTC_OK == ret)
            {
                smtc_reset_read_snap(read);
                return SMTC_OK;
            }
            else if ((SMTC_HDL_DONE == ret)
                || (SMTC_HDL_DISCARD == ret))
            {
                smtc_rsvr_read_release(ctx, recv, sck);
                return SMTC_OK;
            }

            break;
        }
        default:
        {
            log_error(rsvr->log, "Unknown phase!");
            break;
        }
    }

    smtc_rsvr_read_release(ctx, recv, sck);

    return SMTC_ERR;
}

/******************************************************************************
 **函数名称: smtc_rsvr_read_init
 **功    能: 初始化数据接收
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 被操作的套接字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 选择接收队列
 **     2. 为新数据申请空间
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_read_init(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
#define SMTC_RETRY_TIMES    (3)
    int times = 0;
    socket_snap_t *read = &sck->read;

AGAIN:
    /* 1. 随机选择Recv队列 */
    read->rqidx = rand() % ctx->conf.rqnum;

    /* 2. 从队列申请空间 */
    read->dataid = orm_queue_data_malloc(ctx->recvq[read->rqidx], &read->addr);
    if (NULLID == read->dataid)
    {
        smtc_rsvr_cmd_send_proc_all_req(ctx, rsvr);

        if (times++ < SMTC_RETRY_TIMES)
        {
            goto AGAIN;
        }

        log_error(rsvr->log, "Recv queue was full! Perhaps lock conflicts too much!"
                "recv:%llu delay:%llu drop:%llu error:%llu",
                rsvr->recv_total, rsvr->delay_total[read->rqidx],
                rsvr->drop_total, rsvr->err_total);

        /* 创建NULL空间 */
        if (NULL == sck->null)
        {
            sck->null = slab_alloc(rsvr->pool, ctx->conf.recvq.size);
            if (NULL == sck->null)
            {
                log_error(rsvr->log, "Alloc memory from slab failed!");
                return SMTC_ERR;
            }
        }

        /* 指向NULL空间 */
        read->rqidx = 0;
        read->dataid = 0;
        read->addr = sck->null;
    }

    /* 3. 设置标识量 */
    read->off = 0;
    smtc_set_read_phase(read, SMTC_PHASE_READ_HEAD);
    
    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_read_release
 **功    能: 释放数据接收
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 被操作的套接字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static void smtc_rsvr_read_release(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    socket_snap_t *read = &sck->read;

    /* 1. 释放内存 */
    if (read->addr != sck->null)
    {
        queue_dealloc(ctx->recvq[read->rqidx], read->addr);
    }

    /* 2. 重置标识量 */
    smtc_reset_read_snap(read);
}

/******************************************************************************
 **函数名称: smtc_rsvr_recv_header
 **功    能: 接收数据头
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 被操作的套接字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_recv_header(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    int n, left;
    socket_snap_t *read = &sck->read;
    smtc_header_t *head = (smtc_header_t *)read->addr;


    /* 1. 接收数据 */
    while (1)
    {
        /* 1.1 计算剩余数 */
        left = sizeof(smtc_header_t) - read->off;

        /* 1.2 接收数据头 */
        n = read(sck->fd, read->addr + read->off,  left);
        if (n == left)
        {
            read->off += n;
            break;
        }
        else if (n > 0)
        {
            read->off += n;
            continue;
        }
        else if ((n < 0) && (EAGAIN == errno))
        {
            return SMTC_AGAIN;
        }
        else if (0 == n)
        {
            log_error(rsvr->log, "Client disconnected. errmsg:[%d] %s! fd:[%d] n:[%d/%d]",
                    errno, strerror(errno), sck->fd, n, left);
            return SMTC_SCK_CLOSED;
        }
        
        if (EINTR == errno)
        {
            continue; 
        }

        ++rsvr->err_total; /* 错误计数 */

        log_error(rsvr->log, "errmsg:[%d] %s. fd:[%d]", errno, strerror(errno), sck->fd);
        return SMTC_ERR;
    }

    /* 3. 校验数据头 */
    if (smtc_rsvr_check_header(ctx, recv, sck))
    {
        ++rsvr->err_total; /* 错误计数 */

        log_error(rsvr->log, "Check header failed! type:%d len:%d flag:%d mark:[%u/%u]",
                head->type, head->length, head->flag, head->mark, SMTC_MSG_MARK_KEY);
        return SMTC_ERR;
    }

    read->total = sizeof(smtc_header_t) + head->length;

    log_debug(rsvr->log, "Recv header success! type:%d len:%d flag:%d mark:[%u/%u]",
            head->type, head->length, head->flag, head->mark, SMTC_MSG_MARK_KEY);

    return SMTC_DONE;
}

/******************************************************************************
 **函数名称: smtc_rsvr_check_header
 **功    能: 校验数据头
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 被操作的套接字
 **输出参数: NONE
 **返    回: 0:合法 !0:不合法
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_check_header(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    socket_snap_t *read = &sck->read;
    smtc_header_t *head = (smtc_header_t *)read->addr;


    /* 1. 检查校验值 */
    if (SMTC_MSG_MARK_KEY != head->mark)
    {
        log_error(rsvr->log, "Mark [%u/%u] isn't right! type:%d len:%d flag:%d",
                head->mark, SMTC_MSG_MARK_KEY, head->type, head->length, head->flag);
        return SMTC_ERR;
    }

    /* 2. 检查类型 */
    if (!smtc_is_type_valid(head->type))
    {
        log_error(rsvr->log, "Data type is invalid! type:%d len:%d", head->type, head->length);
        return SMTC_ERR;
    }
 
    /* 3. 检查长度: 因所有队列长度一致 因此使用[0]判断 */
    if (!smtc_is_len_valid(ctx->recvq[0], head->length))
    {
        log_error(rsvr->log, "Length is too long! type:%d len:%d", head->type, head->length);
        return SMTC_ERR;
    }

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_recv_body
 **功    能: 接收数据体
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 被操作的套接字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_recv_body(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    int n, left;
    smtc_reg_t *reg;
    socket_snap_t *read = &sck->read;
    smtc_header_t *head = (smtc_header_t *)read->addr;


    while (1)
    {
        /* 1. 接收报体 */
        left = head->length + sizeof(smtc_header_t) - read->off;

        n = read(sck->fd, read->addr + read->off, left);
        if (n == left)
        {
            read->off += n;
            break; 
        }
        else if (n > 0)
        {
            read->off += n;
            continue;
        }
        else if ((n < 0) && (EAGAIN == errno))
        {
            return SMTC_AGAIN;
        }
        else if (0 == n)
        {
            log_error(rsvr->log, "Client disconnected. errmsg:[%d] %s! "
                    "fd:%d type:%d flag:%d bodylen:%d total:%d left:%d off:%d",
                    errno, strerror(errno),
                    sck->fd, head->type, head->flag, head->length, read->total, left, read->off);
            return SMTC_SCK_CLOSED;
        }

        if (EINTR == errno)
        {
            continue;
        }

        ++rsvr->err_total; /* 错误计数 */

        log_error(rsvr->log, "errmsg:[%d] %s! type:%d len:%d n:%d fd:%d total:%d off:%d addr:%p",
                errno, strerror(errno), head->type,
                head->length, n, sck->fd, read->total, read->off, read->addr);

        return SMTC_ERR;
    }

    /* 2. 设置标志变量 */
    smtc_set_read_phase(read, SMTC_PHASE_READ_POST);

    log_trace(rsvr->log, "Recv success! type:%d len:%d", head->type, head->length);
    
    return SMTC_DONE;
}

/******************************************************************************
 **函数名称: smtc_rsvr_read_post
 **功    能: 数据接收完成后的处理
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 系统数据处理
 **     2. 自定义数据处理
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_read_post(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    socket_snap_t *read = &sck->read;
    smtc_header_t *head = read->addr;

    /* 1. 系统数据处理 */
    if (SMTC_SYS_DATA == head->flag)
    {
        return smtc_rsvr_proc_sys_mesg(ctx, recv, sck);
    }

    /* 2. 自定义数据处理 */
    return smtc_rsvr_proc_exp_mesg(ctx, recv, sck);
}

/******************************************************************************
 **函数名称: smtc_rsvr_proc_sys_mesg
 **功    能: 系统消息处理
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_proc_sys_mesg(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    socket_snap_t *read = &sck->read;
    smtc_header_t *head = (smtc_header_t *)read->addr;

    switch (head->type)
    {
        case SMTC_KPALIVE_REQ:
        {
            return smtc_rsvr_keepalive_req_hdl(ctx, recv, sck);
        }
        default:
        {
            log_error(rsvr->log, "Unknown message type! [%d]", head->type);
            return SMTC_HDL_DISCARD;
        }
    }
    
    return SMTC_HDL_DISCARD;
}

/******************************************************************************
 **函数名称: smtc_rsvr_proc_exp_mesg
 **功    能: 自定义消息处理
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 是否在NULL空间: 直接丢弃
 **     2. 放入队列中
 **     3. 发送处理请求
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_proc_exp_mesg(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    socket_snap_t *read = &sck->read;

    ++rsvr->recv_total; /* 总数 */

    /* 1. 是否在NULL空间: 直接丢弃 */
    if (read->addr == sck->null)
    {
        ++rsvr->drop_total;  /* 丢弃计数 */

        log_error(rsvr->log, "Drop data! tidx:%d fd:%d recv:%llu drop:%llu error:%llu",
                rsvr->tidx, sck->fd, rsvr->recv_total,
                rsvr->drop_total, rsvr->err_total);
        return SMTC_OK;
    }

    /* 2. 放入队列中 */
    if (queue_push(ctx->recvq[read->rqidx], read->addr))
    {
        queue_dealloc(ctx->recvq[read->rqidx], read->addr);

        ++rsvr->drop_total;  /* 丢弃计数 */

        log_error(rsvr->log, "Push failed! tidx:[%d] dataid:[%d] recv:%llu drop:%llu error:%llu",
                rsvr->tidx, read->dataid, rsvr->recv_total,
                rsvr->drop_total, rsvr->err_total);
        return SMTC_OK;  /* Note: Don't return error */
    }

    /* 3. 发送处理请求 */
    if (smtc_rsvr_cmd_send_proc_req(ctx, recv, sck))
    {
        /* log_error(rsvr->log, "errmsg:[%d] %s!", errno, strerror(errno)); */
        return SMTC_OK;  /* Note: Don't return error, resend at next time */
    }

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_event_core_hdl
 **功    能: 事件核心处理
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 接收命令数据
 **     2. 遍历接收数据
 **     3. 遍历发送数据
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_event_core_hdl(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr)
{
    /* 1. 接收命令数据 */
    if (!FD_ISSET(rsvr->cmd_sck_id, &rsvr->rdset))
    {
        smtc_rsvr_recv_cmd(ctx, rsvr);
    }

    /* 2. 遍历接收数据 */
    smtc_rsvr_trav_recv(ctx, rsvr);

    /* 3. 遍历发送数据 */
    smtc_rsvr_trav_send(ctx, rsvr);

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_event_timeout_hdl
 **功    能: 事件超时处理
 **输入参数: 
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 检测超时连接
 **     2. 删除超时连接
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_event_timeout_hdl(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr)
{
    smtc_sck_t *curr, *next;

    /* 1. 检测超时连接 */
    curr = rsvr->sck;
    rsvr->ctm = time(NULL);
    while (NULL != curr)
    {
        if (rsvr->ctm - curr->rtm >= 2*SMTC_SCK_KPALIVE_SEC)
        {
            log_trace(rsvr->log, "Didn't active for along time! fd:%d ip:%s",
                curr->fd, curr->ipaddr);

            /* 释放数据 */
            smtc_rsvr_read_release(ctx, recv, curr);

            /* 删除连接 */
            next = curr->next;
            smtc_rsvr_del_conn_hdl(rsvr, curr);

            curr = next;
            continue;
        }

        curr = curr->next;
    }

    /* 2. 重复发送处理命令 */
    smtc_rsvr_cmd_resend_proc_req(ctx, rsvr);

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_keepalive_req_hdl
 **功    能: 保活请求处理
 **输入参数: 
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_keepalive_req_hdl(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    void *addr;
    smtc_header_t *head;

    /* 1. 分配消息空间 */
    addr = slab_alloc(rsvr->pool, sizeof(smtc_header_t));
    if (NULL == addr)
    {
        log_error(rsvr->log, "Alloc memory from slab failed!");
        return SMTC_ERR;
    }

    /* 2. 回复消息内容 */
    head = (smtc_header_t *)addr;

    head->type = SMTC_KPALIVE_REP;
    head->length = 0;
    head->flag = SMTC_SYS_DATA;
    head->mark = SMTC_MSG_MARK_KEY;
    
    /* 3. 加入发送列表 */
    smtc_rsvr_add_mesg(rsvr, sck, addr);

    log_debug(rsvr->log, "Add respond of keepalive request!");

    return SMTC_HDL_DONE;
}

/******************************************************************************
 **函数名称: smtc_rsvr_add_conn_hdl
 **功    能: 添加网络连接
 **输入参数: 
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_add_conn_hdl(smtc_rsvr_t *rsvr, smtc_cmd_add_sck_t *req)
{
    smtc_sck_t *add;

    /* 1. 分配连接空间 */
    add = slab_alloc(rsvr->pool, sizeof(smtc_sck_t));
    if (NULL == add)
    {
        log_error(rsvr->log, "Alloc memory failed!");
        return SMTC_ERR;
    }

    add->fd = req->sckid;
    add->ctm = time(NULL);
    add->rtm = add->ctm;
    add->wtm = add->ctm;
    snprintf(add->ipaddr, sizeof(add->ipaddr), "%s", req->ipaddr);

    add->read.dataid = NULLID;
    add->read.addr = NULL;

    /* 2. 将结点加入到套接字链表 */
    if (NULL == rsvr->sck)
    {
        rsvr->sck = add;
        add->next = NULL;
    }
    else
    {
        add->next = rsvr->sck;
        rsvr->sck = add;
    }

    ++rsvr->connections; /* 统计TCP连接数 */

    log_trace(rsvr->log, "Tidx [%d] insert sckid [%d] success! ip:%s",
        rsvr->tidx, req->sckid, req->ipaddr);

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_del_conn_hdl
 **功    能: 删除网络连接
 **输入参数: 
 **     rsvr: 接收服务
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_del_conn_hdl(smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    smtc_sck_t *curr, *prev;

    curr = rsvr->sck;
    prev = curr;
    while (NULL != curr)
    {
        if (sck == curr)
        {
            if (prev == curr)
            {
                rsvr->sck = curr->next;
            }
            else
            {
                prev->next = curr->next;
            }

            Close(curr->fd);
            smtc_rsvr_clear_mesg(rsvr, curr);
            curr->recv_total = 0;

            if (NULL != curr->null)
            {
                slab_dealloc(rsvr->pool, curr->null);
            }
            slab_dealloc(rsvr->pool, curr);

            --rsvr->connections; /* 统计TCP连接数 */

            return SMTC_OK;
        }

        prev = curr;
        curr= curr->next;
    }

    return SMTC_OK; /* Didn't found */
}

/******************************************************************************
 **函数名称: smtc_rsvr_del_all_conn_hdl
 **功    能: 删除接收线程所有的套接字
 **输入参数: 
 **     rsvr: 接收服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static void smtc_rsvr_del_all_conn_hdl(smtc_rsvr_t *rsvr)
{
    smtc_sck_t *curr, *next;

    curr = rsvr->sck; 
    while (NULL != curr)
    {
        next = curr->next;

        Close(curr->fd);
        smtc_rsvr_clear_mesg(rsvr, curr);

        if (NULL != curr->null)
        {
            slab_dealloc(rsvr->pool, curr->null);
        }
        slab_dealloc(rsvr->pool, curr);

        curr = next;
    }

    rsvr->connections = 0; /* 统计TCP连接数 */
    rsvr->sck = NULL;
    return;
}

/******************************************************************************
 **函数名称: smtc_rsvr_add_mesg
 **功    能: 添加发送消息
 **输入参数: 
 **    rsvr: 接收服务
 **    sck: 套接字对象
 **    addr: 将要发送的数据
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **    将要发送的数据放在链表的末尾
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.07.04 #
 ******************************************************************************/
static int smtc_rsvr_add_mesg(smtc_rsvr_t *rsvr, smtc_sck_t *sck, void *addr)
{
    list_t *add, *item, *tail = NULL;

    /* 1.创建新结点 */
    add = slab_alloc(rsvr->pool, sizeof(list_t));
    if (NULL == add)
    {
        log_debug(rsvr->log, "Alloc memory failed!");
        return SMTC_ERR;
    }

    add->data = addr;
    add->next = NULL;

    /* 2.插入链尾 */
    item = sck->mesg_list;
    if (NULL == item)
    {
        sck->mesg_list = add;
        return SMTC_OK;
    }

    /* 3.查找链尾 */
    do
    {
        tail = item;
        item = item->next;
    }while (NULL != item);

    tail->next = add;

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_fetch_mesg
 **功    能: 获取发送消息
 **输入参数: 
 **    rsvr: 接收服务
 **    sck: 套接字对象
 **输出参数: NONE
 **返    回: 数据地址
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static void *smtc_rsvr_fetch_mesg(smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    void *addr;
    list_t *curr = sck->mesg_list;

    if (NULL == curr)
    {
        return NULL;
    }
    
    sck->mesg_list = curr->next;
    addr = curr->data;

    slab_dealloc(rsvr->pool, curr);

    return addr;
}

/******************************************************************************
 **函数名称: smtc_rsvr_clear_mesg
 **功    能: 清空发送消息
 **输入参数: 
 **    rsvr: 接收服务
 **    sck: 将要清空的套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.07.04 #
 ******************************************************************************/
static int smtc_rsvr_clear_mesg(smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    list_t *curr, *next;

    curr = sck->mesg_list; 
    while (NULL != curr)
    {
        next = curr->next;

        slab_dealloc(rsvr->pool, curr->data);
        slab_dealloc(rsvr->pool, curr);

        curr = next;
    }

    sck->mesg_list = NULL;
    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_cmd_send_proc_req
 **功    能: 发送处理请求
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_cmd_send_proc_req(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr, smtc_sck_t *sck)
{
    int times = 0, widx;
    smtc_cmd_t cmd;
    char path[FILE_PATH_MAX_LEN];
    smtc_conf_t *conf = &ctx->conf;
    smtc_cmd_proc_req_t *req = (smtc_cmd_proc_req_t *)&cmd.args;

    cmd.type = SMTC_CMD_PROC_REQ;
    req->ori_recv_tidx = rsvr->tidx;
    req->num = ++rsvr->delay_total[sck->read.rqidx]; /* +1 */
    req->rqidx = sck->read.rqidx;

    /* 1. 随机选择Work线程 */
    /* widx = smtc_rand_work(ctx); */
    widx = sck->read.rqidx / SMTC_WORKER_HDL_QNUM;

    smtc_wsvr_usck_path(conf, path, widx);

    /* 2. 发送处理命令 */
    if (usck_udp_send(rsvr->cmd_sck_id, path, &cmd, sizeof(smtc_cmd_t)) < 0)
    {
        log_debug(rsvr->log, "Send command failed! errmsg:[%d] %s! path:[%s]",
                errno, strerror(errno), path);
        return SMTC_ERR;
    }

    rsvr->delay_total[sck->read.rqidx] = 0;

    return SMTC_OK;
}

/******************************************************************************
 **函数名称: smtc_rsvr_cmd_resend_proc_req
 **功    能: 重复发送处理请求
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int smtc_rsvr_cmd_resend_proc_req(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr)
{
    int widx, times = 0, idx;
    smtc_cmd_t cmd;
    char path[FILE_PATH_MAX_LEN];
    smtc_cmd_proc_req_t *req = (smtc_cmd_proc_req_t *)&cmd.args;
    smtc_conf_t *conf = &ctx->conf;


    /* 依次检测各Recv队列的滞留数据 */
    for (idx=0; idx<ctx->conf.rqnum; ++idx)
    {
        if (rsvr->delay_total[idx] > 0)
        {
            cmd.type = SMTC_CMD_PROC_REQ;
            req->ori_recv_tidx = rsvr->tidx;
            req->rqidx = idx;
            req->num = rsvr->delay_total[idx];

            /* 1. 随机选择Work线程 */
            /* widx = smtc_rand_work(ctx); */
            widx = idx / SMTC_WORKER_HDL_QNUM;

            smtc_wsvr_usck_path(conf, path, widx);

            /* 2. 发送处理命令 */
            if (usck_udp_send(rsvr->cmd_sck_id, path, &cmd, sizeof(smtc_cmd_t)) < 0)
            {
                log_debug(rsvr->log, "Send command failed! errmsg:[%d] %s! path:[%s]",
                        errno, strerror(errno), path);
                continue;
            }

            rsvr->delay_total[idx] = 0;
        }
    }

    return 0;
}

/******************************************************************************
 **函数名称: smtc_rsvr_cmd_send_proc_all_req
 **功    能: 发送处理所有数据的请求
 **输入参数: 
 **     ctx: 全局对象
 **     rsvr: 接收服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static void smtc_rsvr_cmd_send_proc_all_req(smtc_cntx_t *ctx, smtc_rsvr_t *rsvr)
{
    int widx, times = 0, idx;
    smtc_cmd_t cmd;
    char path[FILE_PATH_MAX_LEN];
    smtc_conf_t *conf = &ctx->conf;
    smtc_cmd_proc_req_t *req = (smtc_cmd_proc_req_t *)&cmd.args;

     /* 1. 设置命令参数 */
    cmd.type = SMTC_CMD_PROC_REQ;
    req->ori_recv_tidx = rsvr->tidx;
    req->num = -1; /* 取出所有数据 */

     /* 2. 依次遍历所有Recv队列 让Work线程处理其中的数据 */
    for (idx=0; idx<conf->rqnum; ++idx)
    {
        req->rqidx = idx;

    AGAIN:
        /* 2.1 随机选择Work线程 */
        widx = rand() % conf->wrk_thd_num;

        smtc_wsvr_usck_path(conf, path, widx);

        /* 2.2 发送处理命令 */
        if (usck_udp_send(rsvr->cmd_sck_id, path, &cmd, sizeof(smtc_cmd_t)) < 0)
        {
            if (++times < 3)
            {
                goto AGAIN;
            }

            log_debug(rsvr->log, "Send command failed! errmsg:[%d] %s! path:%s",
                    errno, strerror(errno), path);

            continue;
        }

        rsvr->delay_total[idx] = 0;
    }

    return;
}