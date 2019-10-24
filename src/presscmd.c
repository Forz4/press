#include "include/presscmd.h"
#include "include/press.h"

int outflag = 0;

static void print_usage()
{
    printf("PRESS VERSION : %s\n" , PRESS_VERSION);
    printf("Usage:\n");
	printf("<deam>    start deamon process\n"  );
	printf("<kill>    kill  deamon process\n"  );
	printf("<init>    start communication process\n"  );
	printf("<stop>    stop  communication process\n"  );
	printf("<send>    start packing and receiving process\n");
	printf("<shut>    shut  packing and recieving process\n");
	printf("<stat>    show  status\n");
	printf("<load>    load  config for packing process\n");
	printf("<moni>    start monitor mode\n");
	printf("<tps      [+-]adjustment[%%] [index]>\n");
	printf("          adjust tps by number or percentage\n");
	printf("          eg. tps +100       add 100 tps to all packing process\n");
	printf("              tps -10%%       minus 10%% tps for all packing process\n");
	printf("              tps +100%% 0    add 100%% tps for packing process with index0\n");
	printf("              tps 10         set tps to 10 for all packing process\n");
	printf("              tps 10 1       set tps to 10 for packing process with index1\n");
	printf("<time     [+-]adjustment[%%] [index]>\n");
	printf("          adjust time by number or percentage\n");
	printf("<exit>    exit  this program\n");
	printf("<help>    print help page\n");
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
            printf("check_deamon error\n");
        }
        if ( press_pid > 0 ) {
            printf("(deamon pid:%d)>" , press_pid);
        } else {
            printf("(no deamon running)>");
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
                printf("deamon started\n");
                sleep(1);
                press_pid = check_deamon();
	            if ( (qid = get_qid("MSGKEY_CMD")) < 0){
	                printf("ERROR: MSGKEY_CMD error\n");
	                exit(1);
	            }
                continue;
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
                    memcmp(inputLine , "shut" , 4) ) {
            print_usage();
            continue;
        } else if ( press_pid == 0 ){
            printf("no press deamon running , input \"deam\" to start deamon\n"); 
            continue;
        }

        if( inputLine[strlen(inputLine)-1] == '\n' )
            inputLine[strlen(inputLine)-1] = '\0';

        /* activate monitor mode */
        if ( memcmp( inputLine ,  "moni" , 4) == 0 ){
            strcpy(inputLine , "stat");
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
                printf("wait for reply time out\n");
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
                printf("input Control + c to quit ...\n");
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
            sleep(1);
        }
    }
	return 0;
}
