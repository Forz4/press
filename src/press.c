#include "include/press.h"
#include "include/log.h"
#include "include/8583.h"

/* global variables */
sigjmp_buf  jmpbuf;                     /* for long jump                             */
char        PIDFILE[200];               /* PIDFILE name                              */
int         PORT_CMD = 0;               /* listen port for command                   */
int         QID_MSG = 0;                /* message queue for content                 */
int         HEART_INTERVAL;             /* interval for idle message                 */
char        ENCODING;                   /* A for Ascii ，H for Hex                   */
int         sock_send = 0;              /* socket for sending                        */
int         sock_recv = 0;              /* socket for receiving                      */
int         pidSend = 0;                /* PID of sending process in JCB mode        */
int         pidRecv = 0;                /* PID of receiving process in JCB mode      */
int         g_shmid = 0;                /* share memory id for global status area    */
stat_st    *g_stat;                     /* global statsu area pointer                */
conn_config_st *p_conn_conf = NULL;     /* connection configs pointer                */
pack_config_st *p_pack_conf = NULL;     /* packing configs pointer                   */
int         server_sockfd;		        /* command listening socket                  */
int         pack_pit_quit;

char CONN_STATUS_STR[11][13]  = {
    "UNKNOWN    ",
    "ESTABLISHED",
    "REDIS ERROR",
    "CONNECTING ",
    "BIND FAIL  ",
    "LISTEN FAIL",
    "ACCEPT FAIL",
    "RECV ERROR ",
    "MSGRCVERROR",
    "SEND ERROR ",
    "ACCEPTING  "
};

/*
    parse matchup key , trannum , retcode from message
 */
