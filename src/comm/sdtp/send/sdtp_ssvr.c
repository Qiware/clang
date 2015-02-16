#include <fcntl.h>
#include <memory.h>
#include <stdarg.h>
#include <sys/un.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "shm_opt.h"
#include "syscall.h"
#include "sdtp_cmd.h"
#include "sdtp_cli.h"
#include "sdtp_ssvr.h"

/* 静态函数 */
static int _sdtp_ssvr_startup(sdtp_ssvr_cntx_t *ctx);
static void *sdtp_ssvr_routine(void *_ctx);

static int sdtp_ssvr_init(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr, int tidx);
static sdtp_ssvr_t *sdtp_ssvr_get_curr(sdtp_ssvr_cntx_t *ctx);

static int sdtp_ssvr_creat_sendq(sdtp_ssvr_t *ssvr, const sdtp_ssvr_conf_t *conf);
static int sdtp_ssvr_creat_usck(sdtp_ssvr_t *ssvr, const sdtp_ssvr_conf_t *conf);

static int sdtp_ssvr_kpalive_req(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr);

static int sdtp_ssvr_recv_cmd(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr);
static int sdtp_ssvr_recv_proc(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr);

static int sdtp_ssvr_data_proc(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr, sdtp_ssvr_sck_t *sck);
static int sdtp_ssvr_sys_mesg_proc(sdtp_ssvr_t *ssvr, sdtp_ssvr_sck_t *sck, void *addr);
static int sdtp_ssvr_exp_mesg_proc(sdtp_ssvr_t *ssvr, sdtp_ssvr_sck_t *sck, void *addr);

static int sdtp_ssvr_timeout_hdl(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr);
static int sdtp_ssvr_proc_cmd(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr, const sdtp_cmd_t *cmd);
static int sdtp_ssvr_send_data(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr);

static int sdtp_ssvr_clear_mesg(sdtp_ssvr_t *ssvr);

/******************************************************************************
 **函数名称: sdtp_ssvr_add_mesg
 **功    能: 添加发送消息
 **输入参数: 
 **     ssvr: 发送服务
 **     addr: 消息地址
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1.创建新结点
 **     2.插入链尾
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.16 #
 ******************************************************************************/
#define sdtp_ssvr_add_mesg(ssvr, addr) list_rpush(ssvr->sck.mesg_list, addr)

/******************************************************************************
 **函数名称: sdtp_ssvr_get_mesg
 **功    能: 获取发送消息
 **输入参数: 
 **     ssvr: 发送服务
 **     sck: 连接对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     取出链首结点的数据
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.16 #
 ******************************************************************************/
#define sdtp_ssvr_get_mesg(ssvr) list_pop(ssvr->sck.mesg_list)



