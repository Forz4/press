#include "include/press.h"
#include "include/log.h"

sigjmp_buf  jmpbuf;             /* 全局跳转结构 */
char        PIDFILE[200];       /* PIDFILE name*/
char        CFGPATH[200];
int         PORT_CMD = 0;       /* 命令监听端口 */
int         QID_MSG = 0;        /* 报文传输队列 */
int         HEART_INTERVAL;     /* 心跳时间间隔 */
char        ENCODING;           /* A代表ascii码，H代表十六进制*/
int         sock_send = 0;      /* 发送套接字 */
int         sock_recv = 0;      /* 接收套接字 */
int         pidSend = 0;        /* 外卡模式下的双工发送子进程ID */
int         pidRecv = 0;        /* 外卡模式下的双工接收子进程ID */
int         g_shmid = 0;        /* 共享内存全局状态区ID */
int         g_mon_shmid = 0;    /* 共享内存实时监控区ID */
stat_st    *g_stat;             /* 全局状态区指针 */
monstat_st *g_mon_stat;         /* 实时状态区指针*/
int         g_mon_semid = 0;    /* 全局信号量用于保护g_mon_shmid*/
conn_config_st *p_conn_conf = NULL;            /*configs for conn*/
pack_config_st *p_pack_conf = NULL;            /*configs for pack*/

/* for calculating real tps */
int         lastTimeSendNum;    /* 上次查询时总发送笔数 */
int         lastTimeRecvNum;    /* 上次查询时总接收笔数 */
struct timeval lastTimeStamp;   /* 上次查询时的时间戳 */

/*
 * 加载/初始化通讯配置
 * */
int conn_config_load(conn_config_st *p_conn_conf)
{
    FILE *fp = NULL;
    char pathname[MAX_PATHNAME_LEN];
    char line[200];
    char buf[100];
    comm_proc_st *cur = NULL;
    int  count = 0;

    if (p_conn_conf == NULL){
        return -1;
    }
    /*load connection config*/
    memset(pathname , 0x00 , sizeof(pathname));
    sprintf(pathname , "%s/cfg/conn.cfg" , getenv("PRESS_HOME"));
    fp = fopen(pathname , "r");
    if ( fp == NULL ){
        log_write(SYSLOG , LOGERR , "调用fopen失败，conn.cfg 配置文件未找到");
        return -1;
    }

    p_conn_conf->process_head = NULL;

    while (fgets(line , sizeof(line), fp) != NULL){

        if ( line[0] == '\n' || line[0] == '#' )
            continue;

        cur = (comm_proc_st *)malloc(sizeof(comm_proc_st));
        if ( cur == NULL ){
            log_write(SYSLOG , LOGERR , "malloc for comm_proc_st 失败");
            fclose(fp);
            return -1;
        }

        /* 类型 */
        if (get_bracket(line , 1 , buf , 100)){
            log_write(SYSLOG , LOGERR , "读取 conn.cfg 的第1域[类型]错误");
            fclose(fp);
            return -1;
        }
        cur->type = buf[0];

        /* IP */
        if (get_bracket(line , 2 , buf , 100)){
            log_write(SYSLOG , LOGERR , "读取 conn.cfg 的第2域[IP]错误");
            fclose(fp);
            return -1;
        }
        strncpy(cur->ip , buf , sizeof(cur->ip));

        /* 端口 */
        if (get_bracket(line , 3 , buf , 100)){
            log_write(SYSLOG , LOGERR , "读取 conn.cfg 的第3域[端口]错误");
            fclose(fp);
            return -1;
        }
        cur->port = atoi(buf);

        /* 持久化开关 */
        if (get_bracket(line , 4 , buf , 100)){
            log_write(SYSLOG , LOGERR , "读取 conn.cfg 的第4域[持久化开关]错误");
            fclose(fp);
            return -1;
        }
        cur->persist = atoi(buf);

        /* 短链接并行度 */
        if (get_bracket(line , 5 , buf , 100)){
            cur->parallel = 1;
        } else {
            cur->parallel = atoi(buf);
        }

        /* 心跳包间隔 */
        if (get_bracket(line , 6 , buf , 100)){
            HEART_INTERVAL=0;
        } else {
            HEART_INTERVAL=atoi(buf);
        }

        /* 持久化编码 */
        if (get_bracket(line , 7 , buf , 100)){
            ENCODING='H';
        } else if ( buf[0] != 'H' && buf[0] != 'A' ){
            ENCODING='H';
        } else {
            ENCODING=buf[0];
        }

        count ++;
        cur->next = p_conn_conf->process_head;
        p_conn_conf->process_head = cur;
    }

    if ( count == 0 ){
        log_write(SYSLOG , LOGERR , "conn.cfg 配置文件为空");
        fclose(fp);
        return 0;
    }

    fclose(fp);
    p_conn_conf->status = 1;
    return 0;
}

/*
 * 释放通讯配置结构
 * */
void conn_config_free(conn_config_st *p_conn_conf)
{
    comm_proc_st *cur , *nex;
    if ( p_conn_conf == NULL)
        return; 

    cur = p_conn_conf->process_head;

    while (cur != NULL)
    {
        nex = cur->next;
        if ( cur )
            free(cur);
        cur = nex;
    }
    return ;
}

/*
 * 启动通讯进程组
 * */
