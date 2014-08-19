#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "xml_tree.h"
#include "xml_comm.h"
#include "dts_comm.h"


#define XmlIsULineChar(ch)      ('_' == ch)     /* 下划线 */
#define XmlIsTableChar(ch)      ('\t' == ch)    /* 制表符 */
#define XmlIsStrEndChar(ch)     ('\0' == ch)    /* 结束符 */
#define XmlIsEqualChar(ch)      ('=' == ch)     /* 等号符 */
#define XmlIsDQuotChar(ch)      ('"' == ch)     /* 双引号 */
#define XmlIsSQuotChar(ch)      ('\'' == ch)    /* 单引号 */
#define XmlIsLPBrackChar(ch)    ('<' == ch)     /* 左尖括号 */
#define XmlIsRPBrackChar(ch)    ('>' == ch)     /* 右尖括号 */
#define XmlIsRDLineChar(ch)     ('/' == ch)     /* 右斜线 */
#define XmlIsDoubtChar(ch)      ('?' == ch)     /* 疑问号 */
#define XmlIsAndChar(ch)        ('&' == ch)     /* 和号 */
#define XmlIsSubChar(ch)        ('-' == ch)     /* 减号 */
#define XmlIsColonChar(ch)      (':' == ch)     /* 冒号 */
#define XmlIsNLineChar(ch)      (('\n'==ch) || ('\r'==ch))  /* 换行符 */

/* 转义关键字 */
#define XmlIsLtStr(str) (0 == strncmp(str, XML_FRWD_LT_STR, XML_FRWD_LT_LEN))       /* 小于 */
#define XmlIsGtStr(str) (0 == strncmp(str, XML_FRWD_GT_STR, XML_FRWD_GT_LEN))       /* 大于 */
#define XmlIsAmpStr(str) (0 == strncmp(str, XML_FRWD_AMP_STR, XML_FRWD_AMP_LEN))    /* 与号 */
#define XmlIsAposStr(str) (0 == strncmp(str, XML_FRWD_APOS_STR, XML_FRWD_APOS_LEN)) /* 单引号 */
#define XmlIsQuotStr(str) (0 == strncmp(str, XML_FRWD_QUOT_STR, XML_FRWD_QUOT_LEN)) /* 双引号 */

#define XmlIsMarkChar(ch) (isalpha(ch) || isdigit(ch)   /* 标签名的合法字符 */ \
                || XmlIsULineChar(ch))  || XmlIsSubChar(ch) || XmlIsColonChar(ch)
#define XmlIsMarkBorder(ch) (isspace(ch) || XmlIsRDLineChar(ch) /* 标签名的合法边界 */ \
                || XmlIsRPBrackChar(ch) || XmlIsTableChar(ch) || XmlIsNLineChar(ch))
#define XmlIsIgnoreChar(ch) (isspace(ch) || XmlIsTableChar(ch) || XmlIsNLineChar(ch)) /* 无意义字符 */


static int xml_parse_version(xml_fparse_t *fparse);
static int xml_parse_note(xml_fparse_t *fparse);
static int xml_parse_mark(xml_tree_t *xml, Stack_t *stack, xml_fparse_t *fparse);
static int xml_parse_end(Stack_t *stack, xml_fparse_t *fparse);

static int xml_mark_get_name(xml_tree_t *xml, Stack_t *stack, xml_fparse_t *fparse);
static int xml_mark_has_attr(xml_fparse_t *fparse);
static int xml_mark_get_attr(Stack_t *stack, xml_fparse_t *fparse);
static int xml_mark_is_end(xml_fparse_t *fparse);
static int xml_mark_has_value(xml_fparse_t *fparse);
static int xml_mark_get_value(Stack_t *stack, xml_fparse_t *fparse);

#if defined(__XML_FRWD_PARSE__)
static const xml_frwd_t *xml_frwd_get(const char *str);
static int xml_frwd_free(xml_frwd_split_t *split);
static int xml_frwd_size(const xml_frwd_split_t *split);
static int xml_frwd_merge(const xml_frwd_split_t *sp, char *dst);
static int xml_frwd_split(const xml_frwd_t *frwd, const char *str, int len, xml_frwd_split_t *split);

/* 转义字串对应的长度: 必须与xml_frwd_e的顺序一致 */
static const xml_frwd_t g_xml_frwd_str[] =
{
    {XML_FRWD_LT,     XML_FRWD_LT_STR,    '<',  XML_FRWD_LT_LEN}   /* &lt; */
    , {XML_FRWD_GT,   XML_FRWD_GT_STR,    '>',  XML_FRWD_GT_LEN}   /* &gt; */
    , {XML_FRWD_AMP,  XML_FRWD_AMP_STR,   '&',  XML_FRWD_AMP_LEN}  /* &amp; */
    , {XML_FRWD_APOS, XML_FRWD_APOS_STR,  '\'', XML_FRWD_APOS_LEN} /* &apos; */
    , {XML_FRWD_QUOT, XML_FRWD_QUOT_STR,  '"',  XML_FRWD_QUOT_LEN} /* &quot; */

    /* 未知类型: 只有&开头才判断是否为转义字符。未知，则是首字母& */
    , {XML_FRWD_UNKNOWN, XML_FRWD_UNKNOWN_STR, '&',  XML_FRWD_UNKNOWN_LEN}
};
#endif /*__XML_FRWD_PARSE__*/

/******************************************************************************
 **函数名称: xml_node_creat
 **功    能: 创建XML节点
 **输入参数: 
 **     type: 节点类型(xml_node_type_e)
 **输出参数: NONE
 **返    回: 节点地址
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.18 #
 ******************************************************************************/
xml_node_t *xml_node_creat(xml_node_type_e type)
{
    xml_node_t *node = NULL;

    node = (xml_node_t*)calloc(1, sizeof(xml_node_t));
    if (NULL == node)
    {
        LogError("Calloc failed!");
        return NULL;
    }

    node->name = NULL;
    node->value = NULL;
    node->type = type;

    node->next = NULL;
    node->firstchild = NULL;
    node->parent = NULL;
    xml_reset_flag(node);
    node->temp = NULL;
    node->tail = NULL;

    return node;
}

/******************************************************************************
 **函数名称: xml_node_creat_ext
 **功    能: 创建XML节点
 **输入参数: 
 **     type: 节点类型(xml_node_type_e)
 **     name: 节点名
 **     vlaue: 节点值
 **输出参数: NONE
 **返    回: 节点地址
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.06.11 #
 ******************************************************************************/
xml_node_t *xml_node_creat_ext(xml_node_type_e type, const char *name, const char *value)
{
    int size=0, ret=0;
    xml_node_t *node = NULL;

    /* 1. 创建节点 */
    node = (xml_node_t*)calloc(1, sizeof(xml_node_t));
    if (NULL == node)
    {
        LogError("Calloc failed!");
        return NULL;
    }

    node->type = type;

    /* 2. 设置节点名 */
    size = strlen(name) + 1;
    node->name = (char *)calloc(1, size);
    if (NULL == node->name)
    {
        xml_node_sfree(node);
        return NULL;
    }
    snprintf(node->name, size, "%s", name);

    /* 3. 设置节点值 */
    ret = xml_set_value(node, value);
    if (0 != ret)
    {
        xml_node_sfree(node);
        return NULL;
    }
    
    node->next = NULL;
    node->firstchild = NULL;
    node->parent = NULL;
    node->temp = NULL;
    node->tail = NULL;

    return node;
}

/******************************************************************************
 **函数名称: xml_init
 **功    能: 初始化XML树
 **输入参数:
 **      xml: XML树
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
int xml_init(xml_tree_t **xml)
{
    *xml = (xml_tree_t*)calloc(1, sizeof(xml_tree_t));
    if (NULL == *xml)
    {
        LogError("Calloc failed!");
        return XML_ERR_CALLOC;
    }

    (*xml)->root = (xml_node_t*)calloc(1, sizeof(xml_node_t));
    if (NULL == (*xml)->root)
    {
        free(*xml);
        *xml = NULL;
        LogError("Calloc failed!");
        return XML_ERR_CALLOC;
    }

    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_fload
 **功    能: 将XML文件载入文件缓存
 **输入参数:
 **      fname: 文件路径名
 **      length: 获取文件长度
 **输出参数:
 **返    回: 文件缓存
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
char *xml_fload(const char *fname)
{
    int ret = 0, left = 0, num = 0, offset = 0;
    FILE *fp = NULL;
    char *buff = NULL;
    struct stat fstate;

    memset(&fstate, 0, sizeof(fstate));

    /* 判断文件状态是否正常 */
    ret = stat(fname, &fstate);
    if (XML_SUCCESS != ret)
    {
        LogError("XML file is not exist! [%s]", fname);
        return NULL;
    }

    /* 分配文件缓存空间 */
    buff = (char *)calloc(1, (fstate.st_size+1)*sizeof(char));
    if (NULL == buff)
    {
        LogError("Calloc failed!");
        return NULL;
    }

    /* 将文件载入缓存 */
    fp = fopen(fname, "r");
    if (NULL == fp)
    {
        free(buff), buff=NULL;
        LogError("Open file failed![%s]", fname);
        return NULL;
    }

    offset = 0;
    left = fstate.st_size;
    while (!feof(fp) && (left > 0))
    {
        num = fread(buff + offset, 1, left, fp);
        if (ferror(fp))
        {
            fclose(fp), fp=NULL;
            free(buff), buff=NULL;
            LogError("Read file failed!");
            return NULL;
        }
        
        left -= num;
        offset += num;
    }

    fclose(fp), fp=NULL;
    return buff;
}

