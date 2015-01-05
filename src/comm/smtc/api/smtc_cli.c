#include "smtc_cmd.h"
#include "smtc_cli.h"
#include "smtc_ssvr.h"
#include "smtc_comm.h"

#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

static int _smtc_cli_init(smtc_cli_t *cli, int idx);
static int smtc_cli_attach_sendq(smtc_cli_t *cli);
static int smtc_cli_creat_usck(smtc_cli_t *cli, int idx);

#define smtc_cli_usck_path(cli, path, idx) \
    snprintf(path, sizeof(path), "./temp/smtc/snd/%s/usck/%s_cli_%d.usck", \
        cli->conf.name, cli->conf.name, idx)


/******************************************************************************
 **函数名称: smpt_cli_init
 **功    能: 发送端初始化(对外接口)
 **输入参数: 
 **     conf: 配置信息
 **输出参数: NONE
 **返    回: 0:Success !0:Failed
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.04.09 #
 ******************************************************************************/
smtc_cli_t *smpt_cli_init(const smtc_conf_t *conf, int idx)
{
    smtc_cli_t *cli;

    /* 1. 创建上下文 */
    cli = (smtc_cli_t *)calloc(1, sizeof(smtc_cli_t));
    if (NULL == cli)
    {
        log_error(cli->log, "errmsg:[%d] %s", errno, strerror(errno));
        return NULL;
    }
    
    /* 2.  加载配置信息 */
    memcpy(&cli->conf, conf, sizeof(smtc_conf_t));

    /* 3.  根据配置进行初始化 */
    if (_smtc_cli_init(cli, idx))
    {
        log_error(cli->log, "Initialize send module failed!");
        Free(cli);
        return NULL;
    }

    return cli;
}

/******************************************************************************
 **函数名称: _smtc_cli_init
 **功    能: 发送端初始化
 **输入参数: 
 **     path: 配置文件路径
 **输出参数: NONE
 **返    回: 0:Success !0:Failed
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.04.09 #
 ******************************************************************************/