int conn_start(conn_config_st *p_conn_conf)
{
    int ret = 0;
    log_write(SYSLOG , LOGINF , "开始启动通讯模块");
    ret = conn_config_load(p_conn_conf);
    if (ret < 0){
        log_write(SYSLOG , LOGERR , "无法加载 conn.cfg");
        exit(1);
    }
    log_write(SYSLOG , LOGDBG , "配置文件加载完成");

    comm_proc_st *cur = p_conn_conf->process_head;

    if (p_conn_conf == NULL || p_conn_conf->process_head == NULL)
        return -1;

    while (cur != NULL){
        if ( cur->type == 'R' ){
            ret = conn_receiver_start(cur);
            if ( ret < 0 ){
                log_write(SYSLOG , LOGERR , "通讯接收进程启动失败 , IP[%s] , PORT[%d]",cur->ip , cur->port);
            } else {
                log_write(SYSLOG , LOGINF , "通讯接收进程启动成功 , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
                cur->pid = ret;
            }

        } else if ( cur->type == 'S' ){
            ret = conn_sender_start(cur);
            if ( ret < 0 ){
                log_write(SYSLOG , LOGERR , "通讯发送进程启动失败 , IP[%s] , PORT[%d]",cur->ip , cur->port);
            } else {
                log_write(SYSLOG , LOGINF , "通讯发送进程启动成功 , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
                cur->pid = ret;
            }
        } else if ( cur->type == 'J' ){
            ret = conn_jips_start(cur);
            if ( ret < 0 ){
                log_write(SYSLOG , LOGERR , "外卡通讯进程启动成功 , IP[%s] , PORT[%d]",cur->ip , cur->port);
            } else {
                log_write(SYSLOG , LOGINF , "外卡通讯进程启动失败 , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
                cur->pid = ret;
            }
        } else if ( cur->type == 'X' ){
            int i = 0 ;
            for ( i = 0 ; i < cur->parallel ; i ++ ){
                ret = conn_sender_start(cur);
                if ( ret < 0 ){
                    log_write(SYSLOG , LOGERR , "短链接通讯进程启动失败 , IP[%s] , PORT[%d]",cur->ip , cur->port);
                } else {
                    log_write(SYSLOG , LOGINF , "短链接通讯送进程启动成功 , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
                    cur->para_pids[i] = ret;
                }
            }
        }
        cur = cur->next;
    }
    p_conn_conf->status = RUNNING;
    return 0;
}
/*
 * 启动通讯接收进程
 * */
int conn_receiver_start(comm_proc_st *p_receiver)
{
    int pid;
    pid = fork();
    if ( pid < 0 ){
        return -1;
    } else if ( pid > 0 ){
        return pid;
    }

    log_write(CONLOG , LOGINF , "长链接接收进程启动 , PORT[%d]" , p_receiver->port);
    signal( SIGTERM , conn_receiver_signal_handler);

    /* 服务端套接字 */
    int server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
    struct sockaddr_in server_sockaddr;
    /* 客户端套接字 */
    char buffer[MAX_LINE_LEN];
    struct sockaddr_in client_addr;
    socklen_t socket_len = sizeof(client_addr);
    /* 发送控制变量 */
    int recvlen = 0;
    int textlen = 0;
    int nTotal = 0;
    int nRead = 0;
    int nLeft = 0;
    msg_st msgs;
    int nTranlen = 0;
    int ret = 0;
    struct timeval ts;

    /* bind 端口*/
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(p_receiver->port);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sockfd , (struct sockaddr *)&server_sockaddr , sizeof(server_sockaddr)) == -1)
    {
        log_write(CONLOG , LOGERR , "长链接接收进程[%d] bind 失败" , p_receiver->port);
        exit(1);
    }
    log_write(CONLOG , LOGDBG , "长链接接收进程[%d] bind 成功" , p_receiver->port);

    /* 启动监听 */
    if(listen(server_sockfd , 1) == -1){
        log_write(CONLOG , LOGERR , "长链接接收进程[%d]监听失败" , p_receiver->port);
        exit(1);
    }
    log_write(CONLOG , LOGDBG , "长链接接收进程[%d]监听成功" , p_receiver->port);

    /* 接受远端连接 */
    sock_recv = accept(server_sockfd , (struct sockaddr *)&client_addr , &socket_len);
    if (sock_recv < 0){
        log_write(CONLOG , LOGERR , "长链接接收进程[%d] accept 失败" , p_receiver->port);
        exit(1);
    }
    log_write(CONLOG , LOGDBG , "长链接接收进程[%d] accept 成功" , p_receiver->port);

    g_mon_stat = (monstat_st *)shmat(g_mon_shmid , NULL , 0);
    if ( g_mon_stat == NULL ){
        log_write(CONLOG , LOGERR , "调用shmat失败, id[%d]" , g_mon_shmid);
        return -1;
    }

    while(1){
        memset(buffer , 0x00 , sizeof(buffer));
        memset(&msgs , 0x00 , sizeof(msg_st));

        log_write(CONLOG , LOGDBG , "长链接接收进程[%d]开始recv",p_receiver->port);
        /*read length of transaction*/
        recvlen = recv(sock_recv , buffer , 4 , 0);
        if (recvlen < 0){
            break;
        } else if ( strncmp(buffer , "0000" , 4) == 0){
            continue;
        } else {
            textlen = atoi(buffer);
            nTotal = 0;
            nRead = 0;
            nLeft = textlen;
            while(nTotal != textlen){
                nRead = recv(sock_recv , buffer + 4 + nTotal , nLeft , 0);
                if (nRead == 0)    break;
                nTotal += nRead;
                nLeft -= nRead;
            }
        }
        log_write(CONLOG , LOGDBG , "长链接接收进程[%d]收到报文,textlen[%d]",p_receiver->port,textlen);

        sem_lock(g_mon_semid);
        g_mon_stat->recv_num ++;
        sem_unlock(g_mon_semid);

        log_write(CONLOG , LOGDBG , "长链接接收进程[%d]更新接收统计数字完成",p_receiver->port);

        if ( p_receiver->persist == 1 ){
            nTranlen = get_length(buffer);
            if ( nTranlen < 0 ){
                log_write(CONLOG , LOGERR , "长链接接收进程获取响应报文长度失败");
            } else {
                memcpy(msgs.text , buffer , nTranlen + 4);
                gettimeofday( &ts , NULL );
                ret = persist( buffer , nTranlen + 4 , LONG_RECV , ts);
                if (ret < 0){
                    log_write(CONLOG , LOGERR , "通讯接收进程调用持久化失败");
                }
            }
        }
    }
    close(sock_recv);
    return 0;
}
/*
 * 通讯接收进程信号处理函数
 * */
void conn_receiver_signal_handler(int no)
{
    log_write(CONLOG , LOGDBG , "长链接接收进程进入信号处理函数退出");
    close(sock_recv);
    exit(0);
}
/*
 * 启动通讯发送进程
 * */
int conn_sender_start(comm_proc_st *p_sender)
{
    int pid;
    pid = fork();
    if ( pid < 0 ){
        return -1;
    } else if ( pid > 0 ){
        return pid;
    }

    log_write(CONLOG , LOGINF , "通讯发送进程[%c:%s:%d]启动" ,p_sender->type ,  p_sender->ip , p_sender->port);
    signal( SIGTERM , conn_sender_signal_handler);

    struct sockaddr_in servaddr;
    msg_st msgs;
    int ret;
    int len = 0;
    int nTotal = 0;
    int nSent = 0;
    int nLeft = 0;
    struct timeval ts;

    /* 短链接读使用 */
    int recvlen = 0;
    int textlen = 0;
    int nRead = 0;
    int nTranlen = 0;
    char buffer[MAX_LINE_LEN];

    /* relocate share memory */
    g_mon_stat = (monstat_st *)shmat(g_mon_shmid , NULL , 0);
    if ( g_mon_stat == NULL ){
        log_write(SYSLOG , LOGERR , "调用shmat失败,shmid[%d]" , g_mon_shmid);
        return -1;
    }

    int connected = 0;
    while(1){
        /* 链接 */
        if ( connected == 0 || p_sender->type == 'X' ){
            connected = 1;
            sock_send = socket(AF_INET , SOCK_STREAM , 0);
            memset(&servaddr , 0 , sizeof(servaddr));
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(p_sender->port);
            servaddr.sin_addr.s_addr = inet_addr(p_sender->ip);
            while (1){
                if (connect(sock_send , (struct sockaddr*)&servaddr , sizeof(servaddr)) < 0){
                    close(sock_send);
                    sock_send = socket(AF_INET , SOCK_STREAM , 0);
                    sleep(1);
                    continue;
                }
                break;
            }
        }
        log_write(CONLOG , LOGDBG , "通讯发送进程[%c:%s:%d] connect 成功" ,p_sender->type ,  p_sender->ip , p_sender->port);

        if (HEART_INTERVAL > 0){
            sigsetjmp(jmpbuf , 1);
            signal(SIGALRM , send_idle);
            alarm(HEART_INTERVAL);
        }

        log_write(CONLOG , LOGDBG , "通讯发送进程[%c:%s:%d] 开始msgrcv" ,p_sender->type ,  p_sender->ip , p_sender->port);

        ret = msgrcv(    (key_t)QID_MSG , \
                        &msgs , \
                        sizeof(msg_st) - sizeof(long) , \
                        0, \
                        0 );
        if ( ret < 0 ){
            log_write(CONLOG , LOGERR , "通讯发送进程调用msgrcv失败,ret[%d],msgid[%d]",ret,QID_MSG);
            close(sock_send);
            exit(1);
        }
        if (HEART_INTERVAL > 0){
            alarm(0);
        }
        log_write(CONLOG , LOGDBG , "通讯发送进程[%c:%s:%d]从队列读到报文,len[%d]" ,p_sender->type ,  p_sender->ip , p_sender->port , msgs.length);

        len = msgs.length;
        nTotal = 0;
        nSent = 0;
        nLeft = len;
        while( nTotal != len){
            nSent = send(sock_send , msgs.text + nTotal , nLeft , 0);
            if (nSent == 0){
                break;
            }
            else if (nSent < 0){
                log_write(CONLOG , LOGERR , "通讯发送进程调用send失败,nSent[%d],errno[%d]" , nSent , errno);
                close(sock_send);
                exit(0);
            }
            nTotal += nSent;
            nLeft -= nSent;
        }
        log_write(CONLOG , LOGDBG , "通讯发送进程[%c:%s:%d] send 成功" ,p_sender->type ,  p_sender->ip , p_sender->port);
        
        sem_lock(g_mon_semid);
        g_mon_stat->send_num ++;
        sem_unlock(g_mon_semid);

        log_write(CONLOG , LOGDBG , "通讯发送进程[%c:%s:%d] 更新统计发送笔数成功" ,p_sender->type ,  p_sender->ip , p_sender->port);

        if ( p_sender->persist == 1 ){
            gettimeofday(&ts , NULL);
            if ( p_sender->type == 'S' )
                msgs.type = LONG_SEND;
            else if ( p_sender->type == 'X' )
                msgs.type = SHORTCONN;
            ret = persist(msgs.text , msgs.length , msgs.type , ts);
            if (ret < 0){
                log_write(SYSLOG , LOGERR , "通讯发送进程持久化失败");
            }
        }
        
/*=========== 短链接的逻辑 ==============*/
        if ( p_sender->type == 'X' ){
            log_write(CONLOG , LOGDBG , "短链接接受进程[%c:%s:%d] 开始recv" ,p_sender->type ,  p_sender->ip , p_sender->port);
            memset( buffer , 0x00 , sizeof(buffer) );
            recvlen = recv(sock_send , buffer , 4 , 0);
            if (recvlen < 0){
                break;
            } else if ( strncmp(buffer , "0000" , 4) == 0){
                continue;
            } else {
                textlen = atoi(buffer);
                log_write(CONLOG , LOGDBG , "短链接接受进程[%c:%s:%d] recv成功,buffer[%02x|%02x|%02x|%02x]" ,\
                    p_sender->type,p_sender->ip,p_sender->port,buffer[0],buffer[1],buffer[2],buffer[3]);
                nTotal = 0;
                nRead = 0;
                nLeft = textlen;
                while(nTotal != textlen){
                    nRead = recv(sock_send , buffer + 4 + nTotal , nLeft , 0);
                    if (nRead == 0)    break;
                    nTotal += nRead;
                    nLeft -= nRead;
                }
            }

            sem_lock(g_mon_semid);
            g_mon_stat->recv_num ++;
            sem_unlock(g_mon_semid);

            log_write(CONLOG , LOGDBG , "短链接接受进程[%c:%s:%d]更新统计数字成功" ,p_sender->type ,  p_sender->ip , p_sender->port);

            if ( p_sender->persist == 1 ){
                nTranlen = get_length(buffer);
                if ( nTranlen < 0 ){
                    log_write(CONLOG , LOGERR , "短链接接收进程获取响应报文长度失败" );
                } else {
                    gettimeofday( &ts , NULL );
                    ret = persist(buffer , nTranlen + 4 , SHORTCONN , ts);
                    if (ret < 0){
                        log_write(CONLOG , LOGERR , "通讯接收进程持久化失败" );
                    }
                }
            }
            close( sock_send );
        }
/*=========== 短链接的逻辑 ==============*/
    }
    close(sock_send);
    return 0;
}
/* 
 * 通讯发送进程信号处理函数
 * */
void conn_sender_signal_handler(int no)
{
    close(sock_send);
    exit(0);
}
/*
 * 启动外卡通讯进程
 * */
int conn_jips_start(comm_proc_st *p_jips)
{
    int pid;
    pid = fork();
    if ( pid < 0 ){
        return -1;
    } else if ( pid > 0 ){
        return pid;
    }

    log_write(CONLOG , LOGINF , "外卡通讯进程[%d]启动" , p_jips->port);

    /* 服务端套接字 */
    int server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
    struct sockaddr_in server_sockaddr;
    /* 客户端套接字 */
    char buffer[MAX_LINE_LEN];
    struct sockaddr_in client_addr;
    socklen_t socket_len = sizeof(client_addr);
    /* 发包控制变量 */
    int len = 0;
    char lenAscii[5];
    int recvlen = 0;
    int textlen = 0;
    int nTotal = 0;
    int nRead = 0;
    int nSent = 0;
    int nLeft = 0;
    msg_st msgs;
    int ret = 0;
    struct timeval ts;

    /*bind listen accectp*/
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(p_jips->port);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sockfd , (struct sockaddr *)&server_sockaddr , sizeof(server_sockaddr)) == -1)
    {
        log_write(CONLOG , LOGERR , "外卡通讯进程[%d] bind 失败" , p_jips->port);
        exit(1);
    }
    log_write(CONLOG , LOGDBG , "外卡通讯进程[%d] bind 成功" , p_jips->port);

    if(listen(server_sockfd , 1) == -1){
        log_write(CONLOG , LOGERR , "外卡通讯进程[%d] listen 失败" , p_jips->port);
        exit(1);
    }
    log_write(CONLOG , LOGDBG , "外卡通讯进程[%d] listen 成功" , p_jips->port);

    sock_recv = accept(server_sockfd , (struct sockaddr *)&client_addr , &socket_len);
    if (sock_recv < 0){
        log_write(SYSLOG , LOGERR , "外卡通讯进程[%d] accept 失败" , p_jips->port);
        exit(1);
    }
    log_write(CONLOG , LOGDBG , "外卡通讯进程[%d] accept 成功" , p_jips->port);

    /* start receiving process */
    pidRecv = fork();
    if ( pidRecv == 0 ){
        log_write(CONLOG , LOGINF , "外卡通讯进程[%d] 接收子进程启动" , p_jips->port);
        g_mon_stat = (monstat_st *)shmat(g_mon_shmid , NULL , 0);
        if ( g_mon_stat == NULL ){
            log_write(CONLOG , LOGERR , "外卡通讯进程调用shmat失败 , shmid[%d]" , g_mon_shmid);
            return -1;
        }

        while(1){
            memset(buffer , 0x00 , sizeof(buffer));
            memset(&msgs , 0x00 , sizeof(msg_st));

            log_write(CONLOG , LOGDBG , "外卡通讯进程[%d]接收子进程开始recv" , p_jips->port);
            recvlen = recv(sock_recv , buffer , 4 , 0);
            if (recvlen < 0){
                break;
            } else if ( strncmp(buffer , "0000" , 4) == 0){
                continue;
            } else {
                textlen = buffer[2]*16*16*16 + buffer[3]*16*16+ buffer[0]*16 + buffer[1]*1 ;
                nTotal = 0;
                nRead = 0;
                nLeft = textlen;
                while(nTotal != textlen){
                    nRead = recv(sock_recv , buffer + 4 + nTotal , nLeft , 0);
                    if (nRead == 0)    break;
                    nTotal += nRead;
                    nLeft -= nRead;
                }
            }

            log_write(CONLOG , LOGDBG , "外卡通讯进程[%d]接收子进程收到报文,textlen[%d]" , p_jips->port , textlen);
            
            sem_lock(g_mon_semid);
            g_mon_stat->recv_num ++;
            sem_unlock(g_mon_semid);

            if ( p_jips->persist == 1 ){
                msgs.type = 1;
                memcpy(msgs.text , buffer , textlen + 4);
                sprintf( lenAscii , "%4d" , textlen);
                memcpy( msgs.text , lenAscii , 4);

                gettimeofday( &ts , NULL );
                ret = persist( msgs.text , textlen + 4 , JIPSCONN , ts);
                if (ret < 0){
                    log_write(SYSLOG , LOGERR , "外卡通讯进程持久化失败");
                }
            }
        }
    } else if ( pidRecv < 0 ){
        return -1;
    }

    /* start jips sender process */
    pidSend = fork();
    if ( pidSend == 0 ){
        log_write(CONLOG , LOGINF , "外卡通讯进程[%d]发送子进程启动" , p_jips->port);
        while(1){
            if (HEART_INTERVAL > 0){
                sigsetjmp(jmpbuf , 1);
                signal(SIGALRM , send_idle);
                alarm(HEART_INTERVAL);
            }
            ret = msgrcv(    (key_t)QID_MSG , \
                            &msgs , \
                            sizeof(msg_st) - sizeof(long) , \
                            0, \
                            0 );
            if (HEART_INTERVAL > 0){
                alarm(0);
            }
            
            len = get_length(msgs.text);

            log_write(CONLOG , LOGDBG , "外卡通讯进程[%d]发送子进程读取到报文,len[%d]" , p_jips->port , len);

            nTotal = 4;
            nSent = 0;
            nLeft = len;
            while( nTotal != len + 4){
                nSent = send(sock_recv , msgs.text + nTotal , nLeft , 0);
                if (nSent == 0){
                    break;
                }
                else if (nSent < 0){
                    log_write(SYSLOG , LOGERR , "外卡通讯进程调用send失败,nSentt[%d],errno[%d]",nSent,errno);
                    close(sock_recv);
                    exit(0);
                }
                nTotal += nSent;
                nLeft -= nSent;
            }
            log_write(CONLOG , LOGDBG , "外卡通讯进程[%d]发送子进程发送报文成功,len[%d]" , p_jips->port , len);

            sem_lock(g_mon_semid);
            g_mon_stat->send_num ++;
            sem_unlock(g_mon_semid);

            if ( p_jips->persist == 1 ){
                gettimeofday(&ts , NULL);
                ret = persist(msgs.text , msgs.length+4 , JIPSCONN , ts);
                if (ret < 0){
                    log_write(SYSLOG , LOGERR , "外卡通讯进程持久化失败");
                }
            }
        }

    } else if ( pidSend < 0 ){
        return -1;
    }

    /*parent sleep and wait for signal */
    close(sock_recv);
    signal( SIGTERM , conn_jips_signal_handler);
    log_write(CONLOG , LOGINF , "外卡通讯发送进程启动成功 , pidSend[%d]" , pidSend);
    log_write(CONLOG , LOGINF , "外卡通讯接收进程启动成功 , pidRecv[%d]" , pidRecv);
    
    while(1){
        sleep(10);
        if ( kill( pidRecv , 0 ) == 0 && kill( pidSend , 0) == 0 ){
            log_write(CONLOG , LOGINF , "外卡通讯进程工作正常 port[%d], pidRecv[%d] , pidSend[%d]" , p_jips->port , pidRecv , pidSend);
            continue;
        } else {
            kill( pidRecv , 9);
            kill( pidSend , 9);
            log_write(CONLOG , LOGERR , "外卡通讯进程工作异常，强行退出进程pidRecv[%d] , pidSend[%d]" , pidRecv , pidSend);
            exit(-1);
        }
    }
}
/*
 * 外卡通讯进程信号处理函数
 * */
