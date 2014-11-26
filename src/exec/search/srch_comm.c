/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: search.c
 ** 版本号: 1.0
 ** 描  述: 搜索引擎
 **         负责接受搜索请求，并将搜索结果返回给客户端
 ** 作  者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/

#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "log.h"
#include "lock.h"
#include "hash.h"
#include "common.h"
#include "search.h"
#include "syscall.h"
#include "xd_socket.h"
#include "srch_worker.h"
#include "srch_listen.h"
#include "srch_recver.h"

#define SRCH_PROC_LOCK_PATH "../temp/srch/srch.lck"

/******************************************************************************
 **函数名称: srch_getopt 
 **功    能: 解析输入参数
 **输入参数: 
 **     argc: 参数个数
 **     argv: 参数列表
 **输出参数:
 **     opt: 参数选项
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 解析输入参数
 **     2. 验证输入参数
 **注意事项: 
 **     c: 配置文件路径
 **     h: 帮助手册
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
int srch_getopt(int argc, char **argv, srch_opt_t *opt)
{
    int ch;

    /* 1. 解析输入参数 */
    while (-1 != (ch = getopt(argc, argv, "c:hd")))
    {
        switch (ch)
        {
            case 'c':   /* 指定配置文件 */
            {
                snprintf(opt->conf_path, sizeof(opt->conf_path), "%s", optarg);
                break;
            }
            case 'd':
            {
                opt->isdaemon = true;
                break;
            }
            case 'h':   /* 显示帮助信息 */
            default:
            {
                return SRCH_SHOW_HELP;
            }
        }
    }

    optarg = NULL;
    optind = 1;

    /* 2. 验证输入参数 */
    if (!strlen(opt->conf_path))
    {
        snprintf(opt->conf_path, sizeof(opt->conf_path), "%s", SRCH_DEF_CONF_PATH);
    }

    return SRCH_OK;
}

/******************************************************************************
 **函数名称: srch_usage
 **功    能: 显示启动参数帮助信息
 **输入参数:
 **     name: 程序名
 **输出参数: NULL
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
int srch_usage(const char *exec)
{
    printf("\nUsage: %s [-h] [-d] -c <config file> [-l log_level]\n", exec);
    printf("\t-h\tShow help\n"
           "\t-c\tConfiguration path\n\n");
    return SRCH_OK;
}

/******************************************************************************
 **函数名称: srch_init_log
 **功    能: 初始化日志模块
 **输入参数:
 **     fname: 日志文件名
 **输出参数: NONE
 **返    回: 获取域名对应的地址信息
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
log_cycle_t *srch_init_log(char *fname)
{
    log_cycle_t *log;
    char path[FILE_NAME_MAX_LEN];

    /* 1. 初始化系统日志 */
    log2_get_path(path, sizeof(path), basename(fname));

    if (log2_init(LOG_LEVEL_ERROR, path))
    {
        fprintf(stderr, "Init log2 failed!");
        return NULL;
    }

    /* 2. 初始化业务日志 */
    log_get_path(path, sizeof(path), basename(fname));

    log = log_init(LOG_LEVEL_ERROR, path);
    if (NULL == log)
    {
        log2_error("Initialize log failed!");
        log2_destroy();
        return NULL;
    }

    return log;
}