int getTranInfo( char *message  , char *key  , char *trannum , char *retcode )
{
    if ( message == NULL && key == NULL ){
        return -1;
    }
    int offset = 0;
    offset += sprintf( key+offset , "matchup|" );

    /* fixed transaction */
    if ( message[4] >= '0' && message[4] <= '9'){
        /* get key */
        if (key){
            memcpy( key+offset , message+4 , 4);
            offset += 4;
            memcpy( key+offset , message+32 , 6);
            offset += 6;
        }
        /* get trannum */
        if (trannum)    memcpy( trannum , message + 4 , 4);
        /* get retcode */
        if (retcode)    memcpy( retcode , message + 8 , 6);

    } else {
        /* CUPS8583 transaction */
        char errmsg[30];
        CUPS_MESSAGE_t *m = CUPS8583_parseMessage( (BYTE *)(message+4) , errmsg);
        if ( m == NULL ){
            log_write(CONLOG, LOGERR, "%s", errmsg);
            return -1;
        }
        /* get key */
        if ( key ){
            /* filed7 */
            memcpy( key+offset , m->fields[6].pchData , m->fields[6].intDataLength );
            offset +=  m->fields[6].intDataLength;
            /* filed11 */
            memcpy( key+offset , m->fields[10].pchData , m->fields[10].intDataLength );
            offset +=  m->fields[10].intDataLength;
            /* filed32 */
            memcpy( key+offset , m->fields[31].pchData , m->fields[31].intDataLength );
            offset +=  m->fields[31].intDataLength;
            /* filed33 */
            memcpy( key+offset , m->fields[32].pchData , m->fields[32].intDataLength );
            offset +=  m->fields[32].intDataLength;
        }
        /* get trannum */
        if ( trannum )  memcpy( trannum , m->pbytMsgtype , 4 );
        /* get retcode */
        if ( retcode && m->bitmap.pbytFlags[38] == '1')  
            memcpy( retcode , m->fields[38].pchData , m->fields[38].intDataLength);
        CUPS8583_freeMessage(m);
    }
    return 0;
}
/*
    write text to file
 */
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
        log_write(CONLOG , LOGERR , "cannot open file [%s]",pathname);
        return -1;
    }

    fprintf( fp , "%ld.%06d " , ts.tv_sec , ts.tv_usec);

    if ( ENCODING == 'H' ){
        fprintf( fp , "HEX PRINT START\n");
        int i = 0;
        int j = 0;
        unsigned char ch = ' ';
        for ( i = 0 ; i <= len/16 ; i ++){
            fprintf( fp , "%06X " , i*16 );
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
    log_write(CONLOG , LOGDBG , "persist OK");
    return 0;
}
rule_st *get_rule(FILE *fp)
{
    char      line[150];
    char      buf[100];
    char      pathname[MAX_PATHNAME_LEN];

    char      temp[MAX_LINE_LEN];
    char      replace[MAX_REP_LEN];
    FILE      *rep_fp = NULL;
    rep_st    *rep_head = NULL;
    rep_st    *rep_tail = NULL;
    rep_st    *rep_cur = NULL;

    rule_st *cur = NULL;
    rule_st *ret = NULL;

    memset(line , 0x00 , sizeof(line));
    memset(buf , 0x00 , sizeof(buf));

    log_write(SYSLOG , LOGINF , "start to load rule file");

    while( fgets(line , sizeof(line) , fp) != NULL ){

        if ( line[0] == '\n' || line[0] == '#' )
            continue;

        cur = (rule_st *)malloc(sizeof(rule_st));
        if ( cur == NULL ){
            log_write(SYSLOG , LOGERR , "malloc for rule_st fail");
            fclose(fp);
            return NULL;
        }

        memset(cur , 0x00 , sizeof(rule_st));

        if (get_bracket(line , 1 , buf , 100))
        {
            log_write(SYSLOG , LOGERR , "format error in field1[start position]");
            return NULL;
        }
        cur->start = atoi(buf);
        log_write(SYSLOG , LOGINF , "read start position[%d]" , cur->start);

        if (get_bracket(line , 2 , buf , 100))
        {
            log_write(SYSLOG , LOGERR , "format error in field2[substution length]");
            return NULL;
        }
        cur->length = atoi(buf);
        log_write(SYSLOG , LOGINF , "read substution length[%d]" , cur->length);

        if (get_bracket(line , 3 , buf , 100))
        {
            log_write(SYSLOG , LOGERR , "format error in field3[padding mode]");
            return NULL;
        }
        cur->pad = atoi(buf);
        log_write(SYSLOG , LOGINF , "read padding mode[%d]" , cur->pad);

        if (get_bracket(line , 4 , buf , 100))
        {
            log_write(SYSLOG , LOGERR , "format error in field4[substution mode]");
            return NULL;
        }
        if ( strncmp( buf , "RAND" , 4 ) == 0  ){
            cur->type = REPTYPE_RANDOM;
        } else if ( strncmp( buf , "FILE:" , 5) == 0 || strncmp( buf, "PIN:" , 4) == 0 ){
            if ( strncmp( buf , "FILE:" , 5) == 0 ){
                cur->type = REPTYPE_FILE;
                memset(pathname , 0x00 , sizeof(pathname));
                sprintf(pathname , "%s/data/rep/%s" , getenv("PRESS_HOME") , buf+5);
            }
            else{
                cur->type = REPTYPE_PIN;
                memset(pathname , 0x00 , sizeof(pathname));
                sprintf(pathname , "%s/data/rep/%s" , getenv("PRESS_HOME") , buf+4);
            }
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
            cur->type = REPTYPE_F7;
        }
        log_write(SYSLOG , LOGINF , "read substution mode[%d]" , cur->type);

        cur->rep_head = rep_head;

        cur->next = ret;
        ret = cur;
    }
    log_write(SYSLOG , LOGINF , "rule file loaded");
    return ret;
}
void clean_rule(rule_st *ruleHead)
{
    rule_st *cur = ruleHead;
    rule_st *nex = ruleHead;
    rep_st  *rep_cur = NULL;

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
int get_template(FILE *fp_tpl , tpl_st *mytpl)
{
    if ( fread(mytpl->text , 1 , 4 , fp_tpl) < 4){
        log_write(SYSLOG , LOGERR , "fail to read first 4 bytes from tpl file");
        return -1;
    }
    mytpl->len = get_length(mytpl->text);
    if ( fread(mytpl->text + 4 , 1 , mytpl->len , fp_tpl) < mytpl->len ){
        log_write(SYSLOG , LOGERR , "fail to read tpl file , length[%d]" , mytpl->len);
        return -1;
    }
    return 0;
}
/*
    load connection configs from file
 */
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

    memset(pathname , 0x00 , sizeof(pathname));
    sprintf(pathname , "%s/cfg/conn.cfg" , getenv("PRESS_HOME"));
    fp = fopen(pathname , "r");
    if ( fp == NULL ){
        log_write(SYSLOG , LOGERR , "fopen fail ，cannot find config file [%s]" , pathname);
        return -1;
    }

    p_conn_conf->process_head = NULL;

    while (fgets(line , sizeof(line), fp) != NULL){

        if ( line[0] == '\n' || line[0] == '#' )
            continue;

        cur = (comm_proc_st *)malloc(sizeof(comm_proc_st));
        if ( cur == NULL ){
            log_write(SYSLOG , LOGERR , "malloc fail for comm_proc_st");
            fclose(fp);
            return -1;
        }

        /* TYPE */
        if (get_bracket(line , 1 , buf , 100)){
            log_write(SYSLOG , LOGERR , "fail to read [TYPE] in conn.cfg");
            fclose(fp);
            return -1;
        }
        cur->type = buf[0];

        /* IP */
        if (get_bracket(line , 2 , buf , 100)){
            log_write(SYSLOG , LOGERR , "fail to read [IP] from conn.cfg");
            fclose(fp);
            return -1;
        }
        strncpy(cur->ip , buf , sizeof(cur->ip));

        /* 端口 */
        if (get_bracket(line , 3 , buf , 100)){
            log_write(SYSLOG , LOGERR , "fail to read [PORT] from conn.cfg");
            fclose(fp);
            return -1;
        }
        cur->port = atoi(buf);

        /* 持久化开关 */
        if (get_bracket(line , 4 , buf , 100)){
            log_write(SYSLOG , LOGERR , "fail to read [PERSIST] from conn.cfg");
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

        /* monitor switch */
        if (get_bracket(line , 8 , buf , 100)){
            cur->monitor = 0;
        } else {
            cur->monitor = atoi(buf);
        }

        count ++;
        cur->next = p_conn_conf->process_head;
        p_conn_conf->process_head = cur;
    }

    if ( count == 0 ){
        log_write(SYSLOG , LOGERR , "empty conn.cfg");
        fclose(fp);
        return 0;
    }

    fclose(fp);
    p_conn_conf->status = 1;
    return 0;
}
/*
    free connection config
 */
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
    start receiver process
 */
int conn_receiver_start(comm_proc_st *p_receiver)
{
    int pid;
    pid = fork();
    if ( pid < 0 ){
        return -1;
    } else if ( pid > 0 ){
        return pid;
    }

    signal( SIGTERM , conn_receiver_signal_handler);
    /* close deamon server_sockfd first */
    close(server_sockfd);
    server_sockfd = 0;
    sock_send = 0;
    sock_recv = 0;

    server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
    struct          sockaddr_in server_sockaddr;
    char            buffer[MAX_LINE_LEN];
    struct sockaddr_in client_addr;
    socklen_t       socket_len = sizeof(client_addr);
    int             recvlen = 0;
    int             textlen = 0;
    int             nTotal = 0;
    int             nRead = 0;
    int             nLeft = 0;
    int             nTranlen = 0;
    int             ret = 0;
    struct timeval  ts;

    redisReply      *reply = NULL;
    char            redisMatchupKey[100];
    char            trannum[4+1];
    char            retcode[6+1];
    redisContext    *c = NULL;

    if ( p_receiver->monitor == 1 ){
        c = redisConnect("127.0.0.1", 6379);
        if(c->err) {
            log_write(SYSLOG, LOGERR, "redis server not found : %s\n", c->errstr);
            close(server_sockfd);
            exit(1);
        }
    }

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(p_receiver->port);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int nOptval = 1;
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR,(const void *)&nOptval , sizeof(int)) < 0)
    {
        log_write(SYSLOG , LOGERR , "setsockopt fail");
    }
    if (bind(server_sockfd , (struct sockaddr *)&server_sockaddr , sizeof(server_sockaddr)) == -1)
    {
        log_write(CONLOG , LOGERR , "bind port[%d] fail" , p_receiver->port);
        close(server_sockfd);
        conn_report_status( CONN_BINDFAIL );
        exit(1);
    }
    log_write(CONLOG , LOGINF , "bind port[%d] OK" , p_receiver->port);

    if(listen(server_sockfd , 1) == -1){
        log_write(CONLOG , LOGERR , "listen port[%d] fail" , p_receiver->port);
        close(server_sockfd);
        conn_report_status( CONN_LISTENFAIL );
        exit(1);
    }
    log_write(CONLOG , LOGINF , "listen port[%d] OK" , p_receiver->port);

    conn_report_status( CONN_ACCEPTING );
    sock_recv = accept(server_sockfd , (struct sockaddr *)&client_addr , &socket_len);
    if (sock_recv < 0){
        log_write(CONLOG , LOGERR , "accept on port[%d] fail" , p_receiver->port);
        close(server_sockfd);
        close(sock_recv);
        conn_report_status( CONN_ACCEPTFAIL );
        exit(1);
    }
    close(server_sockfd);
    conn_report_status( CONN_ESTABLISHED );
    log_write(CONLOG , LOGINF , "accept on port[%d] OK" , p_receiver->port);

    while(1){
        memset(buffer , 0x00 , sizeof(buffer));

        log_write(CONLOG , LOGDBG , "recv start on port[%d]",p_receiver->port);
        recvlen = recv(sock_recv , buffer , 4 , 0);
        if (recvlen < 0){
            log_write(CONLOG , LOGERR , "recv returns[%d] on port[%d] , process exit" , recvlen , p_receiver->port);
            conn_report_status( CONN_RECVERROR );
            break;
        } else if ( strncmp(buffer , "0000" , 4) == 0){
            log_write(CONLOG , LOGDBG , "recv get 0000 on port[%d]" , p_receiver->port);
            continue;
        } else {
            textlen = atoi(buffer);
            log_write(CONLOG , LOGDBG , "recv get [%02x%02x%02x%02x],textlen[%d]",
                    buffer[0],buffer[1],buffer[2],buffer[3],textlen);
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

        /* debug */
        usleep(100);

        if( p_receiver->monitor == 1 ){
            gettimeofday( &ts , NULL );
            /* update tps */
            reply = redisCommand(c, "incr recvtps|%d" , ts.tv_sec );
            freeReplyObject(reply);
            /* match up / average response time / successful rate */
            memset( redisMatchupKey , 0x00 , sizeof(redisMatchupKey) );
            memset( trannum , 0x00 , sizeof(trannum) );
            memset( retcode , 0x00 , sizeof(retcode) );
    
            if ( getTranInfo(buffer, redisMatchupKey, trannum, retcode) == 0 ){
                reply = redisCommand(c, "hget %s tv_sec" , redisMatchupKey);
                if ( reply->type == REDIS_REPLY_NIL ){
                    freeReplyObject(reply);
                    log_write(CONLOG, LOGERR, "[%s] matchup fail", redisMatchupKey);
                } else {
                    int second = atoi(reply->str);
                    freeReplyObject(reply);

                    reply = redisCommand(c, "hget %s tv_usec" , redisMatchupKey);
                    int usec = atoi(reply->str);
                    freeReplyObject(reply);

                    int duration = (ts.tv_sec - second)*1000000 + ts.tv_usec - usec;
    
                    reply = redisCommand( c , "del %s", redisMatchupKey);
                    freeReplyObject(reply);

                    reply = redisCommand( c , "incr trac|%s|total|%d", trannum , ts.tv_sec );
                    freeReplyObject(reply);

                    reply = redisCommand( c , "incrby trac|%s|duration|%d %d", trannum , ts.tv_sec , duration);
                    freeReplyObject(reply);
    
                    if ( strlen(retcode) && atoi(retcode) == 0 ){
                        reply = redisCommand( c , "incr trac|%s|suc|%d", trannum , ts.tv_sec);
                        freeReplyObject(reply);
                    }
                }
            } else {
                log_write(CONLOG, LOGERR , "unpack error");
            }
        }

        if ( p_receiver->persist == 1 ){
            nTranlen = get_length(buffer);
            if ( nTranlen < 0 ){
                log_write(CONLOG , LOGERR , "receive invalid length , fail to persist");
            } else {
                ret = persist( buffer , nTranlen + 4 , LONG_RECV , ts);
                if (ret < 0){
                    log_write(CONLOG , LOGERR , "fail to persist");
                }
            }
        }
    }
    close(sock_recv);
    close(server_sockfd);
    exit(0);
}
/*
    receiver signal handler
 */
void conn_receiver_signal_handler(int no)
{
    log_write(CONLOG , LOGDBG , "into conn_receiver_signal_handler , signo[%d]" , no);
    close(server_sockfd);
    close(sock_recv);
    close(sock_send);
    exit(0);
}
/*
    start connection sender process
 */
int conn_sender_start(comm_proc_st *p_sender)
{
    int pid;
    pid = fork();
    if ( pid < 0 ){
        return -1;
    } else if ( pid > 0 ){
        return pid;
    }
    /* close deamon server_sockfd first */
    close(server_sockfd);

    log_write(CONLOG , LOGINF , "sender process start [%c:%s:%d]" ,\
            p_sender->type ,  p_sender->ip , p_sender->port);
    signal( SIGTERM , conn_sender_signal_handler);

    struct sockaddr_in servaddr;
    msg_st msgs;
    int ret;
    int len = 0;
    int nTotal = 0;
    int nSent = 0;
    int nLeft = 0;
    struct timeval ts;

    int recvlen = 0;
    int textlen = 0;
    int nRead = 0;
    int nTranlen = 0;
    char buffer[MAX_LINE_LEN];

    redisReply      *reply = NULL;
    char            redisMatchupKey[100];
    redisContext    *c = NULL;

    if ( p_sender->monitor == 1 ){
        c = redisConnect("127.0.0.1", 6379);
        if(c->err){
            log_write(SYSLOG, LOGERR, "connection error:%s\n", c->errstr);
            conn_report_status( CONN_REDISERROR );
            exit(1);
        }
    }

    int connected = 0;
    while(1){
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
                    conn_report_status( CONN_CONNECTING );
                    sleep(1);
                    continue;
                }
                break;
            }
            conn_report_status( CONN_ESTABLISHED );
        }
        log_write(CONLOG , LOGDBG , "connect OK");

        if (HEART_INTERVAL > 0){
            sigsetjmp(jmpbuf , 1);
            signal(SIGALRM , conn_send_idle);
            alarm(HEART_INTERVAL);
        }

        log_write(CONLOG , LOGDBG , "start to msgrcv");

        ret = msgrcv(    (key_t)QID_MSG , \
                        &msgs , \
                        sizeof(msg_st) - sizeof(long) , \
                        0, \
                        0 );
        if ( ret < 0 ){
            log_write(CONLOG , LOGERR , "msgrcv fail ,ret[%d],msgid[%d],errno[%d]",ret,QID_MSG , errno);
            close(sock_send);
            close(server_sockfd);
            conn_report_status( CONN_MSGRCVERROR );
            exit(1);
        }
        if (HEART_INTERVAL > 0){
            alarm(0);
        }
        log_write(CONLOG , LOGDBG , "msgrcv OK ,len[%d]" ,msgs.length);

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
                log_write(CONLOG , LOGERR , "send fail,nSent[%d],errno[%d],process exit" , nSent , errno);
                close(sock_send);
                conn_report_status( CONN_SENDERROR );
                exit(0);
            }
            nTotal += nSent;
            nLeft -= nSent;
        }
        log_write(CONLOG , LOGDBG , "send OK , len[%d]" , len);

        if ( p_sender->monitor == 1 ){
            gettimeofday(&ts , NULL);
            /* insert match up info */
            if ( p_sender->type != 'X' ){
                memset( redisMatchupKey , 0x00 , sizeof(redisMatchupKey) );
                if ( getTranInfo(msgs.text, redisMatchupKey, NULL ,  NULL) == 0 ){
                    reply = redisCommand(c, "hmset %s tv_sec %d tv_usec %d " , redisMatchupKey , ts.tv_sec  , ts.tv_usec);
                    freeReplyObject(reply);
                }
            }  
            /* update sendtps */
            reply = redisCommand(c, "incr sendtps|%d" , ts.tv_sec );
            freeReplyObject(reply);
        }

        if ( p_sender->persist == 1 ){
            if ( p_sender->type == 'S' )
                msgs.type = LONG_SEND;
            else if ( p_sender->type == 'X' )
                msgs.type = SHORTCONN;
            ret = persist(msgs.text , msgs.length , msgs.type , ts);
            if (ret < 0){
                log_write(SYSLOG , LOGERR , "fail to persist");
            }
        }
        
        if ( p_sender->type == 'X' ){
            log_write(CONLOG , LOGDBG , "start to recv" ,p_sender->type ,  p_sender->ip , p_sender->port);
            memset( buffer , 0x00 , sizeof(buffer) );

            signal( SIGALRM , conn_sender_signal_handler);
            alarm(10);
            recvlen = recv(sock_send , buffer , 4 , 0);
            if (recvlen <= 0){
                close(sock_send);
                if ( errno == EINTR ){
                    log_write(CONLOG , LOGERR , "recv wait time out 10s , try to reconnect" );
                    alarm(0);
                } else {
                    log_write(CONLOG , LOGERR , "recv returns[%d] , try to reconnect" , recvlen);
                }
                continue;
            } else if ( strncmp(buffer , "0000" , 4) == 0){
                log_write(CONLOG , LOGDBG , "recv get 0000");
                close( sock_send );
                continue;
            } else {
                textlen = atoi(buffer);
                log_write(CONLOG , LOGDBG , "recv length OK ,buffer[%02x|%02x|%02x|%02x]" ,\
                    buffer[0],buffer[1],buffer[2],buffer[3]);
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

            if ( p_sender->persist == 1 ){
                nTranlen = get_length(buffer);
                if ( nTranlen < 0 ){
                    log_write(CONLOG , LOGERR , "persist fail" );
                } else {
                    gettimeofday( &ts , NULL );
                    ret = persist(buffer , nTranlen + 4 , SHORTCONN , ts);
                    if (ret < 0){
                        log_write(CONLOG , LOGERR , "persist fail" );
                    }
                }
            }
            close( sock_send );
        }
    }
    close(sock_send);
    exit(0);
}
/*
    sender signal handler
 */