void conn_jips_signal_handler(int signo)
{
    kill( pidRecv , SIGTERM);
    kill( pidSend , SIGTERM);
    log_write(CONLOG , LOGINF , "外卡通讯进程退出, pidRecv[%d] , pidSend[%d]" , pidRecv , pidSend);
    exit(0);
}
/* 停止通讯模块 */
int conn_stop(conn_config_st *p_conn_conf)
{
    comm_proc_st *cur;

    if (p_conn_conf == NULL){
        return -1;
    }

    cur = p_conn_conf->process_head;
    while (cur != NULL){
        log_write(SYSLOG , LOGINF , "停止通讯进程 pid[%d]", cur->pid);
        if ( cur->type == 'X' ){
            int i = 0;
            for ( i = 0 ;i < cur->parallel ; i ++ ){
                if (cur->para_pids[i] > 0 )
                    kill(cur->para_pids[i] , SIGTERM);
            }
        } else {
            if ( cur->pid > 0 )
                kill(cur->pid , SIGTERM);
        }
        cur=cur->next;
    }
    p_conn_conf->status = NOTLOADED;
    return 0;
}
/*
 * 加载组包进程配置
 * */
int pack_config_load( char *filename , pack_config_st *p_pack_conf)
{
    char     config_fn[MAX_FILENAME_LEN];
    FILE    *fp = NULL;
    char     line[200];
    char     buf[100];
    int     count = 0;
    char     pathname[MAX_PATHNAME_LEN];

    log_write(SYSLOG , LOGINF , "开始加载组包进程配置");
    p_pack_conf->pit_head = NULL;
    pit_proc_st    *pit_cur = NULL;
    memset(config_fn , 0x00 , sizeof(config_fn));

    /* clear previous configs first */
    if ( p_pack_conf )
        pack_config_free(p_pack_conf);
    
    /* load pitcher processes */
    memset(pathname , 0x00 , sizeof(pathname));
    sprintf(pathname , "%s/cfg/%s" , getenv("PRESS_HOME") , filename);
    fp = fopen(pathname , "r");
    if ( fp == NULL ){
        log_write(SYSLOG , LOGERR , "组包进程配置文件[%s]未找到" , filename);
        return -1;
    }

    while (fgets(line , sizeof(line), fp) != NULL) {

        if ( line[0] == '\n' || line[0] == '#' )
            continue;

        log_write(SYSLOG , LOGDBG , "pack.cfg 记录[%s]" , line);

        pit_cur = (pit_proc_st *)malloc(sizeof(pit_proc_st));
        if ( pit_cur == NULL ){
            log_write(SYSLOG , LOGERR , "内部错误:malloc for pit_proc_st失败");
            fclose(fp);
            return -1;
        }

        pit_cur->index = count;

        /* get tpl file */
        if (get_bracket(line , 1 , buf , 100)){
            log_write(SYSLOG , LOGERR , "pack.cfg第1域格式错误");
            fclose(fp);
            return -1;
        }
        log_write(SYSLOG , LOGDBG , "读取第1域模板文件名[%s]" ,buf );

        strcpy(pit_cur->tplFileName , buf);

        memset(pathname , 0x00 , sizeof(pathname));
        sprintf(pathname , "%s/data/tpl/%s" , getenv("PRESS_HOME") , buf);
        pit_cur->tpl_fp = fopen(pathname , "r");
        if ( pit_cur->tpl_fp == NULL ){
            log_write(SYSLOG , LOGERR , "无法找到模板文件[%s]",pathname);
            fclose(fp);
            return -1;
        }

        /* get rule */
        if (get_bracket(line , 2 , buf , 100)){
            log_write(SYSLOG , LOGERR , "pack.cfg第2域格式错误");
            fclose(fp);
            return -1;
        }
        log_write(SYSLOG , LOGDBG , "读取第2域替换规则文件名[%s]" ,buf );
        memset(pathname , 0x00 , sizeof(pathname));
        sprintf(pathname , "%s/data/rule/%s" , getenv("PRESS_HOME") , buf);
        pit_cur->rule_fp = fopen(pathname , "r");
        if ( pit_cur->rule_fp == NULL ){
            log_write(SYSLOG , LOGERR , "无法找到替换规则文件[%s]",pathname);
            fclose(fp);
            return -1;
        }

        /*get tps*/
        if (get_bracket(line , 3 , buf , 100)){
            log_write(SYSLOG , LOGERR , "pack.cfg第3域格式错误");
            fclose(fp);
            return -1;
        }
        pit_cur->tps = atoi(buf);
        log_write(SYSLOG , LOGDBG , "读取第3域tps[%d]" ,pit_cur->tps );

        /* get time */
        if (get_bracket(line , 4 , buf , 100)){
            log_write(SYSLOG , LOGERR , "pack.cfg第4域格式错误");
            fclose(fp);
            return -1;
        }
        pit_cur->time = atoi(buf);
        log_write(SYSLOG , LOGDBG , "读取第4域发送时间time[%d]" ,pit_cur->time );

        /* get qid */
        count ++;
        pit_cur->next = p_pack_conf->pit_head;
        p_pack_conf->pit_head = pit_cur;

    }

    if ( count == 0 ){
        log_write(SYSLOG , LOGERR , "组包配置文件为空");
        fclose(fp);
        return 0;
    }

    fclose(fp);
    log_write(SYSLOG , LOGDBG , "组包配置文件已加载");
    return 0;
}

