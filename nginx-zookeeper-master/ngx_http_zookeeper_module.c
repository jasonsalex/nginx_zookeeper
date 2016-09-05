/**
 * Nginx Zookeeper
 *
 * @author Timandes White <timands@gmail.com>
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "zookeeper/zookeeper.h"
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

typedef struct{
    ngx_keyval_t path_ary;
    ngx_int_t hash;
}upstream_work_load_path;


// Configurations
typedef struct {
    ngx_str_t host;
    ngx_str_t path;

    ngx_str_t gateway_gpsLatitude;
    ngx_str_t gateway_gpsLongitude;

    ngx_str_t host_name;

    ngx_uint_t gateway_tls_ver;

    ngx_array_t *upstream_path_array;
    char *cHost;
    char *cPath;
    zhandle_t *handle;

    ngx_msec_t work_load_update;

} ngx_http_zookeeper_main_conf_t;


typedef struct{

    char ip[100];
    char ser_id[100];
    ngx_uint_t func_id;

}ngx_upstream_ip_t;

ngx_connection_t        dummy, zk_process_event_dummy;
ngx_event_t             timer, zk_process_event_timer;
CPU_OCCUPY              cpu_stat1,cpu_stat2;

int cpu_usage;
int is_connectd = 0;

static ngx_int_t ngx_http_zookeeper_init_module(ngx_cycle_t *cycle);
static void ngx_http_zookeeper_exit_master(ngx_cycle_t *cycle);

static void ngx_http_zookeeper_init_timer(ngx_msec_t time, void * call_back_p);
static void ngx_http_zookeeper_process_event_timer(ngx_socket_t fd,ngx_msec_t time, void * call_back_p);

static char *ngx_http_parse_upstream_work_load(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void *ngx_http_zookeeper_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_zookeeper_init_main_conf(ngx_conf_t *cf, void *conf);
static void ngx_http_zookeeper_watcher_master(zhandle_t *zh, int type,int state, const char *path,void *watcherCtx);
static void ngx_http_zookeeper_string_completion_t(int rc, const char *value, const void *data);

static void ngx_http_zookeeper_single_upstream_data_completion(int rc, const char *value, int value_len,
                                                 const struct Stat *stat, const void *data);

static void ngx_http_zookeeper_hash_upstream_data_completion(int rc, const char *value, int value_len,
                                                 const struct Stat *stat, const void *data);

static void ngx_http_zookeeper_upstream_workload_data_completion_t(int rc, const char *value, int value_len,
                                                 const struct Stat *stat, const void *data);


static void ngx_http_zookeeper_stat_completion_t(int rc, const struct Stat *stat,const void *data);
static void ngx_http_zookeeper_hash_upstream_child_completion_t(int rc,
                                                    const struct String_vector *strings, const void *data);

static void ngx_http_zookeeper_send_to_processes_data(ngx_upstream_ip_t *upstream, ngx_uint_t is_hash);
static void ngx_http_zookeeper_update_workload_to_zk(ngx_event_t *ev);
static void ngx_http_zookeeper_update_cpu_uage(ngx_event_t *ev);

static void ngx_http_zookeeper_process_event(ngx_event_t *ev);

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
        ngx_string("gateway_gps_latitude"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, gateway_gpsLatitude),
        NULL
    },


    {
        ngx_string("gateway_gps_longitude"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, gateway_gpsLongitude),
        NULL
    },

    {
        ngx_string("gateway_host_name"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, host_name),
        NULL
    },

    {
        ngx_string("gateway_tls_ver"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, gateway_tls_ver),
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
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE23,
        ngx_http_parse_upstream_work_load,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_zookeeper_main_conf_t, upstream_path_array),
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

    char num[10];
    ngx_memzero(num,10);

    char *start_path = zmf->cPath;

    memcpy(zmf->cPath, zmf->path.data, zmf->path.len);
    zmf->cPath += zmf->path.len;

    memcpy(zmf->cPath,"/",1);
    zmf->cPath += 1;

    memcpy(zmf->cPath,cycle->hostname.data,cycle->hostname.len);
    zmf->cPath += cycle->hostname.len;

    ngx_sprintf(num,"#%d",ngx_getpid());
    memcpy(zmf->cPath,num,sizeof(num));

    zmf->cPath = start_path;
   // mf->cPath[len] = 0;


    // init zookeeper
    zmf->handle = zookeeper_init(zmf->cHost, ngx_http_zookeeper_watcher_master, 10000, 0, NULL, 0);
    if (NULL == zmf->handle) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Fail to init zookeeper instance");
        ngx_log_stderr(0, "Fail to init zookeeper instance");
        return NGX_ERROR;
    }

    ngx_http_zookeeper_process_event_timer((ngx_socket_t)-2, 10, ngx_http_zookeeper_process_event);

    return NGX_OK;
}

static void ngx_http_zookeeper_process_event(ngx_event_t *ev)
{
       ngx_http_zookeeper_main_conf_t *zmf;

       zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_zookeeper_module);

       int fd, interest, events;

       fd_set rfds, wfds, efds;
       FD_ZERO(&rfds);
       FD_ZERO(&wfds);
       FD_ZERO(&efds);


       struct timeval tv;

       zookeeper_interest(zmf->handle, &fd, &interest, &tv);

       tv.tv_sec = 0;
       tv.tv_usec = 0;

       if (fd != -1) {
           if(interest&ZOOKEEPER_READ)
           {
               FD_SET(fd, &rfds);
           } else {
               FD_CLR(fd, &rfds);
           }

           if(interest&ZOOKEEPER_WRITE)
           {
               FD_SET(fd, &wfds);
           } else{
               FD_CLR(fd, &wfds);
           }
       } else{
           fd = 0;
       }

       if (select(fd+1, &rfds, &wfds, &efds, &tv) < 0)
       {
           printf("[%s %d]select failed, err=%d, msg=%s\n", __FUNCTION__, __LINE__, errno, strerror(errno));
       }

       events = 0;
       if (FD_ISSET(fd, &rfds))
       {
        events = ZOOKEEPER_READ;
       }
       if (FD_ISSET(fd, &wfds))
       {
           events = ZOOKEEPER_WRITE;
       }

       zookeeper_process(zmf->handle, events);

       ngx_http_zookeeper_process_event_timer((ngx_socket_t)-2, 10, ngx_http_zookeeper_process_event);
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

        ngx_add_timer(&timer, time);
}

static void ngx_http_zookeeper_process_event_timer(ngx_socket_t fd,ngx_msec_t time, void * call_back_p)
{
    zk_process_event_dummy.fd = fd;
    ngx_memzero(&zk_process_event_timer, sizeof(ngx_event_t));

    zk_process_event_timer.handler = call_back_p;
    zk_process_event_timer.log = ngx_cycle->log;
    zk_process_event_timer.data = &zk_process_event_dummy;

    ngx_add_timer(&zk_process_event_timer, time);
}


static char *ngx_http_parse_upstream_work_load(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{

    char  *p = conf;

    ngx_str_t         *value;
    ngx_array_t      **a;
    upstream_work_load_path      *kv;
    ngx_conf_post_t   *post;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NULL) {
        *a = ngx_array_create(cf->pool, 10, sizeof(upstream_work_load_path));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    kv = ngx_array_push(*a);
    if (kv == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    kv->path_ary.key = value[1];
    kv->path_ary.value = value[2];

    if(cf->args->nelts > 3)
    {
        if(ngx_strncmp(value[3].data,"hash",4) == 0)
        {
            kv->hash = 1;
        }else
        {
            kv->hash = 0;
        }
    }else
    {
        kv->hash = 0;
    }


    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, kv);
    }

    return NGX_CONF_OK;
}

static void ngx_http_zookeeper_watcher_master(zhandle_t *zh, int type, int state, const char *path,void *watcherCtx)
{
    ngx_http_zookeeper_main_conf_t *zmf;

    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_zookeeper_module);

    if(state == ZOO_CONNECTED_STATE && is_connectd == 0)
    {
        // create node
       zoo_acreate(zh, zmf->cPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL, ngx_http_zookeeper_string_completion_t, zmf);
       is_connectd = 1;

    }else if( state == ZOO_EXPIRED_SESSION_STATE)
    {
       // zmf->handle = zookeeper_init(zmf->cHost, ngx_http_zookeeper_watcher_master, 10000, 0, NULL, 0);
        if (NULL == zmf->handle) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "Fail to init zookeeper instance");
        }

        is_connectd = 0;
    }

    if(type == ZOO_CHANGED_EVENT || type == ZOO_CHILD_EVENT)
    {
        ngx_uint_t i;
        upstream_work_load_path *path;
        int rc;

        path = zmf->upstream_path_array->elts;

        for(i = 0; i<zmf->upstream_path_array->nelts; ++i)
        {
            if(path[i].hash == 0 && type == ZOO_CHANGED_EVENT)
                rc = zoo_aget(zmf->handle, (char*)path[i].path_ary.value.data, ZOO_CHANGED_EVENT, ngx_http_zookeeper_single_upstream_data_completion, &path[i].path_ary);
            else
                rc = zoo_aget_children(zmf->handle, (char*)path[i].path_ary.value.data, ZOO_CHILD_EVENT, ngx_http_zookeeper_hash_upstream_child_completion_t, &path[i].path_ary);

            if(rc != ZOK)
            {
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "get upstream workload data is failed:%V,error info:%s",&path[i].path_ary, zerror(rc));
                if(rc == ZCLOSING) zookeeper_close(zmf->handle);

            }
        }
    }

}

static void ngx_http_zookeeper_string_completion_t(int rc, const char *value, const void *data)
{

    ngx_http_zookeeper_main_conf_t *zmf = (ngx_http_zookeeper_main_conf_t *)data;
    ngx_uint_t i;
    upstream_work_load_path *path;

    if (ZOK != rc) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "Fail to create zookeeper node:%s",zerror(rc));
        if(rc == ZCLOSING) zookeeper_close(zmf->handle);

        zmf->handle = NULL;
    }else
    {
        ngx_http_zookeeper_init_timer(zmf->work_load_update, ngx_http_zookeeper_update_workload_to_zk);
        ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, "create zookeeper node:%s",value);

        path = zmf->upstream_path_array->elts;

        for(i = 0; i<zmf->upstream_path_array->nelts; ++i)
        {
            if(path[i].hash == 0)
                rc = zoo_aget(zmf->handle, (char*)path[i].path_ary.value.data, ZOO_CHANGED_EVENT, ngx_http_zookeeper_single_upstream_data_completion, &path[i].path_ary);
            else
                rc = zoo_aget_children(zmf->handle, (char*)path[i].path_ary.value.data, ZOO_CHILD_EVENT, ngx_http_zookeeper_hash_upstream_child_completion_t, &path[i].path_ary);

            if(rc != ZOK)
            {                
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "get upstream workload data is failed:%V,error info:%s",&path[i].path_ary, zerror(rc));
                if(rc == ZCLOSING) zookeeper_close(zmf->handle);

            }
        }
    }

}

static void ngx_http_zookeeper_single_upstream_data_completion(int rc, const char *value, int value_len,const struct Stat *stat, const void *data)
{

    ngx_http_zookeeper_main_conf_t *zmf;
    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_zookeeper_module);

    if(rc == ZOK)
    {
        ngx_uint_t func_id;
        ngx_keyval_t  *path = (ngx_keyval_t*)data;

        func_id = ngx_atoi(path->key.data, path->key.len);

        ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, "get single upstream, app_id:%d, chilren node:%s, node data:%s", func_id, path->value.data, value);

        cJSON *root = cJSON_Parse(value);

        if(root == NULL)
        {
            ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0, "prase json format is failed, app_id:%d, chilren node:%s, node data:%s", func_id, path->value.data, value);

            return;
        }

        cJSON *obj = cJSON_GetObjectItem(root, "machineName");

        ngx_upstream_ip_t upstream_ip;
        ngx_memzero(&upstream_ip, sizeof(upstream_ip));

        upstream_ip.func_id = func_id;

        ngx_memcpy(upstream_ip.ip, obj->valuestring, ngx_strlen(obj->valuestring));

        ngx_http_zookeeper_send_to_processes_data(&upstream_ip, 0);

        cJSON_Delete(root);

    }else
    {

        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "zookeeper error info:%s", zerror(rc));

        if(rc == ZCLOSING) zookeeper_close(zmf->handle);
    }
}

static void ngx_http_zookeeper_hash_upstream_data_completion(int rc, const char *value, int value_len,
                                                 const struct Stat *stat, const void *data)
{
    ngx_upstream_ip_t upstream_ip;
    char buf[80];
    int buf_len = sizeof(buf);


    ngx_keyval_t *path = (ngx_keyval_t*)data;
    ngx_uint_t func_id;

    func_id = ngx_atoi(path->key.data, path->key.len);


    ngx_memzero(&upstream_ip, sizeof(upstream_ip));
    ngx_memzero(buf, sizeof(buf));
    ngx_memcpy(buf, value, value_len);

    if(ngx_strcmp(buf, "Vargo-PC:21100") ==0) return;

    if( rc == ZOK)
    {
        upstream_ip.func_id = func_id;

        char * split = ngx_strchr(buf, ',');

        if(split != NULL)
        {
           split = strtok(buf, ",");
           ngx_memcpy(upstream_ip.ser_id, split, ngx_strlen(split));

           split = strtok(NULL, ",");
           ngx_memcpy(upstream_ip.ip, split, ngx_strlen(split));

        }else
        {
             ngx_memcpy(upstream_ip.ip, buf, buf_len);
        }

        ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, "get hash upstream node data, app_id:%V, node data:%s", &path->key, value);

        ngx_http_zookeeper_send_to_processes_data(&upstream_ip, 1);
    }
}


static void ngx_http_zookeeper_hash_upstream_child_completion_t(int rc, const struct String_vector *strings, const void *data)
{
    ngx_http_zookeeper_main_conf_t *zmf;
    zmf = (ngx_http_zookeeper_main_conf_t *)ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_zookeeper_module);

    if(rc == ZOK)
    {
        ngx_int_t i = 0;
        ngx_keyval_t *path = (ngx_keyval_t*)data;

        for( i= 0; i < strings->count; ++i)
        {

            char cPath[80];

            ngx_memzero(cPath, sizeof(cPath));

            strcat(cPath, (char*)path->value.data);
            strcat(cPath, "/");
            strcat(cPath, strings->data[i]);

            int rc = zoo_aget(zmf->handle, cPath, ZOO_CHANGED_EVENT, ngx_http_zookeeper_hash_upstream_data_completion, data);

            if(rc == ZOK)
            {
                ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, "get hash upstream, app_id:%V, chilren node:%s", &path->key, cPath);

            }else
            {
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "zookeeper error info:%s, app_id:%V, chilren node:%s", zerror(rc), &path->key, cPath);

                if(rc == ZCLOSING) zookeeper_close(zmf->handle);
            }

        }


    }else
    {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "zookeeper error info:%s", zerror(rc));
        if(rc == ZCLOSING) zookeeper_close(zmf->handle);
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

       cJSON *cpuObj, *memObj, *conObj, *hostObj, *gpsLatitude, *gpsLongitude, *tls_ver;

       cpuObj = cJSON_GetObjectItem(obj,"cpu");
       memObj = cJSON_GetObjectItem(obj,"mem");
       conObj = cJSON_GetObjectItem(obj,"con_num");
       hostObj = cJSON_GetObjectItem(obj,"host_name");
       gpsLatitude = cJSON_GetObjectItem(obj,"gpsLatitude");
       gpsLongitude = cJSON_GetObjectItem(obj, "gpsLongitude");
       tls_ver = cJSON_GetObjectItem(obj,"tls_ver");

       if(cpuObj == NULL)
            cJSON_AddItemToObject(obj, "cpu", cpuObj = cJSON_CreateNumber(0));

       if(memObj == NULL)
            cJSON_AddItemToObject(obj, "mem", memObj = cJSON_CreateNumber(0));

       if(conObj == NULL)
           cJSON_AddItemToObject(obj, "con_num", conObj = cJSON_CreateNumber(0));

       if(hostObj == NULL)
           cJSON_AddItemToObject(obj, "host_name", hostObj = cJSON_CreateString((char*)zmf->host_name.data));

       if(gpsLatitude == NULL)
           cJSON_AddItemToObject(obj, "gpsLatitude", gpsLatitude = cJSON_CreateString((char*)zmf->gateway_gpsLatitude.data));

       if(gpsLongitude == NULL)
           cJSON_AddItemToObject(obj, "gpsLongitude", gpsLongitude = cJSON_CreateString((char*)zmf->gateway_gpsLongitude.data));

       if(tls_ver == NULL)
           cJSON_AddItemToObject(obj, "tls_ver", tls_ver = cJSON_CreateNumber(zmf->gateway_tls_ver));


       MEM_OCCUPY  mem_stat;
       ngx_get_memoccupy(&mem_stat);

       float memUsage = (mem_stat.total-(mem_stat.free+mem_stat.buffers+mem_stat.cached))/(float)mem_stat.total;

       cJSON_SetNumberValue(cpuObj, cpu_usage/100);
       cJSON_SetNumberValue(memObj, memUsage*100);
       cJSON_SetNumberValue(conObj, ngx_cycle->connection_n - ngx_cycle->free_connection_n);

       char * json_buf = cJSON_PrintUnformatted(root);

       zoo_aset(zmf->handle, zmf->cPath, json_buf, ngx_strlen(json_buf), -1, ngx_http_zookeeper_stat_completion_t, zmf);

       cJSON_Delete(root);

    }else
    {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "zookeeper error info:%s", zerror(rc));
        if(rc == ZCLOSING) zookeeper_close(zmf->handle);
    }

}


static void ngx_http_zookeeper_stat_completion_t(int rc, const struct Stat *stat,const void *data)
{

    if(rc == ZOK)
    {
        //ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,"update to upstream workload!");

    }else
    {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,"update to upstream workload is failed,error info:%s",zerror(rc));
        if(rc == ZCLOSING) zookeeper_close(data);

    }


}



static void ngx_http_zookeeper_send_to_processes_data(ngx_upstream_ip_t *upstream, ngx_uint_t is_hash)
{
    sleep(1);
    ngx_tcp_link_to_upstream(upstream->func_id, upstream->ip, upstream->ser_id, is_hash);
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

        rc = zoo_aget(zmf->handle,zmf->cPath, 0, ngx_http_zookeeper_upstream_workload_data_completion_t, zmf);

        if(rc != ZOK)
        {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "get gateway path is failed, %s,error info:%s",zmf->cPath,zerror(rc));
            if(rc == ZCLOSING) zookeeper_close(zmf->handle);
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


    retval->cHost = NULL;
    retval->cPath = NULL;
    retval->handle = NULL;
    retval->upstream_path_array = NULL;
    retval->work_load_update = NGX_CONF_UNSET_MSEC;
    retval->gateway_tls_ver = NGX_CONF_UNSET_UINT;

    ngx_str_null(&retval->gateway_gpsLatitude);
    ngx_str_null(&retval->gateway_gpsLongitude);
    ngx_str_null(&retval->host_name);
    ngx_str_null(&retval->path);
    ngx_str_null(&retval->host);

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

        char num[10];
        ngx_memzero(num,10);

        int len = mf->path.len +sizeof("/")+cf->cycle->hostname.len+sizeof(num);
        mf->cPath = malloc(len+1);

        if (NULL == mf->cPath) {
            ngx_log_stderr(0, "Fail to malloc for path");
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}