/******************************************************************************
 **函数名称: srch_cntx_init
 **功    能: 初始化全局信息
 **输入参数: 
 **     pname: 进程名
 **     conf_path: 配置文件路径
 **输出参数: NONE
 **返    回: 全局对象
 **实现描述: 
 **     1. 初始化日志模块
 **     2. 判断程序是否已运行
 **     3. 创建全局对象 
 **     4. 加载配置文件
 **     5. 创建Worker线程池
 **     6. 创建Recver线程池
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
srch_cntx_t *srch_cntx_init(char *pname, const char *conf_path)
{
    log_cycle_t *log;
    srch_cntx_t *ctx;

    /* 1. 初始化日志模块 */
    log = srch_init_log(pname);
    if (NULL == log)
    {
        fprintf(stderr, "Initialize log failed!");
        return NULL;
    }

    /* 2. 判断程序是否已运行 */
    if (0 != srch_proc_lock())
    {
        log_error(log, "Crawler is running!");
        return NULL;
    }

    /* 3. 创建全局对象 */
    ctx = (srch_cntx_t *)calloc(1, sizeof(srch_cntx_t));
    if (NULL == ctx)
    {
        log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    /* 4. 加载配置文件 */
    ctx->conf = srch_conf_load(conf_path, log);
    if (NULL == ctx->conf)
    {
        free(ctx);
        log_error(log, "Load configuration failed! path:%s", conf_path);
        return NULL;
    }

    ctx->log = log;
    log_set_level(log, ctx->conf->log.level);
    log2_set_level(ctx->conf->log.level2);

    if (limit_file_num(ctx->conf->connections.max)) /* 设置进程打开文件的最大数目 */
    {
        free(ctx);
        log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    /* 5. 创建Worker线程池 */
    if (srch_init_workers(ctx))
    {
        srch_conf_destroy(ctx->conf);
        free(ctx);
        log_error(log, "Initialize worker thread pool failed!");
        return NULL;
    }

    /* 6. 创建Recver线程池 */
    if (srch_init_recvers(ctx))
    {
        srch_conf_destroy(ctx->conf);
        free(ctx);
        log_error(log, "Initialize recver thread pool failed!");
        return NULL;
    }

    return ctx;
}

/******************************************************************************
 **函数名称: srch_cntx_destroy
 **功    能: 销毁搜索引擎上下文
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: VOID
 **实现描述: 
 **     依次销毁侦听线程、接收线程、工作线程、日志对象等
 **注意事项: 按序销毁
 **作    者: # Qifeng.zou # 2014.11.17 #
 ******************************************************************************/
void srch_cntx_destroy(srch_cntx_t *ctx)
{
    pthread_cancel(ctx->lsn_tid);
    srch_workers_destroy(ctx);
    srch_recvers_destroy(ctx);

    log_destroy(&ctx->log);
    log2_destroy();
}

/******************************************************************************
 **函数名称: srch_startup
 **功    能: 启动搜索引擎服务
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     设置线程回调
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
int srch_startup(srch_cntx_t *ctx)
{
    int idx;
    const srch_conf_t *conf = ctx->conf;


    /* 1. 设置Worker线程回调 */
    for (idx=0; idx<conf->worker_num; ++idx)
    {
        thread_pool_add_worker(ctx->workers, srch_worker_routine, ctx);
    }

    /* 2. 设置Recver线程回调 */
    for (idx=0; idx<conf->recver_num; ++idx)
    {
        thread_pool_add_worker(ctx->recvers, srch_recver_routine, ctx);
    }
    
    /* 3. 设置Listen线程回调 */
    if (thread_creat(&ctx->lsn_tid, srch_listen_routine, ctx))
    {
        log_error(ctx->log, "Create listen thread failed!");
        return SRCH_ERR;
    }

    return SRCH_OK;
}

/******************************************************************************
 **函数名称: srch_init_workers
 **功    能: 初始化Worker线程池
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
int srch_init_workers(srch_cntx_t *ctx)
{
    int idx, num;
    srch_worker_t *worker;
    const srch_conf_t *conf = ctx->conf;

    /* 1. 创建Worker线程池 */
    ctx->workers = thread_pool_init(conf->worker_num, 0);
    if (NULL == ctx->workers)
    {
        log_error(ctx->log, "Initialize thread pool failed!");
        return SRCH_ERR;
    }

    /* 2. 新建Worker对象 */
    ctx->workers->data = (srch_worker_t *)calloc(conf->worker_num, sizeof(srch_worker_t));
    if (NULL == ctx->workers->data)
    {
        thread_pool_destroy(ctx->workers);
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SRCH_ERR;
    }

    /* 3. 依次初始化Worker对象 */
    for (idx=0; idx<conf->worker_num; ++idx)
    {
        worker = (srch_worker_t *)ctx->workers->data + idx;

        worker->tidx = idx;

        if (srch_worker_init(ctx, worker))
        {
            log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
            break;
        }
    }

    if (idx == conf->worker_num)
    {
        return SRCH_OK; /* 成功 */
    }

    /* 4. 释放Worker对象 */
    num = idx;
    for (idx=0; idx<num; ++idx)
    {
        worker = (srch_worker_t *)ctx->workers->data + idx;

        srch_worker_destroy(worker);
    }

    free(ctx->workers->data);
    thread_pool_destroy(ctx->workers);

    return SRCH_ERR;
}