/******************************************************************************
 **函数名称: xml_parse
 **功    能: 解析XML文件缓存
 **输入参数:
 **      xml: XML树
 **      stack: XML栈
 **      str: XML字串
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **      1. 解析版本信息
 **      2. 解析XML BODY
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
int xml_parse(xml_tree_t *xml, Stack_t *stack, const char *str)
{
    int ret = 0;
    xml_fparse_t fparse;

    fparse.str = str;
    fparse.ptr = str;
    fparse.length = -1; /* 未知 */

    while (!XmlIsStrEndChar(*(fparse.ptr)))
    {
        while (XmlIsIgnoreChar(*(fparse.ptr))) fparse.ptr++;    /* 跳过无意义的字符 */

        switch(*(fparse.ptr))
        {
            case XML_BEGIN_FLAG:
            {
                switch(*(fparse.ptr+1))
                {
                    case XML_VERS_FLAG:  /* "<?" 版本开始 */ 
                    {
                        /* 版本信息不用加载到XML树中 */
                        ret = xml_parse_version(&fparse);
                        if (XML_SUCCESS != ret)
                        {
                            LogError("XML format is wrong![%-.32s] [%ld]",
                                fparse.ptr, fparse.ptr-fparse.str);
                            return XML_ERR_FORMAT;
                        }
                        break;
                    }
                    case XML_NOTE_FLAG:   /* "<!--" 注释信息 */
                    {
                        /* 注释信息不用加载到XML树中 */
                        ret = xml_parse_note(&fparse);
                        if (XML_SUCCESS != ret)
                        {
                            LogError("XML format is wrong![%-.32s]", fparse.ptr);
                            return XML_ERR_FORMAT;
                        }
                        break;
                    }
                    case XML_END_FLAG:   /* "</" 节点结束 */
                    {
                        ret = xml_parse_end(stack, &fparse);
                        if (XML_SUCCESS != ret)
                        {
                            LogError("XML format is wrong![%-.32s] [%ld]",
                                fparse.ptr, fparse.ptr-fparse.str);
                            return XML_ERR_FORMAT;
                        }
                        break;
                    }
                    default:    /* "<XYZ" 节点开始 */
                    {
                        ret = xml_parse_mark(xml, stack, &fparse);
                        if (XML_SUCCESS != ret)
                        {
                            LogError("Parse XML failed! [%-.32s] [%ld]",
                                fparse.ptr, fparse.ptr-fparse.str);
                            return XML_ERR_FORMAT;
                        }
                        break;
                    }
                }
                break;
            }
            case STR_END_FLAG:  /* 字串结束'\0' */
            {
                if (stack_isempty(stack))
                {
                    LogDebug("Parse xml success!");
                    return XML_SUCCESS;
                }
                LogError("Invalid format! [%-.32s] [%ld]", fparse.ptr, fparse.ptr-fparse.str);
                return XML_ERR_FORMAT;
            }
            default:            /* 非法字符 */
            {
                LogError("Invalid format! [%-.32s] [%ld]", fparse.ptr, fparse.ptr-fparse.str);
                return XML_ERR_FORMAT;
            }
        }
    }

    if (!stack_isempty(stack))
    {
        LogError("Invalid format! [%-.32s]", fparse.ptr);
        return XML_ERR_FORMAT;
    }
    
    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_parse_version
 **功    能: 解析XML文件缓存版本信息
 **输入参数:
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
static int xml_parse_version(xml_fparse_t *fparse)
{
    int ret = 0;
    char border = '"';
    const char *ptr = NULL;

    /* 匹配版本开头"<?xml " */
    ret = strncmp(fparse->ptr, XML_VERS_BEGIN, XML_VERS_BEGIN_LEN);
    if (0 != ret)
    {
        LogError("XML format is wrong![%-.32s]", fparse->ptr);
        return XML_ERR_FORMAT;
    }

    fparse->ptr += XML_VERS_BEGIN_LEN; /* 跳过版本开头"<?xml " */

    /* 检查格式是否正确 */
    /* 跳过无意义字符 */
    while (XmlIsIgnoreChar(*fparse->ptr)) fparse->ptr++;
    while (!XmlIsDoubtChar(*fparse->ptr) && !XmlIsStrEndChar(*fparse->ptr))
    {
        ptr = fparse->ptr;

        /* 属性名是否正确 */
        while (XmlIsMarkChar(*ptr)) ptr++;
        if (ptr == fparse->ptr)
        {
            LogError("XML format is wrong![%-.32s]", fparse->ptr);
            return XML_ERR_FORMAT;
        }
        
        if (!XmlIsEqualChar(*ptr))
        {
            LogError("XML format is wrong![%-.32s]", fparse->ptr);
            return XML_ERR_FORMAT;
        }
        ptr++;
        fparse->ptr = ptr;
        
        /* 属性值是否正确 */
        while (XmlIsIgnoreChar(*ptr)) ptr++; /* 跳过=之后的无意义字符 */

        /* 判断是双引号(")还是单引号(')为版本属性值的边界 */
        if (XmlIsDQuotChar(*ptr) || XmlIsSQuotChar(*ptr))
        {
            border = *ptr;
        }
        else                                /* 不为双/单引号，则格式错误 */
        {
            LogError("XML format is wrong![%-.32s]", fparse->ptr);
            return XML_ERR_FORMAT;
        }
        ptr++;
        fparse->ptr = ptr;
        
        while ((*ptr != border) && !XmlIsStrEndChar(*ptr))
        {
            ptr++;
        }

        if (*ptr != border)
        {
            LogError("XML format is wrong![%-.32s]", fparse->ptr);
            return XML_ERR_FORMAT;
        }
        ptr++;  /* 跳过双/单引号 */
        
		/* 跳过无意义字符 */
        while (XmlIsIgnoreChar(*ptr)) ptr++;
        fparse->ptr = ptr;
    }

    /* 版本信息以"?>"结束 */
    if (!XmlIsDoubtChar(*fparse->ptr))
    {
        LogError("XML format is wrong![%-.32s]", fparse->ptr);
        return XML_ERR_FORMAT;
    }
    fparse->ptr++;  /* 跳过? */
    
    if (!XmlIsRPBrackChar(*fparse->ptr))
    {
        LogError("XML format is wrong![%-.32s]", fparse->ptr);
        return XML_ERR_FORMAT;
    }
    
    fparse->ptr++;
    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_parse_note
 **功    能: 解析XML文件缓存注释信息
 **输入参数:
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
static int xml_parse_note(xml_fparse_t *fparse)
{
    int ret = 0;
    const char *ptr = NULL;

	/* 匹配注释开头"<!--" */
    ret = strncmp(fparse->ptr, XML_NOTE_BEGIN, XML_NOTE_BEGIN_LEN);
    if (0 != ret)
    {
        LogError("XML format is wrong![%-.32s]", fparse->ptr);
        return XML_ERR_FORMAT;
    }

    fparse->ptr += XML_NOTE_BEGIN_LEN; /* 跳过注释开头"<!--" */
    
    /* 因在注释信息的节点中不允许出现"-->"，所以可使用如下匹配查找结束 */
    ptr = strstr(fparse->ptr, XML_NOTE_END1);
    if ((NULL == ptr) || (XML_NOTE_END2 != *(ptr + XML_NOTE_END1_LEN)))
    {
        LogError("XML format is wrong![%-.32s]", fparse->ptr);
        return XML_ERR_FORMAT;
    }

    fparse->ptr = ptr;
    fparse->ptr += XML_NOTE_END_LEN;
    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_mark_end
 **功    能: 标签以/>为标志结束
 **输入参数:
 **      stack: 栈
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.23 #
 ******************************************************************************/
#define xml_mark_end(stack, fparse) (fparse->ptr+=XML_MARK_END2_LEN, stack_pop(stack))

/******************************************************************************
 **函数名称: xml_parse_mark
 **功    能: 处理标签节点
 **输入参数:
 **      xml: XML树
 **      stack: XML栈
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **      1. 提取标签名
 **      2. 提取标签各属性
 **      3. 提取标签值
 **注意事项: 
 **      注意: 以上3个步骤是固定的，否则将会出现混乱
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
static int xml_parse_mark(xml_tree_t *xml, Stack_t *stack, xml_fparse_t *fparse)
{
    int ret = 0;

    fparse->ptr += XML_MARK_BEGIN_LEN;    /* 跳过"<" */

    /* 1. 提取标签名，并入栈 */
    ret = xml_mark_get_name(xml, stack, fparse);
    if (XML_SUCCESS != ret)
    {
        LogError("Get mark name failed!");
        return ret;
    }
    
    /* 2. 提取标签属性 */
    if (xml_mark_has_attr(fparse))
    {
        ret = xml_mark_get_attr(stack, fparse);
        if (XML_SUCCESS != ret)
        {
            LogError("Get mark attr failed!");
            return ret;
        }
    }

    /* 3. 标签是否结束:
            如果是<ABC DEF="EFG"/>格式时，此时标签结束；
            如果是<ABC DEF="EFG">HIGK</ABC>格式时，此时标签不结束 */
    if (xml_mark_is_end(fparse))
    {
        return xml_mark_end(stack, fparse);
    }

    /* 4. 提取标签值 */
    ret = xml_mark_has_value(fparse);
    switch(ret)
    {
        case true:
        {
            return xml_mark_get_value(stack, fparse);
        }
        case false:
        {
            return XML_SUCCESS;
        }
        default:
        {
            return ret;
        }
    }
 
    return XML_SUCCESS;
}
   
/******************************************************************************
 **函数名称: xml_parse_end
 **功    能: 处理结束节点(处理</XXX>格式的结束)
 **输入参数:
 **      stack: XML栈
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
static int xml_parse_end(Stack_t *stack, xml_fparse_t *fparse)
{
    int ret = 0;
    size_t len = 0;
    xml_node_t *top = NULL;
    const char *ptr = NULL;

    fparse->ptr += XML_MARK_END2_LEN; /* 跳过</ */
    ptr = fparse->ptr;
    
    /* 1. 确定结束节点名长度 */
    while (XmlIsMarkChar(*ptr)) ptr++;
    
    if (!XmlIsRPBrackChar(*ptr))
    {
        LogError("XML format is wrong![%-.32s]", fparse->ptr);
        return XML_ERR_FORMAT;
    }

    len = ptr - fparse->ptr;

    /* 2. 获取栈中顶节点信息 */
    top = (xml_node_t*)stack_gettop(stack);
    if (NULL == top)
    {
        LogError("Get stack top member failed!");
        return XML_ERR_STACK;
    }

    /* 3. 节点名是否一致 */
    if (len != strlen(top->name)
        || (0 != strncmp(top->name, fparse->ptr, len)))
    {
        LogError("Mark name is not match![%s][%-.32s]", top->name, fparse->ptr);
        return XML_ERR_MARK_MISMATCH;
    }

    /* 4. 弹出栈顶节点 */
    ret = stack_pop(stack);
    if (XML_SUCCESS != ret)
    {
        LogError("Pop failed!");
        return XML_ERR_STACK;
    }

    ptr++;
    fparse->ptr = ptr;
        
    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_mark_get_name
 **功    能: 提取标签名，并入栈
 **输入参数:
 **      xml: XML树
 **      stack: XML栈
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      注意: 1. 因新建节点已加入XML树中，因此在此不必去释放新节点的内存空间
 **            2. 此时tail用来记录孩子节点链表尾
 **作    者: # Qifeng.zou # 2013.02.23 #
 ******************************************************************************/
static int xml_mark_get_name(xml_tree_t *xml, Stack_t *stack, xml_fparse_t *fparse)
{
    int ret=0, len=0;
    const char *ptr = fparse->ptr;
    xml_node_t *node = NULL, *top = NULL;

    /* 1. 新建节点，并初始化 */
    node = xml_node_creat(XML_NODE_UNKNOWN);
    if (NULL == node)
    {
        LogError("Create xml node failed!");
        return XML_ERR_CREAT_NODE;
    }

    /* 2. 将节点加入XML树中 */
    if (stack_isempty(stack))
    {
        if (NULL == xml->root->tail)
        {
            xml->root->firstchild = node;
        }
        else
        {
            xml->root->tail->next = node;
        }
        xml->root->tail = node;
        node->parent = xml->root;
        xml_set_type(node, XML_NODE_CHILD);
        xml_set_child_flag(xml->root);
    }
    else
    {
        xml_set_type(node, XML_NODE_CHILD);
        top = stack_gettop(stack);
        node->parent = top;
        if (NULL == top->tail)
        {
            top->firstchild = node;
        }
        else
        {
            top->tail->next = node;
        }
        top->tail = node;
        xml_set_child_flag(top);
    }

    /* 3. 确定节点名长度 */
    while (XmlIsMarkChar(*ptr)) ptr++;

    /* 4.判断标签名边界是否合法 */
    if (!XmlIsMarkBorder(*ptr))
    {
        LogError("XML format is wrong!\n[%-32.32s]", fparse->ptr);
        return XML_ERR_FORMAT;
    }

    len = ptr - fparse->ptr;

    /* 5. 提取出节点名 */
    node->name = calloc(1, (len+1)*sizeof(char));
    if (NULL == node->name)
    {
        LogError("Calloc failed!");
        return XML_ERR_CALLOC;
    }
    strncpy(node->name, fparse->ptr, len);

    /* 6. 将节点入栈 */
    ret = stack_push(stack, (void*)node);
    if (XML_SUCCESS != ret)
    {
        LogError("Stack push failed!");
        return XML_ERR_STACK;
    }

    fparse->ptr = ptr;

    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_mark_has_attr
 **功    能: 判断标签是否有属性节点
 **输入参数:
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.23 #
 ******************************************************************************/
static int xml_mark_has_attr(xml_fparse_t *fparse)
{
    const char *ptr = fparse->ptr;

    while (XmlIsIgnoreChar(*ptr)) ptr++;    /* 跳过无意义的字符 */

    fparse->ptr = ptr;
    
    if (XmlIsMarkChar(*ptr))
    {
        return true;
    }

    return false;
}

/******************************************************************************
 **函数名称: xml_mark_get_attr
 **功    能: 解析有属性的标签
 **输入参数:
 **      stack: XML栈
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **     1. 属性值可使用双引号或单引号确定属性值范围
 **     2. 转义符号的转换对应关系如下:
 **        &lt;    <    小于
 **        &gt;    >    大于
 **        &amp;   &    和号
 **        &apos;  '    单引号
 **        &quot;  "    引号
 **作    者: # Qifeng.zou # 2013.02.18 #
 **修    改: # Qifeng.zou # 2014.01.06 #
 ******************************************************************************/
static int xml_mark_get_attr(Stack_t *stack, xml_fparse_t *fparse)
{
    char border = '"';
    xml_node_t *node = NULL, *top = NULL;
    int len = 0, errflg = 0, size = 0, ret = 0;
    const char *ptr = fparse->ptr;
#if defined(__XML_FRWD_PARSE__)
    xml_frwd_split_t split;
    const xml_frwd_t *frwd = NULL;


    memset(&split, 0, sizeof(split));
#endif /*__XML_FRWD_PARSE__*/

    /* 1. 获取正在处理的标签 */
    top = (xml_node_t*)stack_gettop(stack);
    if (NULL == top)
    {
        LogError("Get stack top failed!");
        return XML_ERR_STACK;
    }

    /* 3. 将属性节点依次加入标签子节点链表 */
    do
    {
        /* 3.1 新建节点，并初始化 */
        node = xml_node_creat(XML_NODE_ATTR);
        if (NULL == node)
        {
            LogError("Create xml node failed!");
            return XML_ERR_CREAT_NODE;
        }

        /* 3.2 获取属性名 */
        while (XmlIsIgnoreChar(*ptr)) ptr++;/* 跳过属性名之前无意义的空格 */

        fparse->ptr = ptr;
        while (XmlIsMarkChar(*ptr)) ptr++;  /* 查找属性名的边界 */

        len = ptr - fparse->ptr;
        node->name = (char*)calloc(1, (len+1)*sizeof(char));
        if (NULL == node->name)
        {
            errflg = 1;
            LogError("Calloc failed!");
            break;
        }

        memcpy(node->name, fparse->ptr, len);
        
        /* 3.3 获取属性值 */
        while (XmlIsIgnoreChar(*ptr)) ptr++;         /* 跳过=之前的无意义字符 */

        if (!XmlIsEqualChar(*ptr))                      /* 不为等号，则格式错误 */
        {
            errflg = 1;
            LogError("Attribute format is incorrect![%-.32s]", fparse->ptr);
            break;
        }
        ptr++;                                  /* 跳过"=" */
        while (XmlIsIgnoreChar(*ptr)) ptr++;     /* 跳过=之后的无意义字符 */

        /* 判断是单引号(')还是双引号(")为属性的边界 */
        if (XmlIsDQuotChar(*ptr) || XmlIsSQuotChar(*ptr))
        {
            border = *ptr;
        }
        else                  /* 不为 双/单 引号，则格式错误 */
        {
            errflg = 1;
            LogError("XML format is wrong![%-.32s]", fparse->ptr);
            break;
        }

        ptr++;
        fparse->ptr = ptr;
        while ((*ptr != border) && !XmlIsStrEndChar(*ptr))   /* 计算 双/单 引号之间的数据长度 */
        {
        #if defined(__XML_FRWD_PARSE__)
            if (XmlIsAndChar(*ptr))
            {
                /* 判断并获取转义字串类型及相关信息 */
                frwd = xml_frwd_get(ptr);

                /* 对包含有转义字串的字串进行切割 */
                ret = xml_frwd_split(frwd, fparse->ptr, ptr-fparse->ptr+1, &split);
                if (XML_SUCCESS != ret)
                {
                    errflg = 1;
                    LogError("Parse forwad string failed!");
                    break;
                }

                ptr += frwd->length;
                fparse->ptr = ptr;
            }
            else
        #endif /*__XML_FRWD_PARSE__*/
            {
                ptr++;
            }
        }

        if (*ptr != border)
        {
            errflg = 1;
            LogError("Mismatch border [%c]![%-.32s]", border, fparse->ptr);
            break;
        }

        len = ptr - fparse->ptr;
        ptr++;  /* 跳过" */

    #if defined(__XML_FRWD_PARSE__)
        if (NULL != split.head)
        {
            size = xml_frwd_size(&split);
            size += len+1;
    
            node->value = (char *)calloc(1, size);
            if (NULL == node->value)
            {
                errflg = 1;
                LogError("Alloc memory failed!");
                break;
            }

            xml_frwd_merge(&split, node->value);
            
            strncat(node->value, fparse->ptr, len);

            xml_frwd_free(&split);
        }
        else
    #endif /*__XML_FRWD_PARSE__*/
        {
            node->value = (char*)calloc(1, (len+1)*sizeof(char));
            if (NULL == node->value)
            {
                errflg = 1;
                LogError("Calloc failed!");
                break;
            }

            memcpy(node->value, fparse->ptr, len);
        }

        /* 3.4 将节点加入属性链表 */
        if (NULL == top->tail) /* 还没有孩子节点 */
        {
            top->firstchild = node;
        }
        else
        {
            top->tail->next = node;
        }
        node->parent = top;
        top->tail = node;
        
        /* 3.5 指针向后移动 */
        while (XmlIsIgnoreChar(*ptr)) ptr++;

    }while (XmlIsMarkChar(*ptr));

#if defined(__XML_FRWD_PARSE__)
    xml_frwd_free(&split);
#endif /*__XML_FRWD_PARSE__*/

    if (1 == errflg)         /* 防止内存泄漏 */
    {
        xml_node_free(node);
        node = NULL;
        return XML_ERR_GET_ATTR;
    }

    fparse->ptr = ptr;
    xml_set_attr_flag(top);

    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_mark_is_end
 **功    能: 标签是否结束 "/>"
 **输入参数:
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.23 #
 ******************************************************************************/
static int xml_mark_is_end(xml_fparse_t *fparse)
{
    int ret = 0;
    const char *ptr = fparse->ptr;
    
    while (XmlIsIgnoreChar(*ptr)) ptr++;

    /* 1. 是否有节点值 */
    ret = strncmp(ptr, XML_MARK_END1, XML_MARK_END1_LEN);
    if (0 != ret)
    {
        return false;
    }
    return true;
}

/******************************************************************************
 **函数名称: xml_mark_has_value
 **功    能: 是否有节点值
 **输入参数:
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: true:有 false:无 -1: 错误格式
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.23 #
 ******************************************************************************/
static int xml_mark_has_value(xml_fparse_t *fparse)
{
    const char *ptr = fparse->ptr;

    while (XmlIsIgnoreChar(*ptr)) ptr++;
    
    if (XmlIsRPBrackChar(*ptr))
    {
        ptr++;

        /* 跳过起始的空格和换行符 */
        while (XmlIsIgnoreChar(*ptr)) ptr++;

        fparse->ptr = ptr;
        if (XmlIsLPBrackChar(*ptr)) /* 出现子节点 */
        {
            return false;
        }
        return true;
    }
    
    LogError("XML format is wrong![%-.32s]", fparse->ptr);
    return XML_ERR_FORMAT;
}

/******************************************************************************
 **函数名称: xml_mark_get_value
 **功    能: 获取节点值
 **输入参数: 
 **      stack: XML栈
 **      fparse: 解析文件缓存信息
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.23 #
 ******************************************************************************/
static int xml_mark_get_value(Stack_t *stack, xml_fparse_t *fparse)
{
    int ret = 0, len = 0, size = 0;
    const char *p1=NULL, *p2=NULL;
    xml_node_t *current = NULL;
#if defined(__XML_FRWD_PARSE__)
    const xml_frwd_t *frwd = NULL;
    xml_frwd_split_t split;

    memset(&split, 0, sizeof(split));
#endif /*__XML_FRWD_PARSE__*/

    current = (xml_node_t*)stack_gettop(stack);
    if (NULL == current)
    {
        return XML_ERR_STACK;
    }

    p1 = fparse->ptr;

    while (XmlIsIgnoreChar(*p1)) p1++;

    fparse->ptr = p1;
    
    /* 提取节点值: 允许节点值中出现空格和换行符 */    
    while (!XmlIsStrEndChar(*p1) && !XmlIsLPBrackChar(*p1))
    {
    #if defined(__XML_FRWD_PARSE__)
        if (XmlIsAndChar(*p1))
        {
            frwd = xml_frwd_get(p1);

            ret = xml_frwd_split(frwd, fparse->ptr, p1-fparse->ptr+1, &split);
            if (XML_SUCCESS != ret)
            {
                xml_frwd_free(&split);
                LogError("Parse forwad string failed!");
                return XML_FAILED;
            }
            
            p1 += frwd->length;
            fparse->ptr = p1;
        }
        else
    #endif /*__XML_FRWD_PARSE__*/
        {
            p1++;
        }
    }

    if (XmlIsStrEndChar(*p1))
    {
    #if defined(__XML_FRWD_PARSE__)
        xml_frwd_free(&split);
    #endif /*__XML_FRWD_PARSE__*/
        LogError("XML format is wrong! MarkName:[%s]", current->name);
        return XML_ERR_FORMAT;
    }
    
    p2 = p1;
    p1--;
    while (XmlIsIgnoreChar(*p1)) p1--;

    p1++;

    len = p1 - fparse->ptr;
#if defined(__XML_FRWD_PARSE__)
    size = xml_frwd_size(&split);
#endif /*__XML_FRWD_PARSE__*/
    size += len+1;

    current->value = (char*)calloc(1, size*sizeof(char));
    if (NULL == current->value)
    {
    #if defined(__XML_FRWD_PARSE__)
        xml_frwd_free(&split);
    #endif /*__XML_FRWD_PARSE__*/
        LogError("Calloc failed!");
        return XML_ERR_CALLOC;
    }

#if defined(__XML_FRWD_PARSE__)
    if (NULL != split.head)
    {
        xml_frwd_merge(&split, current->value);

        strncat(current->value, fparse->ptr, len);

        xml_frwd_free(&split);
    }
    else
#endif /*__XML_FRWD_PARSE__*/
    {
        strncpy(current->value, fparse->ptr, len);
    }

    fparse->ptr = p2;
    xml_set_value_flag(current);

#if defined(__XML_OCOV__)
    /* 判断：有数值的情况下，是否还有孩子节点 */
    if ((XML_BEGIN_FLAG == *p2) && (XML_END_FLAG == *(p2+1)))
    {
        return XML_SUCCESS;
    }

    LogError("XML format is wrong: Node have child and value at same time!");
    return XML_ERR_FORMAT;
#endif /*__XML_OCOV__*/

    return XML_SUCCESS;
}

/* 打印节点名(注: XML有层次格式) */
#define xml_fprint_name(fp, node, depth) \
{ \
    while (depth > 1) \
    {   \
        fprintf(fp, "\t"); \
        depth--; \
    } \
    fprintf(fp, "<%s", node->name); \
}

/* 打印属性节点(注: XML有层次格式) */
#define xml_fprint_attr(fp, node) \
{ \
    while (NULL != node->temp) \
    { \
        if (xml_is_attr(node->temp)) \
        { \
            fprintf(fp, " %s=\"%s\"", node->temp->name, node->temp->value); \
            node->temp = node->temp->next; \
            continue; \
        } \
        break; \
    } \
}

/* 打印节点值(注: XML有层次格式) */
#define xml_fprint_value(fp, node) \
{ \
    if (xml_has_value(node)) \
    { \
        if (xml_has_child(node))  /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            fprintf(fp, ">%s\n", node->value); \
        } \
        else \
        { \
            fprintf(fp, ">%s</%s>\n", node->value, node->name); \
        } \
    } \
    else \
    { \
        if (NULL != node->temp)   /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            fprintf(fp, ">\n"); \
        } \
        else \
        { \
            fprintf(fp, "/>\n"); \
        } \
    } \
}

/******************************************************************************
 **函数名称: xml_fprint_next
 **功    能: 选择下一个处理的节点(注: XML有层次格式)
 **输入参数:
 **      root: 根节点
 **      stack: 栈
 **      fp: 文件指针
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      注意:1. 刚开始时，栈的深度必须为1.
 **           2. 此时temp用来记录正在处理孩子节点
 **           3. 不打印root的兄弟节点
 **作    者: # Qifeng.zou # 2013.02.27 #
 ******************************************************************************/
static xml_node_t *xml_fprint_next(Stack_t *stack, xml_node_t *node, FILE *fp)
{
    int ret = 0, depth = 0, level = 0;
    xml_node_t *top = NULL, *child = NULL;

    if (NULL != node->temp)      /* 首先: 处理孩子节点: 选出下一个孩子节点 */
    {
        child = node->temp;
        node->temp = child->next;
        node = child;
        return node;
    }
    else                        /* 再次: 处理其兄弟节点: 选出下一个兄弟节点 */
    {
        /* 1. 弹出已经处理完成的节点 */
        top = stack_gettop(stack);
        if (xml_has_child(top))
        {
            depth = stack_depth(stack);
            level = depth - 1;
            while (level > 1)
            {
                fprintf(fp, "\t");
                level--;
            }
            fprintf(fp, "</%s>\n", top->name);
        }
        
        ret = stack_pop(stack);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack pop failed!");
            return NULL;
        }
        
        if (stack_isempty(stack))
        {
            LogError("Compelte fprint!");
            return NULL;
        }
        
        /* 2. 处理其下一个兄弟节点 */
        node = top->next;
        while (NULL == node)     /* 所有兄弟节点已经处理完成，说明父亲节点也处理完成 */
        {
            /* 3. 父亲节点出栈 */
            top = stack_gettop(stack);
            ret = stack_pop(stack);
            if (XML_SUCCESS != ret)
            {
                LogError("Stack pop failed!");
                return NULL;
            }
        
            /* 4. 打印父亲节点结束标志 */
            if (xml_has_child(top))
            {
                depth = stack_depth(stack);
                level = depth + 1;
                while (level > 1)
                {
                    fprintf(fp, "\t");
                    level--;
                }
                fprintf(fp, "</%s>\n", top->name);
            }
            
            if (stack_isempty(stack))
            {
                return NULL;    /* 处理完成 */
            }

            /* 5. 选择父亲的兄弟节点 */
            node = top->next;
        }
    }    
    
    return node;
}

