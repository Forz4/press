#include "include/presscmd.h"

int outflag = 0;

static void print_usage()
{
    printf("<deam>    start deamon process\n"  );
	printf("<kill>    stop deamon process\n"  );
	printf("<init>    start connection module\n"  );
	printf("<stop>    stop connection module\n"  );
	printf("<send>    start packing module\n");
	printf("<shut>    stop packing module\n");
	printf("<stat>    check status\n");
	printf("<load>    load packing configs\n");
	printf("          <load [filename]>\n");
	printf("<moni>    start monitor mode\n");
	printf("<tps>     set real TPS\n");
	printf("          <tps [+-]adjustment[%%] [index]>\n");
	printf("           tps +100\t\tadd 100 tps to all packing processes\n");
	printf("           tps -10%%\t\tminus 10%% tps from all packing processes\n");
	printf("           tps +100%% 0\t\tadd 100%% tps to packing process with index 0\n");
	printf("           tps 10\t\tset all packing process to 10 tps\n");
	printf("           tps 10 1\t\tset packing process with index 1 to 10 tps\n");
	printf("<time>    set TIME\n");
	printf("          <time [+-]adjustment[%%] [index]>\n");
	printf("<para>    set parallel number of connection process(only for short connection)\n");
	printf("          <para parallel_num>\n");
	printf("<list>    list all nodes\n");
	printf("<conn>    connection to a single node\n");
	printf("          <conn index>\n");
    printf("<snap>    create or show snapshots\n");
    printf("          <snap create snapshot_name>\n"); 
    printf("          <snap remove snapshot_name>\n"); 
    printf("          <snap show>\n");   
	printf("<exit>    exit presscmd\n");
	printf("<help>    print help\n");
	return;
}
static void print_help()
{
    printf("PRESS VERSION: %s\n" , PRESS_VERSION);
    printf("presscmd [-n ip:port] [-f nodelist]\n");
    return;
}
void sig_handler(int signo)
{
    switch (signo){
        case SIGALRM:
            alarm(0);
            break;
        case SIGINT:
            outflag = 1;
            break;
    }
    return;
}

int parse_ip_port( char *optarg , char *ip , int *port)
{
    int i = 0;
    int len = strlen(optarg);
    while( i < len ){
        if( optarg[i] == ':' )
            break;
        i++;
    }
    if ( i == len )
        return -1;
    memcpy( ip , optarg , i );
    *port = atoi(optarg+i+1);
    return 0;
}

void  getNodeList( char *filename , char ip[MAX_NODE_NUM][20] , int port[MAX_NODE_NUM] , int *numOfNode )
{
    FILE    *fp = fopen( filename , "r");
    char    buffer[200];
    int     i = 0;
    if ( fp == NULL )
        return;
    while ( fscanf( fp , "%s" , buffer) != EOF ){
        parse_ip_port( buffer , ip[i] , &port[i] );
        i ++;
    }
    fclose(fp);
    *numOfNode = i;
    return ;
}