void conn_sender_signal_handler(int no)
{
    switch(no){
        case SIGTERM :
            close(server_sockfd);
            close(sock_send);
            exit(0);
        case SIGALRM :
            return;
    }
}
/*
    start connection jips process
 */
int conn_jips_start(comm_proc_st *p_jips)
{
    int pid;
    pid = fork();
    if ( pid < 0 ){
        return -1;
    } else if ( pid > 0 ){
        return pid;
    }

    log_write(CONLOG , LOGINF , "connection process start on port [%d]" , p_jips->port);
    close(server_sockfd);

    server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
    struct sockaddr_in server_sockaddr;
    char buffer[MAX_LINE_LEN];
    struct sockaddr_in client_addr;
    socklen_t socket_len = sizeof(client_addr);
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

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(p_jips->port);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sockfd , (struct sockaddr *)&server_sockaddr , sizeof(server_sockaddr)) == -1)
    {
        log_write(CONLOG , LOGERR , "bind port[%d] fail" , p_jips->port);
        exit(1);
    }
    log_write(CONLOG , LOGINF , "bind port[%d] OK" , p_jips->port);

    if(listen(server_sockfd , 1) == -1){
        log_write(CONLOG , LOGERR , "listen on port[%d] fail" , p_jips->port);
        exit(1);
    }
    log_write(CONLOG , LOGINF , "listen on port[%d] OK" , p_jips->port);

    sock_recv = accept(server_sockfd , (struct sockaddr *)&client_addr , &socket_len);
    if (sock_recv < 0){
        log_write(SYSLOG , LOGERR , "accept on port[%d] fail" , p_jips->port);
        exit(1);
    }
    log_write(CONLOG , LOGINF , "accept on port[%d] OK" , p_jips->port);

    /* start receiving process */
    pidRecv = fork();
    if ( pidRecv == 0 ){
        while(1){
            memset(buffer , 0x00 , sizeof(buffer));
            memset(&msgs , 0x00 , sizeof(msg_st));

            log_write(CONLOG , LOGDBG , "start to recv");
            recvlen = recv(sock_recv , buffer , 4 , 0);
            if (recvlen <= 0){
                log_write(CONLOG , LOGERR , "recv returns[%d] , process exit" , recvlen);
                close(sock_recv);
                exit(1);
            } else if ( strncmp(buffer , "0000" , 4) == 0){
                log_write(CONLOG , LOGDBG , "recv 0000");
                continue;
            } else {
                textlen = buffer[2]*16*16*16 + buffer[3]*16*16+ buffer[0]*16 + buffer[1]*1 ;
                log_write(CONLOG , LOGDBG , "recv length %d]" , textlen);
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

            log_write(CONLOG , LOGDBG , "recv OK ,textlen[%d]" , p_jips->port , textlen);
            
            if ( p_jips->persist == 1 ){
                msgs.type = 1;
                memcpy(msgs.text , buffer , textlen + 4);
                sprintf( lenAscii , "%4d" , textlen);
                memcpy( msgs.text , lenAscii , 4);

                gettimeofday( &ts , NULL );
                ret = persist( msgs.text , textlen + 4 , JIPSCONN , ts);
                if (ret < 0){
                    log_write(SYSLOG , LOGERR , "persist fail");
                }
            }
        }
    } else if ( pidRecv < 0 ){
         return -1;
    }

    /* start jips sender process */
    pidSend = fork();
    if ( pidSend == 0 ){
        while(1){
            if (HEART_INTERVAL > 0){
                sigsetjmp(jmpbuf , 1);
                signal(SIGALRM , conn_send_idle);
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

            log_write(CONLOG , LOGDBG , "msgrcv OK , len[%d]"  , len);

            nTotal = 4;
            nSent = 0;
            nLeft = len;
            while( nTotal != len + 4){
                nSent = send(sock_recv , msgs.text + nTotal , nLeft , 0);
                if (nSent == 0){
                    break;
                }
                else if (nSent < 0){
                    log_write(SYSLOG , LOGERR , "send fail,nSentt[%d],errno[%d]",nSent,errno);
                    close(sock_recv);
                    exit(0);
                }
                nTotal += nSent;
                nLeft -= nSent;
            }
            log_write(CONLOG , LOGDBG , "send OK , len[%d]" , len);

            if ( p_jips->persist == 1 ){
                gettimeofday(&ts , NULL);
                ret = persist(msgs.text , msgs.length+4 , JIPSCONN , ts);
                if (ret < 0){
                    log_write(SYSLOG , LOGERR , "persist fail");
                }
            }
        }

    } else if ( pidSend < 0 ){
        return -1;
    }

    /*parent sleep and wait for signal */
    close(sock_recv);
    signal( SIGTERM , conn_jips_signal_handler);
    log_write(CONLOG , LOGINF , "starting children started , pidSend[%d]" , pidSend);
    log_write(CONLOG , LOGINF , "receiving children startd  , pidRecv[%d]" , pidRecv);
    
    while(1){
        sleep(10);
        if ( kill( pidRecv , 0 ) == 0 && kill( pidSend , 0) == 0 ){
            log_write(CONLOG , LOGINF , "status check OK , port[%d], pidRecv[%d] , pidSend[%d]" , p_jips->port , pidRecv , pidSend);
            continue;
        } else {
            kill( pidRecv , 9);
            kill( pidSend , 9);
            log_write(CONLOG , LOGERR , "status check fail ，killing pidRecv[%d] , pidSend[%d]" , pidRecv , pidSend);
            exit(-1);
        }
    }
}
/*
    jips signal handler
 */
void conn_jips_signal_handler(int signo)
{
    kill( pidRecv , SIGTERM);
    kill( pidSend , SIGTERM);
    log_write(CONLOG , LOGINF , "killing pidRecv[%d] , pidSend[%d]" , pidRecv , pidSend);
    exit(0);
}
/*
    report connection process status to deamon
 */
void conn_report_status( int status )
{
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT_CMD);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int sock = 0;
    int retry = 3;
    while(retry --){
        sock = socket(AF_INET , SOCK_STREAM , 0);
        if (connect(sock , (struct sockaddr*)&servaddr , sizeof(servaddr)) < 0){
            close(sock);
            usleep(5000);
        } else {
            break;
        }
    }
    char message[30];
    memset( message , 0x00 , sizeof(message));
    sprintf( message , "report %d %d" , getpid() , status);
    send( sock , message , strlen(message) , 0);
    close(sock);
    log_write(CONLOG, LOGDBG, "%s", message);
    return;
}
/*
    send idle message    
*/
void conn_send_idle(int signo)
{
    send(sock_send , "0000" , 4 , 0);
    siglongjmp(jmpbuf , 1);
}

