Homework: Nginx
===============

# Specification
`TAP` Header に指定した文字列が含まれる場合、指定した Origin Server に Proxy する。  
含まれない場合は yahoo.co.jp に Proxy する。  
`TAP` Header に含まれる文字列と、Origin Server に Proxy する文字列は http, server, location Context で利用できる Directive で指定する。

## Exercise
Origin Server を変更した場合は Response の最後に署名を記載する。  
e.g.

    <!-- Origin server is changed from yahoo.co.jp to fpr.yahoo.co.jp by name -->

# Module Description
`homework_target` directive が指定されている場合 `TAP` Header を検索して `homework_host` Nginx 変数に値を設定する。  
値が設定された場合 Response の最後に署名が表示される。

## Directive
`homework_target` という directive を指定することができる。  
この directive では引数を２つ取る
1. `TAP` Header の値を指定
1. Origin Server の Hostname を指定

e.g.

    homework_target test hello-world.com;

## Nginx Variable
この Module は Nginx 変数 `homework_host` を利用する。  
`homework_host` には `TAP` Header と `homework_target` の第一引数が一致した場合にのみ、`homework_target` の第二引数が設定される。

# Usage
## Build

    $ cd <nginx directory>
    $ ./configure --with-compat --add-dynamic-module=<git directory>
    $ make modules

## Install

    $ sudo cp <nginx directory>/objs/ngx_http_homework_module.so <your nginx module directory>

## Config Sample


    worker_processes  1;
    load_module modules/ngx_http_homework_module.so;

    events {
        worker_connections  1024;
    }

    http {
        default_type  application/octet-stream;
        sendfile        on;
        keepalive_timeout  65;

        # 動的に proxy_pass の値を変える場合は必須
        # /etc/resolv.conf に記載されている NameServer の IP を指定する
        resolver 8.8.8.8 valid=5s;

        server {
            listen       8080;
            server_name  localhost;

            # Default では yahoo.co.jp になるように設定する
            set $homework_host www.yahoo.co.jp;
            # TAP Header が test の時 hello-world.com に Access する
            homework_target test hello-world.com;

            location / {
                # Proxy 先を変数で指定
                proxy_pass http://$homework_host;
            }

            error_page   500 502 503 504  /50x.html;
            location = /50x.html {
                root   html;
            }
        }
    }


