/******************************************************************************
 ** Copyright(C) 2014-2024 Qiware technology Co., Ltd
 **
 ** 文件名: sdrd_recv.c
 ** 版本号: 1.0
 ** 描  述: 共享消息传输通道(Sharing Message Transaction Channel)
 **         1. 主要用于异步系统之间数据消息的传输
 ** 作  者: # Qifeng.zou # 2014.12.29 #
 ******************************************************************************/

#include "log.h"
#include "redo.h"
#include "shm_opt.h"
#include "sdtp_mesg.h"
#include "sdtp_comm.h"
#include "sdrd_recv.h"
#include "thread_pool.h"

static int _sdrd_init(sdrd_cntx_t *ctx);

static int sdrd_creat_recvq(sdrd_cntx_t *ctx);
static int sdrd_creat_sendq(sdrd_cntx_t *ctx);

static int sdrd_creat_recvtp(sdrd_cntx_t *ctx);
void sdrd_recvtp_destroy(void *_ctx, void *args);

static int sdrd_creat_worktp(sdrd_cntx_t *ctx);
void sdrd_worktp_destroy(void *_ctx, void *args);

static int sdrd_proc_def_hdl(int type, int orig, char *buff, size_t len, void *args);

/******************************************************************************
 **函数名称: sdrd_init
 **功    能: 初始化SDTP接收端
 **输入参数:
 **     conf: 配置信息
 **     log: 日志对象
 **输出参数: NONE
 **返    回: 全局对象
 **实现描述:
 **     1. 创建全局对象
 **     2. 备份配置信息
 **     3. 初始化接收端
 **注意事项:
 **作    者: # Qifeng.zou # 2014.12.30 #
 ******************************************************************************/
sdrd_cntx_t *sdrd_init(const sdrd_conf_t *conf, log_cycle_t *log)
{
    sdrd_cntx_t *ctx;

    /* > 创建全局对象 */
    ctx = (sdrd_cntx_t *)calloc(1, sizeof(sdrd_cntx_t));
    if (NULL == ctx) {
        fprintf(stderr, "errmsg:[%d] %s!\n", errno, strerror(errno));
        return NULL;
    }

    ctx->log = log;

    /* > 备份配置信息 */
    memcpy(&ctx->conf, conf, sizeof(sdrd_conf_t));

    ctx->conf.recvq_num = SDTP_WORKER_HDL_QNUM * conf->work_thd_num;

    /* > 初始化接收端 */
    if (_sdrd_init(ctx)) {
        log_error(ctx->log, "Initialize recv failed!");
        FREE(ctx);
        return NULL;
    }

    return ctx;
}

/******************************************************************************
 **函数名称: sdrd_launch
 **功    能: 启动SDTP接收端
 **输入参数:
 **     conf: 配置信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 设置接收线程回调
 **     2. 设置工作线程回调
 **     3. 创建侦听线程
 **注意事项:
 **作    者: # Qifeng.zou # 2014.12.30 #
 ******************************************************************************/