/*
    load packing module configs
 */
int pack_config_load( char *filename , pack_config_st *p_pack_conf)
{
    char     config_fn[MAX_FILENAME_LEN];
    FILE    *fp = NULL;
    char     line[200];
    char     buf[100];
    int     count = 0;
    char     pathname[MAX_PATHNAME_LEN];

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
        log_write(SYSLOG , LOGERR , "cannot find packing config file[%s]" , filename);
        return -1;
    }

    while (fgets(line , sizeof(line), fp) != NULL) {

        if ( line[0] == '\n' || line[0] == '#' )
            continue;

        log_write(SYSLOG , LOGDBG , "read pack.cfg line[%s]" , line);

        pit_cur = (pit_proc_st *)malloc(sizeof(pit_proc_st));
        if ( pit_cur == NULL ){
            log_write(SYSLOG , LOGERR , "malloc for pit_proc_st fail");
            fclose(fp);
            return -1;
        }

        pit_cur->index = count;

        /* get tpl file */
        if (get_bracket(line , 1 , buf , 100)){
            log_write(SYSLOG , LOGERR , "format error in filed1[tplfilename]");
            fclose(fp);
            return -1;
        }
        log_write(SYSLOG , LOGDBG , "read tplfilename[%s]" ,buf );

        strcpy(pit_cur->tplFileName , buf);
        memcpy(pit_cur->trannum , buf , 4);
        pit_cur->trannum[4] = '\0';

        memset(pathname , 0x00 , sizeof(pathname));
        sprintf(pathname , "%s/data/tpl/%s" , getenv("PRESS_HOME") , buf);
        pit_cur->tpl_fp = fopen(pathname , "r");
        if ( pit_cur->tpl_fp == NULL ){
            log_write(SYSLOG , LOGERR , "cannnot find tpl file [%s]",pathname);
            fclose(fp);
            return -1;
        }

        /* get rule */
        if (get_bracket(line , 2 , buf , 100)){
            log_write(SYSLOG , LOGERR , "format error in filed2[rule file]");
            fclose(fp);
            return -1;
        }
        log_write(SYSLOG , LOGDBG , "read fule filename[%s]" ,buf );
        memset(pathname , 0x00 , sizeof(pathname));
        sprintf(pathname , "%s/data/rule/%s" , getenv("PRESS_HOME") , buf);
        pit_cur->rule_fp = fopen(pathname , "r");
        if ( pit_cur->rule_fp == NULL ){
            log_write(SYSLOG , LOGERR , "cannot find rule file[%s]",pathname);
            fclose(fp);
            return -1;
        }

        /*get tps*/
        if (get_bracket(line , 3 , buf , 100)){
            log_write(SYSLOG , LOGERR , "format error in filed3[tps]");
            fclose(fp);
            return -1;
        }
        pit_cur->tps = atoi(buf);
        log_write(SYSLOG , LOGDBG , "read tps[%d]" ,pit_cur->tps );

        /* get time */
        if (get_bracket(line , 4 , buf , 100)){
            log_write(SYSLOG , LOGERR , "format error in filed4[time]");
            fclose(fp);
            return -1;
        }
        pit_cur->time = atoi(buf);
        log_write(SYSLOG , LOGDBG , "read time[%d]" ,pit_cur->time );

        /* get qid */
        count ++;
        pit_cur->next = p_pack_conf->pit_head;
        p_pack_conf->pit_head = pit_cur;

    }

    if ( count == 0 ){
        log_write(SYSLOG , LOGERR , "empty pack.cfg");
        fclose(fp);
        return 0;
    }

    fclose(fp);
    log_write(SYSLOG , LOGDBG , "packing config loaded");
    return 0;
}
/*
    free packing configs
 */
