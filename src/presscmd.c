#include "include/presscmd.h"
#include "include/press.h"

int outflag = 0;

static void print_usage()
{
	printf("<deam>    启动PRESS守护进程\n"  );
	printf("<kill>    停止PRESS守护进程\n"  );
	printf("<init>    启动通讯模块\n"  );
	printf("<stop>    停止通讯模块\n"  );
	printf("<send>    启动组包和持久化进程\n");
	printf("<shut>    停止组包和持久化进程\n");
	printf("<stat>    查看状态\n");
	printf("<load>    加载组包配置文件\n");
	printf("          <load [文件名]>\n");
	printf("<moni>    开启监视器模式\n");
	printf("<tps>     设置实时TPS值,命令格式:\n");
	printf("          <tps [+-]调整值[%%] [序号]>\n");
	printf("           tps +100\t\t将所有组包进程tps增加100\n");
	printf("           tps -10%%\t\t将所有组包进程tps减少10%%\n");
	printf("           tps +100%% 0\t\t将序号为0的组包进程tps增加100%%\n");
	printf("           tps 10\t\t将所有的组包进程tps设置为10\n");
	printf("           tps 10 1\t\t将序号为1的组包进程tps设置为10\n");
	printf("<time>    设置发送时间,命令格式:\n");
	printf("          <time [+-]调整值[%%] [序号]>\n");
	printf("          举例参考tps命令\n");
	printf("<para>    设置短链接并发数\n");
	printf("          <para parallel_num>\n");
	printf("<exit>    退出客户端\n");
	printf("<help>    打印帮助\n");
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

int main(int argc , char *argv[])
{
	/*variables*/
	int ret = 0;
	int qid = 0;
    char inputLine[200];
	msg_st msgs;
	memset(&msgs , 0x00 , sizeof(msgs));
    int  press_pid = 0;
    int  monitor_mode = 0;

	/*load MSGKEY from config*/
	if ( (qid = get_qid("MSGKEY_CMD")) < 0){
	    printf("ERROR: MSGKEY_CMD error\n");
	    exit(1);
	}

    while( 1 ) {
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
        memset(inputLine , 0x00 , sizeof(inputLine));
        fgets( inputLine , sizeof(inputLine) , stdin);
        if ( inputLine[0] == '\n' )  continue;
        if ( memcmp(inputLine , "help" , 4) == 0 ){
            print_usage();
            continue;
        } else if ( memcmp(inputLine , "exit" , 4) == 0 ){
            break;
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
	                    if ( (qid = get_qid("MSGKEY_CMD")) < 0){
	                        printf("内部错误:无法从配置文件读取MSGKEY_CMD\n");
	                        exit(1);
	                    }
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
                    memcmp(inputLine , "shut" , 4) ) {
            print_usage();
            continue;
        } else if ( press_pid == 0 ){
            continue;
        }

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
	    msgs.type = press_pid;
	    msgs.pid = getpid();
        strcpy(msgs.text , inputLine);
	    /*send cmd to message queue*/
	    ret = msgsnd( (key_t)qid , &msgs , sizeof(struct message_struct)-sizeof(long) , 0 );
	    if ( ret < 0 ){
	    	printf("msgsnd error , errno:%d\n" ,errno );
	    	exit(1);
	    }

        memset( &msgs , 0x00 , sizeof(msgs));
        signal(SIGALRM , sig_handler);
        alarm(5);
        ret = msgrcv((key_t)qid , &msgs , sizeof(msg_st) - sizeof(long) , getpid() , 0);
        alarm(0);
        signal(SIGALRM , sig_handler);
        if ( ret < 0 ){
            if ( errno == EINTR ){
                printf("等待响应超时,请检查日志\n");
                continue;
            } else {
                printf("msgrcv error , errno:%d\n" , errno);
                exit(1);
            }
        } else {
	        printf("%s\n" , msgs.text);
        }

        if( monitor_mode == 1 ){
            if ( outflag == 0 ){
                signal(SIGINT , sig_handler);
                printf("输入Control + c 退出...\n");
                sleep(1);
                system("clear");
                goto TAGMONITOR;
            } else {
                monitor_mode = 0;
                outflag = 0;
            }
        }

        if ( memcmp(inputLine , "kill" , 4) == 0 ){
            /* wait for deamon to exit */
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
    }
	return 0;
}
