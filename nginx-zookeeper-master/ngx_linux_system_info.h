#ifndef NGX_LINUX_SYSTEM_INFO_H
#define NGX_LINUX_SYSTEM_INFO_H

/**
*    @file:
*
*    @brief:        从linux系统获取cpu及内存使用情况
*
*    @version       1.0
*
*    @author       jasonsalex
*
*    @github        https://github.com/jasonsalex/nginx_zookeeper
*
*    @date          2015年12月17日
*
*/

typedef struct         //定义一个cpu occupy的结构体
{
    char name[20];      //定义一个char类型的数组名name有20个元素
    unsigned int user; //定义一个无符号的int类型的user
    unsigned int nice; //定义一个无符号的int类型的nice
    unsigned int system;//定义一个无符号的int类型的system
    unsigned int idle; //定义一个无符号的int类型的idle
}CPU_OCCUPY;

typedef struct         //定义一个mem occupy的结构体
{
    char name[20];      //定义一个char类型的数组名name有20个元素
    unsigned int total;
    char name2[20];
    unsigned int free;
}MEM_OCCUPY;


void ngx_get_memoccupy (MEM_OCCUPY *mem); //对无类型get函数含有一个形参结构体类弄的指针O
int ngx_cal_cpuoccupy (CPU_OCCUPY *o, CPU_OCCUPY *n);
void ngx_get_cpuoccupy (CPU_OCCUPY *cpust); //对无类型get函数含有一个形参结构体类弄的指针O

#endif // NGX_LINUX_SYSTEM_INFO_H