int pack_config_free(pack_config_st *p_pack_conf)
{
    pack_config_st *cur = p_pack_conf;
    pit_proc_st *pit_cur = NULL;
    pit_proc_st *pit_del = NULL;
    if ( cur == NULL )
    {
        return 0;
    }
    pit_cur = cur->pit_head;

    while ( pit_cur != NULL ){
        pit_del = pit_cur;
        pit_cur = pit_cur->next;
        fclose(pit_del->tpl_fp);
        fclose(pit_del->rule_fp);
        if ( pit_del )
            free(pit_del);
    }

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

    pack_pit_quit = 0;
    signal( SIGTERM , pack_pit_signal_handler);

    log_write(PCKLOG , LOGINF , "packing process start[%s]" , p_pitcher->tplFileName);
    /* relocate share memory */
    g_stat = (stat_st *)shmat(g_shmid , NULL,  0);
    if ( g_stat == NULL ){
        log_write(SYSLOG , LOGERR , "shmat for stat_st fail");
        return -1;
    }

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

    time_t t;
    struct tm* lt;

    /* relocate l_stat and update share memory */
    stat_st *l_stat = g_stat + p_pitcher->index;
    l_stat->tag = RUNNING;

    /* load template */
    memset(&mytpl , 0x00 , sizeof(mytpl));
    if (get_template(p_pitcher->tpl_fp , &mytpl)){
        log_write(PCKLOG , LOGERR , "load tpl file fail");
        fclose(p_pitcher->tpl_fp);
        exit(-1);
    }
    log_write(PCKLOG , LOGDBG , "tpl file loaded");
    /* load rules */
    ruleHead = get_rule(p_pitcher->rule_fp);
    if (ruleHead == NULL){
        log_write(PCKLOG , LOGERR , "load rule file fail");
        fclose(p_pitcher->rule_fp);
        exit(-1);
    }
    log_write(PCKLOG , LOGDBG , "load file loaded");

    msgs.type = 1;
    msgs.length = mytpl.len + 4;
    gettimeofday(&tv_begin,NULL);
    while( l_stat->left_num > 0 && pack_pit_quit == 0 ){
        log_write(PCKLOG , LOGDBG , "start to pack");
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
                memcpy(msgs.text + currule->start - 1 + 4 , temp , currule->length);
                log_write(PCKLOG , LOGDBG , "RANDOM[%s]" , temp);
            } else if ( currule->type == REPTYPE_FILE ){
                strcpy(temp , currule->rep_head->text);
                currule->rep_head = currule->rep_head->next;
                log_write(PCKLOG , LOGDBG , "FILE[%s]" , temp);
                memcpy(msgs.text + currule->start - 1 + 4 , temp , currule->length);
            } else if ( currule->type == REPTYPE_F7 ){
                time(&t);
                lt = localtime(&t);
                sprintf(temp,"%02d%02d%02d%02d%02d",lt->tm_mon+1,lt->tm_mday,lt->tm_hour,lt->tm_min,lt->tm_sec);
                log_write(PCKLOG , LOGDBG , "F7[%s]" , temp);
                memcpy(msgs.text + currule->start - 1 + 4 , temp , currule->length);
            } else if ( currule->type == REPTYPE_PIN ) {
                unsigned int c = 0;
                int i = 0;
                log_write(PCKLOG , LOGDBG , "read pinblock[%s]" , currule->rep_head->text);
                for ( i = 0 ; i <  currule->length / 2 ; i ++ ){
                    sscanf( currule->rep_head->text + 2*i  , "%02x" , &c );
                    sprintf( temp+i , "%c" , (unsigned char)c );
                    log_write(PCKLOG , LOGDBG , "pinblock after convert[%d][%02x]" , i , (unsigned char )c);
                }
                memcpy(msgs.text + currule->start - 1 + 4 , temp , currule->length / 2 );
                currule->rep_head = currule->rep_head->next;
            }
            currule = currule->next;
        }
        log_write(PCKLOG , LOGDBG , "pack OK , prepare to send to queue");
        /* send to out queue */
        ret = msgsnd((key_t)QID_MSG , &msgs , sizeof(msg_st) - sizeof(long) , 0);
        if (ret < 0){
            log_write(PCKLOG , LOGERR , "msgsnd fail，ret[%d],errno[%d]",ret,errno);
            exit(1);
        }
        log_write(PCKLOG , LOGDBG , "package sent to queue");
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
    //clean_rule(ruleHead);
    exit(0);
}
void pack_pit_signal_handler(int signo)
{
    pack_pit_quit = 1;
    return ;
}
/*
    stop connection module
 */
int command_conn_stop(conn_config_st *p_conn_conf)
{
    comm_proc_st *cur;

    if (p_conn_conf == NULL){
        return -1;
    }

    cur = p_conn_conf->process_head;
    while (cur != NULL){
        if ( cur->type == 'X' ){
            int i = 0;
            for ( i = 0 ;i < cur->parallel ; i ++ ){
                if (cur->para_pids[i] > 0 ){
                    kill(cur->para_pids[i] , SIGTERM);
                    log_write(SYSLOG , LOGINF , "connection process pid[%d] stop OK", cur->pid);
                }
            }
        } else {
            if ( cur->pid > 0 ){
                kill(cur->pid , SIGTERM);
                log_write(SYSLOG , LOGINF , "connection process pid[%d] stop OK", cur->pid);
            }

        }
        cur=cur->next;
    }
    p_conn_conf->status = NOTLOADED;
    return 0;
}
/*
    start connection module
 */
