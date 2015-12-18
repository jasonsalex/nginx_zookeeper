/**
 * Nginx Zookeeper
 *
 * @author Timandes White <timands@gmail.com>
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <zookeeper/zookeeper.h>
#include <ngx_tcp.h>
#include "ngx_linux_system_info.h"
#include "cJSON.h"

/**
*    @file:
*
*    @brief:        sync to zookeeper data...
*
*    @version       1.0
*
*    @github        https://github.com/jasonsalex/nginx_zookeeper
*
*    @author        jasonsalex
*
*    @date          2015年12月17日
*
*/

typedef struct {
    ngx_rbtree_t   rbtree;
    ngx_rbtree_node_t sentinel;
}ngx_zookeeper_rbtree_t;

// Configurations
typedef struct {
    ngx_str_t host;
    ngx_str_t path;
    ngx_array_t *upstream_workload_path_ary;
    char *cHost;
    char *cPath;
    zhandle_t *handle;
    ngx_zookeeper_rbtree_t rbtree;

    ngx_msec_t work_load_update;

} ngx_http_zookeeper_main_conf_t;


typedef struct{

    ngx_rbtree_node_t node;
    char ip[30];

}ngx_upstream_ip_t;

ngx_connection_t        dummy;
ngx_event_t             timer;
CPU_OCCUPY              cpu_stat1,cpu_stat2;
int cpu_usage;

static ngx_int_t ngx_http_zookeeper_init_module(ngx_cycle_t *cycle);
static void ngx_http_zookeeper_exit_master(ngx_cycle_t *cycle);
static void ngx_http_zookeeper_init_timer(ngx_msec_t time, void * call_back_p);

static void *ngx_http_zookeeper_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_zookeeper_init_main_conf(ngx_conf_t *cf, void *conf);
static void ngx_http_zookeeper_watcher_master(zhandle_t *zh, int type,int state, const char *path,void *watcherCtx);
static void ngx_http_zookeeper_string_completion_t(int rc, const char *value, const void *data);

static void ngx_http_zookeeper_data_completion_t(int rc, const char *value, int value_len,
                                                 const struct Stat *stat, const void *data);

static void ngx_http_zookeeper_upstream_workload_data_completion_t(int rc, const char *value, int value_len,
                                                 const struct Stat *stat, const void *data);


static void ngx_http_zookeeper_stat_completion_t(int rc, const struct Stat *stat,const void *data);
static void ngx_http_zookeeper_strings_completion_t(int rc,
                                                    const struct String_vector *strings, const void *data);

static void ngx_http_zookeeper_send_to_processes_data(ngx_upstream_ip_t *upstream);
static void ngx_http_zookeeper_update_workload_to_zk(ngx_event_t *ev);
static void ngx_http_zookeeper_update_cpu_uage(ngx_event_t *ev);


// Directives
static ngx_command_t ngx_http_zookeeper_commands[] = {
    {
        ngx_string("zookeeper_path"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, path),
        NULL
    },
    {
        ngx_string("zookeeper_host"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, host),
        NULL
    },

    {
        ngx_string("zookeeper_workload_update"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, work_load_update),
        NULL
    },

    {
        ngx_string("zookeeper_upstream_workload_path"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
        ngx_conf_set_keyval_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, upstream_workload_path_ary),
        NULL
    },
    ngx_null_command
};

// Context
static ngx_http_module_t ngx_http_zookeeper_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */
    ngx_http_zookeeper_create_main_conf,   /* create main configuration */
    ngx_http_zookeeper_init_main_conf,     /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
    NULL,                                  /* create location configration */
    NULL                                   /* merge location configration */
};

// Module
ngx_module_t ngx_http_zookeeper_module = {
    NGX_MODULE_V1,
    &ngx_http_zookeeper_module_ctx,        /* module context */
    ngx_http_zookeeper_commands,           /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_zookeeper_init_module,        /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    ngx_http_zookeeper_exit_master,        /* exit master */
    NGX_MODULE_V1_PADDING
};

