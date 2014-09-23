#include <string.h>

#include "x_string.h"

/******************************************************************************
 **函数名称: str_to_lower
 **功    能: 将字串转为小字母
 **输入参数:
 **     str: 字串
 **输出参数:
 **返    回: 字串对象
 **实现描述: 
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.18 #
 ******************************************************************************/
string_t *str_to_lower(string_t *s)
{
    int idx;

    for (idx=0; idx<s->len; ++idx)
    {
        switch (s->str[idx])
        {
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'G':
            case 'H':
            case 'I':
            case 'J':
            case 'K':
            case 'L':
            case 'M':
            case 'N':
            case 'O':
            case 'P':
            case 'Q':
            case 'R':
            case 'S':
            case 'T':
            case 'U':
            case 'V':
            case 'W':
            case 'X':
            case 'Y':
            case 'Z':
            {
                s->str[idx] += 32;
                break;
            }
            default:
            {
                break;
            }
        }
    }

    return s;
}

/******************************************************************************
 **函数名称: str_to_upper
 **功    能: 将字串转为大字母
 **输入参数:
 **     str: 字串
 **输出参数:
 **返    回: 字串对象
 **实现描述: 
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.18 #
 ******************************************************************************/
string_t *str_to_upper(string_t *s)
{
    int idx;

    for (idx=0; idx<s->len; ++idx)
    {
        switch (s->str[idx])
        {
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'h':
            case 'i':
            case 'j':
            case 'k':
            case 'l':
            case 'm':
            case 'n':
            case 'o':
            case 'p':
            case 'q':
            case 'r':
            case 's':
            case 't':
            case 'u':
            case 'v':
            case 'w':
            case 'x':
            case 'y':
            case 'z':
            {
                s->str[idx] -= 32;
                break;
            }
            default:
            {
                break;
            }
        }
    }

    return s;
}