int command_conn_start(conn_config_st *p_conn_conf)
{
    int ret = 0;
    log_write(SYSLOG , LOGINF , "starting conn module");
    ret = conn_config_load(p_conn_conf);
    if (ret < 0){
        log_write(SYSLOG , LOGERR , "fail to load conn.cfg");
        exit(1);
    }
    log_write(SYSLOG , LOGINF , "conn.cfg loaded");

    comm_proc_st *cur = p_conn_conf->process_head;

    if (p_conn_conf == NULL || p_conn_conf->process_head == NULL)
        return -1;

    while (cur != NULL){
        if ( cur->type == 'R' ){
            ret = conn_receiver_start(cur);
            if ( ret < 0 ){
                log_write(SYSLOG , LOGERR , "connection process start fail , TYPE[R] , IP[%s] , PORT[%d]",cur->ip , cur->port);
            } else {
                log_write(SYSLOG , LOGINF , "connection process start OK ,  TYPE[R] , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
                cur->pid = ret;
            }

        } else if ( cur->type == 'S' ){
            ret = conn_sender_start(cur);
            if ( ret < 0 ){
                log_write(SYSLOG , LOGERR , "connection process start fail TYPE[S] , IP[%s] , PORT[%d]",cur->ip , cur->port);
            } else {
                log_write(SYSLOG , LOGINF , "connection process start OK TYPE[S] , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
                cur->pid = ret;
            }
        } else if ( cur->type == 'J' ){
            ret = conn_jips_start(cur);
            if ( ret < 0 ){
                log_write(SYSLOG , LOGERR , "connection process start fail TYPE[J] , IP[%s] , PORT[%d]",cur->ip , cur->port);
            } else {
                log_write(SYSLOG , LOGINF , "connection process start OK TYPE[J] , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
                cur->pid = ret;
            }
        } else if ( cur->type == 'X' ){
            int i = 0 ;
            for ( i = 0 ; i < cur->parallel ; i ++ ){
                ret = conn_sender_start(cur);
                if ( ret < 0 ){
                    log_write(SYSLOG , LOGERR , "connection process start OK , TYPE[X] , IP[%s] , PORT[%d]",cur->ip , cur->port);
                } else {
                    log_write(SYSLOG , LOGINF , "connection process start fail ,  TYPE[X] , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
                    cur->para_pids[i] = ret;
                }
            }
        }
        cur = cur->next;
    }
    p_conn_conf->status = RUNNING;
    return 0;
}

int command_pack_load(char *msg , pack_config_st *p_pack_conf)
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

    return 0;
}

int command_pack_send(pack_config_st *p_pack_conf)
{
    pit_proc_st *cur = p_pack_conf->pit_head;
    /* start pitchers */
    while ( cur != NULL ) {
        cur->pid = pack_pit_start(cur);
        log_write(SYSLOG , LOGINF , "packing process start,PID[%d]" ,cur->pid);
        cur = cur->next;
    }
    p_pack_conf->status = RUNNING;

    return 0;
}

int command_pack_shut(pack_config_st *p_pack_conf)
{
    pit_proc_st     *pit_cur = p_pack_conf->pit_head;
    stat_st         *l_stat = NULL;

    while ( pit_cur != NULL ){
        if( pit_cur->pid > 0 ){
            kill(pit_cur->pid , SIGTERM);
            l_stat = g_stat+pit_cur->index;
            l_stat->tag = FINISHED;
            log_write(SYSLOG , LOGINF , "packing process stop,pid[%d]" , pit_cur->pid);
        }
        pit_cur = pit_cur->next;
    }

    p_pack_conf->status = FINISHED;

    return 0;
}

char *command_get_stat(int flag , conn_config_st *p_conn_conf , pack_config_st *p_pack_conf)
{
    char *ret = (char *)malloc(MAX_REPLY_LEN);
    int offset = 0;
    char status[20];
    char type[20];
    struct  timeval nowTimeStamp;
    /*struct  timeval timeInterval;*/
    memset(ret , 0x00 , MAX_REPLY_LEN);
    comm_proc_st *p_comm = p_conn_conf->process_head;
    pit_proc_st  *p_pack = p_pack_conf->pit_head;
    stat_st *l_stat;
    if ( (flag & STAT_CONN) ){
        if ( p_conn_conf->status == RUNNING ){
            offset += sprintf(ret+offset , \
                    "CONNECTION MONITOR\n");
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
            offset += sprintf(ret+offset , \
                    "[TYPE][IP             ][PORT  ][PID     ][STATUS         ]\n");
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
                    strcpy(status , CONN_STATUS_STR[p_comm->status]);
                }

                type[0] = p_comm->type;

                offset += sprintf(ret+offset , 
                                "[%-4s][%-15s][%-6d][%-8d][%-15s]\n" , \
                                type , \
                                p_comm->ip ,\
                                p_comm->port ,\
                                p_comm->pid,\
                                status);
                p_comm = p_comm->next;
            }
            /*
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
            */
        } else {
            offset += sprintf(ret+offset , "NO CONNECTION PROCESS,ENTER init TO START CONNECTION MODULE\n");
        }
    }
    if ( (flag & STAT_PACK) ){
        if ( p_pack_conf->status == LOADED || p_pack_conf->status == RUNNING ) {
            offset += sprintf(ret+offset , \
                    "\nPACKING MONITOR\n");
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
            offset += sprintf(ret+offset , \
                    "[INDEX][TPLFILE    ][STATUS     ][SET TPS ][PACKAGE SENT][SENTTIME/TOTALTIME]\n");
            while ( p_pack != NULL ) {
                l_stat = g_stat + p_pack->index;
                memset( status , 0x00 , sizeof(status));
                if ( l_stat->tag != NOTLOADED ){
                    if ( l_stat->tag == RUNNING ){
                        strcpy(status , "RUNNING    ");
                    } else if ( l_stat->tag == LOADED ){
                        strcpy(status , "CONF LOADED");
                    } else if ( l_stat->tag == FINISHED ){
                        strcpy(status , "ALL SENT   ");
                    }
                    offset += sprintf(ret+offset , \
                                    "[%-5d][%-11s][%-11s][%-8d][%-12d][%-8d/%-9d]\n" , \
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
            /*
            offset += sprintf(ret+offset , \
                    "===========================================================================\n");
            */
        } else {
            offset += sprintf(ret+offset , "NO PACKING PROCESS, ENTER load TO LOAD CONFIGS\n");
        }
    }
    if ( flag & STAT_MONI ){

        if ( p_pack_conf->status == RUNNING ) {

            /* connect to redis*/        
            redisContext *c = redisConnect("127.0.0.1", 6379);
            if(c->err){

                offset += sprintf(ret+offset , "redis error : %s\n" , c->errstr);
            } else {

                offset += sprintf(ret+offset , \
                    "\nTPS-MONITOR\n");
                offset += sprintf(ret+offset , \
                    "===========================================================================\n");
                redisReply *reply = NULL;
                redisReply *reply1 = NULL;
                int sendtps = 0;
                int recvtps = 0;
                time_t t = 0;
                struct tm *temp = NULL;
                char trannum[4+1];
                double  recvnum = 0.0;
                int  sucnum = 0;
                int  duration = 0;
                int  i = 0;
                gettimeofday(&nowTimeStamp,NULL);

                for( i = 1 ; i <= 5 ; i ++){
                    t = nowTimeStamp.tv_sec - i;
                    temp = localtime(&t);
                    /* time */
                    offset += sprintf(ret+offset , "[%02d:%02d:%02d]",temp->tm_hour , temp->tm_min , temp->tm_sec);

                    /* tps */
                    reply = redisCommand( c , "get sendtps|%d", nowTimeStamp.tv_sec-i);
                    if ( reply->type != REDIS_REPLY_NIL && reply->str )   sendtps = atoi(reply->str);
                    freeReplyObject(reply);
                    reply = redisCommand( c , "get recvtps|%d", nowTimeStamp.tv_sec-i);
                    if ( reply->type != REDIS_REPLY_NIL && reply->str )   recvtps = atoi(reply->str);
                    freeReplyObject(reply);
                    offset += sprintf(ret+offset , "[SENDTPS:%-8d][RECVTPS:%-8d]\n",sendtps,recvtps);
                }      

                /* ratio/response time*/
                offset += sprintf(ret+offset , \
                    "\nTRANSACTION-MONITOR\n");
                offset += sprintf(ret+offset , \
                    "===========================================================================\n");
                offset += sprintf(ret+offset , "[TRAN][RATIO  ][RESPONSE ]\n");


                pit_proc_st *proc = p_pack_conf->pit_head;
                while ( proc != NULL ){
                    strcpy( trannum , proc->trannum);
                    recvnum = 0;
                    sucnum = 0;
                    duration = 0;
                    for( i = 1 ; i <=5 ; i ++ ){
                        reply1 = redisCommand(c, "get trac|%s|total|%d", trannum , nowTimeStamp.tv_sec-i);
                        if ( reply1->type != REDIS_REPLY_NIL )  recvnum += atoi(reply1->str);
                        freeReplyObject(reply1);
                        reply1 = redisCommand(c, "get trac|%s|suc|%d", trannum , nowTimeStamp.tv_sec-i);
                        if ( reply1->type != REDIS_REPLY_NIL )  sucnum += atoi(reply1->str);
                        freeReplyObject(reply1);
                        reply1 = redisCommand(c, "get trac|%s|duration|%d", trannum , nowTimeStamp.tv_sec-i);
                        if ( reply1->type != REDIS_REPLY_NIL )  duration += atoi(reply1->str);
                        freeReplyObject(reply1);
                    }
                    if ( recvnum > 0 ){
                        offset += sprintf(ret+offset , "[%4s][%-3.2f%%][%.2fms]\n" ,\
                                trannum , (double)sucnum*100/recvnum , (double)duration/recvnum/1000);
                    }
                    proc = proc->next;
                }
            }
            redisFree(c);
        } else {
            offset += sprintf(ret+offset , "NO REAL TIME MONITOR");
        }
    }
    return ret;
}

char *command_adjust_status(int flag , char *msg , pack_config_st *p_pack_conf)
{
    char *ret = (char *)malloc(MAX_REPLY_LEN);
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
                offset += sprintf(ret+offset , "[%s]command format error , lacking number",msg);
                return ret;
            }
            break;
        }
        if ( msg[i] < '0' || msg[i] > '9' ){
            offset += sprintf(ret+offset , "[%s]command format error , non-number detacted",msg);
            return ret;
        }
        adjustment*=10;
        adjustment+=(msg[i] - '0');
    }
    if ( msg[i] == '%' ){
        if ( direc == 0 ){
            offset += sprintf(ret+offset , "[%s]command format error , lack +/-",msg);
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
        deamon_status_op(flag , id , adjustment , percent , direc , &before , &after);
        if ( flag == 1 )
            offset += sprintf(ret+offset , "alter packing process[%d] TPS from [%d] to [%d] OK" , id , before , after);
        else
            offset += sprintf(ret+offset , "alter packing process[%d] TIME from [%d] to [%d] OK" , id , before , after);
    } else {
        while ( p_pack != NULL ){
            deamon_status_op(flag , p_pack->index , adjustment , percent , direc , &before , &after);
            if ( flag == 1 )
                offset += sprintf(ret+offset , "alter packing process[%d] TPS from [%d] to [%d] OK\n" , p_pack->index , before , after);
            else
                offset += sprintf(ret+offset , "alter packing process[%d] TIME from [%d] to [%d] OK\n" , p_pack->index , before , after);
            p_pack = p_pack->next;
        }
    }
    return ret;
}

char *command_adjust_para( char *msg , conn_config_st *p_conn_config)
{
    int new_para = 0;
    comm_proc_st *shortconn = p_conn_config->process_head;
    int i = 0;
    int offset = 0;
    char *ret = (char *)malloc(MAX_REPLY_LEN);
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
        offset = sprintf( ret+offset , "same parallel number");
    } else if ( new_para > shortconn->parallel ){
        for ( i = shortconn->parallel ; i < new_para ; i ++ ){
            pid = conn_sender_start(shortconn);
            if ( pid < 0 ) {
                log_write(SYSLOG , LOGERR , "connection process start OK , IP[%s] , PORT[%d]",shortconn->ip , shortconn->port);
            } else {
                log_write(SYSLOG , LOGINF , "connection process start fail , IP[%s] , PORT[%d] , PID[%d]",shortconn->ip , shortconn->port , ret);
                shortconn->para_pids[i] = pid;
            }
        }
        offset = sprintf( ret+offset , "alter parallel number to [%d] OK",new_para);
        shortconn->parallel = new_para;
    } else if ( new_para < shortconn->parallel ){
        for ( i = shortconn->parallel-1 ; i >= new_para ; i -- ){
            kill( shortconn->para_pids[i] , 9 );
        }
        offset = sprintf( ret+offset , "alter parallel number to [%d] OK",new_para);
        shortconn->parallel = new_para;
    } else if ( new_para <= 0 ){
        offset = sprintf( ret+offset , "invalid parallel number");
    }
    return ret;
}
void command_update_conn_status( char *buffer_cmd , conn_config_st *p_conn_conf )
{
    int pid = 0;
    int status = 0;
    sscanf( buffer_cmd+7 , "%d %d" , &pid , &status);
    log_write(SYSLOG, LOGDBG, "update conn status [%d][%d]", pid , status);
    comm_proc_st *temp = p_conn_conf->process_head;
    while ( temp ){
        if ( temp->pid == pid ){
            temp->status = status;
            break;
        }
        temp = temp->next;
    }
    return;
}
void deamon_status_op(int flag , int id , int adjustment , int percent , int direc , int *before , int *after)
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

int deamon_check()
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
void deamon_start()
{
    pid_t pid = 0;
    pid = fork();
    if ( pid < 0 ){
        printf("fork fail\n");
        exit(-1);
    } else if ( pid > 0 ){
        exit(0);
    } else {
        if(setsid() == -1)
        {
            printf("setsid fail , errno[%d]\n",errno);
            exit(1);
        }
        umask(0);
        return; 
    }
}
void deamon_exit()
{
    log_write(SYSLOG , LOGINF ,"enter deamon_exit");
    close(server_sockfd);
    if ( msgctl((key_t)QID_MSG , IPC_RMID , NULL) ){
        log_write(SYSLOG , LOGERR ,"delete msgid=%d fail",QID_MSG);
    } else {
        log_write(SYSLOG , LOGINF ,"delete msgid=%d OK",QID_MSG);
    }

    shmdt((void *)g_stat);
    if ( shmctl(g_shmid , IPC_RMID , 0) ){
        log_write(SYSLOG , LOGERR ,"delete shmid=%d fail",g_shmid);
    } else {
        log_write(SYSLOG , LOGINF ,"delete shmid=%d OK",g_shmid);
    }

    //log_clear();

    pack_config_free(p_pack_conf);
    conn_config_free(p_conn_conf);

    remove(PIDFILE);
    exit(0);
}

void deamon_signal_handler( int signo)
{
    deamon_exit();
}
void deamon_print_help()
{
    printf("press [-p port] [-l loglevel] [-h]\n");
    printf("       -p specify listening port for command(default:6043)\n");
    printf("       -l specify log level[1-7](default:4)\n");
    return ;
}
int main(int argc , char *argv[])
{
    if ( getenv("PRESS_HOME") == NULL ){
        printf("lacking environment parameter :${PRESS_HOME}\n");
        exit(1);
    }
    int op = 0;
    PORT_CMD = 6043; 
    LOGLEVEL = 4; 
    while ( ( op = getopt( argc , argv , "p:l:h") ) > 0 ) {
        switch(op){
            case 'h' :
                deamon_print_help();
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

    if ( deamon_check() ){
        printf("deamon already started , do not rerun press\n");
        exit(1);
    }

    deamon_start();

    log_init(SYSLOG , "system");
    log_init(PCKLOG , "packing");
    log_init(CONLOG , "connection");

    FILE *fp = fopen( PIDFILE , "wb");
    if ( fp == NULL ){
        log_write(SYSLOG , LOGERR , "cannot open pidfile %s , errno = %d" , PIDFILE , errno) ;
    } else {
        fprintf(fp , "%d" , getpid());
        fclose(fp);
    }
    log_write(SYSLOG , LOGINF , "deamon start OK");
    log_write(SYSLOG , LOGINF , "${PRESS_HOME}    [%s]" , getenv("PRESS_HOME"));
    log_write(SYSLOG , LOGINF , "listenning port  [%d]" , PORT_CMD);

    signal(SIGCHLD , SIG_IGN);
    signal(SIGKILL , deamon_signal_handler);
    signal(SIGTERM , deamon_signal_handler);

    char *retmsg = NULL;

    if ( (QID_MSG = msgget(IPC_PRIVATE , IPC_CREAT|0660)) < 0){
        log_write(SYSLOG , LOGERR , "init message queue fail");
        deamon_exit();
    }
    log_write(SYSLOG , LOGINF , "init message queue OK , QID_MSG=%d" , QID_MSG);

    p_conn_conf = (conn_config_st *)malloc(sizeof(conn_config_st));
    if ( p_conn_conf == NULL ){
        log_write(SYSLOG , LOGERR , "malloc for conn_config_st fail");
        deamon_exit();
    }
    p_conn_conf->status = NOTLOADED;

    p_pack_conf = (pack_config_st *)malloc(sizeof(pack_config_st));
    if ( p_pack_conf == NULL ){
        log_write(SYSLOG , LOGERR , "malloc for pack_config_st fail");
        deamon_exit();
    }
    p_pack_conf->status = NOTLOADED;

    g_shmid = shmget( IPC_PRIVATE , MAX_PROC_NUM*sizeof(stat_st) , IPC_CREAT|0660);
    if ( g_shmid < 0 ){
        log_write(SYSLOG , LOGERR , "shmat for stat_st fail");
        deamon_exit();
    }
    g_stat = (stat_st *)shmat(g_shmid , NULL,  0);
    if ( g_stat == NULL ){
        log_write(SYSLOG , LOGERR , "shmat for stat_st fail , shmid = %d",g_shmid);
        deamon_exit();
    }
    log_write(SYSLOG , LOGINF , "shmat for stat_st OK , g_shmid = %d" , g_shmid );

    char buffer_cmd[MAX_CMD_LEN];

    server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
    int sock_recv = 0;
    struct sockaddr_in server_sockaddr;
    struct sockaddr_in client_addr;
    socklen_t socket_len = sizeof(client_addr);

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(PORT_CMD);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    int nOptval = 1;
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR,(const void *)&nOptval , sizeof(int)) < 0)
    {
        log_write(SYSLOG , LOGERR , "setsockopt fail");
    }
    if (bind(server_sockfd , (struct sockaddr *)&server_sockaddr , sizeof(server_sockaddr)) == -1)
    {
        log_write(SYSLOG , LOGERR , "bind port[%d] fail" , PORT_CMD);
        deamon_exit();
    }
    log_write(SYSLOG , LOGINF , "bind port[%d] OK" , PORT_CMD);

    if(listen(server_sockfd , 10) == -1){
        log_write(SYSLOG , LOGERR , "listen on port[%d] fail" , PORT_CMD);
        deamon_exit();
    }
    log_write(SYSLOG , LOGDBG , "listen on port[%d] OK" , PORT_CMD);

    while (1){

        sock_recv = accept(server_sockfd , (struct sockaddr *)&client_addr , &socket_len);
        if (sock_recv < 0){
            log_write(SYSLOG , LOGERR , "accept on port[%d] fail , errno[%d]" , PORT_CMD , errno);
            deamon_exit();
        }
        log_write(SYSLOG , LOGDBG , "accept on port[%d] OK" , PORT_CMD);
        memset( buffer_cmd , 0x00 , sizeof(buffer_cmd));

        if ( recv(sock_recv , buffer_cmd , MAX_CMD_LEN , 0) < 0 ){
            log_write(SYSLOG , LOGERR , "recv on port[%d] fail , errno[%d]" , PORT_CMD , errno);
            deamon_exit();
        }
        log_write(SYSLOG , LOGDBG , "recv command[%s]\n" , buffer_cmd);

        if ( strncmp(buffer_cmd , "init" , 4) == 0 ){
            if (p_conn_conf->status == RUNNING){
                send( sock_recv , "exec [init] fail , connection module already started" , MAX_CMD_LEN , 0);
            } else {
                command_conn_start(p_conn_conf);
                send( sock_recv , "exec [init] OK , enter [stat] to check out" , MAX_CMD_LEN , 0 );
            }
        } else if ( strncmp(buffer_cmd , "stop" , 4) == 0 ){
            if (p_conn_conf->status != RUNNING){
                send( sock_recv , "exec [stop] fail , connection module not started" , MAX_CMD_LEN , 0);
            } else {
                command_conn_stop(p_conn_conf);
                send( sock_recv , "exec [stop] OK , connection module is stopped" , MAX_CMD_LEN , 0);
            }
        } else if ( strncmp(buffer_cmd , "send" , 4) == 0 ) {
            if ( p_pack_conf->status != LOADED ){
                send( sock_recv , "exec [send] fail , enter [load] to load packing config first" , MAX_CMD_LEN , 0);
            } else {
                command_pack_send(p_pack_conf);
                send( sock_recv , "exec [send] OK , enter [stat/moni] to check out" , MAX_CMD_LEN , 0);
            }
        } else if ( strncmp(buffer_cmd , "shut" , 4) == 0){
            command_pack_shut(p_pack_conf);
            send( sock_recv , "exec [shut] OK , packing module is stopped" , MAX_CMD_LEN , 0);
        } else if ( strncmp(buffer_cmd , "kill" , 4) == 0 ){
            if ( (p_pack_conf->status != FINISHED && p_pack_conf->status != NOTLOADED) || p_conn_conf->status != NOTLOADED ){
                send( sock_recv , "exec [kill] fail , enter [stop/shut] first" , MAX_CMD_LEN , 0);
            } else {
                if (command_pack_shut(p_pack_conf) ) {
                    log_write(SYSLOG , LOGERR ,"packing module stop fail");
                }
                if ( command_conn_stop(p_conn_conf) ){
                    log_write(SYSLOG , LOGERR ,"connection module stop fail");
                }
                send( sock_recv , "exec [kill] OK , deamon process quitting" , MAX_CMD_LEN , 0);
                close(sock_recv);
                deamon_exit();
            }
        } else if ( strncmp(buffer_cmd , "stat" , 4) == 0){
            retmsg = command_get_stat( STAT_CONN|STAT_PACK|STAT_MONI , p_conn_conf , p_pack_conf);
            send( sock_recv , retmsg , MAX_CMD_LEN , 0);
            free(retmsg);
        } else if ( strncmp(buffer_cmd , "moni" , 4) == 0){
            retmsg = command_get_stat( STAT_PACK|STAT_MONI|STAT_CONN , p_conn_conf , p_pack_conf);
            send( sock_recv , retmsg , MAX_CMD_LEN , 0);
            free(retmsg);
        } else if ( strncmp(buffer_cmd , "load" , 4) == 0){
            if ( command_pack_load(buffer_cmd , p_pack_conf) ){
                send( sock_recv , "exec [load] fail" , MAX_CMD_LEN , 0);
            } else {
                send( sock_recv , "exec [load] OK , enter [stat] to check out" , MAX_CMD_LEN , 0);
            }
        } else if ( strncmp( buffer_cmd , "tps" , 3) == 0){
            if ( p_pack_conf->status == NOTLOADED  || p_pack_conf->status == FINISHED ){
                send( sock_recv , "exec [tps] fail , enter [load] to load packing configs first" , MAX_CMD_LEN , 0);
            } else {
                retmsg = command_adjust_status( 1 , buffer_cmd , p_pack_conf);
                send( sock_recv , retmsg , MAX_CMD_LEN , 0);
                free(retmsg);
            }
        } else if ( strncmp( buffer_cmd , "time" , 4) == 0 ){
            if ( p_pack_conf->status == NOTLOADED || p_pack_conf->status == FINISHED ){
                send( sock_recv , "exec [tps] fail , enter [load] to load packing configs first" , MAX_CMD_LEN , 0);
            } else {
                retmsg = command_adjust_status( 2 , buffer_cmd , p_pack_conf);
                send( sock_recv , retmsg , MAX_CMD_LEN , 0);
                free(retmsg);
            }
        } else if ( strncmp( buffer_cmd , "para" , 4) == 0 ){
            if ( p_conn_conf->status != RUNNING ){
                send( sock_recv , "exec [para] fail , enter [init] to start connection module first" , MAX_CMD_LEN , 0);
            }
            retmsg = command_adjust_para( buffer_cmd , p_conn_conf);
            send( sock_recv , retmsg , MAX_CMD_LEN , 0);
            free(retmsg);
        } else if ( strncmp( buffer_cmd , "list" , 4) == 0 ){
            send( sock_recv , "OK" , MAX_CMD_LEN , 0);
        } else if ( strncmp( buffer_cmd , "report" , 6) == 0 ){
            command_update_conn_status(buffer_cmd , p_conn_conf);
        }
        close(sock_recv);
    }
    deamon_exit();
}