int pack_config_free(pack_config_st *p_pack_conf)
{
    log_write(SYSLOG , LOGDBG , "开始清理组包配置");
    pack_config_st *cur = p_pack_conf;
    pit_proc_st *pit_cur = NULL;
    pit_proc_st *pit_del = NULL;
    if ( cur == NULL )
    {
        return 0;
    }
    pit_cur = cur->pit_head;
    /*文件指针的关闭放在子进程中*/
    while ( pit_cur != NULL ){
        pit_del = pit_cur;
        pit_cur = pit_cur->next;
        fclose(pit_del->tpl_fp);
        fclose(pit_del->rule_fp);
        if ( pit_del )
            free(pit_del);
    }

    log_write(SYSLOG , LOGDBG ,  "清理组包配置完成");
    return 0;
}

int pack_load(char *msg , pack_config_st *p_pack_conf)
{
    /* laod config from file */
    int  i = 4;
    char buf[20];
    int ret = 0;
    memset(buf , 0x00 , sizeof(buf));
    /* skip spaces */
    while ( msg[i] == ' '){
        i ++;
    }
    if ( strlen(msg) <= 5 ){
        strcpy(buf , "pack.cfg");
    } else {
        strcpy(buf , msg+i);
    }
    log_write(SYSLOG , LOGINF , "开始加载组包配置文件%s" , buf);

    ret = pack_config_load( buf , p_pack_conf );
    if ( ret ){
        return -1;
    }
    pit_proc_st *cur = p_pack_conf->pit_head;
    while ( cur != NULL ) {
        pack_pit_load(g_stat+cur->index , cur);
        cur = cur->next;
    }
    p_pack_conf->status = LOADED;

    memset(g_mon_stat , 0x0 , sizeof(monstat_st));
    lastTimeSendNum = 0;
    lastTimeRecvNum = 0;
    memset(&lastTimeStamp , 0x0 , sizeof(struct timeval));

    log_write(SYSLOG , LOGINF , "组包配置文件加载完成 " );
    return 0;
}

int pack_send(pack_config_st *p_pack_conf)
{
    pit_proc_st *cur = p_pack_conf->pit_head;
    /* start pitchers */
    while ( cur != NULL ) {
        cur->pid = pack_pit_start(cur);
        log_write(SYSLOG , LOGINF , "组包进程启动,PID[%d]" ,cur->pid);
        cur = cur->next;
    }
    p_pack_conf->status = RUNNING;

    return 0;
}

int pack_shut(pack_config_st *p_pack_conf)
{
    pit_proc_st     *pit_cur = p_pack_conf->pit_head;
    stat_st         *l_stat = NULL;

    while ( pit_cur != NULL ){
        if( pit_cur->pid > 0 ){
            kill(pit_cur->pid , SIGTERM);
            l_stat = g_stat+pit_cur->index;
            l_stat->tag = FINISHED;
            log_write(SYSLOG , LOGINF , "停止组包进程,pid[%d]" , pit_cur->pid);
        }
        pit_cur = pit_cur->next;
    }

    p_pack_conf->status = FINISHED;
    memset(g_mon_stat , 0x0 , sizeof(monstat_st));
    lastTimeSendNum = 0;
    lastTimeRecvNum = 0;
    memset(&lastTimeStamp , 0x0 , sizeof(struct timeval));

    return 0;
}

void pack_pit_load(stat_st *l_stat , pit_proc_st *p_pitcher)
{
    /* set share memory */
    l_stat->tag = LOADED;
    l_stat->tps = p_pitcher->tps;
    l_stat->timeleft = p_pitcher->time;
    l_stat->timetotal = p_pitcher->time;
    l_stat->left_num = l_stat->tps * l_stat->timeleft;
    l_stat->send_num = 0;
    l_stat->timelast = 0;
    return; 
}


int pack_pit_start(pit_proc_st *p_pitcher)
{
    int pid;
    pid = fork();
    if ( pid < 0 ){
        return -1;
    } else if ( pid > 0 ){
        return pid;
    }

    log_write(PCKLOG , LOGINF , "组包进程[%s]启动" , p_pitcher->tplFileName);
    /* relocate share memory */
    g_stat = (stat_st *)shmat(g_shmid , NULL,  0);
    if ( g_stat == NULL ){
        log_write(SYSLOG , LOGERR , "shmat fail");
        return -1;
    }
    log_write(PCKLOG , LOGDBG , "shmat OK , g_stat = %u" , g_stat);

    tpl_st mytpl;
    rule_st *ruleHead = NULL;
    rule_st *currule = NULL;
    char temp[MAX_LINE_LEN];
    int  random = 0;
    srand(time(NULL));
    msg_st    msgs;
    int ret = 0;
    struct timeval tv_now;
    struct timeval tv_start;
    struct timeval tv_interval;
    struct timeval tv_begin;
    int timeIntervalUs = 1000000 / p_pitcher->tps;
    /* F7计算 */
    time_t t;
    struct tm* lt;

    /* relocate l_stat and update share memory */
    stat_st *l_stat = g_stat + p_pitcher->index;
    l_stat->tag = RUNNING;

    /* load template */
    memset(&mytpl , 0x00 , sizeof(mytpl));
    if (get_template(p_pitcher->tpl_fp , &mytpl)){
        log_write(PCKLOG , LOGERR , "加载模板文件内容失败");
        fclose(p_pitcher->tpl_fp);
        exit(-1);
    }
    log_write(PCKLOG , LOGDBG , "模板文件加载完毕");
    /* load rules */
    ruleHead = get_rule(p_pitcher->rule_fp);
    if (ruleHead == NULL){
        log_write(PCKLOG , LOGERR , "加载替换规则文件失败");
        fclose(p_pitcher->rule_fp);
        exit(-1);
    }
    log_write(PCKLOG , LOGDBG , "替换规则文件加载完毕");

    msgs.type = 1;
    msgs.length = mytpl.len + 4;
    gettimeofday(&tv_begin,NULL);
    while( l_stat->left_num > 0 ){
        log_write(PCKLOG , LOGDBG , "组包开始");
        gettimeofday(&tv_start,NULL);
        memset(msgs.text , 0x00 , sizeof(msgs.text));
        memcpy(msgs.text , mytpl.text , mytpl.len + 4);
        currule = ruleHead;
        /* apply substitution rules */
        while (currule != NULL){
            memset(temp , 0x00 , sizeof(temp));
            if ( currule->type == REPTYPE_RANDOM ){
                random = rand()%((int)_pow(10,currule->length));
                sprintf( temp , "%0*d" , currule->length , random);
            } else if ( currule->type == REPTYPE_FILE ){
                strcpy(temp , currule->rep_head->text);
                currule->rep_head = currule->rep_head->next;
            } else if ( currule->type == REPTYPE_F7 ){
                time(&t);
                lt = localtime(&t);
                sprintf(temp,"%02d%02d%02d%02d%02d",lt->tm_mon+1,lt->tm_mday,lt->tm_hour,lt->tm_min,lt->tm_sec);
            }
            memcpy(msgs.text + currule->start - 1 + 4 , temp , currule->length);
            currule = currule->next;
        }
        log_write(PCKLOG , LOGDBG , "组包完成,准备发送至报文队列");
        /* send to out queue */
        ret = msgsnd((key_t)QID_MSG , &msgs , sizeof(msg_st) - sizeof(long) , 0);
        if (ret < 0){
            log_write(PCKLOG , LOGERR , "内部错误:组包进程调用msgsnd失败，ret[%d],errno[%d]",ret,errno);
            exit(1);
        }
        log_write(PCKLOG , LOGDBG , "报文发送完成");
        /* update share memory*/
        gettimeofday(&tv_now,NULL);
        timersub(&tv_now , &tv_begin , &tv_interval);
        l_stat->timelast = tv_interval.tv_sec;
        l_stat->send_num ++;
        l_stat->timeleft = (l_stat->timetotal - l_stat->timelast <= 0) ? 0 : l_stat->timetotal - l_stat->timelast;
        l_stat->left_num = (l_stat->timeleft * l_stat->tps <= 0) ? 0 : l_stat->timeleft * l_stat->tps;

        /* calculate interval time */
        timersub(&tv_now , &tv_start , &tv_interval);
        timeIntervalUs = 1000000 / l_stat->tps;
        if ( tv_interval.tv_usec < timeIntervalUs )
            usleep( timeIntervalUs - tv_interval.tv_usec );
    }
    l_stat->tag = FINISHED;
    cleanRule(ruleHead);
    exit(0);
}

