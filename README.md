# nginx_zookeeper
linux system info(cpu.memory,nginx connectings) sync to zookeeper ..
sysc upstream state from zookeeper..

Nginx Zookeeper Module
----

https://github.com/jasonsalex/nginx_zookeeper
e-mail:531401335@qq.com

Add zookeeper support for Nginx Server.

Requirements
====

* Zookeeper C Client

Install
====

    export LIBZOOKEEPER_PREFIX=/path/to/libzookeeper
    $ ./configure --add-module=/path/to/nginx-zookeeper
    $ make
    # make install

Configuration
====

* zookeeper_host

    CSV list of host:port values.

* zookeeper_path

    Which path will module create when Nginx server starts.

* zookeeper_workload_update
    system workload update time..
    	
* zookeeper_upstream_workload_path
    get upstream servsers workload data
    	
* gateway_gps_latitude
	set nginx gps  latitude info

* gateway_gps_longitude
    set nginx gps longitude info

* gateway_host_name
   set nginx public ip and port

* gateway_tls_ver
  set nginx ssl/tls version 
	
Examples
====

    zookeeper_path "/vargoservice/gateway";

    zookeeper_workload_update 10s;

    gateway_gps_latitude "10.200";

    gateway_gps_longitude "11.300";

    gateway_host_name "192.168.1.182:4001";

    gateway_tls_ver 10200;
