/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: frwd_comm.c
 ** 版本号: 1.0
 ** 描  述: 通用函数定义
 ** 作  者: # Qifeng.zou # Wed 10 Jun 2015 12:14:26 PM CST #
 ******************************************************************************/

#include "frwd.h"
#include "mesg.h"

#define FRWD_DEF_CONF_PATH  "../conf/frwder.xml"

static int frwd_init_log(frwd_cntx_t *frwd, const char *pname);

/******************************************************************************
 **函数名称: frwd_init 
 **功    能: 初始化转发服务
 **输入参数: 
 **     conf: 配置信息
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.06.10 #
 ******************************************************************************/
frwd_cntx_t *frwd_init(const frwd_conf_t *conf)
{
    frwd_cntx_t *frwd;

    frwd = (frwd_cntx_t *)calloc(1, sizeof(frwd_cntx_t));
    if (NULL == frwd)
    {
        fprintf(stderr, "errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    do {
        /* > 初始化日志 */
        if (frwd_init_log(frwd, "frwder"))
        {
            fprintf(stderr, "Initialize log failed!\n");
            break;
        }

        /* > 连接发送队列 */
        frwd->send_to_listend = shm_queue_attach(conf->to_listend);
        if (NULL == frwd->send_to_listend)
        {
            log_fatal(frwd->log, "errmsg:[%d] %s!", errno, strerror(errno));
            break;
        }

        /* > 初始化发送服务 */
        frwd->rttp = rtsd_init(&conf->conn_invtd, frwd->log);
        if (NULL == frwd->rttp) 
        {
            log_fatal(frwd->log, "Initialize send-server failed!");
            break;
        }

        return frwd;
    } while (0);

    free(frwd);
    return NULL;
}

/******************************************************************************
 **函数名称: frwd_startup
 **功    能: 初始化转发服务
 **输入参数: 
 **     frwd: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.06.10 #
 ******************************************************************************/
int frwd_startup(frwd_cntx_t *frwd)
{
    if (rtsd_start(frwd->rttp))
    {
        log_fatal(frwd->log, "Start up send-server failed!");
        return FRWD_ERR;
    }

    return FRWD_OK;
}

/******************************************************************************
 **函数名称: frwd_getopt 
 **功    能: 解析输入参数
 **输入参数: 
 **     argc: 参数个数
 **     argv: 参数列表
 **输出参数:
 **     opt: 参数选项
 **返    回: 0:成功 !0:失败
 **实现描述: 解析和验证输入参数
 **注意事项: 
 **     c: 配置文件路径
 **     h: 帮助手册
 **     d: 以精灵进程运行
 **作    者: # Qifeng.zou # 2015.06.10 #
 ******************************************************************************/
int frwd_getopt(int argc, char **argv, frwd_opt_t *opt)
{
    int ch;

    memset(opt, 0, sizeof(frwd_opt_t));

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
                return FRWD_SHOW_HELP;
            }
        }
    }

    optarg = NULL;
    optind = 1;

    /* 2. 验证输入参数 */
    if (!strlen(opt->conf_path))
    {
        snprintf(opt->conf_path, sizeof(opt->conf_path), "%s", FRWD_DEF_CONF_PATH);
    }

    return FRWD_OK;
}

/******************************************************************************
 **函数名称: frwd_usage
 **功    能: 显示启动参数帮助信息
 **输入参数:
 **     exec: 程序名
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.06.10 #
 ******************************************************************************/
int frwd_usage(const char *exec)
{
    printf("\nUsage: %s [-h] [-d] -c <config file> [-l log_level]\n", exec);
    printf("\t-h\tShow help\n"
           "\t-c\tConfiguration path\n\n");
    return FRWD_OK;
}

/******************************************************************************
 **函数名称: frwd_init_log
 **功    能: 初始化日志模块
 **输入参数:
 **     frwd: 全局对象
 **     pname: 进程名
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015-06-10 #
 ******************************************************************************/
static int frwd_init_log(frwd_cntx_t *frwd, const char *pname)
{
    char path[FILE_PATH_MAX_LEN];
    frwd_conf_t *conf = &frwd->conf;

    snprintf(path, sizeof(path), "../log/%s.log", pname);

    frwd->log = log_init(conf->log_level, path);
    if (NULL == frwd->log)
    {
        fprintf(stderr, "errmsg:[%d] %s!", errno, strerror(errno));
        return FRWD_ERR;
    }

    return 0;
}

/******************************************************************************
 **函数名称: frwd_shmq_push 
 **功    能: 将数据转发到指定SHMQ队列
 **输入参数: 
 **     shmq: SHM队列
 **     type: 数据类型
 **     orig: 源设备ID
 **     data: 需要转发的数据
 **     len: 数据长度
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.06.10 #
 ******************************************************************************/
static int frwd_shmq_push(shm_queue_t *shmq, int type, int orig, char *data, size_t len)
{
    void *addr;
    size_t size;
    rttp_header_t *head;

    /* > 判断合法性 */
    size = sizeof(rttp_header_t) + shm_queue_size(shmq);
    if (size < len)
    {
        return FRWD_ERR;
    }

    /* > 申请队列空间 */
    addr = shm_queue_malloc(shmq);
    if (NULL == addr)
    {
        return FRWD_ERR;
    }

    /* > 设置应答数据 */
    head = (rttp_header_t *)addr;

    head->type = type;
    head->devid = orig;
    head->length = len;
    head->flag = RTTP_EXP_MESG;
    head->checksum = RTTP_CHECK_SUM;

    memcpy(head+1, data, len);

    if (shm_queue_push(shmq, addr))
    {
        shm_queue_dealloc(shmq, addr);
        return FRWD_ERR;
    }

    return 0;
}

/******************************************************************************
 **函数名称: frwd_search_rep_hdl
 **功    能: 搜索应答处理
 **输入参数:
 **     type: 数据类型
 **     orig: 源设备ID
 **     data: 需要转发的数据
 **     len: 数据长度
 **     args: 附加参数
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.06.10 #
 ******************************************************************************/
static int frwd_search_rep_hdl(int type, int orig, char *data, size_t len, void *args)
{
    frwd_cntx_t *ctx = (frwd_cntx_t *)args;

    log_trace(ctx->log, "Call %s()", __func__);

    return frwd_shmq_push(ctx->send_to_listend, type, orig, data, len);
}

/******************************************************************************
 **函数名称: frwd_set_reg
 **功    能: 注册处理回调
 **输入参数:
 **     frwd: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.06.10 #
 ******************************************************************************/
int frwd_set_reg(frwd_cntx_t *frwd)
{
    rtsd_register(frwd->rttp, MSG_SEARCH_REP, frwd_search_rep_hdl, frwd);
    return 0;
}