int sdrd_launch(sdrd_cntx_t *ctx)
{
    int idx;
    pthread_t tid;
    thread_pool_t *tp;
    sdrd_lsn_t *lsn = &ctx->listen;

    /* > 设置接收线程回调 */
    tp = ctx->recvtp;
    for (idx=0; idx<tp->num; ++idx) {
        thread_pool_add_worker(tp, sdrd_rsvr_routine, ctx);
    }

    /* > 设置工作线程回调 */
    tp = ctx->worktp;
    for (idx=0; idx<tp->num; ++idx) {
        thread_pool_add_worker(tp, sdrd_worker_routine, ctx);
    }

    /* > 创建侦听线程 */
    if (thread_creat(&lsn->tid, sdrd_lsn_routine, ctx)) {
        log_error(ctx->log, "Start listen failed");
        return SDTP_ERR;
    }

    /* > 创建分发线程 */
    if (thread_creat(&tid, sdrd_dist_routine, ctx)) {
        log_error(ctx->log, "Start distribute thread failed");
        return SDTP_ERR;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdrd_register
 **功    能: 消息处理的注册接口
 **输入参数:
 **     ctx: 全局对象
 **     type: 扩展消息类型 Range:(0 ~ SDTP_TYPE_MAX)
 **     proc: 回调函数
 **     args: 附加参数
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **     1. 只能用于注册处理扩展数据类型的处理
 **     2. 不允许重复注册
 **作    者: # Qifeng.zou # 2014.12.30 #
 ******************************************************************************/
int sdrd_register(sdrd_cntx_t *ctx, int type, sdtp_reg_cb_t proc, void *args)
{
    sdtp_reg_t *reg;

    if (type >= SDTP_TYPE_MAX) {
        log_error(ctx->log, "Data type is out of range!");
        return SDTP_ERR;
    }

    if (0 != ctx->reg[type].flag) {
        log_error(ctx->log, "Repeat register type [%d]!", type);
        return SDTP_ERR_REPEAT_REG;
    }

    reg = &ctx->reg[type];
    reg->type = type;
    reg->proc = proc;
    reg->args = args;
    reg->flag = 1;

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdrd_reg_init
 **功    能: 初始化注册对象
 **输入参数:
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.12.30 #
 ******************************************************************************/
static int sdrd_reg_init(sdrd_cntx_t *ctx)
{
    int idx;
    sdtp_reg_t *reg = &ctx->reg[0];

    for (idx=0; idx<SDTP_TYPE_MAX; ++idx, ++reg) {
        reg->type = idx;
        reg->proc = sdrd_proc_def_hdl;
        reg->flag = 0;
        reg->args = NULL;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: _sdrd_init
 **功    能: 初始化接收对象
 **输入参数:
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 初始化注册信息
 **     2. 创建接收队列
 **     3. 创建接收线程池
 **     4. 创建工作线程池
 **注意事项:
 **作    者: # Qifeng.zou # 2014.12.30 #
 ******************************************************************************/
static int _sdrd_init(sdrd_cntx_t *ctx)
{
    /* > 构建NODE->SVR映射表 */
    if (sdrd_node_to_svr_map_init(ctx)) {
        log_error(ctx->log, "Initialize sck-dev map table failed!");
        return SDTP_ERR;
    }

    /* > 初始化注册信息 */
    sdrd_reg_init(ctx);

    /* > 创建接收队列 */
    if (sdrd_creat_recvq(ctx)) {
        log_error(ctx->log, "Create recv queue failed!");
        return SDTP_ERR;
    }

    /* > 创建发送队列 */
    if (sdrd_creat_sendq(ctx)) {
        log_error(ctx->log, "Create send queue failed!");
        return SDTP_ERR;
    }

    /* > 创建接收线程池 */
    if (sdrd_creat_recvtp(ctx)) {
        log_error(ctx->log, "Create recv thread pool failed!");
        return SDTP_ERR;
    }

    /* > 创建工作线程池 */
    if (sdrd_creat_worktp(ctx)) {
        log_error(ctx->log, "Create worker thread pool failed!");
        return SDTP_ERR;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdrd_creat_recvq
 **功    能: 创建接收队列
 **输入参数:
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 创建队列数组
 **     2. 依次创建接收队列
 **注意事项:
 **作    者: # Qifeng.zou # 2014.12.30 #
 ******************************************************************************/
static int sdrd_creat_recvq(sdrd_cntx_t *ctx)
{
    int idx;
    sdrd_conf_t *conf = &ctx->conf;

    /* > 创建队列数组 */
    ctx->recvq = calloc(conf->recvq_num, sizeof(queue_t *));
    if (NULL == ctx->recvq) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    /* > 依次创建接收队列 */
    for(idx=0; idx<conf->recvq_num; ++idx) {
        ctx->recvq[idx] = queue_creat(conf->recvq.max, conf->recvq.size);
        if (NULL == ctx->recvq[idx]) {
            log_error(ctx->log, "Create queue failed! max:%d size:%d",
                    conf->recvq.max, conf->recvq.size);
            return SDTP_ERR;
        }
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: _sdrd_creat_sendq
 **功    能: 创建发送队列
 **输入参数:
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 创建队列数组
 **     2. 依次创建接收队列
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.22 #
 ******************************************************************************/
static int _sdrd_creat_sendq(sdrd_cntx_t *ctx)
{
    int idx;
    sdrd_conf_t *conf = &ctx->conf;

    /* > 创建队列数组 */
    ctx->sendq = calloc(conf->recv_thd_num, sizeof(queue_t *));
    if (NULL == ctx->sendq) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    /* > 依次创建发送队列 */
    for(idx=0; idx<conf->recv_thd_num; ++idx)
    {
        ctx->sendq[idx] = queue_creat(conf->sendq.max, conf->sendq.size);
        if (NULL == ctx->sendq[idx]) {
            log_error(ctx->log, "Create send-queue failed! max:%d size:%d",
                    conf->sendq.max, conf->sendq.size);
            return SDTP_ERR;
        }
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdrd_creat_sendq
 **功    能: 创建发送队列
 **输入参数:
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 通过路径生成KEY，再根据KEY创建共享内存队列
 **注意事项:
 **作    者: # Qifeng.zou # 2015.05.20 #
 ******************************************************************************/
static int sdrd_creat_sendq(sdrd_cntx_t *ctx)
{
    ctx->distq = sdrd_shm_distq_creat(&ctx->conf);
    if (NULL == ctx->distq) {
        log_error(ctx->log, "Create shm-queue failed!");
        return SDTP_ERR;
    }

    if (_sdrd_creat_sendq(ctx)) {
        log_error(ctx->log, "Create queue failed!");
        return SDTP_ERR;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdrd_creat_recvtp
 **功    能: 创建接收线程池
 **输入参数:
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 创建线程池
 **     2. 创建接收对象
 **     3. 初始化接收对象
 **注意事项:
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
static int sdrd_creat_recvtp(sdrd_cntx_t *ctx)
{
    int idx;
    sdrd_rsvr_t *rsvr;
    sdrd_conf_t *conf = &ctx->conf;

    /* > 创建接收对象 */
    rsvr = (sdrd_rsvr_t *)calloc(conf->recv_thd_num, sizeof(sdrd_rsvr_t));
    if (NULL == rsvr) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    /* > 创建线程池 */
    ctx->recvtp = thread_pool_init(conf->recv_thd_num, NULL, (void *)rsvr);
    if (NULL == ctx->recvtp) {
        log_error(ctx->log, "Initialize thread pool failed!");
        free(rsvr);
        return SDTP_ERR;
    }

    /* > 初始化接收对象 */
    for (idx=0; idx<conf->recv_thd_num; ++idx) {
        if (sdrd_rsvr_init(ctx, rsvr+idx, idx)) {
            log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));

            free(rsvr);
            thread_pool_destroy(ctx->recvtp);
            ctx->recvtp = NULL;

            return SDTP_ERR;
        }
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdrd_recvtp_destroy
 **功    能: 销毁接收线程池
 **输入参数:
 **     ctx: 全局对象
 **     args: 附加参数
 **输出参数: NONE
 **返    回: VOID
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.01.01 #
 ******************************************************************************/
void sdrd_recvtp_destroy(void *_ctx, void *args)
{
    int idx;
    sdrd_cntx_t *ctx = (sdrd_cntx_t *)_ctx;
    sdrd_rsvr_t *rsvr = (sdrd_rsvr_t *)ctx->recvtp->data;

    for (idx=0; idx<ctx->conf.recv_thd_num; ++idx, ++rsvr) {
        /* > 关闭命令套接字 */
        CLOSE(rsvr->cmd_sck_id);

        /* > 关闭通信套接字 */
        sdrd_rsvr_del_all_conn_hdl(ctx, rsvr);
    }

    FREE(ctx->recvtp->data);
    thread_pool_destroy(ctx->recvtp);

    return;
}

/******************************************************************************
 **函数名称: sdrd_creat_worktp
 **功    能: 创建工作线程池
 **输入参数:
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 创建线程池
 **     2. 创建工作对象
 **     3. 初始化工作对象
 **注意事项:
 **作    者: # Qifeng.zou # 2015.01.06 #
 ******************************************************************************/
static int sdrd_creat_worktp(sdrd_cntx_t *ctx)
{
    int idx;
    sdtp_worker_t *wrk;
    sdrd_conf_t *conf = &ctx->conf;

    /* > 创建工作对象 */
    wrk = (void *)calloc(conf->work_thd_num, sizeof(sdtp_worker_t));
    if (NULL == wrk) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    /* > 创建线程池 */
    ctx->worktp = thread_pool_init(conf->work_thd_num, NULL, (void *)wrk);
    if (NULL == ctx->worktp) {
        log_error(ctx->log, "Initialize thread pool failed!");
        free(wrk);
        return SDTP_ERR;
    }

    /* > 初始化工作对象 */
    for (idx=0; idx<conf->work_thd_num; ++idx) {
        if (sdrd_worker_init(ctx, wrk+idx, idx)) {
            log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
            free(wrk);
            thread_pool_destroy(ctx->recvtp);
            ctx->recvtp = NULL;
            return SDTP_ERR;
        }
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdrd_worktp_destroy
 **功    能: 销毁工作线程池
 **输入参数:
 **     ctx: 全局对象
 **     args: 附加参数
 **输出参数: NONE
 **返    回: VOID
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.01.06 #
 ******************************************************************************/
void sdrd_worktp_destroy(void *_ctx, void *args)
{
    int idx;
    sdrd_cntx_t *ctx = (sdrd_cntx_t *)_ctx;
    sdrd_conf_t *conf = &ctx->conf;
    sdtp_worker_t *wrk = (sdtp_worker_t *)ctx->worktp->data;

    for (idx=0; idx<conf->work_thd_num; ++idx, ++wrk) {
        CLOSE(wrk->cmd_sck_id);
    }

    FREE(ctx->worktp->data);
    thread_pool_destroy(ctx->recvtp);

    return;
}

/******************************************************************************
 **函数名称: sdrd_proc_def_hdl
 **功    能: 默认消息处理函数
 **输入参数:
 **     type: 消息类型
 **     orig: 源设备ID
 **     buff: 消息内容
 **     len: 内容长度
 **     args: 附加参数
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.01.06 #
 ******************************************************************************/
static int sdrd_proc_def_hdl(int type, int orig, char *buff, size_t len, void *args)
{
    return SDTP_OK;
}