// Init module
static ngx_int_t ngx_http_zookeeper_init_module(ngx_cycle_t *cycle)
{
    ngx_http_zookeeper_main_conf_t *zmf;

    if (ngx_test_config) // ignore znode registering when testing configuration
        return NGX_OK;

    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(cycle, ngx_http_zookeeper_module);


    if (NULL == zmf) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_http_zookeeper_module: Fail to get configuration");
        ngx_log_stderr(0, "ngx_http_zookeeper_module: Fail to get configuration");
        return NGX_ERROR;
    }

    if (zmf->host.len <= 0) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "No zookeeper host was given");
        return NGX_OK;
    }
    if (zmf->path.len <= 0) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "No zookeeper path was given");
        return NGX_OK;
    }
    if (NULL == zmf->cHost) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Impossible cHost");
        ngx_log_stderr(0, "Impossible cHost");
        return NGX_ERROR;
    }
    if (NULL == zmf->cPath) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Impossible cPath");
        ngx_log_stderr(0, "Impossible cPath");
        return NGX_ERROR;
    }

    // init zookeeper
    zmf->handle = zookeeper_init(zmf->cHost, ngx_http_zookeeper_watcher_master, 10000, 0, NULL, 0);
    if (NULL == zmf->handle) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Fail to init zookeeper instance");
        ngx_log_stderr(0, "Fail to init zookeeper instance");
        return NGX_ERROR;
    }

    ngx_http_zookeeper_init_timer(zmf->work_load_update,ngx_http_zookeeper_update_workload_to_zk);

    return NGX_OK;
}

// Exit master
static void ngx_http_zookeeper_exit_master(ngx_cycle_t *cycle)
{
    ngx_http_zookeeper_main_conf_t *zmf;

    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(cycle, ngx_http_zookeeper_module);
    if (zmf&& zmf->handle)
        zookeeper_close(zmf->handle);
}

static void ngx_http_zookeeper_init_timer(ngx_msec_t time, void * call_back_p)
{
        dummy.fd = (ngx_socket_t)-1;
        ngx_memzero(&timer,sizeof(ngx_event_t));

        timer.handler = call_back_p;
        timer.log = ngx_cycle->log;
        timer.data = &dummy;

        ngx_add_timer(&timer,time);
}



static void ngx_http_zookeeper_watcher_master(zhandle_t *zh, int type,int state, const char *path,void *watcherCtx)
{
    ngx_http_zookeeper_main_conf_t *zmf;

    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_zookeeper_module);

    if(state == ZOO_CONNECTED_STATE)
    {
        // create node
       zoo_acreate(zh, zmf->cPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL|ZOO_SEQUENCE, ngx_http_zookeeper_string_completion_t, zmf);

    }else if( state == ZOO_EXPIRED_SESSION_STATE)
    {
        zmf->handle = zookeeper_init(zmf->cHost, ngx_http_zookeeper_watcher_master, 10000, 0, NULL, 0);
        if (NULL == zmf->handle) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "Fail to init zookeeper instance");
        }
    }
}

static void ngx_http_zookeeper_string_completion_t(int rc, const char *value, const void *data)
{
    ngx_http_zookeeper_main_conf_t *zmf = (ngx_http_zookeeper_main_conf_t *)data;
    ngx_uint_t i;
    ngx_keyval_t *path;

    if (ZOK != rc) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "Fail to create zookeeper node");
        ngx_log_stderr(0, "Fail to create zookeeper node");
        zookeeper_close(zmf->handle);
        zmf->handle = NULL;
    }else
    {
        ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, "create zookeeper node:%s",value);
        path = zmf->upstream_workload_path_ary->elts;

        for(i = 0; i<zmf->upstream_workload_path_ary->nelts; ++i)
        {
            rc = zoo_aget(zmf->handle, (char*)path[i].value.data, ZOO_CHANGED_EVENT, ngx_http_zookeeper_data_completion_t, &path[i].key);

            if(rc != ZOK)
            {
                zookeeper_close(zmf->handle);
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "get upstream workload data is failed:%V,error info:%s",&path[i].key, zerror(rc));
            }
        }

    }
}