/******************************************************************************
 **函数名称: xml_fprint_tree
 **功    能: 将栈中XML节点的相关信息打印至文件(注: XML有层次格式)
 **输入参数:
 **      root: 根节点
 **      stack: 栈
 **      fp: 文件指针
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      注意:1. 刚开始时，栈的深度必须为1.
 **           2. 此时temp用来记录正在处理孩子节点
 **           3. 不打印root的兄弟节点
 **作    者: # Qifeng.zou # 2013.02.27 #
 ******************************************************************************/
int xml_fprint_tree(xml_node_t *root, Stack_t *stack, FILE *fp)
{
    int ret = 0, depth = 0;
    xml_node_t *node = root;

    depth = stack_depth(stack);
    if (0 != depth)
    {
        LogError("Stack depth must empty. depth:[%d]", depth);
        return XML_ERR_STACK;
    }

    do
    {
        /* 1. 将要处理的节点压栈 */
        node->temp = node->firstchild;
        ret = stack_push(stack, node);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack push failed!");
            return XML_ERR_STACK;
        }
        
        /* 2. 打印节点名 */
        depth = stack_depth(stack);
        
        xml_fprint_name(fp, node, depth);
        
        /* 3. 打印属性节点 */
        if (xml_has_attr(node))
        {
            xml_fprint_attr(fp, node);
        }
        
        /* 4. 打印节点值 */
        xml_fprint_value(fp, node);
        
        /* 5. 选择下一个处理的节点: 从父亲节点、兄弟节点、孩子节点中 */
        node = xml_fprint_next(stack, node, fp);
    }while (NULL != node);

    if (!stack_isempty(stack))
    {
        return XML_ERR_STACK;
    }
    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_node_sfree
 **功    能: 释放单个节点
 **输入参数:
 **      node: 需要被释放的节点
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.27 #
 ******************************************************************************/