int persist(char *text , int len , char type , struct timeval ts)
{
    FILE *fp = NULL;
    char pathname[MAX_PATHNAME_LEN];

    memset(pathname , 0x00 , sizeof(pathname));
    if ( type == LONG_SEND )
        sprintf(pathname , "%s/data/result/longsend_%d" , getenv("PRESS_HOME") , getpid());
    else if ( type == LONG_RECV )
        sprintf(pathname , "%s/data/result/longrecv_%d" , getenv("PRESS_HOME") , getpid());
    else if ( type == SHORTCONN )
        sprintf(pathname , "%s/data/result/shorconn_%d" , getenv("PRESS_HOME") , getpid());
    else if ( type == JIPSCONN )
        sprintf(pathname , "%s/data/result/jipsconn_%d" , getenv("PRESS_HOME") , getpid());
    fp = fopen(pathname , "a+");
    if (fp == NULL){
        log_write(CONLOG , LOGERR , "内部错误:无法打开持久化结果文件[%s]",pathname);
        return -1;
    }

    fprintf( fp , "%ld.%06d " , ts.tv_sec , ts.tv_usec);

    if ( ENCODING == 'H' ){
        fprintf( fp , "HEX PRINT START\n");
        int i = 0;
        int j = 0;
        char ch = ' ';
        for ( i = 0 ; i <= len/16 ; i ++){
            fprintf( fp , "%06d " , i);
            for ( j = 0 ; j+i*16 < len && j < 16 ;j ++){
                ch = (unsigned char)text[j+i*16];
                fprintf( fp , "%02X " , ch);
            }
            while ( j++ < 16 ){
                fprintf( fp , "   ");
            }
            fprintf( fp , "|");
            for ( j = 0 ; j+i*16 < len && j < 16 ; j ++ ){
                int pos = j+i*16;
                if ( (text[pos] >= 'a' && text[pos] <= 'z') ||
                     (text[pos] >= 'A' && text[pos] <= 'Z') || 
                     (text[pos] >= '0' && text[pos] <= '9') ){
                    fprintf( fp , "%c",text[pos]);
                } else {
                    fprintf( fp , ".");
                }
            }
            fprintf( fp , "\n");
        }
        fprintf( fp , "================HEX PRINT END\n");
    } else {
        fwrite(text , 1 , len , fp);
        fwrite("\n" , 1, 1 , fp);
    }
    fclose(fp);
    log_write(CONLOG , LOGDBG , "持久化成功");
    return 0;
}
/*
 * 初始化信号量
 * */
int sem_init()
{
    int semid = 0;
    int ret = 0;
    union semun arg;

    semid = semget(IPC_PRIVATE , 1 , IPC_CREAT|0660);
    if ( semid < 0 ){
        log_write(SYSLOG , LOGERR , "semget失败");
        return -1;
    }
    arg.val = 1;
    ret = semctl( semid , 0 , SETVAL , arg);
    if ( ret < 0 ){
        log_write(SYSLOG , LOGERR , "调用semctl失败");
        return -1;
    }
    return semid;
}
/*
 * 销毁信号量
 * */
void sem_destroy(int semid)
{
    return ;
}
/*
 * 上锁操作
 * */
int sem_lock(int semid)
{
    struct sembuf sops={0,-1, SEM_UNDO};
    return (semop(semid,&sops,1));
}
/* 解锁操作 */
int sem_unlock(int semid)
{
    struct sembuf sops={0,+1, SEM_UNDO};
    return (semop(semid,&sops,1));
}
/* 
 * 发送空闲包
 * */
void send_idle(int signo)
{
    send(sock_send , "0000" , 4 , 0);
    siglongjmp(jmpbuf , 1);
}

rule_st *get_rule(FILE *fp)
{
    char     line[150];
    char     buf[100];
    char     pathname[MAX_PATHNAME_LEN];

    char     temp[MAX_LINE_LEN];
    char    replace[MAX_REP_LEN];
    FILE     *rep_fp = NULL;
    rep_st    *rep_head = NULL;
    rep_st    *rep_tail = NULL;
    rep_st    *rep_cur = NULL;

    rule_st *cur = NULL;
    rule_st *ret = NULL;

    memset(line , 0x00 , sizeof(line));
    memset(buf , 0x00 , sizeof(buf));

    log_write(SYSLOG , LOGDBG , "开始加载替换规则");

    while( fgets(line , sizeof(line) , fp) != NULL ){

        if ( line[0] == '\n' || line[0] == '#' )
            continue;

        cur = (rule_st *)malloc(sizeof(rule_st));
        if ( cur == NULL ){
            log_write(SYSLOG , LOGERR , "内部错误:malloc for rule_st失败");
            fclose(fp);
            return NULL;
        }

        memset(cur , 0x00 , sizeof(rule_st));

        /*第一域为替换起始位置*/
        if (get_bracket(line , 1 , buf , 100))
        {
            log_write(SYSLOG , LOGERR , "替换规则文件第1域格式错误");
            return NULL;
        }
        cur->start = atoi(buf);
        log_write(SYSLOG , LOGDBG , "读取替换规则第1域，起始位置[%d]" , cur->start);

        /* 第二域为替换长度*/
        if (get_bracket(line , 2 , buf , 100))
        {
            log_write(SYSLOG , LOGERR , "替换规则文件第2域格式错误");
            return NULL;
        }
        cur->length = atoi(buf);
        log_write(SYSLOG , LOGDBG , "读取替换规则第2域，替换长度[%d]" , cur->length);

        /* 第三域为补位方式*/
        if (get_bracket(line , 3 , buf , 100))
        {
            log_write(SYSLOG , LOGERR , "替换规则文件第3域格式错误");
            return NULL;
        }
        cur->pad = atoi(buf);
        log_write(SYSLOG , LOGDBG , "读取替换规则第3域，补位方式[%d]" , cur->pad);

        /* 第四域为替换方式*/
        if (get_bracket(line , 4 , buf , 100))
        {
            log_write(SYSLOG , LOGERR , "替换规则文件第4域格式错误");
            return NULL;
        }
        if ( strncmp( buf , "RAND" , 4 ) == 0  ){
            /* 类型1: 随机数 */
            cur->type = REPTYPE_RANDOM;
        } else if ( strncmp( buf , "FILE:" , 5) == 0 ){
            /* 类型2: 文件 */
            cur->type = REPTYPE_FILE;
            memset(pathname , 0x00 , sizeof(pathname));
            sprintf(pathname , "%s/data/rep/%s" , getenv("PRESS_HOME") , buf+5);
            rep_fp = fopen(pathname , "r");
            if (rep_fp == NULL){
                log_write(SYSLOG , LOGERR , "rep_file[%s] not found",pathname);
                return NULL;
            }

            memset(temp , 0x00 , sizeof(temp));
            rep_head = NULL;
            rep_tail = NULL;
            rep_cur = NULL;
            while( fscanf( rep_fp , "%s" , temp) != EOF ){
                rep_cur = (rep_st *)malloc(sizeof(rep_st));
                if (rep_cur == NULL){
                    log_write(SYSLOG , LOGERR , "loading replace file , malloc fail ");
                    fclose(rep_fp);
                    return NULL;
                }

                /* do paddings */
                memset( replace , 0x00 , MAX_REP_LEN );
                if (cur->pad == 1){
                    padding(temp, 'l' , '0' , replace , cur->length);
                } else if (cur->pad == 2){
                    padding(temp , 'r' , ' ' , replace , cur->length);
                } else {
                    memcpy(replace , temp , cur->length);
                }

                strcpy(rep_cur->text , replace);

                if( rep_head == NULL ){
                    rep_head = rep_cur;
                    rep_tail = rep_cur;
                    rep_cur->next = rep_cur;
                } else {
                    rep_tail->next = rep_cur;
                    rep_cur->next = rep_head;
                    rep_tail = rep_cur;
                }
            }
            fclose(rep_fp);
        } else if ( strncmp( buf , "F7" , 2) == 0 ){
            /* 类型3: 7域 */
            cur->type = REPTYPE_F7;
        }
        log_write(SYSLOG , LOGDBG , "读取替换规则第5域，替换方式[%d]" , cur->type);

        cur->rep_head = rep_head;

        cur->next = ret;
        ret = cur;
    }
    log_write(SYSLOG , LOGDBG , "替换规则加载完成");
    return ret;
}