static void ngx_http_zookeeper_data_completion_t(int rc, const char *value, int value_len,const struct Stat *stat, const void *data)
{
    ngx_http_zookeeper_main_conf_t *zmf;
    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_zookeeper_module);

    if(rc == ZOK)
    {
        ngx_uint_t func_id;
        ngx_str_t  *func_str = (ngx_str_t*)data;
        ngx_upstream_ip_t *upstream_ip;

        func_id = ngx_atoi(func_str->data, func_str->len);
        upstream_ip = (ngx_upstream_ip_t*) ngx_uint_rbtree_lookup(func_id,&zmf->rbtree.rbtree,&zmf->rbtree.sentinel);

        if(upstream_ip != NULL)
        {
            if(ngx_strncmp(upstream_ip->ip,value,value_len) != 0)
            {
                ngx_memmove(upstream_ip->ip,value,value_len);
                ngx_http_zookeeper_send_to_processes_data(upstream_ip);
            }

        }else
        {
            upstream_ip = ngx_pcalloc(ngx_cycle->pool,sizeof(ngx_upstream_ip_t));
            upstream_ip->node.key = func_id;
            ngx_memcpy(upstream_ip->ip,value,value_len);

            ngx_rbtree_insert(&zmf->rbtree.rbtree,&upstream_ip->node);

            ngx_http_zookeeper_send_to_processes_data(upstream_ip);
        }

    }else
    {
        zookeeper_close(zmf->handle);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "zookeeper error info:%s", zerror(rc));
    }
}

static void ngx_http_zookeeper_upstream_workload_data_completion_t(int rc, const char *value, int value_len,
                                                 const struct Stat *stat, const void *data)
{
    ngx_http_zookeeper_main_conf_t *zmf = (ngx_http_zookeeper_main_conf_t*)data;

    if(rc == ZOK)
    {
       //ngx_memmove(value,value,value_len);

       cJSON *root = cJSON_Parse(value);
       if(root == NULL)
       {
            root = cJSON_CreateObject();
       }

       cJSON *obj = cJSON_GetObjectItem(root,(char*)ngx_cycle->hostname.data);

       if(obj == NULL)
       {
           cJSON_AddItemToObject(root,(char*)ngx_cycle->hostname.data,obj = cJSON_CreateObject());
       }

       cJSON *cpuObj, *memObj, *conObj;

       cpuObj = cJSON_GetObjectItem(obj,"cpu");
       memObj = cJSON_GetObjectItem(obj,"mem");
       conObj = cJSON_GetObjectItem(obj,"con_num");

       if(cpuObj == NULL)
            cJSON_AddItemToObject(obj,"cpu",cpuObj = cJSON_CreateNumber(0));

       if(memObj == NULL)
            cJSON_AddItemToObject(obj,"mem",memObj = cJSON_CreateNumber(0));

       if(conObj == NULL)
           cJSON_AddItemToObject(obj,"con_num",conObj = cJSON_CreateNumber(0));

       MEM_OCCUPY  mem_stat;

       ngx_get_memoccupy(&mem_stat);

       cJSON_SetNumberValue(cpuObj, cpu_usage/100);
       cJSON_SetNumberValue(memObj, mem_stat.free/1024);
       cJSON_SetNumberValue(conObj, ngx_cycle->connection_n - ngx_cycle->free_connection_n);

//       ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,"#################%s", cJSON_Print(root));

       char * json_buf = cJSON_PrintUnformatted(root);

       zoo_aset(zmf->handle,(char*) zmf->path.data,json_buf,ngx_strlen(json_buf),-1,ngx_http_zookeeper_stat_completion_t,zmf);

       cJSON_Delete(root);

    }else
    {
        zookeeper_close(zmf->handle);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "zookeeper error info:%s", zerror(rc));
    }
}


static void ngx_http_zookeeper_stat_completion_t(int rc, const struct Stat *stat,const void *data)
{
    if(rc == ZOK)
    {
        //ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,"update to upstream workload!");

    }else
    {
        zookeeper_close(((ngx_http_zookeeper_main_conf_t *)data)->handle);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,"update to upstream workload is failed,error info:%s",zerror(rc));
    }
}

static void ngx_http_zookeeper_strings_completion_t(int rc, const struct String_vector *strings, const void *data)
{

}