static int _smtc_cli_init(smtc_cli_t *cli, int idx)
{
    int ret = 0;
    
    /* 1. 连接Queue队列 */
    ret = smtc_cli_attach_sendq(cli);
    if (0 != ret)
    {
        log_error(cli->log, "Attach queue failed!");
        return -1;
    }

    /* 2. 创建通信套接字 */
    ret = smtc_cli_creat_usck(cli, idx);
    if (0 != ret)
    {
        log_error(cli->log, "Create usck failed!");
        return -1;
    }

    cli->snd_num = (unsigned int *)calloc(cli->conf.snd_thd_num, sizeof(unsigned int));
    if (NULL == cli->snd_num)
    {
        log_error(cli->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return -1;
    }

    return 0;
}

/******************************************************************************
 **函数名称: smtc_cli_attach_sendq
 **功    能: Attach发送队列
 **输入参数: 
 **     cli: 上下文信息
 **输出参数: NONE
 **返    回: 0:Success !0:Failed
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.04.26 #
 ******************************************************************************/
static int smtc_cli_attach_sendq(smtc_cli_t *cli)
{
    int idx;
    smtc_queue_conf_t *qcf;
    smtc_conf_t *conf = &cli->conf;
    char qname[FILE_NAME_MAX_LEN];

    /* Send队列数组 */
    cli->sq = (shm_queue_t **)calloc(conf->snd_thd_num, sizeof(shm_queue_t *));
    if (NULL == cli->sq)
    {
        log_error(cli->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return -1;
    }

    qcf = &conf->send_qcf;
    for (idx=0; idx<conf->snd_thd_num; ++idx)
    {
        snprintf(qname, sizeof(qname), "%s-%d", qcf->name, idx);

        cli->sq[idx] = shm_queue_attach(qcf->size, qcf->count, 0, qname);
        if (NULL == cli->sq[idx])
        {
            log_error(cli->log, "Attach queue failed! [%s]", qname);
            return -1;
        }
    }

    return 0;
}

/******************************************************************************
 **函数名称: smtc_cli_creat_usck
 **功    能: 创建Unix-SCK命令套接字
 **输入参数: 
 **     cli: 上下文信息
 **输出参数: NONE
 **返    回: 0:Success !0:Failed
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.04.26 #
 ******************************************************************************/
static int smtc_cli_creat_usck(smtc_cli_t *cli, int idx)
{
    char path[FILE_NAME_MAX_LEN];

    smtc_cli_usck_path(cli, path, idx);

    cli->cmdfd = usck_udp_creat(path);
    if (cli->cmdfd < 0)
    {
        log_error(cli->log, "Create usck failed! errmsg:[%d] %s! path:[%s]",
                errno, strerror(errno), path);
        return -1;                    
    }

    return 0;
}

/******************************************************************************
 **函数名称: smtc_cli_notify_svr
 **功    能: 通知Send服务线程
 **输入参数: 
 **     cli: 上下文信息
 **输出参数: NONE
 **返    回: 0:Success !0:Failed
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.04.26 #
 ******************************************************************************/
static int smtc_cli_notify_svr(smtc_cli_t *cli, int idx)
{
    int ret = 0;
    smtc_cmd_t cmd;
    smtc_conf_t *conf = &cli->conf;
    char path[FILE_NAME_MAX_LEN];

    memset(&cmd, 0, sizeof(cmd));

    cmd.type = SMTC_CMD_SEND_ALL;
    smtc_ssvr_usck_path(conf, path, idx);

    ret = usck_udp_send(cli->cmdfd, path, &cmd, sizeof(cmd));
    if (ret < 0)
    {
        log_debug(cli->log, "errmsg:[%d] %s! cmdfd:%d path:%s",
                errno, strerror(errno), cli->cmdfd, path);
        return -1;
    }

    return 0;
}

/******************************************************************************
 **函数名称: smtc_cli_send
 **功    能: 发送数据至服务端(对外接口)
 **输入参数: 
 **     cli: 上下文信息
 **     data: 需要发送的数据
 **     type: 发送数据的类型
 **     size: 所发送数据的长度
 **输出参数: NONE
 **返    回: 0:Success !0:Failed
 **实现描述: 
 **     将数据按照约定格式放入队列中
 **注意事项: 
 **     1. 只能用于发送自定义数据类型, 而不能用于系统数据类型
 **     2. 不过关注变量num, is_notify在多线程中的值，因其不影响安全性
 **作    者: # Qifeng.zou # 2014.03.26 #
 ******************************************************************************/
int smtc_cli_send(smtc_cli_t *cli, const void *data, int type, size_t size)
{
    static uint32_t num = 0;
    unsigned int is_notify, idx;
    void *addr;
    smtc_header_t *header;
    smtc_conf_t *conf = &cli->conf;

    idx = (num++)%conf->snd_thd_num;

    /* 1. 判断存储空间是否充足 */
    if (size + sizeof(smtc_header_t) > cli->sq[idx]->size)
    {
        log_error(cli->log, "Send size is too large!");
        return SMTC_ERR_QSIZE;
    }
    else if (type >= SMTC_TYPE_MAX)
    {
        log_error(cli->log, "Data type is invalid! type:%d", type);
        return SMTC_ERR_DATA_TYPE;
    }

    /* 2. 向申请队列空间 */
    addr = shm_queue_alloc(cli->sq[idx], &addr);
    if (NULL == addr)
    {
        log_error(cli->log, "Queue space is not enough!");
        smtc_cli_notify_svr(cli, idx);
        return SMTC_ERR_QALLOC;
    }

    /* 3. 将数据放入队列空间 */
    header = (smtc_header_t *)addr;
    header->type = type;
    header->body_len = size;
    header->flag = SMTC_EXP_DATA; /* 外部数据 */
    header->mark = SMTC_MSG_MARK_KEY;

    memcpy(addr+sizeof(smtc_header_t), data, size);

    /* 4. 压入队列 */
    if (shm_queue_push(cli->sq[idx], addr))
    {
        log_error(cli->log, "Push id into queue failed!");
        
        shm_queue_dealloc(cli->sq[idx], addr);
        return SMTC_ERR;
    }

    is_notify = (cli->snd_num[idx]++) % SMTC_SND_REQ_INTV_NUM;

    /* 5. 通知Send线程 */
    if (!is_notify)
    {
        smtc_cli_notify_svr(cli, idx);
    }
    
    return 0;
}