/******************************************************************************
 **函数名称: srch_workers_destroy
 **功    能: 销毁爬虫线程池
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.14 #
 ******************************************************************************/
int srch_workers_destroy(srch_cntx_t *ctx)
{
    int idx;
    void *data;
    srch_worker_t *worker;
    const srch_conf_t *conf = ctx->conf;

    /* 1. 释放Worker对象 */
    for (idx=0; idx<conf->worker_num; ++idx)
    {
        worker = (srch_worker_t *)ctx->workers->data + idx;

        srch_worker_destroy(worker);
    }

    /* 2. 释放线程池对象 */
    data = ctx->workers->data;

    thread_pool_destroy(ctx->workers);

    free(data);

    ctx->workers = NULL;

    return SRCH_ERR;
}

/******************************************************************************
 **函数名称: srch_init_recvers
 **功    能: 初始化Recver线程池
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
int srch_init_recvers(srch_cntx_t *ctx)
{
    int idx, num;
    srch_recver_t *recver;
    const srch_conf_t *conf = ctx->conf;

    /* 1. 创建Worker线程池 */
    ctx->recvers = thread_pool_init(conf->recver_num, 0);
    if (NULL == ctx->recvers)
    {
        log_error(ctx->log, "Initialize thread pool failed!");
        return SRCH_ERR;
    }

    /* 2. 新建Recver对象 */
    ctx->recvers->data = (srch_recver_t *)calloc(conf->recver_num, sizeof(srch_recver_t));
    if (NULL == ctx->recvers->data)
    {
        thread_pool_destroy(ctx->recvers);
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SRCH_ERR;
    }

    /* 3. 依次初始化Recver对象 */
    for (idx=0; idx<conf->recver_num; ++idx)
    {
        recver = (srch_recver_t *)ctx->recvers->data + idx;

        recver->tidx = idx;

        if (srch_recver_init(ctx, recver))
        {
            log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
            break;
        }
    }

    if (idx == conf->recver_num)
    {
        return SRCH_OK; /* 成功 */
    }

    /* 4. 释放Recver对象 */
    num = idx;
    for (idx=0; idx<num; ++idx)
    {
        recver = (srch_recver_t *)ctx->recvers->data + idx;

        srch_recver_destroy(recver);
    }

    free(ctx->recvers->data);
    thread_pool_destroy(ctx->recvers);

    return SRCH_ERR;
}

/******************************************************************************
 **函数名称: srch_recvers_destroy
 **功    能: 销毁Recver线程池
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
int srch_recvers_destroy(srch_cntx_t *ctx)
{
    int idx;
    void *data;
    srch_recver_t *recver;
    const srch_conf_t *conf = ctx->conf;

    /* 1. 释放Recver对象 */
    for (idx=0; idx<conf->recver_num; ++idx)
    {
        recver = (srch_recver_t *)ctx->recvers->data + idx;

        srch_recver_destroy(recver);
    }

    /* 2. 释放线程池对象 */
    data = ctx->recvers->data;

    thread_pool_destroy(ctx->recvers);

    free(data);

    ctx->recvers = NULL;

    return SRCH_ERR;
}

/******************************************************************************
 **函数名称: srch_proc_lock
 **功    能: 搜索引擎进程锁(防止同时启动两个服务进程)
 **输入参数: NONE
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
int srch_proc_lock(void)
{
    int fd;
    char path[FILE_PATH_MAX_LEN];

    /* 1. 获取路径 */
    snprintf(path, sizeof(path), "%s", SRCH_PROC_LOCK_PATH);

    Mkdir2(path, DIR_MODE);

    /* 2. 打开文件 */
    fd = Open(path, OPEN_FLAGS, OPEN_MODE);
    if(fd < 0)
    {
        return -1;
    }

    /* 3. 尝试加锁 */
    if(proc_try_wrlock(fd) < 0)
    {
        Close(fd);
        return -1;
    }

    return 0;
}