/******************************************************************************
 **函数名称: sdtp_ssvr_startup
 **功    能: 启动发送端
 **输入参数: 
 **     conf: 配置信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 创建上下文对象
 **     2. 加载配置文件
 **     3. 启动各发送服务
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
sdtp_ssvr_cntx_t *sdtp_ssvr_startup(const sdtp_ssvr_conf_t *conf, log_cycle_t *log)
{
    sdtp_ssvr_cntx_t *ctx;

    /* 1. 创建上下文对象 */
    ctx = (sdtp_ssvr_cntx_t *)calloc(1, sizeof(sdtp_ssvr_cntx_t));
    if (NULL == ctx)
    {
        printf("errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    ctx->log = log;

    /* 2. 加载配置信息 */
    memcpy(&ctx->conf, conf, sizeof(sdtp_ssvr_conf_t));

    /* 3. 启动各发送服务 */
    if (_sdtp_ssvr_startup(ctx))
    {
        printf("Initalize send thread failed!");
        return NULL;
    }

    return ctx;
}

/******************************************************************************
 **函数名称: _sdtp_ssvr_startup
 **功    能: 启动发送端
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 创建发送线程池
 **     2. 创建发送线程对象
 **     3. 设置发送线程对象
 **     4. 注册发送线程回调
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int _sdtp_ssvr_startup(sdtp_ssvr_cntx_t *ctx)
{
    int idx;
    sdtp_ssvr_t *ssvr;
    thread_pool_option_t option;
    sdtp_ssvr_conf_t *conf = &ctx->conf;

    memset(&option, 0, sizeof(option));

    option.pool = (void *)ctx->slab;
    option.alloc = (mem_alloc_cb_t)slab_alloc;
    option.dealloc = (mem_dealloc_cb_t)slab_dealloc;

    /* 1. 创建发送线程池 */
    ctx->sendtp = thread_pool_init(conf->snd_thd_num, &option);
    if (NULL == ctx->sendtp)
    {
        thread_pool_destroy(ctx->sendtp);
        ctx->sendtp = NULL;
        return SDTP_ERR;
    }

    /* 2. 创建发送线程对象 */
    ctx->sendtp->data = calloc(conf->snd_thd_num, sizeof(sdtp_ssvr_t));
    if (NULL == ctx->sendtp->data)
    {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    /* 3. 设置发送线程对象 */
    ssvr = (sdtp_ssvr_t *)ctx->sendtp->data;
    for (idx=0; idx<conf->snd_thd_num; ++idx, ++ssvr)
    {
        if (sdtp_ssvr_init(ctx, ssvr, idx))
        {
            log_fatal(ctx->log, "Initialize send thread failed!");
            return SDTP_ERR;
        }
    }

    /* 4. 注册发送线程回调 */
    for (idx=0; idx<conf->snd_thd_num; idx++)
    {
        thread_pool_add_worker(ctx->sendtp, sdtp_ssvr_routine, ctx);
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_init
 **功    能: 初始化发送线程
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 发送线程对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_init(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr, int tidx)
{
    void *addr;
    sdtp_ssvr_conf_t *conf = &ctx->conf;
    sdtp_snap_t *recv = &ssvr->sck.recv;
    sdtp_snap_t *send = &ssvr->sck.send;

    ssvr->tidx = tidx;
    ssvr->log = ctx->log;

    /* 1. 创建发送队列 */
    if (sdtp_ssvr_creat_sendq(ssvr, conf))
    {
        log_error(ssvr->log, "Initialize send queue failed!");
        return SDTP_ERR;
    }

    /* 2. 创建unix套接字 */
    if (sdtp_ssvr_creat_usck(ssvr, conf))
    {
        log_error(ssvr->log, "Initialize send queue failed!");
        return SDTP_ERR;
    }

    /* 3. 创建SLAB内存池 */
    addr = calloc(1, SDTP_MEM_POOL_SIZE);
    if (NULL == addr)
    {
        log_error(ssvr->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    ssvr->pool = slab_init(addr, SDTP_MEM_POOL_SIZE);
    if (NULL == ssvr->pool)
    {
        log_error(ssvr->log, "Initialize slab mem-pool failed!");
        return SDTP_ERR;
    }

    /* 4. 初始化发送缓存 */
    addr = calloc(1, conf->send_buff_size);
    if (NULL == addr)
    {
        log_error(ssvr->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    sdtp_snap_setup(send, addr, conf->send_buff_size);

    /* 5. 初始化接收缓存 */
    addr = calloc(1, conf->recv_buff_size);
    if (NULL == addr)
    {
        log_error(ssvr->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    sdtp_snap_setup(recv, addr, conf->recv_buff_size);

    /* 6. 连接接收服务器 */
    if ((ssvr->sck.fd = tcp_connect(AF_INET, conf->ipaddr, conf->port)) < 0)
    {
        log_error(ssvr->log, "Connect recv server failed!");
        /* Note: Don't return error! */
    }
 
    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_creat_sendq
 **功    能: 创建发送线程的发送队列
 **输入参数: 
 **     ssvr: 发送线程对象
 **     conf: 配置信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_creat_sendq(sdtp_ssvr_t *ssvr, const sdtp_ssvr_conf_t *conf)
{
    key_t key;
    const sdtp_queue_conf_t *qcf = &conf->qcf;

    /* 1. 创建/连接发送队列 */
    key = shm_ftok(qcf->name, ssvr->tidx);
    if (-1 == key)
    {
        log_error(ssvr->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    ssvr->sq = shm_queue_creat(key, qcf->count, qcf->size);
    if (NULL == ssvr->sq)
    {
        log_error(ssvr->log, "Create send-queue failed!");
        return SDTP_ERR;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_creat_usck
 **功    能: 创建发送线程的命令接收套接字
 **输入参数: 
 **     ssvr: 发送线程对象
 **     conf: 配置信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_creat_usck(sdtp_ssvr_t *ssvr, const sdtp_ssvr_conf_t *conf)
{
    char path[FILE_PATH_MAX_LEN];

    sdtp_ssvr_usck_path(conf, path, ssvr->tidx);
    
    ssvr->cmd_sck_id = unix_udp_creat(path);
    if (ssvr->cmd_sck_id < 0)
    {
        log_error(ssvr->log, "errmsg:[%d] %s! path:%s", errno, strerror(errno), path);
        return SDTP_ERR;
    }

    log_trace(ssvr->log, "cmd_sck_id:[%d] path:%s", ssvr->cmd_sck_id, path);
    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_bind_cpu
 **功    能: 绑定CPU
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 线程对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.16 #
 ******************************************************************************/
static void sdtp_ssvr_bind_cpu(sdtp_ssvr_cntx_t *ctx, int tidx)
{
    int idx, mod;
    cpu_set_t cpuset;
    sdtp_cpu_conf_t *cpu = &ctx->conf.cpu;

    mod = sysconf(_SC_NPROCESSORS_CONF) - cpu->start;
    if (mod <= 0)
    {
        idx = tidx % sysconf(_SC_NPROCESSORS_CONF);
    }
    else
    {
        idx = cpu->start + (tidx % mod);
    }

    CPU_ZERO(&cpuset);
    CPU_SET(idx, &cpuset);

    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}

/******************************************************************************
 **函数名称: sdtp_ssvr_set_rwset
 **功    能: 设置读写集
 **输入参数: 
 **     ssvr: 发送线程对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.16 #
 ******************************************************************************/
void sdtp_ssvr_set_rwset(sdtp_ssvr_t *ssvr) 
{ 
    FD_ZERO(&ssvr->rset);
    FD_ZERO(&ssvr->wset);

    FD_SET(ssvr->cmd_sck_id, &ssvr->rset);

    if (ssvr->sck.fd < 0)
    {
        return;
    }

    ssvr->max = (ssvr->cmd_sck_id > ssvr->sck.fd)? ssvr->cmd_sck_id : ssvr->sck.fd;

    /* 1 设置读集合 */
    FD_SET(ssvr->sck.fd, &ssvr->rset);

    /* 2 设置写集合: 发送至接收端 */
    if (NULL != ssvr->sck.mesg_list->head
        || 0 != shm_queue_data_count(ssvr->sq))
    {
        FD_SET(ssvr->sck.fd, &ssvr->wset);
    }

    return;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_routine
 **功    能: Snd线程调用的主程序
 **输入参数: 
 **     _ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 获取发送线程
 **     2. 绑定CPU
 **     3. 调用发送主程
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.16 #
 ******************************************************************************/
static void *sdtp_ssvr_routine(void *_ctx)
{
    int ret;
    sdtp_ssvr_t *ssvr;
    sdtp_ssvr_sck_t *sck;
    struct timeval timeout;
    sdtp_ssvr_cntx_t *ctx = (sdtp_ssvr_cntx_t *)_ctx;
    sdtp_ssvr_conf_t *conf = &ctx->conf;


    /* 1. 获取发送线程 */
    ssvr = sdtp_ssvr_get_curr(ctx);
    if (NULL == ssvr)
    {
        log_fatal(ssvr->log, "Get current thread failed!");
        abort();
        return (void *)-1;
    }

    sck = &ssvr->sck;

    /* 2. 绑定指定CPU */
    sdtp_ssvr_bind_cpu(ctx, ssvr->tidx);

    /* 3. 进行事件处理 */
    for (;;)
    {
        /* 3.1 连接合法性判断 */
        if (sck->fd < 0)
        {
            sdtp_ssvr_clear_mesg(ssvr);

            /* 重连Recv端 */
            if ((sck->fd = tcp_connect(AF_INET, conf->ipaddr, conf->port)) < 0)
            {
                log_error(ssvr->log, "Conncet receive-server failed!");

                Sleep(SDTP_RECONN_INTV);
                continue;
            }
        }

        /* 3.2 等待事件通知 */
        sdtp_ssvr_set_rwset(ssvr);

        timeout.tv_sec = SDTP_SSVR_TMOUT_SEC;
        timeout.tv_usec = SDTP_SSVR_TMOUT_USEC;
        ret = select(ssvr->max+1, &ssvr->rset, &ssvr->wset, NULL, &timeout);
        if (ret < 0)
        {
            if (EINTR == errno)
            {
                continue;
            }

            log_fatal(ssvr->log, "errmsg:[%d] %s!", errno, strerror(errno));
            abort();
            return (void *)-1;
        }
        else if (0 == ret)
        {
            sdtp_ssvr_timeout_hdl(ctx, ssvr);
            continue;
        }

        /* 发送数据: 发送优先 */
        if (FD_ISSET(sck->fd, &ssvr->wset))
        {
            sdtp_ssvr_send_data(ctx, ssvr);
        }

        /* 接收命令 */
        if (FD_ISSET(ssvr->cmd_sck_id, &ssvr->rset))
        {
            sdtp_ssvr_recv_cmd(ctx, ssvr);
        }

        /* 接收Recv服务的数据 */
        if (FD_ISSET(sck->fd, &ssvr->rset))
        {
            sdtp_ssvr_recv_proc(ctx, ssvr);
        }
    }

    abort();
    return (void *)-1;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_kpalive_req
 **功    能: 发送保活命令
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: Snd线程对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **     因发送KeepAlive请求时，说明链路空闲时间较长，
 **     因此发送数据时，不用判断EAGAIN的情况是否存在。
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_kpalive_req(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr)
{
    void *addr;
    sdtp_header_t *head;
    int size = sizeof(sdtp_header_t);
    sdtp_ssvr_sck_t *sck = &ssvr->sck;
    sdtp_snap_t *send = &ssvr->sck.send;

    /* 1. 上次发送保活请求之后 仍未收到应答 */
    if ((sck->fd < 0) 
        || (SDTP_KPALIVE_STAT_SENT == sck->kpalive)) 
    {
        Close(sck->fd);
        Free(send->addr);

        log_error(ssvr->log, "Didn't get keepalive respond for a long time!");
        return SDTP_OK;
    }

    addr = slab_alloc(ssvr->pool, size);
    if (NULL == addr)
    {
        log_error(ssvr->log, "Alloc memory from slab failed!");
        return SDTP_OK;
    }

    /* 2. 设置心跳数据 */
    head = (sdtp_header_t *)addr;

    head->type = SDTP_KPALIVE_REQ;
    head->length = 0;
    head->flag = SDTP_SYS_MESG;
    head->checksum = SDTP_CHECK_SUM;

    /* 3. 加入发送列表 */
    sdtp_ssvr_add_mesg(ssvr, addr);

    log_debug(ssvr->log, "Add keepalive request success! fd:[%d]", sck->fd);

    sdtp_set_kpalive_stat(sck, SDTP_KPALIVE_STAT_SENT);
    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_get_curr
 **功    能: 获取当前发送线程的上下文
 **输入参数: 
 **     ssvr: 发送线程对象
 **     conf: 配置信息
 **输出参数: NONE
 **返    回: Address of sndsvr
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static sdtp_ssvr_t *sdtp_ssvr_get_curr(sdtp_ssvr_cntx_t *ctx)
{
    int tidx;

    /* 1. 获取线程索引 */
    tidx = thread_pool_get_tidx(ctx->sendtp);
    if (tidx < 0)
    {
        log_error(ctx->log, "Get current thread index failed!");
        return NULL;
    }

    /* 2. 返回线程对象 */
    return (sdtp_ssvr_t *)(ctx->sendtp->data + tidx * sizeof(sdtp_ssvr_t));
}

/******************************************************************************
 **函数名称: sdtp_ssvr_timeout_hdl
 **功    能: 超时处理
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 发送线程全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 判断是否长时间无数据通信
 **     2. 发送保活数据
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_timeout_hdl(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr)
{
    time_t curr_tm = time(NULL);
    sdtp_ssvr_sck_t *sck = &ssvr->sck;

    /* 1. 判断是否长时无数据 */
    if ((curr_tm - sck->wrtm) < SDTP_KPALIVE_INTV)
    {
        return SDTP_OK;
    }

    /* 2. 发送保活请求 */
    if (sdtp_ssvr_kpalive_req(ctx, ssvr))
    {
        log_error(ssvr->log, "Connection keepalive failed!");
        return SDTP_ERR;
    }

    sck->wrtm = curr_tm;

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_recv_proc
 **功    能: 接收网络数据
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 发送服务
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 接收网络数据
 **     2. 进行数据处理
 **注意事项: 
 **       ------------------------------------------------
 **      | 已处理 |     未处理     |       剩余空间       |
 **       ------------------------------------------------
 **      |XXXXXXXX|////////////////|                      |
 **      |XXXXXXXX|////////////////|         left         |
 **      |XXXXXXXX|////////////////|                      |
 **       ------------------------------------------------
 **      ^        ^                ^                      ^
 **      |        |                |                      |
 **     addr     optr             iptr                   end
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_recv_proc(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr)
{
    int n, left;
    sdtp_ssvr_sck_t *sck = &ssvr->sck;
    sdtp_snap_t *recv = &sck->recv;

    sck->rdtm = time(NULL);

    while (1)
    {
        /* 1. 接收网络数据 */
        left = (int)(recv->end - recv->iptr);

        n = read(sck->fd, recv->iptr, left);
        if (n > 0)
        {
            recv->iptr += n;

            /* 2. 进行数据处理 */
            if (sdtp_ssvr_data_proc(ctx, ssvr, sck))
            {
                log_error(ssvr->log, "Proc data failed! fd:%d", sck->fd);

                Close(sck->fd);
                sdtp_snap_reset(recv);
                return SDTP_ERR;
            }
            continue;
        }
        else if (0 == n)
        {
            log_info(ssvr->log, "Client disconnected. fd:%d n:%d/%d", sck->fd, n, left);
            Close(sck->fd);
            sdtp_snap_reset(recv);
            return SDTP_DISCONN;
        }
        else if ((n < 0) && (EAGAIN == errno))
        {
            return SDTP_OK; /* Again */
        }

        if (EINTR == errno)
        {
            continue;
        }

        log_error(ssvr->log, "errmsg:[%d] %s. fd:%d", errno, strerror(errno), sck->fd);

        Close(sck->fd);
        sdtp_snap_reset(recv);
        return SDTP_ERR;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_data_proc
 **功    能: 进行数据处理
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 发送服务
 **     sck: 连接对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 是否含有完整数据
 **     2. 校验数据合法性
 **     3. 进行数据处理
 **注意事项: 
 **       ------------------------------------------------
 **      | 已处理 |     未处理     |       剩余空间       |
 **       ------------------------------------------------
 **      |XXXXXXXX|////////////////|                      |
 **      |XXXXXXXX|////////////////|         left         |
 **      |XXXXXXXX|////////////////|                      |
 **       ------------------------------------------------
 **      ^        ^                ^                      ^
 **      |        |                |                      |
 **     addr     optr             iptr                   end
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_data_proc(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr, sdtp_ssvr_sck_t *sck)
{
    sdtp_header_t *head;
    int len, mesg_len;
    sdtp_snap_t *recv = &sck->recv;

    while (1)
    {
        head = (sdtp_header_t *)recv->optr;
        len = (int)(recv->iptr - recv->optr);
        if (len < sizeof(sdtp_header_t))
        {
            goto LEN_NOT_ENOUGH; /* 不足一条数据时 */
        }

        /* 1. 是否不足一条数据 */
        mesg_len = sizeof(sdtp_header_t) + ntohl(head->length);
        if (len < mesg_len)
        {
        LEN_NOT_ENOUGH:
            if (recv->iptr == recv->end) 
            {
                /* 防止OverWrite的情况发生 */
                if ((recv->optr - recv->addr) < (recv->end - recv->iptr))
                {
                    log_error(ssvr->log, "Data length is invalid!");
                    return SDTP_ERR;
                }

                memcpy(recv->addr, recv->optr, len);
                recv->optr = recv->addr;
                recv->iptr = recv->optr + len;
                return SDTP_OK;
            }
            return SDTP_OK;
        }

        /* 2. 至少一条数据时 */
        /* 2.1 转化字节序 */
        head->type = ntohs(head->type);
        head->flag = head->flag;
        head->length = ntohl(head->length);
        head->checksum = ntohl(head->checksum);

        /* 2.2 校验合法性 */
        if (!SDTP_HEAD_ISVALID(head))
        {
            log_error(ssvr->log, "Header is invalid! CheckSum:%u/%u type:%d len:%d flag:%d",
                    head->checksum, SDTP_CHECK_SUM, head->type, head->length, head->flag);
            return SDTP_ERR;
        }

        /* 2.3 进行数据处理 */
        if (SDTP_SYS_MESG == head->flag)
        {
            sdtp_ssvr_sys_mesg_proc(ssvr, sck, recv->addr);
        }
        else
        {
            sdtp_ssvr_exp_mesg_proc(ssvr, sck, recv->addr);
        }

        recv->optr += mesg_len;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_recv_cmd
 **功    能: 接收命令数据
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 发送线程对象
 **输出参数: 
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 接收命令
 **     2. 处理命令
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_recv_cmd(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr)
{
    sdtp_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));

    /* 1. 接收命令 */
    if (unix_udp_recv(ssvr->cmd_sck_id, &cmd, sizeof(cmd)) < 0)
    {
        log_error(ssvr->log, "Recv command failed! errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    /* 2. 处理命令 */
    return sdtp_ssvr_proc_cmd(ctx, ssvr, &cmd);
}

/******************************************************************************
 **函数名称: sdtp_ssvr_proc_cmd
 **功    能: 命令处理
 **输入参数: 
 **     ssvr: 发送线程对象
 **     cmd: 接收到的命令信息
 **输出参数: 
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_proc_cmd(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr, const sdtp_cmd_t *cmd)
{
    sdtp_ssvr_sck_t *sck = &ssvr->sck;

    switch (cmd->type)
    {
        case SDTP_CMD_SEND:
        case SDTP_CMD_SEND_ALL:
        {
            if (fd_is_writable(sck->fd))
            {
                return sdtp_ssvr_send_data(ctx, ssvr);
            }
            return SDTP_OK;
        }
        default:
        {
            log_error(ssvr->log, "Unknown command! type:[%d]", cmd->type);
            return SDTP_OK;
        }
    }
    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_fill_send_buff
 **功    能: 填充发送缓冲区
 **输入参数: 
 **     ssvr: 发送线程
 **     sck: 连接对象
 **输出参数: 
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 从消息链表取数据
 **     2. 从发送队列取数据
 **注意事项: 
 **       ------------------------------------------------
 **      | 已处理 |     未处理     |       剩余空间       |
 **       ------------------------------------------------
 **      |XXXXXXXX|////////////////|                      |
 **      |XXXXXXXX|////////////////|         left         |
 **      |XXXXXXXX|////////////////|                      |
 **       ------------------------------------------------
 **      ^        ^                ^                      ^
 **      |        |                |                      |
 **     addr     optr             iptr                   end
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_fill_send_buff(sdtp_ssvr_t *ssvr, sdtp_ssvr_sck_t *sck)
{
    void *addr;
    int left, mesg_len;
    sdtp_header_t *head;
    sdtp_snap_t *send = &sck->send;

    /* 1. 从消息链表取数据 */
    for (;;)
    {
        /* 1.1 是否有数据 */
        head = (sdtp_header_t *)list_pop(ssvr->sck.mesg_list);
        if (NULL == head)
        {
            break; /* 无数据 */
        }

        /* 1.2 判断剩余空间 */
        if (SDTP_CHECK_SUM != head->checksum)
        {
            assert(0);
        }

        left = (int)(send->end - send->iptr);
        mesg_len = sizeof(sdtp_header_t) + head->length;
        if (left < mesg_len)
        {
            list_push(ssvr->sck.mesg_list, head);
            break; /* 空间不足 */
        }

        /* 1.3 取发送的数据 */
        head->type = htons(head->type);
        head->flag = head->flag;
        head->length = htonl(head->length);
        head->checksum = htonl(head->checksum);

        /* 1.4 拷贝至发送缓存 */
        memcpy(send->iptr, (void *)head, mesg_len);

        send->iptr += mesg_len;
        continue;
    }

    /* 2. 从发送队列取数据 */
    for (;;)
    {
        /* 2.1 判断发送缓存的剩余空间是否足够 */
        left = (int)(send->end - send->iptr);
        if (left < ssvr->sq->info->size)
        {
            break;  /* 空间不足 */
        }

        /* 2.2 取发送的数据 */
        addr = shm_queue_pop(ssvr->sq);
        if (NULL == addr)
        {
            break;  /* 无数据 */
        }

        head = (sdtp_header_t *)addr;

        mesg_len = sizeof(sdtp_header_t) + head->length;

        head->type = htons(head->type);
        head->flag = head->flag;
        head->length = htonl(head->length);
        head->checksum = htonl(head->checksum);

        /* 2.3 拷贝至发送缓存 */
        memcpy(send->iptr, addr, mesg_len);

        send->iptr += mesg_len;

        shm_queue_dealloc(ssvr->sq, addr);
        continue;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_send_data
 **功    能: 发送数据的请求处理
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 发送线程
 **输出参数: 
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 填充发送缓存
 **     2. 发送缓存数据
 **     3. 重置标识量
 **注意事项: 
 **       ------------------------------------------------
 **      | 已发送 |     待发送     |       剩余空间       |
 **       ------------------------------------------------
 **      |XXXXXXXX|////////////////|                      |
 **      |XXXXXXXX|////////////////|         left         |
 **      |XXXXXXXX|////////////////|                      |
 **       ------------------------------------------------
 **      ^        ^                ^                      ^
 **      |        |                |                      |
 **     addr     optr             iptr                   end
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_ssvr_send_data(sdtp_ssvr_cntx_t *ctx, sdtp_ssvr_t *ssvr)
{
    int n, len;
    time_t ctm = time(NULL);
    sdtp_ssvr_sck_t *sck = &ssvr->sck;
    sdtp_snap_t *send = &sck->send;

    sck->wrtm = ctm;

    for (;;)
    {
        /* 1. 填充发送缓存 */
        if (send->iptr == send->optr)
        {
            sdtp_ssvr_fill_send_buff(ssvr, sck);
        }

        /* 2. 发送缓存数据 */
        len = send->iptr - send->optr;
        if (0 == len)
        {
            break;
        }

        n = Writen(sck->fd, send->optr, len);
        if (n < 0)
        {
        #if defined(__SDTP_DEBUG__)
            send->fail++;
        #endif /*__SDTP_DEBUG__*/

            log_error(ssvr->log, "errmsg:[%d] %s! fd:%d len:[%d]",
                    errno, strerror(errno), sck->fd, len);

            Close(sck->fd);
            sdtp_snap_reset(send);
            return SDTP_ERR;
        }
        /* 只发送了部分数据 */
        else if (n != len)
        {
            send->optr += n;
        #if defined(__SDTP_DEBUG__)
            send->again++;
        #endif /*__SDTP_DEBUG__*/
            return SDTP_OK;
        }

        /* 3. 重置标识量 */
        sdtp_snap_reset(send);

    #if defined(__SDTP_DEBUG__)
        send->succ++;
    #endif /*__SDTP_DEBUG__*/
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_clear_mesg
 **功    能: 清空发送消息
 **输入参数: 
 **     ssvr: 发送服务
 **     sck: 连接对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     依次取出每条消息, 并释放所占有的空间
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.16 #
 ******************************************************************************/
static int sdtp_ssvr_clear_mesg(sdtp_ssvr_t *ssvr)
{
    void *data;

    while (1)
    {
        data = list_pop(ssvr->sck.mesg_list);
        if (NULL == data)
        {
            return SDTP_OK;
        }

        slab_dealloc(ssvr->pool, data);
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_sys_mesg_proc
 **功    能: 系统消息的处理
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 发送服务
 **     sck: 连接对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     根据消息类型调用对应的处理接口
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.16 #
 ******************************************************************************/
static int sdtp_ssvr_sys_mesg_proc(sdtp_ssvr_t *ssvr, sdtp_ssvr_sck_t *sck, void *addr)
{
    sdtp_header_t *head = (sdtp_header_t *)addr;

    switch (head->type)
    {
        case SDTP_KPALIVE_REP:  /* 保活应答 */
        {
            log_debug(ssvr->log, "Received keepalive respond!");

            sdtp_set_kpalive_stat(sck, SDTP_KPALIVE_STAT_SUCC);
            return SDTP_OK;
        }
        default:
        {
            log_error(ssvr->log, "Unknown type [%d]!", head->type);
            return SDTP_ERR;
        }
    }
    
    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_ssvr_exp_mesg_proc
 **功    能: 自定义消息的处理
 **输入参数: 
 **     ctx: 全局信息
 **     ssvr: 发送服务
 **     sck: 连接对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.16 #
 ******************************************************************************/
static int sdtp_ssvr_exp_mesg_proc(sdtp_ssvr_t *ssvr, sdtp_ssvr_sck_t *sck, void *addr)
{
    return SDTP_OK;
}