int xml_node_sfree(xml_node_t *node)
{
    if (NULL != node->name)
    {
        free(node->name);
        node->name = NULL;
    }

    if (NULL != node->value)
    {
        free(node->value);
        node->value = NULL;
    }

    free(node);
    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_free_next
 **功    能: 获取下一个需要被处理的节点
 **输入参数:
 **      stack: 栈
 **      current: 当前正在处理的节点
 **输出参数:
 **返    回: 下一个需要处理的节点
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.27 #
 ******************************************************************************/
xml_node_t *xml_free_next(Stack_t *stack, xml_node_t *current)
{
    int ret = 0;
    xml_node_t *child = NULL, *top = NULL;
    
    /* 1. 释放孩子节点 */
    if (NULL != current->temp)       /* 首先: 处理孩子节点: 选出下一个孩子节点 */
    {
        child = current->temp;
        current->temp = child->next;
        current = child;
        return current;
    }
    else                            /* 再次: 处理其兄弟节点: 选出下一个兄弟节点 */
    {
        /* 1. 弹出已经处理完成的节点, 并释放 */
        top = stack_gettop(stack);
        
        ret = stack_pop(stack);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack pop failed!");
            return NULL;
        }
        
        if (stack_isempty(stack))
        {
            xml_node_sfree(top), top = NULL;
            return NULL;
        }
        
        /* 2. 处理其下一个兄弟节点 */
        current = top->next;
        xml_node_sfree(top), top = NULL;
        while (NULL == current)     /* 所有兄弟节点已经处理完成，说明父亲节点也处理完成 */
        {
            /* 3. 父亲节点出栈 */
            top = stack_gettop(stack);
            ret = stack_pop(stack);
            if (XML_SUCCESS != ret)
            {
                LogError("Stack pop failed!");
                return NULL;
            }
            
            if (stack_isempty(stack))
            {
                xml_node_sfree(top), top = NULL;
                return NULL;
            }
    
            /* 5. 选择父亲的兄弟节点 */
            current = top->next;
            xml_node_sfree(top), top = NULL;
        }
    }

    return current;
}

