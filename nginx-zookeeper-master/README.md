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
    	
	
Examples
====

    zookeeper_host "192.168.0.2:2181,192.168.0.3:2181"
    zookeeper_path "/nginx/foo"
    zookeeper_workload_update 1s;
    zookeeper_upstream_workload_path  500 "/service/min";