int get_template(FILE *fp_tpl , tpl_st *mytpl)
{
    if ( fread(mytpl->text , 1 , 4 , fp_tpl) < 4){
        log_write(SYSLOG , LOGERR , "读取模板报文4位长度失败");
        return -1;
    }
    mytpl->len = get_length(mytpl->text);
    if ( fread(mytpl->text + 4 , 1 , mytpl->len , fp_tpl) < mytpl->len ){
        log_write(SYSLOG , LOGERR , "读取模板报文内容失败");
        return -1;
    }
    return 0;
}

void cleanRule(rule_st *ruleHead)
{
    rule_st *cur = ruleHead;
    rule_st *nex = ruleHead;
    rep_st    *rep_cur = NULL;

    while ( cur != NULL ){
        while( cur->rep_head != NULL){
            if( cur->rep_head->next == cur->rep_head ){
                free(cur->rep_head);
                cur->rep_head = NULL;
            } else {
                rep_cur = cur->rep_head->next->next;
                free(cur->rep_head->next);
                cur->rep_head->next = rep_cur;
            }
        }
        nex = cur->next;
        free(cur);
        cur = nex;
    }
    return;
}

char *get_stat(int flag , conn_config_st *p_conn_conf , pack_config_st *p_pack_conf)
{
    char *ret = (char *)malloc(MAX_MSG_LEN);
    int offset = 0;
    char status[20];
    char type[20];
    struct  timeval nowTimeStamp;
    struct  timeval timeInterval;
    memset(ret , 0x00 , MAX_MSG_LEN);
    comm_proc_st *p_comm = p_conn_conf->process_head;
    pit_proc_st  *p_pack = p_pack_conf->pit_head;
    stat_st *l_stat;
    if ( (flag & STAT_CONN) ){
        if ( p_conn_conf->status == RUNNING ){
            offset += sprintf(ret+offset , \
                    "通讯进程状态\n");
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
            offset += sprintf(ret+offset , \
                    "[类型][IP             ][端口  ][状态   ]\n");
            while ( p_comm != NULL ){
                memset( status , 0x00 , sizeof(status));
                memset( type , 0x00 , sizeof(type));
                if ( p_comm->type == 'X' ){
                    int i = 0 ;
                    int count = 0;
                    for(i = 0 ; i < p_comm->parallel ; i ++ ){
                        if ( kill( p_comm->para_pids[i] , 0 ) == 0 ){
                            count ++;
                        }
                    }
                    sprintf(status , "%-3d/%-3d" , count , p_comm->parallel);
                } else {
                    if ( kill( p_comm->pid , 0 ) == 0 ){
                        strcpy(status , "运行中  ");
                    } else {
                        strcpy(status , "已退出  ");
                    }
                }
                if ( p_comm->type == 'S' ){
                    strcpy(type , "发送");
                } else if ( p_comm->type == 'R' ){
                    strcpy(type , "接收");
                } else if ( p_comm->type == 'J' ){
                    strcpy(type , "外卡");
                } else if ( p_comm->type == 'X' ){
                    strcpy(type , "短链");
                }

                offset += sprintf(ret+offset , 
                                "[%-4s][%-15s][%-6d][%-7s]\n" , \
                                type , \
                                p_comm->ip , \
                                p_comm->port ,\
                                status);
                p_comm = p_comm->next;
            }
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
        } else {
            offset += sprintf(ret+offset , "无通讯模块进程信息,输入init启动通讯模块\n");
        }
    }
    if ( (flag & STAT_PACK) ){
        if ( p_pack_conf->status == LOADED || p_pack_conf->status == RUNNING ) {
            offset += sprintf(ret+offset , \
                    "组包进程状态\n");
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
            offset += sprintf(ret+offset , \
                    "[序号 ][模板文件   ][状态      ][设置速度][已发送报文数][已发送时间/总时间]\n");
            while ( p_pack != NULL ) {
                l_stat = g_stat + p_pack->index;
                memset( status , 0x00 , sizeof(status));
                if ( l_stat->tag != NOTLOADED ){
                    if ( l_stat->tag == RUNNING ){
                        strcpy(status , "运行中    ");
                    } else if ( l_stat->tag == LOADED ){
                        strcpy(status , "配置已加载");
                    } else if ( l_stat->tag == FINISHED ){
                        strcpy(status , "发送完毕  ");
                    }
                    offset += sprintf(ret+offset , \
                                    "[%-5d][%-11s][%-10s][%-8d][%-12d][%-8d/%-8d]\n" , \
                                    p_pack->index,\
                                    p_pack->tplFileName,\
                                    status,\
                                    l_stat->tps,\
                                    l_stat->send_num,\
                                    l_stat->timelast,\
                                    l_stat->timetotal);
                }
                p_pack = p_pack->next;
            }
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
        } else {
            offset += sprintf(ret+offset , "无组包进程信息,输入load载入组包配置\n");
        }
    }
    if ( flag & STAT_MONI ){
        if ( p_pack_conf->status == RUNNING ) {
            offset += sprintf(ret+offset , \
                    "实时监控数据\n");
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
            offset += sprintf(ret+offset , \
                    "[发送总笔数][接收总笔数][发送速度][接收速度]\n");
            gettimeofday(&nowTimeStamp,NULL);
            timersub(&nowTimeStamp , &lastTimeStamp , &timeInterval);
            memcpy(&lastTimeStamp , &nowTimeStamp , sizeof(struct timeval));
            if ( lastTimeSendNum > 0 ){
                g_mon_stat->real_send_tps = (g_mon_stat->send_num - lastTimeSendNum) / (timeInterval.tv_sec+(float)timeInterval.tv_usec/1000000);
            }
            lastTimeSendNum = g_mon_stat->send_num;
            if ( lastTimeRecvNum > 0 ){
                g_mon_stat->real_recv_tps = (g_mon_stat->recv_num - lastTimeRecvNum ) / (timeInterval.tv_sec+(float)timeInterval.tv_usec/1000000);
            }
            lastTimeRecvNum = g_mon_stat->recv_num;
            offset += sprintf(ret+offset , \
                    "[%-10d][%-10d][%-8d][%-8d]\n", \
                    g_mon_stat->send_num,\
                    g_mon_stat->recv_num,\
                    g_mon_stat->real_send_tps,\
                    g_mon_stat->real_recv_tps);

            offset += sprintf(ret+offset , \
                    "===========================================================================\n");

        } else {
            offset += sprintf(ret+offset , "无实时监控信息");
        }
    }

    return ret;
}

char *adjust_status(int flag , char *msg , pack_config_st *p_pack_conf)
{
    char *ret = (char *)malloc(MAX_MSG_LEN);
    int i = 0; 
    int offset = 0;

    int direc = 0;
    int adjustment = 0;
    int percent = 0;
    int index = 0;
    int id = -1;

    int before = 0;
    int after = 0;

    pit_proc_st  *p_pack = p_pack_conf->pit_head;

    /* read first parameter */
    while ( msg[i] != ' ' ){
        i ++;
    }
    i++;
    
    /* get direction of adjustment */
    if ( msg[i] == '+' ){
        direc = 1;
        i++;
    } else if ( msg[i] == '-' ){
        direc = -1;
        i++;
    } else {
        direc = 0;
    }

    /* get adjustment and percent */
    for ( ; i < strlen(msg) ; i ++){
        if ( msg[i] == ' ' || msg[i] == '%' ){
            if ( adjustment == 0 && direc != 0 ){
                offset += sprintf(ret+offset , "[%s]命令格式错误,缺少数字",msg);
                return ret;
            }
            break;
        }
        if ( msg[i] < '0' || msg[i] > '9' ){
            offset += sprintf(ret+offset , "[%s]命令格式错误,监测到非数字",msg);
            return ret;
        }
        adjustment*=10;
        adjustment+=(msg[i] - '0');
    }
    if ( msg[i] == '%' ){
        if ( direc == 0 ){
            offset += sprintf(ret+offset , "[%s]命令格式错误,shiyong%%未指定+-",msg);
            return ret;
        } else {
            percent = 1;
            i ++;
        }
    }
    if ( msg[i] == ' ' ){
        i++;
        if ( i == strlen(msg) || msg[i] < '0' || msg[i] > '9'){
            id = -1;
        }
        else if ( msg[i] == '0' ){
            id = 0;
        } else {
            for ( ; i < strlen(msg) ; i ++){
                index *= 10;
                index += (msg[i] - '0');
            }
            id = index;
        }
    } else {
        id = -1;
    }

    if ( id >= 0 ){
        status_op(flag , id , adjustment , percent , direc , &before , &after);
        if ( flag == 1 )
            offset += sprintf(ret+offset , "序号[%d]组包进程TPS调整成功,调整前[%d],调整后[%d]" , id , before , after);
        else
            offset += sprintf(ret+offset , "序号[%d]组包进程发送时间调整成功,调整前[%d],调整后[%d]" , id , before , after);
    } else {
        while ( p_pack != NULL ){
            status_op(flag , p_pack->index , adjustment , percent , direc , &before , &after);
            if ( flag == 1 )
                offset += sprintf(ret+offset , "序号[%d]组包进程TPS调整成功,调整前[%d],调整后[%d]\n" , p_pack->index , before , after);
            else
                offset += sprintf(ret+offset , "序号[%d]组包进程发送时间调整成功,调整前[%d],调整后[%d]\n" , p_pack->index , before , after);
            p_pack = p_pack->next;
        }
    }
    return ret;
}