/* 打印节点名(注: XML有层次格式) */
#define xml_sprint_name(sp, node, depth) \
{ \
    while (depth > 1) \
    {   \
        sprintf(sp->ptr, "\t"); sp->ptr++;\
        depth--; \
    } \
    sprintf(sp->ptr, "<%s", node->name);\
    sp->ptr += strlen(sp->ptr);\
}

/* 打印属性节点(注: XML有层次格式) */
#define xml_sprint_attr(sp, node) \
{ \
    while (NULL != node->temp) \
    { \
        if (xml_is_attr(node->temp)) \
        { \
            sprintf(sp->ptr, " %s=\"%s\"", node->temp->name, node->temp->value); \
            sp->ptr += strlen(sp->ptr); \
            node->temp = node->temp->next; \
            continue; \
        } \
        break; \
    } \
}

/* 打印节点值(注: XML有层次格式) */
#define xml_sprint_value(sp, node) \
{ \
    if (xml_has_value(node)) \
    { \
        if (xml_has_child(node))  /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            sprintf(sp->ptr, ">%s\n", node->value); \
        } \
        else \
        { \
            sprintf(sp->ptr, ">%s</%s>\n", node->value, node->name); \
        } \
        sp->ptr += strlen(sp->ptr); \
    } \
    else \
    { \
        if (NULL != node->temp)   /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            sprintf(sp->ptr, ">\n"); \
            sp->ptr += 2; \
        } \
        else \
        { \
            sprintf(sp->ptr, "/>\n"); \
            sp->ptr += 3; \
        } \
    } \
}

/******************************************************************************
 **函数名称: xml_sprint_next
 **功    能: 选择下一个处理的节点(注: XML有层次格式)
 **输入参数:
 **      root: 根节点
 **      stack: 栈
 **      sp: 缓存指针
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      注意:1. 刚开始时，栈的深度必须为1.
 **           2. 此时temp用来记录正在处理孩子节点
 **           3. 不打印root的兄弟节点
 **作    者: # Qifeng.zou # 2013.02.27 #
 ******************************************************************************/
static xml_node_t *xml_sprint_next(Stack_t *stack, xml_node_t *node, sprint_t *sp)
{
    int ret = 0, depth = 0, level = 0;
    xml_node_t *top = NULL, *child = NULL;

    if (NULL != node->temp)      /* 首先: 处理孩子节点: 选出下一个孩子节点 */
    {
        child = node->temp;
        node->temp = child->next;
        node = child;
        return node;
    }
    else                        /* 再次: 处理其兄弟节点: 选出下一个兄弟节点 */
    {
        /* 1. 弹出已经处理完成的节点 */
        top = stack_gettop(stack);
        if (xml_has_child(top))
        {
            depth = stack_depth(stack);
            level = depth - 1;
            while (level > 1)
            {
                sprintf(sp->ptr, "\t");
                sp->ptr++;
                level--;
            }
            sprintf(sp->ptr, "</%s>\n", top->name);
            sp->ptr += strlen(sp->ptr);
        }
        
        ret = stack_pop(stack);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack pop failed!");
            return NULL;
        }
        
        if (stack_isempty(stack))
        {
            LogError("Compelte fprint!");
            return NULL;
        }
        
        /* 2. 处理其下一个兄弟节点 */
        node = top->next;
        while (NULL == node)     /* 所有兄弟节点已经处理完成，说明父亲节点也处理完成 */
        {
            /* 3. 父亲节点出栈 */
            top = stack_gettop(stack);
            ret = stack_pop(stack);
            if (XML_SUCCESS != ret)
            {
                LogError("Stack pop failed!");
                return NULL;
            }
        
            /* 4. 打印父亲节点结束标志 */
            if (xml_has_child(top))
            {
                depth = stack_depth(stack);
                level = depth + 1;
                while (level > 1)
                {
                    sprintf(sp->ptr, "\t");
                    sp->ptr++;
                    level--;
                }
                sprintf(sp->ptr, "</%s>\n", top->name);
                sp->ptr += strlen(sp->ptr);
            }
            
            if (stack_isempty(stack))
            {
                return NULL;    /* 处理完成 */
            }

            /* 5. 选择父亲的兄弟节点 */
            node = top->next;
        }
    }    
    
    return node;
}