int main(int argc , char *argv[])
{
	/*variables*/
    char inputLine[200];
    char buffer[MAX_REPLY_LEN];
    int  monitor_mode = 0;

    int  op = 0;
    int  sock_send = 0;
    int  numOfNode = 0;
    int  port[MAX_NODE_NUM];
    char ip[MAX_NODE_NUM][20];
    struct sockaddr_in servaddr[MAX_NODE_NUM];
    memset( ip , 0x00 , sizeof(ip) );
    memset( port , 0x00 , sizeof(port) );
    memset( servaddr , 0 , sizeof(servaddr));
    int  nodeIndex = 0; 

    int  i = 0;
    while ( ( op = getopt( argc , argv , "n:f:h") ) > 0 ) {
        switch(op){
            case 'n' :
                parse_ip_port( optarg , ip[numOfNode] , &port[numOfNode] );
                numOfNode ++;
                break;
            case 'f' :
                getNodeList( optarg , ip , port , &numOfNode );
                break;
            case 'h' :
                print_help();
                exit(0);
        }
    }
    if ( numOfNode == 0 ){
        numOfNode = 1;
        strcpy( ip[0] , "127.0.0.1" );
        port[0] = 6043;
    }
    for ( i = 0 ; i < numOfNode ; i ++){
        servaddr[i].sin_family = AF_INET;
        servaddr[i].sin_port = htons(port[i]);
        servaddr[i].sin_addr.s_addr = inet_addr(ip[i]);
    }

    signal( SIGCHLD , SIG_IGN );

    while( 1 ) {
        if ( nodeIndex == 0 ){
            printf("presscmd(ALL NODES)> ");
        } else {
            printf("presscmd(NODE[%d])> " , nodeIndex);
        }
        memset(inputLine , 0x00 , sizeof(inputLine));
        fgets( inputLine , sizeof(inputLine) , stdin);
        if ( inputLine[0] == '\n' ){
            print_usage();
            continue;
        }

        if( inputLine[strlen(inputLine)-1] == '\n' )
            inputLine[strlen(inputLine)-1] = '\0';

        if ( memcmp(inputLine , "help" , 4) == 0 ){
            print_usage();
            continue;
        } else if ( memcmp(inputLine , "exit" , 4) == 0 ){
            break;
        } else if ( memcmp(inputLine , "deam" , 4) == 0 ){
            int pid = fork();
            if ( pid == 0 ){
                execlp("press" , "press" , (char *)0 );
                exit(0);
            } else if ( pid > 0 ){
                printf("deamon start\n");
            } else {
                printf("fork fail");
                exit(1);
            }
            continue;
        } else if ( memcmp(inputLine , "conn" , 4) == 0 ){
            nodeIndex = atoi(inputLine+5) ;
            if ( nodeIndex <= 0 || nodeIndex > numOfNode ){
                nodeIndex = 0;
                printf("switch to ALL NODES mode\n" );
            } else {
                printf("switch to node[%s:%d]\n" , ip[nodeIndex-1] , port[nodeIndex-1]);
            }
            continue;
        } else if ( memcmp(inputLine , "snap" , 4) == 0 ){
            redisContext    *c = redisConnect("127.0.0.1", 6379);
            redisReply      *reply = NULL;
            redisReply      *reply1 = NULL;
            time_t          t = 0;
            struct tm       *temp = NULL;
            struct timeval  ts;

            if(c->err){
                printf("redis error : %s\n" , c->errstr);
            } else {
                if ( memcmp(inputLine+5 , "show" , 4) == 0 ){
                    reply = redisCommand(c, "zrange snap 0 -1");
                    if ( reply->elements > 0 ){ 
                        for ( int i = 0 ; i < reply->elements ; i ++ ){
                            reply1 = redisCommand(c , "zscore snap %s", reply->element[i]->str);
                            t = atoi(reply1->str);
                            temp = localtime(&t);
                            printf("[%s]:[%04d-%02d-%02d %02d:%02d:%02d]\n" ,\
                                reply->element[i]->str,\
                                temp->tm_year ,\
                                temp->tm_mon ,\
                                temp->tm_mday ,\
                                temp->tm_hour ,\
                                temp->tm_min ,\
                                temp->tm_sec);
                            freeReplyObject(reply1);
                        }
                    } else {
                        printf("no snapshots found\n");
                    }
                    freeReplyObject(reply);
                } else if ( memcmp(inputLine+5 , "create" , 6 ) == 0 ){
                    gettimeofday(&ts,NULL);
                    reply = redisCommand(c, "zadd snap %d %s" , ts.tv_sec , inputLine+12);
                    freeReplyObject(reply);
                    printf("create snapshot OK\n");
                } else if ( memcmp( inputLine+5 , "remove" , 6) == 0 ){
                    reply = redisCommand(c, "zrem snap %s" , inputLine+12);
                    if ( reply->integer == 1 ){
                        printf("remove snapshot OK\n");
                    } else {
                        printf("remove snapshot fail\n");
                    }
                    freeReplyObject(reply);
                } else {
                    printf("invalid command , snap [create snapshot_name|show]\n");
                }
            }
            redisFree(c);
            continue;
        } else if ( 
                    memcmp(inputLine , "init" , 4) && \
                    memcmp(inputLine , "stop" , 4) && \
                    memcmp(inputLine , "kill" , 4) && \
                    memcmp(inputLine , "send" , 4) && \
                    memcmp(inputLine , "stat" , 4) && \
                    memcmp(inputLine , "load" , 4) && \
                    memcmp(inputLine , "moni" , 4) && \
                    memcmp(inputLine , "tps"  , 3) && \
                    memcmp(inputLine , "time" , 4) && \
                    memcmp(inputLine , "para" , 4) && \
                    memcmp(inputLine , "list" , 4) && \
                    memcmp(inputLine , "shut" , 4) ) {
            print_usage();
            continue;
        } 

        /* commands that should be sent to deamon or switch nodes */
        /* activate monitor mode */
        if ( memcmp( inputLine ,  "moni" , 4) == 0 ){
            strcpy(inputLine , "moni");
            system("clear");
            monitor_mode = 1;
            outflag = 0;
        }

TAGMONITOR:
        for ( i = 0 ; i < numOfNode ; i ++ ){
            if ( nodeIndex > 0 && nodeIndex-1 != i )
                continue;
            sock_send = socket(AF_INET , SOCK_STREAM , 0);
            if (connect(sock_send , (struct sockaddr*)&servaddr[i] , sizeof(servaddr[i])) < 0){
                printf("connect to node %d[%s:%d] fail\n" , i+1 , ip[i] , port[i]);
                close(sock_send);
                continue;
            }
            send( sock_send , inputLine , sizeof(inputLine) , 0);
            signal(SIGALRM , sig_handler);
            alarm(5);
            memset(buffer , 0x00 , sizeof(buffer));
            recv( sock_send , buffer , sizeof(buffer) , 0);
            alarm(0);
            signal(SIGALRM , sig_handler);
            if ( memcmp( inputLine , "list" , 4) == 0 ){
                printf("node %d[%s:%d]:%s\n" , i+1 , ip[i] , port[i] , buffer);
            } else {
                printf("node %d[%s:%d]\n" , i+1 , ip[i] , port[i]);
	            printf("%s\n" , buffer);
            }
            close(sock_send);

            if( monitor_mode == 1 && i == numOfNode - 1){
                if ( outflag == 0 ){
                    signal(SIGINT , sig_handler);
                    printf("enter Control + c to quit...\n");
                    sleep(1);
                    system("clear");
                    goto TAGMONITOR;
                } else {
                    monitor_mode = 0;
                    outflag = 0;
                }
            }
        }
    }
	return 0;
}