char *adjust_para( char *msg , conn_config_st *p_conn_config)
{
    int new_para = 0;
    comm_proc_st *shortconn = p_conn_config->process_head;
    int i = 0;
    int offset = 0;
    char *ret = (char *)malloc(MAX_MSG_LEN);
    int pid = 0;

    while ( msg[i] != ' ' ){
        i ++;
    }
    i++;
    while ( msg[i] >= '0' && msg[i] <= '9' ){
        new_para *= 10;
        new_para += msg[i] - '0';
        i ++;
    }
    
    if ( new_para == shortconn->parallel ){
        offset = sprintf( ret+offset , "输入短链接并发数与当前并发数相同");
    } else if ( new_para > shortconn->parallel ){
        for ( i = shortconn->parallel ; i < new_para ; i ++ ){
            pid = conn_sender_start(shortconn);
            if ( pid < 0 ) {
                log_write(SYSLOG , LOGERR , "短链接通讯进程启动失败 , IP[%s] , PORT[%d]",shortconn->ip , shortconn->port);
            } else {
                log_write(SYSLOG , LOGINF , "短链接通讯送进程启动成功 , IP[%s] , PORT[%d] , PID[%d]",shortconn->ip , shortconn->port , ret);
                shortconn->para_pids[i] = pid;
            }
        }
        offset = sprintf( ret+offset , "调整短链接通讯进程数为%d",new_para);
        shortconn->parallel = new_para;
    } else if ( new_para < shortconn->parallel ){
        for ( i = shortconn->parallel-1 ; i >= new_para ; i -- ){
            kill( shortconn->para_pids[i] , 9 );
        }
        offset = sprintf( ret+offset , "调整短链接通讯进程数为%d",new_para);
        shortconn->parallel = new_para;
    } else if ( new_para <= 0 ){
        offset = sprintf( ret+offset , "输入短链接并发数不合法");
    }
    return ret;
}

void status_op(int flag , int id , int adjustment , int percent , int direc , int *before , int *after)
{
    stat_st *l_stat = g_stat + id;
    if ( flag == 1 ){
        *before = l_stat->tps;
        if ( percent == 0 ){
            if ( direc > 0 )
                l_stat->tps = l_stat->tps + adjustment;
            else if ( direc < 0 )
                l_stat->tps = (l_stat->tps - adjustment <= 0) ? 0 : (l_stat->tps - adjustment);
            else if ( direc == 0)
                l_stat->tps = adjustment;
        } else if ( percent == 1 ) {
            if ( direc > 0 )
                l_stat->tps = l_stat->tps * (adjustment+100) / 100;
            else if ( direc < 0 ){
                if ( adjustment <= 100 )
                    l_stat->tps = l_stat->tps * (100 - adjustment) / 100;
            }
            else if ( direc == 0)
                l_stat->tps = adjustment;
        }
        *after = l_stat->tps;
    } else {
        *before = l_stat->timetotal;
        if ( percent == 0 ){
            if ( direc > 0 )
                l_stat->timetotal = l_stat->timetotal + adjustment;
            else if ( direc < 0 )
                l_stat->timetotal = (l_stat->timetotal - adjustment <= 0) ? 0 : (l_stat->timetotal - adjustment);
            else if ( direc == 0 )
                l_stat->timetotal = adjustment;
        } else if ( percent == 1 ) {
            if ( direc > 0 )
                l_stat->timetotal = l_stat->timetotal * (adjustment+100) / 100;
            else if ( direc < 0 ){
                if ( adjustment <= 100 )
                    l_stat->timetotal = l_stat->timetotal * (100 - adjustment) / 100;
            }
            else if ( direc == 0 )
                l_stat->timetotal = adjustment;
        }
        *after = l_stat->timetotal;
    }
    l_stat->timeleft = (l_stat->timetotal - l_stat->timelast <= 0) ? 0 : l_stat->timetotal - l_stat->timelast;
    l_stat->left_num = (l_stat->timeleft * l_stat->tps <= 0) ? 0 : l_stat->timeleft * l_stat->tps;
    return;
}

int check_deamon()
{
    FILE *fp = NULL;
    int  pid;
    char pidbuf[20];
    if ( access( PIDFILE , F_OK) == 0 ){
        fp = fopen(PIDFILE , "rb");
        if ( fp == NULL ){
            return -1;
        }
        fgets( pidbuf , 20 , fp);
        pid = atoi(pidbuf);
        fclose(fp);
        if ( kill(pid , 0) == 0 ){
            return pid;
        } else {
            remove(PIDFILE);
            return 0;
        }
    } else {
        return 0;
    }
}

void deamon_exit()
{
    sleep(1);
    /* 删除报文消息队列key */
    if ( msgctl((key_t)QID_MSG , IPC_RMID , NULL) ){
        log_write(SYSLOG , LOGERR ,"msgctl删除命令消息队列msgid=%d失败",QID_MSG);
    } else {
        log_write(SYSLOG , LOGINF ,"msgctl删除命令消息队列msgid=%d成功",QID_MSG);
    }
    /* 卸载、删除共享内存key */
    shmdt((void *)g_stat);
    if ( shmctl(g_shmid , IPC_RMID , 0) ){
        log_write(SYSLOG , LOGERR ,"shmctl删除shmid=%d失败",g_shmid);
    } else {
        log_write(SYSLOG , LOGINF ,"shmctl删除shmid=%d成功",g_shmid);
    }
    shmdt((void *)g_mon_stat);
    if ( shmctl(g_mon_shmid , IPC_RMID , 0) ){
        log_write(SYSLOG , LOGERR ,"shmctl删除shmid=%d失败",g_mon_shmid);
    } else {
        log_write(SYSLOG , LOGINF ,"shmctl删除shmid=%d成功",g_mon_shmid);
    }
    /* 删除信号量key */
    union semun arg;
    arg.val = (short)0;
    if ( semctl(g_mon_semid,0,IPC_RMID,arg) ){
        log_write(SYSLOG , LOGERR ,"semctl删除semid=%d失败",g_mon_semid);
    } else {
        log_write(SYSLOG , LOGINF ,"semctl删除semid=%d成功",g_mon_semid);
    }
    /* 释放日志句柄 */
    log_clear();
    /* 释放全局结构体 */
    pack_config_free(p_pack_conf);
    conn_config_free(p_conn_conf);
    /* 删除pidfile */
    remove(PIDFILE);
    exit(0);
}