/******************************************************************************
 **函数名称: xml_sprint_tree
 **功    能: 将栈中XML节点的相关信息打印至缓存(注: XML有层次格式)
 **输入参数:
 **      root: 根节点
 **      stack: 栈
 **      sp: 缓存指针
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      注意:1. 刚开始时，栈的深度必须为1.
 **           2. 此时temp用来记录正在处理孩子节点
 **           3. 不打印root的兄弟节点
 **作    者: # Qifeng.zou # 2013.02.27 #
 ******************************************************************************/
int xml_sprint_tree(xml_node_t *root, Stack_t *stack, sprint_t *sp)
{
    int ret = 0, depth = 0;
    xml_node_t *node = root;

    depth = stack_depth(stack);
    if (0 != depth)
    {
        LogError("Stack depth must empty. depth:[%d]", depth);
        return XML_ERR_STACK;
    }

    do
    {
        /* 1. 将要处理的节点压栈 */
        node->temp = node->firstchild;
        ret = stack_push(stack, node);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack push failed!");
            return XML_ERR_STACK;
        }
        
        /* 2. 打印节点名 */
        depth = stack_depth(stack);
        
        xml_sprint_name(sp, node, depth);
        
        /* 3. 打印属性节点 */
        if (xml_has_attr(node))
        {
            xml_sprint_attr(sp, node);
        }
        
        /* 4. 打印节点值 */
        xml_sprint_value(sp, node);
        
        /* 5. 选择下一个处理的节点: 从父亲节点、兄弟节点、孩子节点中 */
        node = xml_sprint_next(stack, node, sp);

    }while (NULL != node);

    if (!stack_isempty(stack))
    {
        LogError("Stack is not empty!");
        return XML_ERR_STACK;
    }
    return XML_SUCCESS;
}

/* 打印节点名(注: XML无层次格式) */
#define xml_pack_name(sp, node) \
{ \
    sprintf(sp->ptr, "<%s", node->name);\
    sp->ptr += strlen(sp->ptr);\
}

/* 打印属性节点(注: XML无层次格式) */
#define xml_pack_attr(sp, node) \
{ \
    while (NULL != node->temp) \
    { \
        if (xml_is_attr(node->temp)) \
        { \
            sprintf(sp->ptr, " %s=\"%s\"", node->temp->name, node->temp->value); \
            sp->ptr += strlen(sp->ptr); \
            node->temp = node->temp->next; \
            continue; \
        } \
        break; \
    } \
}

#if defined(__XML_PACK_CMARK__)
/* 打印节点值(注: XML无层次格式) */
#define xml_pack_value(sp, node) \
{ \
    if (xml_has_value(node)) \
    { \
        if (xml_has_child(node))  /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            sprintf(sp->ptr, ">%s", node->value); \
        } \
        else \
        { \
            sprintf(sp->ptr, ">%s</%s>", node->value, node->name); \
        } \
        sp->ptr += strlen(sp->ptr); \
    } \
    else \
    { \
        if (NULL != node->temp)   /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            sprintf(sp->ptr, ">"); \
            sp->ptr += 1; \
        } \
        else \
        { \
            sprintf(sp->ptr, "></%s>", node->name); \
            sp->ptr += strlen(sp->ptr); \
        } \
    } \
}
#else /*__XML_PACK_CMARK__*/
/* 打印节点值(注: XML无层次格式) */
#define xml_pack_value(sp, node) \
{ \
    if (xml_has_value(node)) \
    { \
        if (xml_has_child(node))  /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            sprintf(sp->ptr, ">%s", node->value); \
        } \
        else \
        { \
            sprintf(sp->ptr, ">%s</%s>", node->value, node->name); \
        } \
        sp->ptr += strlen(sp->ptr); \
    } \
    else \
    { \
        if (NULL != node->temp)   /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            sprintf(sp->ptr, ">"); \
            sp->ptr += 1; \
        } \
        else \
        { \
            sprintf(sp->ptr, "/>"); \
            sp->ptr += 2; \
        } \
    } \
}
#endif /*__XML_PACK_CMARK__*/

/******************************************************************************
 **函数名称: xml_pack_next
 **功    能: 选择下一个处理的节点(注: XML无层次格式)
 **输入参数:
 **      root: 根节点
 **      stack: 栈
 **      sp: 缓存指针
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      注意:1. 刚开始时，栈的深度必须为1.
 **           2. 此时temp用来记录正在处理孩子节点
 **           3. 不打印root的兄弟节点
 **作    者: # Qifeng.zou # 2013.03.01 #
 ******************************************************************************/
static xml_node_t *xml_pack_next(Stack_t *stack, xml_node_t *node, sprint_t *sp)
{
    int ret = 0;
    xml_node_t *top = NULL, *child = NULL;

    if (NULL != node->temp)      /* 首先: 处理孩子节点: 选出下一个孩子节点 */
    {
        child = node->temp;
        node->temp = child->next;
        node = child;
        return node;
    }
    else                        /* 再次: 处理其兄弟节点: 选出下一个兄弟节点 */
    {
        /* 1. 弹出已经处理完成的节点 */
        top = stack_gettop(stack);
        if (xml_has_child(top))
        {
            sprintf(sp->ptr, "</%s>", top->name);
            sp->ptr += strlen(sp->ptr);
        }
        
        ret = stack_pop(stack);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack pop failed!");
            return NULL;
        }
        
        if (stack_isempty(stack))
        {
            LogError("Compelte fprint!");
            return NULL;
        }
        
        /* 2. 处理其下一个兄弟节点 */
        node = top->next;
        while (NULL == node)     /* 所有兄弟节点已经处理完成，说明父亲节点也处理完成 */
        {
            /* 3. 父亲节点出栈 */
            top = stack_gettop(stack);
            ret = stack_pop(stack);
            if (XML_SUCCESS != ret)
            {
                LogError("Stack pop failed!");
                return NULL;
            }
        
            /* 4. 打印父亲节点结束标志 */
            if (xml_has_child(top))
            {
                sprintf(sp->ptr, "</%s>", top->name);
                sp->ptr += strlen(sp->ptr);
            }
            
            if (stack_isempty(stack))
            {
                return NULL;    /* 处理完成 */
            }

            /* 5. 选择父亲的兄弟节点 */
            node = top->next;
        }
    }    
    
    return node;
}

/******************************************************************************
 **函数名称: xml_pack_tree
 **功    能: 将栈中XML节点的相关信息打印至缓存(注: XML无层次格式)
 **输入参数:
 **      root: 根节点
 **      stack: 栈
 **      sp: 缓存指针
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      注意:1. 刚开始时，栈的深度必须为1.
 **           2. 此时temp用来记录正在处理孩子节点
 **           3. 不打印root的兄弟节点
 **作    者: # Qifeng.zou # 2013.03.01 #
 ******************************************************************************/
int xml_pack_tree(xml_node_t *root, Stack_t *stack, sprint_t *sp)
{
    int ret = 0, depth = 0;
    xml_node_t *node = root;

    depth = stack_depth(stack);
    if (0 != depth)
    {
        LogError("Stack depth must empty. depth:[%d]", depth);
        return XML_ERR_STACK;
    }

    do
    {
        /* 1. 将要处理的节点压栈 */
        node->temp = node->firstchild;
        ret = stack_push(stack, node);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack push failed!");
            return XML_ERR_STACK;
        }
        
        /* 2. 打印节点名 */
        depth = stack_depth(stack);
        
        xml_pack_name(sp, node);
        
        /* 3. 打印属性节点 */
        if (xml_has_attr(node))
        {
            xml_pack_attr(sp, node);
        }
        
        /* 4. 打印节点值 */
        xml_pack_value(sp, node);
        
        /* 5. 选择下一个处理的节点: 从父亲节点、兄弟节点、孩子节点中 */
        node = xml_pack_next(stack, node, sp);

    }while (NULL != node);

    if (!stack_isempty(stack))
    {
        LogError("Stack is not empty!");
        return XML_ERR_STACK;
    }
    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_delete_child
 **功    能: 从孩子节点链表中删除指定的孩子节点
 **输入参数:
 **      node: 需要删除孩子节点的节点
 **      child: 孩子节点
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **     从树中删除的节点，只是从树中被剥离出来，其相关内存并没有被释放.
 **     释放时需调用函数xml_node_free()
 **作    者: # Qifeng.zou # 2013.03.02 #
 ******************************************************************************/
