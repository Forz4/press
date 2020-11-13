#include "include/presscmd.h"

int outflag = 0;

static void print_usage()
{
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
    char buffer[2000];
    //int  press_pid = 0;
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

    while( 1 ) {
        /*
        press_pid = check_deamon();
        if ( press_pid < 0 ){
            printf("守护进程检查失败\n");
            exit(0);
        }
        if ( press_pid > 0 ) {
            printf("(DEAMON PID:%d)$ " , press_pid);
        } else {
            printf("(输入deam启动守护或help查看命令)$ ");
        }
        */
        if ( nodeIndex == 0 ){
            printf("presscmd(ALL NODES)> ");
        } else {
            printf("presscmd(NODE[%d])> " , nodeIndex);
        }
        memset(inputLine , 0x00 , sizeof(inputLine));
        fgets( inputLine , sizeof(inputLine) , stdin);
        if ( inputLine[0] == '\n' )  continue;
        if ( memcmp(inputLine , "help" , 4) == 0 ){
            print_usage();
            continue;
        } else if ( memcmp(inputLine , "exit" , 4) == 0 ){
            break;
            /*
        } else if ( memcmp(inputLine , "deam" , 4) == 0 ){
            int pid = fork();
            if ( pid == 0 ){
                system("press");
                exit(0);
            } else if ( pid > 0 ){
                int timeout = 3;
                while(-- timeout){
                    press_pid = check_deamon();
                    if ( press_pid > 0 ){
                        printf("守护进程启动成功\n");
                        break;
                    }
                    sleep(1);
                }
                if ( timeout == 0 ){
                    printf("守护进程启动等待超时失败,请查看日志\n");
                    exit(1);
                } else {
                    continue;
                }
            } else {
                printf("fork fail");
                exit(1);
            }
            */
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
                    memcmp(inputLine , "conn" , 4) && \
                    memcmp(inputLine , "shut" , 4) ) {
            print_usage();
            continue;
        } 

    /*
        if ( memcmp(inputLine , "list" , 4) == 0 ){
            for ( i = 0 ; i < numOfNode ; i ++ ){
                printf("节点%d:\n" , i+1);
                printf("IP[%s]\n" , ip[i]);
                printf("PORT[%d]\n" , port[i]);
            }
            continue;
        } 
    */
        if ( memcmp(inputLine , "conn" , 4) == 0 ){
            nodeIndex = atoi(inputLine+5) ;
            if ( nodeIndex <= 0 || nodeIndex > numOfNode ){
                nodeIndex = 0;
                printf("switch to ALL NODES mode\n" );
            } else {
                printf("switch to node[%s:%d]\n" , ip[nodeIndex-1] , port[nodeIndex-1]);
            }
            continue;
        }

    /*
        else if ( press_pid == 0 ){
            continue;
        }
    */

        if( inputLine[strlen(inputLine)-1] == '\n' )
            inputLine[strlen(inputLine)-1] = '\0';

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

            /*
            if ( memcmp(inputLine , "kill" , 4) == 0 ){
                int timeout = 5;
                while(-- timeout){
                    press_pid = check_deamon();
                    if ( press_pid == 0 ){
                        printf("守护进程停止成功\n");
                        break;
                    }
                    sleep(1);
                }
                if ( timeout == 0 ){
                    printf("守护进程停止等待超时,请查看日志\n");
                } 
                continue;
            }
            */
        }
    }
	return 0;
}