void deamon_signal_handler( int signo)
{
    deamon_exit();
}
void print_help()
{
    printf("press [-p port] [-l loglevel] [-h]\n");
    printf("       -p 指定监听端口(默认6043)\n");
    printf("       -l 指定日志级别(默认4)\n");
    return ;
}
int main(int argc , char *argv[])
{
    /* 检查环境变量 */
    if ( getenv("PRESS_HOME") == NULL ){
        printf("缺少环境变量${PRESS_HOME}\n");
        exit(1);
    }
    int op = 0;
    PORT_CMD = 6043;        /* 默认监听端口 */
    LOGLEVEL = 4;           /* 默认日志级别 */
    while ( ( op = getopt( argc , argv , "p:l:h") ) > 0 ) {
        switch(op){
            case 'h' :
                print_help();
                exit(0);
            case 'p' :
                PORT_CMD = atoi(optarg);
                break;
            case 'l' :
                LOGLEVEL = atoi(optarg);
                break;
        }
    }
    sprintf(PIDFILE , "/tmp/press.%d" , PORT_CMD);

    /* 检查是否已存在运行的实例 */
    if ( check_deamon() ){
        printf("守护进程已经在运行,不可重复启动\n");
        exit(1);
    }

    /* 成为后台守护进程 */
    daemon_start();

    /* 初始化日志模块 */
    log_init(SYSLOG , "system");
    log_init(PCKLOG , "packing");
    log_init(CONLOG , "connection");

    /* 产生pidfile */
    FILE *fp = fopen( PIDFILE , "wb");
    if ( fp == NULL ){
        log_write(SYSLOG , LOGERR , "无法打开pidfile %s , errno = %d" , PIDFILE , errno) ;
    } else {
        fprintf(fp , "%d" , getpid());
        fclose(fp);
    }
    log_write(SYSLOG , LOGINF , "守护进程启动成功");
    log_write(SYSLOG , LOGINF , "环境变量${PRESS_HOME} [%s]" , getenv("PRESS_HOME"));
    log_write(SYSLOG , LOGINF , "监听端口              [%d]" , PORT_CMD);

    /* 信号处理 */
    signal(SIGCHLD , SIG_IGN);
    signal(SIGKILL , deamon_signal_handler);
    signal(SIGTERM , deamon_signal_handler);

    char *retmsg = NULL;
    /* 初始化信号量 */
    g_mon_semid = sem_init();
    if ( g_mon_semid < 0 ){
        log_write(SYSLOG , LOGERR , "初始化信号量失败");
        deamon_exit();
    }
    log_write(SYSLOG , LOGINF , "初始化监控区信号量成功,g_mon_semid=%d",g_mon_semid);

    /* 初始化报文消息队列 */
    if ( (QID_MSG = msgget(IPC_PRIVATE , IPC_CREAT|0660)) < 0){
        log_write(SYSLOG , LOGERR , "配置错误:无法从press.cfg中读取配置项[MSGKEY_MSG]");
        deamon_exit();
    }
    log_write(SYSLOG , LOGINF , "初始化报文消息队列成功,QID_MSG=%d" , QID_MSG);

    /* 初始化全局结构体 */
    p_conn_conf = (conn_config_st *)malloc(sizeof(conn_config_st));
    if ( p_conn_conf == NULL ){
        log_write(SYSLOG , LOGERR , "内部错误:malloc conn_config_st失败");
        deamon_exit();
    }
    p_conn_conf->status = NOTLOADED;

    p_pack_conf = (pack_config_st *)malloc(sizeof(pack_config_st));
    if ( p_pack_conf == NULL ){
        log_write(SYSLOG , LOGERR , "内部错误:调用malloc pack_config_st失败");
        deamon_exit();
    }
    p_pack_conf->status = NOTLOADED;

    /* 初始化共享内存 */
    g_shmid = shmget( IPC_PRIVATE , MAX_PROC_NUM*sizeof(stat_st) , IPC_CREAT|0660);
    if ( g_shmid < 0 ){
        log_write(SYSLOG , LOGERR , "内部错误:调用shmget失败");
        deamon_exit();
    }
    g_stat = (stat_st *)shmat(g_shmid , NULL,  0);
    if ( g_stat == NULL ){
        log_write(SYSLOG , LOGERR , "内部错误:调用shmat失败,shmid = %d",g_shmid);
        deamon_exit();
    }
    log_write(SYSLOG , LOGINF , "初始化全局状态内存区成功,g_shmid = %d" , g_shmid );

    g_mon_shmid = shmget( IPC_PRIVATE , sizeof(monstat_st) , IPC_CREAT|0660);
    if ( g_mon_shmid < 0 ){
        log_write(SYSLOG , LOGERR , "内部错误:调用shmget失败");
        deamon_exit();
    }
    g_mon_stat = (monstat_st *)shmat(g_mon_shmid , NULL , 0);
    if ( g_mon_stat == NULL ){
        log_write(SYSLOG , LOGERR , "内部错误:调用shmat失败,shmid = %d",g_mon_shmid);
        deamon_exit();
    }
    log_write(SYSLOG , LOGINF , "初始化监控状态内存区成功,g_mon_shmid = %d" , g_mon_shmid);

    /* 加载监听端口配置 */
    char buffer_cmd[MAX_CMD_LEN];
    /* 创建监听socket */
    int server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
    int sock_recv = 0;
    struct sockaddr_in server_sockaddr;
    struct sockaddr_in client_addr;
    socklen_t socket_len = sizeof(client_addr);
    /* bind 端口*/
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(PORT_CMD);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sockfd , (struct sockaddr *)&server_sockaddr , sizeof(server_sockaddr)) == -1)
    {
        log_write(SYSLOG , LOGERR , "命令监听端口[%d]bind 失败" , PORT_CMD);
        deamon_exit();
    }
    log_write(SYSLOG , LOGDBG , "命令监听端口[%d]bind 成功" , PORT_CMD);
    /* 启动监听 */
    if(listen(server_sockfd , 1) == -1){
        log_write(SYSLOG , LOGERR , "命令监听端口[%d]监听失败" , PORT_CMD);
        deamon_exit();
    }
    log_write(SYSLOG , LOGDBG , "命令监听端口[%d]监听成功" , PORT_CMD);

    /* 循环处理命令 */
    while (1){
        log_write(SYSLOG , LOGINF , "守护进程等待命令输入...");

        /* 接受远端连接 */
        sock_recv = accept(server_sockfd , (struct sockaddr *)&client_addr , &socket_len);
        if (sock_recv < 0){
            log_write(SYSLOG , LOGERR , "命令监听端口[%d]accept 失败" , PORT_CMD);
            deamon_exit();
        }
        log_write(SYSLOG , LOGDBG , "命令监听端口[%d]accept 成功" , PORT_CMD);
        memset( buffer_cmd , 0x00 , sizeof(buffer_cmd));
        /* 读取命令内容 */
        if ( recv(sock_recv , buffer_cmd , MAX_CMD_LEN , 0) < 0 ){
            log_write(SYSLOG , LOGERR , "监听命令端口[%d]recv 失败" , PORT_CMD);
            deamon_exit();
        }
        log_write(SYSLOG , LOGDBG , "收到命令[%s]\n" , buffer_cmd);

        if ( strncmp(buffer_cmd , "init" , 4) == 0 ){
            if (p_conn_conf->status == RUNNING){
                log_write(SYSLOG , LOGINF ,"[init]执行失败,通讯模块不可重复启动");
                send( sock_recv , "[init]执行失败,通讯模块不可重复启动" , MAX_CMD_LEN , 0);
            } else {
                conn_start(p_conn_conf);
                send( sock_recv , "[init]执行成功,输入[stat]查看状态" , MAX_CMD_LEN , 0 );
            }
        } else if ( strncmp(buffer_cmd , "stop" , 4) == 0 ){
            if (p_conn_conf->status != RUNNING){
                log_write(SYSLOG , LOGINF ,"[stop]执行失败,通讯模块未启动");
                send( sock_recv , "[stop]执行失败,输入[init]启动通讯模块" , MAX_CMD_LEN , 0);
            } else {
                conn_stop(p_conn_conf);
                send( sock_recv , "[stop]执行成功,通讯进程已停止" , MAX_CMD_LEN , 0);
            }
        } else if ( strncmp(buffer_cmd , "send" , 4) == 0 ) {
            if ( p_pack_conf->status != LOADED ){
                log_write(SYSLOG , LOGINF ,"[send]执行失败,组包进程配置未加载");
                send( sock_recv , "[send]执行失败,输入[load]加载组包进程配置" , MAX_CMD_LEN , 0);
            } else {
                pack_send(p_pack_conf);
                log_write(SYSLOG , LOGINF ,"[send]执行成功");
                send( sock_recv , "[send]执行成功,输入[stat]或[moni]查看发送情况" , MAX_CMD_LEN , 0);
            }
        } else if ( strncmp(buffer_cmd , "shut" , 4) == 0){
            pack_shut(p_pack_conf);
            log_write(SYSLOG , LOGINF ,"[shut]执行成功");
            send( sock_recv , "[shut]执行成功,组包进程已停止" , MAX_CMD_LEN , 0);
        } else if ( strncmp(buffer_cmd , "kill" , 4) == 0 ){
            if (pack_shut(p_pack_conf) ) {
                log_write(SYSLOG , LOGERR ,"组包进程停止失败");
            }
            if ( conn_stop(p_conn_conf) ){
                log_write(SYSLOG , LOGERR ,"通讯进程停止失败");
            }
            send( sock_recv , "[kill]执行成功,守护进程正在退出" , MAX_CMD_LEN , 0);
            close(sock_recv);
            deamon_exit();
        } else if ( strncmp(buffer_cmd , "stat" , 4) == 0){
            retmsg = get_stat( STAT_CONN|STAT_PACK|STAT_MONI , p_conn_conf , p_pack_conf);
            send( sock_recv , retmsg , MAX_CMD_LEN , 0);
            free(retmsg);
        } else if ( strncmp(buffer_cmd , "moni" , 4) == 0){
            retmsg = get_stat( STAT_PACK|STAT_MONI , p_conn_conf , p_pack_conf);
            send( sock_recv , retmsg , MAX_CMD_LEN , 0);
            free(retmsg);
        } else if ( strncmp(buffer_cmd , "load" , 4) == 0){
            if ( pack_load(buffer_cmd , p_pack_conf) ){
                log_write(SYSLOG , LOGERR ,"[load]执行失败");
                send( sock_recv , "[load]执行失败" , MAX_CMD_LEN , 0);
            } else {
                log_write(SYSLOG , LOGINF ,"[load]执行成功");
                send( sock_recv , "[load]执行成功" , MAX_CMD_LEN , 0);
            }
        } else if ( strncmp( buffer_cmd , "tps" , 3) == 0){
            if ( p_pack_conf->status == NOTLOADED  || p_pack_conf->status == FINISHED ){
                log_write(SYSLOG , LOGINF ,"[tps]执行失败,组包进程配置未加载");
                send( sock_recv , "[tps]执行失败,输入[load]加载组包进程配置" , MAX_CMD_LEN , 0);
            } else {
                retmsg = adjust_status( 1 , buffer_cmd , p_pack_conf);
                send( sock_recv , retmsg , MAX_CMD_LEN , 0);
                free(retmsg);
            }
        } else if ( strncmp( buffer_cmd , "time" , 4) == 0 ){
            if ( p_pack_conf->status == NOTLOADED || p_pack_conf->status == FINISHED ){
                log_write(SYSLOG , LOGINF ,"[tps]执行失败,组包进程配置未加载");
                send( sock_recv , "[tps]执行失败,输入[load]加载组包进程配置" , MAX_CMD_LEN , 0);
            } else {
                retmsg = adjust_status( 2 , buffer_cmd , p_pack_conf);
                send( sock_recv , retmsg , MAX_CMD_LEN , 0);
                free(retmsg);
            }
        } else if ( strncmp( buffer_cmd , "para" , 4) == 0 ){
            if ( p_conn_conf->status != RUNNING ){
                log_write(SYSLOG , LOGINF ,"[para]执行失败,组包进程未启动");
                send( sock_recv , "[para]执行失败,组包进程未启动" , MAX_CMD_LEN , 0);
            }
            retmsg = adjust_para( buffer_cmd , p_conn_conf);
            send( sock_recv , retmsg , MAX_CMD_LEN , 0);
            free(retmsg);
        } else if ( strncmp( buffer_cmd , "list" , 4) == 0 ){
            send( sock_recv , "OK" , MAX_CMD_LEN , 0);
        }
        close(sock_recv);
    }
    /* 退出清理 */
    deamon_exit();
}