int xml_delete_child(xml_node_t *node, xml_node_t *child)
{
    xml_node_t *p1 = NULL, *p2 = NULL;

    if (node != child->parent)
    {
        LogError("Parent node is not right!");
        return -1;
    }
    
    if (node->firstchild == child)    /* 1. 要删的是子节点链表的开始节点 */
    {
        node->firstchild = child->next;  /* 剔除链表 */
        if (NULL == node->firstchild)
        {
            node->tail = NULL;
            if (xml_is_attr(child))
            {
                xml_unset_attr_flag(node);
            }
        }
        else if (xml_is_attr(child) && !xml_is_attr(node->firstchild))
        {
            xml_unset_attr_flag(node);
        }
        return XML_SUCCESS;
    }

    p1 = node->firstchild;
    p2 = p1->next;
    while (NULL != p2)
    {
        if (child == p2)
        {
            p1->next = child->next; /* 剔除链表 */
            if (node->tail == child)
            {
                node->tail = p1;
            }

            if (NULL == child->next)
            {
                if (xml_is_child(child) && !xml_is_child(p1))
                {
                    xml_unset_child_flag(node);
                }
            }
            return XML_SUCCESS;
        }
        p1 = p2;
        p2 = p2->next;
    }
	return XML_SUCCESS;
}

/* 打印节点名长度(注: XML有层次格式) */
#define xml_node_name_length(node, depth, length) \
{ \
    while (depth > 1) \
    { \
        /*fprintf(fp, "\t");*/ \
        length++; \
        depth--; \
    } \
    /*fprintf(fp, "<%s", node->name);*/ \
    length += (1 + strlen(node->name)); \
}

/* 打印属性节点长度(注: XML有层次格式) */
#define xml_node_attr_length(node, length) \
{ \
    while (NULL != node->temp) \
    { \
        if (xml_is_attr(node->temp)) \
        { \
            /*fprintf(fp, " %s=\"%s\"", node->temp->name, node->temp->value);*/ \
            length += strlen(node->temp->name) + strlen(node->temp->value) + 4; \
            node->temp = node->temp->next; \
            continue; \
        } \
        break; \
    } \
}

/* 打印节点值长度(注: XML有层次格式) */
#define xml_node_value_length(node, length) \
{ \
    if (xml_has_value(node)) \
    { \
        if (xml_has_child(node))  /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            /* fprintf(fp, ">%s\n", node->value); */ \
            length += strlen(node->value) + 2; \
        } \
        else \
        { \
            /* fprintf(fp, ">%s</%s>\n", node->value, node->name); */ \
            length += strlen(node->value) + strlen(node->name) + 5; \
        } \
    } \
    else \
    { \
        if (NULL != node->temp)   /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            /* fprintf(fp, ">\n"); */ \
            length += 2; \
        } \
        else \
        { \
            /* fprintf(fp, "/>\n"); */ \
            length += 3; \
        } \
    } \
}

/******************************************************************************
 **函数名称: xml_node_next_length
 **功    能: 获取下一个要处理的节点，并计算当前结束节点的长度(注: XML有层次结构)
 **输入参数:
 **     root: XML树根节点
 **     stack: 栈
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.06.10 #
 ******************************************************************************/
static xml_node_t *xml_node_next_length(Stack_t *stack, xml_node_t *node, int *length)
{
    int ret = 0, depth = 0, level = 0, length2 = 0;
    xml_node_t *top = NULL, *child = NULL;

    if (NULL != node->temp)      /* 首先: 处理孩子节点: 选出下一个孩子节点 */
    {
        child = node->temp;
        node->temp = child->next;
        node = child;
        return node;
    }
    else                        /* 再次: 处理其兄弟节点: 选出下一个兄弟节点 */
    {
        /* 1. 弹出已经处理完成的节点 */
        top = stack_gettop(stack);
        if (xml_has_child(top))
        {
            depth = stack_depth(stack);
            level = depth - 1;
            while (level > 1)
            {
                /* fprintf(fp, "\t"); */
                length2++;
                level--;
            }
            /* fprintf(fp, "</%s>\n", top->name); */
            length2 += strlen(top->name) + 4;
        }
        
        ret = stack_pop(stack);
        if (XML_SUCCESS != ret)
        {
            *length += length2;
            LogError("Stack pop failed!");
            return NULL;
        }
        
        if (stack_isempty(stack))
        {
            *length += length2;
            LogError("Compelte fprint!");
            return NULL;
        }
        
        /* 2. 处理其下一个兄弟节点 */
        node = top->next;
        while (NULL == node)     /* 所有兄弟节点已经处理完成，说明父亲节点也处理完成 */
        {
            /* 3. 父亲节点出栈 */
            top = stack_gettop(stack);
            ret = stack_pop(stack);
            if (XML_SUCCESS != ret)
            {
                *length += length2;
                LogError("Stack pop failed!");
                return NULL;
            }
        
            /* 4. 打印父亲节点结束标志 */
            if (xml_has_child(top))
            {
                depth = stack_depth(stack);
                level = depth + 1;
                while (level > 1)
                {
                    /* fprintf(fp, "\t"); */
                    length2++;
                    level--;
                }
                /* fprintf(fp, "</%s>\n", top->name); */
                length2 += strlen(top->name) + 4;
            }
            
            if (stack_isempty(stack))
            {
                *length += length2;
                return NULL;    /* 处理完成 */
            }

            /* 5. 选择父亲的兄弟节点 */
            node = top->next;
        }
    }    

    *length += length2;
    return node;
}

/******************************************************************************
 **函数名称: _xml_node_length
 **功    能: 计算节点打印成XML格式字串时的长度(注: XML有层次结构)
 **输入参数:
 **     root: XML树根节点
 **     stack: 栈
 **输出参数:
 **返    回: 节点及其属性、孩子节点的总长度
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.06.10 #
 ******************************************************************************/
int _xml_node_length(xml_node_t *root, Stack_t *stack)
{
    int ret = 0, depth = 0, length=0;
    xml_node_t *node = root;

    depth = stack_depth(stack);
    if (0 != depth)
    {
        LogError("Stack depth must empty. depth:[%d]", depth);
        return XML_ERR_STACK;
    }

    do
    {
        /* 1. 将要处理的节点压栈 */
        node->temp = node->firstchild;
        ret = stack_push(stack, node);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack push failed!");
            return XML_ERR_STACK;
        }
        
        /* 2. 打印节点名 */
        depth = stack_depth(stack);
        
        xml_node_name_length(node, depth, length);
        
        /* 3. 打印属性节点 */
        if (xml_has_attr(node))
        {
            xml_node_attr_length(node, length);
        }
        
        /* 4. 打印节点值 */
        xml_node_value_length(node, length);
        
        /* 5. 选择下一个处理的节点: 从父亲节点、兄弟节点、孩子节点中 */
        node = xml_node_next_length(stack, node, &length);
        
    }while (NULL != node);

    if (!stack_isempty(stack))
    {
        return XML_ERR_STACK;
    }
    return length;
}


/* 组包节点名的长度(注: XML无层次格式) */
#define xml_pack_name_length(node, length) \
{ \
    /*fprintf(fp, "<%s", node->name);*/ \
    length += (1 + strlen(node->name)); \
}

/* 组包属性节点的长度(注: XML无层次格式) */
#define xml_pack_attr_length(node, length) \
{ \
    while (NULL != node->temp) \
    { \
        if (xml_is_attr(node->temp)) \
        { \
            /* sprintf(sp->ptr, " %s=\"%s\"", node->temp->name, node->temp->value); */ \
            /* sp->ptr += strlen(sp->ptr); */ \
            length += strlen(node->temp->name) + strlen(node->temp->value) + 4; \
            node->temp = node->temp->next; \
            continue; \
        } \
        break; \
    } \
}

#if defined(__XML_PACK_CMARK__)
/* 组包节点值的长度(注: XML无层次格式) */
#define xml_pack_value_length(node, length) \
{ \
    if (xml_has_value(node)) \
    { \
        if (xml_has_child(node))  /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            /* sprintf(sp->ptr, ">%s", node->value); */ \
            length += strlen(node->value)+1; \
        } \
        else \
        { \
            /* sprintf(sp->ptr, ">%s</%s>", node->value, node->name); */ \
            length += strlen(node->value) + strlen(node->name) + 4; \
        } \
    } \
    else \
    { \
        if (NULL != node->temp)   /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            /* sprintf(sp->ptr, ">"); */ \
            length++; \
        } \
        else \
        { \
            /* sprintf(sp->ptr, "></%s>", node->name); */ \
            length += strlen(node->name) + 4; \
        } \
    } \
}
#else /*__XML_PACK_CMARK__*/
/* 组包节点值的长度(注: XML无层次格式) */
#define xml_pack_value_length(node, length) \
{ \
    if (xml_has_value(node)) \
    { \
        if (xml_has_child(node))  /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            /* sprintf(sp->ptr, ">%s", node->value); */ \
            length += strlen(node->value)+1; \
        } \
        else \
        { \
            /* sprintf(sp->ptr, ">%s</%s>", node->value, node->name); */ \
            length += strlen(node->value) + strlen(node->name) + 4; \
        } \
    } \
    else \
    { \
        if (NULL != node->temp)   /* 此时temp指向node的孩子节点 或 NULL */ \
        { \
            /* sprintf(sp->ptr, ">"); */ \
            length++; \
        } \
        else \
        { \
            /* sprintf(sp->ptr, "/>"); */ \
            length += 2; \
        } \
    } \
}
#endif /*__XML_PACK_CMARK__*/