static void ngx_http_zookeeper_send_to_processes_data(ngx_upstream_ip_t *upstream)
{
    ngx_tcp_link_to_upstream(upstream->node.key,upstream->ip);
}

static void ngx_http_zookeeper_update_workload_to_zk(ngx_event_t *ev)
{
    ngx_http_zookeeper_main_conf_t *zmf;
    int rc;

    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_zookeeper_module);

    if(zoo_state(zmf->handle) == ZOO_CONNECTED_STATE)
    {

      //  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "get gateway path is failed, %V",zmf->path);

        ngx_get_cpuoccupy(&cpu_stat1);

        rc = zoo_aget(zmf->handle,(char*) zmf->path.data, 0, ngx_http_zookeeper_upstream_workload_data_completion_t, zmf);

        if(rc != ZOK)
        {
            zookeeper_close(zmf->handle);
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "get gateway path is failed, %s,error info:%s",zmf->cPath,zerror(rc));
        }
    }

    ngx_http_zookeeper_init_timer(zmf->work_load_update,ngx_http_zookeeper_update_cpu_uage);

  //  ngx_add_timer(ev,zmf->work_load_update);
}

static void ngx_http_zookeeper_update_cpu_uage(ngx_event_t *ev)
{
    ngx_http_zookeeper_main_conf_t *zmf;
    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_zookeeper_module);

    ngx_get_cpuoccupy(&cpu_stat2);
    cpu_usage = ngx_cal_cpuoccupy(&cpu_stat1, &cpu_stat2);

    ngx_http_zookeeper_init_timer(zmf->work_load_update,ngx_http_zookeeper_update_workload_to_zk);
}



// Create main conf
static void *ngx_http_zookeeper_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_zookeeper_main_conf_t *retval;
    retval = ngx_pcalloc(cf->pool, sizeof(ngx_http_zookeeper_main_conf_t));
    if (NULL == retval) {
        ngx_log_stderr(0, "Fail to create main conf of nginx-zookeeper");
        return NULL;
    }

    retval->host.len = 0;
    retval->host.data = NULL;
    retval->cHost = NULL;
    retval->path.len = 0;
    retval->path.data = NULL;
    retval->cPath = NULL;
    retval->handle = NULL;
    retval->upstream_workload_path_ary = NULL;
    retval->work_load_update = NGX_CONF_UNSET_MSEC;

    ngx_rbtree_init(&retval->rbtree.rbtree, &retval->rbtree.sentinel,ngx_rbtree_insert_value);

    return retval;
}

// Init main conf
static char *ngx_http_zookeeper_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_zookeeper_main_conf_t *mf = conf;


    if (NULL == mf) {
        ngx_log_stderr(0, "Impossible conf");
        return NGX_CONF_ERROR;
    }

    // host
    if (mf->host.len <= 0)
        ngx_log_stderr(0, "WARNING: No zookeeper host was given");
    else {
        mf->cHost = malloc(mf->host.len + 1);
        if (NULL == mf->cHost) {
            ngx_log_stderr(0, "Fail to malloc for host");
            return NGX_CONF_ERROR;
        }
        memcpy(mf->cHost, mf->host.data, mf->host.len);
        mf->cHost[mf->host.len] = 0;
    }

    // path
    if (mf->path.len <= 0)
        ngx_log_stderr(0, "WARNING: No zookeeper path was given");
    else {

        int len = mf->path.len +sizeof("/")+cf->cycle->hostname.len;
        mf->cPath = malloc(len+1);

        if (NULL == mf->cPath) {
            ngx_log_stderr(0, "Fail to malloc for path");
            return NGX_CONF_ERROR;
        }

        char *start_path = mf->cPath;

        memcpy(mf->cPath, mf->path.data, mf->path.len);
        mf->cPath += mf->path.len;

        memcpy(mf->cPath,"/",1);
        mf->cPath += 1;

        memcpy(mf->cPath,cf->cycle->hostname.data,cf->cycle->hostname.len);

        mf->cPath = start_path;
        mf->cPath[len] = 0;
    }
    

    return NGX_CONF_OK;
}