/******************************************************************************
 **函数名称: xml_pack_next_length
 **功    能: 选择下一个处理的节点, 并计算组包结束节点报文的长度(注: XML无层次格式)
 **输入参数:
 **      root: 根节点
 **      stack: 栈
 **      length: 长度统计
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      注意:1. 刚开始时，栈的深度必须为1.
 **           2. 此时temp用来记录正在处理孩子节点
 **           3. 不打印root的兄弟节点
 **作    者: # Qifeng.zou # 2013.06.11 #
 ******************************************************************************/
static xml_node_t *xml_pack_next_length(Stack_t *stack, xml_node_t *node, int *length)
{
    int ret = 0, length2 = 0;
    xml_node_t *top = NULL, *child = NULL;

    if (NULL != node->temp)      /* 首先: 处理孩子节点: 选出下一个孩子节点 */
    {
        child = node->temp;
        node->temp = child->next;
        node = child;
        return node;
    }
    else                        /* 再次: 处理其兄弟节点: 选出下一个兄弟节点 */
    {
        /* 1. 弹出已经处理完成的节点 */
        top = stack_gettop(stack);
        if (xml_has_child(top))
        {
            /* sprintf(sp->ptr, "</%s>", top->name); */
            length2 += strlen(top->name) + 3;
        }
        
        ret = stack_pop(stack);
        if (XML_SUCCESS != ret)
        {
            *length += length2;
            LogError("Stack pop failed!");
            return NULL;
        }
        
        if (stack_isempty(stack))
        {
            *length += length2;
            LogError("Compelte fprint!");
            return NULL;
        }
        
        /* 2. 处理其下一个兄弟节点 */
        node = top->next;
        while (NULL == node)     /* 所有兄弟节点已经处理完成，说明父亲节点也处理完成 */
        {
            /* 3. 父亲节点出栈 */
            top = stack_gettop(stack);
            ret = stack_pop(stack);
            if (XML_SUCCESS != ret)
            {
                *length += length2;
                LogError("Stack pop failed!");
                return NULL;
            }
        
            /* 4. 打印父亲节点结束标志 */
            if (xml_has_child(top))
            {
                /* sprintf(sp->ptr, "</%s>", top->name); */
                length2 += strlen(top->name)+3;
            }
            
            if (stack_isempty(stack))
            {
                *length += length2;
                return NULL;    /* 处理完成 */
            }

            /* 5. 选择父亲的兄弟节点 */
            node = top->next;
        }
    }    

    *length += length2;
    return node;
}

/******************************************************************************
 **函数名称: xml_pack_node_length
 **功    能: 计算节点打印成XML报文字串时的长度(注: XML无层次结构)
 **输入参数:
 **     root: XML树根节点
 **     stack: 栈
 **输出参数:
 **返    回: 节点及其属性、孩子节点的总长度
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.06.11 #
 ******************************************************************************/
int xml_pack_node_length(xml_node_t *root, Stack_t *stack)
{
    int ret = 0, depth = 0, length=0;
    xml_node_t *node = root;

    depth = stack_depth(stack);
    if (0 != depth)
    {
        LogError("Stack depth must empty. depth:[%d]", depth);
        return XML_ERR_STACK;
    }

    do
    {
        /* 1. 将要处理的节点压栈 */
        node->temp = node->firstchild;
        
        ret = stack_push(stack, node);
        if (XML_SUCCESS != ret)
        {
            LogError("Stack push failed!");
            return XML_ERR_STACK;
        }
        
        /* 2. 打印节点名 */
        depth = stack_depth(stack);
        
        xml_pack_name_length(node, length);
        
        /* 3. 打印属性节点 */
        if (xml_has_attr(node))
        {
            xml_pack_attr_length(node, length);
        }
        
        /* 4. 打印节点值 */
        xml_pack_value_length(node, length);
        
        /* 5. 选择下一个处理的节点: 从父亲节点、兄弟节点、孩子节点中 */
        node = xml_pack_next_length(stack, node, &length);
        
    }while (NULL != node);

    if (!stack_isempty(stack))
    {
        return XML_ERR_STACK;
    }
    return length;
}

#if defined(__XML_FRWD_PARSE__)
/******************************************************************************
 **函数名称: xml_frwd_get
 **功    能: 获取转义字串的信息
 **输入参数:
 **      str: 以&开头的字串
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **     转义字串的对应关系如下:
 **        &lt;    <    小于
 **        &gt;    >    大于
 **        &amp;   &    和号
 **        &apos;  '    单引号
 **        &quot;  "    引号
 **作    者: # Qifeng.zou # 2014.01.06 #
 ******************************************************************************/
static const xml_frwd_t *xml_frwd_get(const char *str)
{
    if (XmlIsLtStr(str))         /* &lt; */
    {
        return &g_xml_frwd_str[XML_FRWD_LT];
    }
    else if (XmlIsGtStr(str))    /* &gt; */
    {
        return &g_xml_frwd_str[XML_FRWD_GT];
    }
    else if (XmlIsAmpStr(str))   /* &amp; */
    {
        return &g_xml_frwd_str[XML_FRWD_AMP];
    }
    else if (XmlIsAposStr(str))  /* &apos; */
    {
        return &g_xml_frwd_str[XML_FRWD_APOS];
    }
    else if (XmlIsQuotStr(str))  /* &quot; */
    {
        return &g_xml_frwd_str[XML_FRWD_QUOT];
    }

    return &g_xml_frwd_str[XML_FRWD_UNKNOWN];    /* 未知类型 */
}

/******************************************************************************
 **函数名称: xml_frwd_size
 **功    能: 获取转义切割之前字段长度之和
 **输入参数:
 **      s: 被切割后的字串链表
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.01.06 #
 ******************************************************************************/
static int xml_frwd_size(const xml_frwd_split_t *sp)
{
    int size = 0;
    xml_frwd_node_t *node = sp->head;
    
    while (NULL != node)
    {
        size = node->length;
        node = node->next;
    }

    return size;
}

/******************************************************************************
 **函数名称: xml_frwd_merge
 **功    能: 合并被切割转义的字串
 **输入参数:
 **      s: 被切割后的字串链表
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.01.06 #
 ******************************************************************************/
static int xml_frwd_merge(const xml_frwd_split_t *sp, char *dst)
{
    char *ptr = dst;
    xml_frwd_node_t *fnode = sp->head;

    while (NULL != fnode)
    {
        sprintf(ptr, "%s", fnode->str);
        ptr += fnode->length;
        fnode = fnode->next;
    }

    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_frwd_free
 **功    能: 是否转义切割对象
 **输入参数:
 **     split: 切割对象
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.01.06 #
 ******************************************************************************/
static int xml_frwd_free(xml_frwd_split_t *split)
{
    xml_frwd_node_t *node = split->head, *next = NULL;

    while (NULL != node)
    {
        next = node->next;
        free(node->str);
        free(node);
        node = next;
    }
    split->head = NULL;
    split->tail = NULL;
    
    return XML_SUCCESS;
}

/******************************************************************************
 **函数名称: xml_frwd_split
 **功    能: 切割并转义从转义字串及之前的字串
 **输入参数:
 **     frwd: 对应的转义信息
 **     str: 字串
 **     len: str+len处的字串需要进行转义, 即:str[len]='&'
 **输出参数:
 **     split: 分割后的结果
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **     转义字串的对应关系如下:
 **        &lt;    <    小于
 **        &gt;    >    大于
 **        &amp;   &    和号
 **        &apos;  '    单引号
 **        &quot;  "    引号
 **作    者: # Qifeng.zou # 2014.01.06 #
 ******************************************************************************/
static int xml_frwd_split(const xml_frwd_t *frwd,
    const char *str, int len, xml_frwd_split_t *split)
{
    xml_frwd_node_t *node = NULL;

    node = (xml_frwd_node_t *)calloc(1, sizeof(xml_frwd_node_t));
    if (NULL == node)
    {
        LogError("Calloc memory failed!");
        return XML_ERR_CALLOC;
    }

    node->str = (char *)calloc(1, len+1);
    if (NULL == node->str)
    {
        free(node);
        return XML_ERR_CALLOC;
    }

    strncpy(node->str, str, len-1);
    node->str[len-1] = frwd->ch;
    node->length = len;
    
    if (NULL == split->head)
    {
        split->head = node;
    }
    else
    {
        split->tail->next = node;
    }
    split->tail = node;

    return XML_SUCCESS;
}
#endif /*__XML_FRWD_PARSE__*/
