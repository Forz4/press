#include "include/press.h"

sigjmp_buf jmpbuf;
int     QID_CMD = 0;			/*qid for command*/
int     HEART_INTERVAL;			/*heart beat interval*/
char    ENCODING;               /*A:ascii H:hex*/
int     sock_send = 0;			/*socket for sender*/
int     sock_recv = 0;			/*socket for receiver*/
int     g_shmid = 0;
int     presscmd_pid = 0;      /* indicate which presscmd is sending msg */
stat_st *g_stat;

int conn_config_load(conn_config_st *p_conn_conf)
{
	FILE *fp = NULL;
	char pathname[MAX_PATHNAME_LEN];
	char line[200];
	char buf[100];
	char value[MAX_CFG_VAL_LEN];
	comm_proc_st *cur = NULL;
	int  count = 0;

	if (p_conn_conf == NULL){
		return -1;
	}

	/*load qid*/
	if ( (p_conn_conf->qid_out = get_qid("MSGKEY_OUT")) < 0){
		log_write(SYSLOG , LOGERR , "MSGKEY_OUT not found");
		return -1;
	}
	if ( (p_conn_conf->qid_in = get_qid("MSGKEY_IN")) < 0){
		log_write(SYSLOG , LOGERR , "MSGKEY_IN not found");
		return -1;
	}

	if (loadConfig("HEART_INTERVAL" , value , MAX_CFG_VAL_LEN) < 0)
		HEART_INTERVAL = 0;
	else
		HEART_INTERVAL = atoi(value);

	if (loadConfig("ENCODING" , value , MAX_CFG_VAL_LEN) < 0)
		ENCODING = 'A';
	else
		ENCODING = value[0];

	/*load connection config*/
	memset(pathname , 0x00 , sizeof(pathname));
	sprintf(pathname , "%s/cfg/conn.cfg" , getenv("PRESS_HOME"));
	fp = fopen(pathname , "r");
	if ( fp == NULL ){
		log_write(SYSLOG , LOGERR , "conn.cfg not found");
		return -1;
	}

	p_conn_conf->process_head = NULL;

	while (fgets(line , sizeof(line), fp) != NULL){

		if ( line[0] == '\n' || line[0] == '#' )
			continue;

		cur = (comm_proc_st *)malloc(sizeof(comm_proc_st));
		if ( cur == NULL ){
			log_write(SYSLOG , LOGERR , "malloc fails");
			fclose(fp);
			return -1;
		}

		if (get_bracket(line , 1 , buf , 100)){
			log_write(SYSLOG , LOGERR , "conn.cfg format error");
			fclose(fp);
			return -1;
		}
		cur->type = buf[0];
        if ( cur->type == 'S' ){
            cur->qidRead = p_conn_conf->qid_out;
            cur->qidSend = p_conn_conf->qid_in;
        } else if ( cur->type == 'R' ){
            cur->qidSend = p_conn_conf->qid_in;
        }

		if (get_bracket(line , 2 , buf , 100)){
			log_write(SYSLOG , LOGERR , "conn.cfg format error");
			fclose(fp);
			return -1;
		}
		strncpy(cur->ip , buf , sizeof(cur->ip));

		if (get_bracket(line , 3 , buf , 100)){
			log_write(SYSLOG , LOGERR , "conn.cfg format error");
			fclose(fp);
			return -1;
		}
		cur->port = atoi(buf);

		if (get_bracket(line , 4 , buf , 100)){
			log_write(SYSLOG , LOGERR , "conn.cfg format error");
			fclose(fp);
			return -1;
		}
		cur->persist = atoi(buf);

		count ++;
		cur->next = p_conn_conf->process_head;
		p_conn_conf->process_head = cur;
	}

	if ( count == 0 ){
		log_write(SYSLOG , LOGERR , "conn.cfg is empty");
		fclose(fp);
		return 0;
	}

	fclose(fp);
	p_conn_conf->status = 1;
	return 0;
}

void conn_config_free(conn_config_st *p_conn_conf)
{
	comm_proc_st *cur , *nex;
	if ( p_conn_conf == NULL)
		return; 

	cur = p_conn_conf->process_head;

	while (cur != NULL)
	{
		nex = cur->next;
		free(cur);
		cur = nex;
	}
	return ;
}

int conn_start(conn_config_st *p_conn_conf)
{
	int ret = 0;
	log_write(SYSLOG , LOGDBG , "loading config");
	ret = conn_config_load(p_conn_conf);
	if (ret < 0){
		log_write(SYSLOG , LOGERR , "fail to load conn.cfg");
		exit(1);
	}

	comm_proc_st *cur = p_conn_conf->process_head;

	if (p_conn_conf == NULL || p_conn_conf->process_head == NULL)
		return -1;

	while (cur != NULL){
		if ( cur->type == 'R' ){
			ret = conn_receiver_start(cur);
			if ( ret < 0 ){
				log_write(SYSLOG , LOGERR , "receiver fail to start , IP[%s] , PORT[%d]",cur->ip , cur->port);
			} else {
				log_write(SYSLOG , LOGINF , "receiver start , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
				cur->pid = ret;
			}

		} else if ( cur->type == 'S' ){
			ret = conn_sender_start(cur);
			if ( ret < 0 ){
				log_write(SYSLOG , LOGERR , "sender fail to start , IP[%s] , PORT[%d]",cur->ip , cur->port);
			} else {
				log_write(SYSLOG , LOGINF , "sender start , IP[%s] , PORT[%d] , PID[%d]",cur->ip , cur->port , ret);
				cur->pid = ret;
			}
		}
		cur = cur->next;
	}
	p_conn_conf->status = RUNNING;
	return 0;
}

int conn_receiver_start(comm_proc_st *p_receiver)
{
	int pid;
	pid = fork();
	if ( pid < 0 ){
		return -1;
	} else if ( pid > 0 ){
		return pid;
	}

	/*socket for server*/
	int server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
	struct sockaddr_in server_sockaddr;
	/*socket for client*/
	char buffer[MAX_LINE_LEN];
	struct sockaddr_in client_addr;
	socklen_t socket_len = sizeof(client_addr);
	/*variable for package*/
	int recvlen = 0;
	int textlen = 0;
	int nTotal = 0;
	int nRead = 0;
	int nLeft = 0;
	msg_st msgs;
	int nTranlen = 0;
	int ret = 0;
    struct timeval ts;

	/*bind listen accectp*/
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_port = htons(p_receiver->port);
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(server_sockfd , (struct sockaddr *)&server_sockaddr , sizeof(server_sockaddr)) == -1)
	{
		log_write(SYSLOG , LOGERR , "receiver bind error");
		exit(1);
	}

	if(listen(server_sockfd , 1) == -1){
		log_write(SYSLOG , LOGERR , "receiver listen error");
		exit(1);
	}

	sock_recv = accept(server_sockfd , (struct sockaddr *)&client_addr , &socket_len);
	if (sock_recv < 0){
		log_write(SYSLOG , LOGERR , "receiver accept error");
		exit(1);
	}

	log_write(SYSLOG , LOGINF , "RECEIVER connectted");
	while(1){
		memset(buffer , 0x00 , sizeof(buffer));
		memset(&msgs , 0x00 , sizeof(msg_st));

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
				if (nRead == 0)	break;
				nTotal += nRead;
				nLeft -= nRead;
			}
		}

        if ( p_receiver->persist == 1 ){
		    msgs.type = 1;
		    nTranlen = get_length(buffer);
		    memcpy(msgs.text , buffer , nTranlen + 4);

            gettimeofday( &ts , NULL );
            memcpy( &(msgs.ts) , &ts , sizeof(struct timeval));
            msgs.flag = 'I';
		    ret = msgsnd((key_t)p_receiver->qidSend , &msgs , sizeof(msg_st) - sizeof(long) , 0);
		    if (ret < 0){
		    	log_write(SYSLOG , LOGERR , "receiver msgsnd error");
		    }
        }
	}
	close(sock_recv);
	return 0;
}
void conn_receiver_signal_handler(int no)
{
    close(sock_recv);
    exit(0);
}
int conn_sender_start(comm_proc_st *p_sender)
{
	int pid;
	pid = fork();
	if ( pid < 0 ){
		return -1;
	} else if ( pid > 0 ){
		return pid;
	}

	struct sockaddr_in servaddr;
	msg_st msgs;
	int ret;
	int len = 0;
	int nTotal = 0;
	int nSent = 0;
	int nLeft = 0;
    struct timeval ts;

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

	log_write(SYSLOG , LOGINF , "SENDER connectted");

	while(1){
		if (HEART_INTERVAL > 0){
			sigsetjmp(jmpbuf , 1);
			signal(SIGALRM , send_idle);
			alarm(HEART_INTERVAL);
		}
		ret = msgrcv(	(key_t)p_sender->qidRead , \
						&msgs , \
						sizeof(msg_st) - sizeof(long) , \
						0, \
						0 );
		if (HEART_INTERVAL > 0){
			alarm(0);
		}
		
		len = get_length(msgs.text);

		nTotal = 0;
		nSent = 0;
		nLeft = len + 4;
		while( nTotal != len + 4){
			nSent = send(sock_send , msgs.text + nTotal , nLeft , 0);
			if (nSent == 0){
				break;
			}
			else if (nSent < 0){
				log_write(SYSLOG , LOGERR , "SENDER send error");
				close(sock_send);
				exit(0);
			}
			nTotal += nSent;
			nLeft -= nSent;
		}


        if ( p_sender->persist == 1 ){
            gettimeofday(&ts , NULL);
            memcpy( &(msgs.ts) , &ts , sizeof(struct timeval));
            msgs.flag = 'O';
		    ret = msgsnd((key_t)p_sender->qidSend , &msgs , sizeof(msg_st) - sizeof(long) , 0);
		    if (ret < 0){
		    	log_write(SYSLOG , LOGERR , "sender msgsnd error");
		    }
        }
	}
	close(sock_send);
	return 0;
}
void conn_sender_signal_handler(int no)
{
    close(sock_send);
    exit(0);
}
void send_idle(int signo)
{
	send(sock_send , "0000" , 4 , 0);
	siglongjmp(jmpbuf , 1);
}

int conn_stop(conn_config_st *p_conn_conf)
{
	int ret = 0;
	comm_proc_st *cur;

	if (p_conn_conf == NULL){
		return -1;
	}

	ret = msgctl((key_t)p_conn_conf->qid_out , IPC_RMID , NULL);
	ret = msgctl((key_t)p_conn_conf->qid_in , IPC_RMID , NULL);

	cur = p_conn_conf->process_head;
	while (cur != NULL){
		log_write(SYSLOG , LOGINF , "killing connection process , pid[%d]", cur->pid);
		kill(cur->pid , SIGTERM);
		cur=cur->next;
	}
	p_conn_conf->status = NOTLOADED;
	return 0;
}

int pack_config_load( char *filename , pack_config_st *p_pack_conf)
{
	char 	config_fn[MAX_FILENAME_LEN];
	FILE	*fp = NULL;
	char 	line[200];
	char 	buf[100];
	int 	count = 0;
	char 	pathname[MAX_PATHNAME_LEN];

	log_write(SYSLOG , LOGDBG , "loading pack configs");
	p_pack_conf->pit_head = NULL;
	p_pack_conf->cat_head = NULL;

	pit_proc_st	*pit_cur = NULL;

	memset(config_fn , 0x00 , sizeof(config_fn));

	/* clear previous configs first */
	pack_config_free(p_pack_conf);
	
	/* load qids*/
	if ( (p_pack_conf->qid_out = get_qid("MSGKEY_OUT")) < 0){
		log_write(SYSLOG , LOGERR , "MSGKEY_OUT not found");
		return -1;
	}

	if ( (p_pack_conf->qid_in = get_qid("MSGKEY_IN")) < 0){
		log_write(SYSLOG , LOGERR , "MSGKEY_IN not found");
		return -1;
	}

	p_pack_conf->cat_head = (cat_proc_st *)malloc(sizeof(cat_proc_st));
	if ( p_pack_conf->cat_head == NULL ){
		log_write(SYSLOG , LOGERR , "malloc fails");
		return -1;
	}

	p_pack_conf->cat_head->qid = p_pack_conf->qid_in;

	/* load pitcher processes */
	memset(pathname , 0x00 , sizeof(pathname));
	sprintf(pathname , "%s/cfg/%s" , getenv("PRESS_HOME") , filename);
	fp = fopen(pathname , "r");
	if ( fp == NULL ){
		log_write(SYSLOG , LOGERR , "pack config file [%s] not found" , filename);
		return -1;
	}

	while (fgets(line , sizeof(line), fp) != NULL) {

		if ( line[0] == '\n' || line[0] == '#' )
			continue;

		log_write(SYSLOG , LOGDBG , "pack.cfg line[%s]" , line);

		pit_cur = (pit_proc_st *)malloc(sizeof(pit_proc_st));
		if ( pit_cur == NULL ){
			log_write(SYSLOG , LOGERR , "PRESS DAEMON: malloc fails");
			fclose(fp);
			return -1;
		}

        pit_cur->index = count;

		/* get tpl file */
		if (get_bracket(line , 1 , buf , 100)){
			log_write(SYSLOG , LOGERR , "pack.cfg format error");
			fclose(fp);
			return -1;
		}
		log_write(SYSLOG , LOGDBG , "tpl_filename[%s]" ,buf );

        strcpy(pit_cur->tplFileName , buf);

		memset(pathname , 0x00 , sizeof(pathname));
		sprintf(pathname , "%s/data/tpl/%s" , getenv("PRESS_HOME") , buf);
		pit_cur->tpl_fp = fopen(pathname , "r");
		if ( pit_cur->tpl_fp == NULL ){
			log_write(SYSLOG , LOGERR , "can not open tpl file[%s]",pathname);
			fclose(fp);
			return -1;
		}

		/* get rule */
		if (get_bracket(line , 2 , buf , 100)){
			log_write(SYSLOG , LOGERR , "pack.cfg format error");
			fclose(fp);
			return -1;
		}
		log_write(SYSLOG , LOGDBG , "rule_filename[%s]" ,buf );
		memset(pathname , 0x00 , sizeof(pathname));
		sprintf(pathname , "%s/data/rule/%s" , getenv("PRESS_HOME") , buf);
		pit_cur->rule_fp = fopen(pathname , "r");
		if ( pit_cur->rule_fp == NULL ){
			log_write(SYSLOG , LOGERR , "can not open rule file[%s]",pathname);
			fclose(fp);
			return -1;
		}

		/*get tps*/
		if (get_bracket(line , 3 , buf , 100)){
			log_write(SYSLOG , LOGERR , "pack.cfg format error");
			fclose(fp);
			return -1;
		}
		pit_cur->tps = atoi(buf);
		log_write(SYSLOG , LOGDBG , "tps[%d]" ,pit_cur->tps );

		/* get time */
		if (get_bracket(line , 4 , buf , 100)){
			log_write(SYSLOG , LOGERR , "pack.cfg format error");
			fclose(fp);
			return -1;
		}
		pit_cur->time = atoi(buf);
		log_write(SYSLOG , LOGDBG , "time[%d]" ,pit_cur->time );

		/* get qid */
		pit_cur->qid = p_pack_conf->qid_out;

		count ++;
		pit_cur->next = p_pack_conf->pit_head;
		p_pack_conf->pit_head = pit_cur;

	}

	if ( count == 0 ){
		log_write(SYSLOG , LOGERR , "pack.cfg is empty");
		fclose(fp);
		return 0;
	}

	fclose(fp);
	log_write(SYSLOG , LOGDBG , "pack.cfg loaded");
	return 0;
}

int pack_config_free(pack_config_st *p_pack_conf)
{
	log_write(SYSLOG , LOGDBG , "clearing configs");
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
		free(pit_del);
	}
	free(cur->cat_head);

	log_write(SYSLOG , LOGDBG ,  "clearing configs end");
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
	log_write(SYSLOG , LOGINF , "load %s" , buf);

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
	log_write(SYSLOG , LOGINF , "pitcher config loaded " );
    return 0;
}

int pack_send(pack_config_st *p_pack_conf)
{
	pit_proc_st *cur = p_pack_conf->pit_head;
	/* start catcher */
	p_pack_conf->cat_head->pid = pack_cat_start(p_pack_conf->cat_head);
	log_write(SYSLOG , LOGINF , "catcher start , PID[%d]",p_pack_conf->cat_head->pid);
	/* start pitchers */
	while ( cur != NULL ) {
		cur->pid = pack_pit_start(cur);
		log_write(SYSLOG , LOGINF , "pitcher start , PID[%d]" ,cur->pid);
		cur = cur->next;
	}
	p_pack_conf->status = RUNNING;

	return 0;
}

int pack_shut(pack_config_st *p_pack_conf)
{
	pit_proc_st 	*pit_cur = p_pack_conf->pit_head;
	cat_proc_st		*cat_cur = p_pack_conf->cat_head;
    stat_st         *l_stat = NULL;

	while ( pit_cur != NULL ){
        if( pit_cur->pid){
		    kill(pit_cur->pid , SIGTERM);
            l_stat = g_stat+pit_cur->index;
            l_stat->tag = FINISHED;
		    log_write(SYSLOG , LOGINF , "killing pitcher pid[%d]" , pit_cur->pid);
        }
		pit_cur = pit_cur->next;
	}

    if( cat_cur->pid){
	    kill(cat_cur->pid , SIGTERM);
	    log_write(SYSLOG , LOGINF , "killing catcher pid[%d]" , cat_cur->pid);
    }

    p_pack_conf->status = FINISHED;

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

    /* relocate share memory */
    g_stat = (stat_st *)shmat(g_shmid , NULL,  0);
    if ( g_stat == NULL ){
		log_write(SYSLOG , LOGERR , "shmat fail");
		return -1;
    }
	log_write(SYSLOG , LOGDBG , "shmat OK , g_stat = %u" , g_stat);

	log_write(SYSLOG , LOGDBG , "enter pack_pit_start");
	tpl_st mytpl;
	rule_st *ruleHead = NULL;
	rule_st *currule = NULL;
	char replace[MAX_LINE_LEN];
	char temp[MAX_LINE_LEN];
	msg_st	msgs;
	int ret = 0;
	struct timeval tv_now;
	struct timeval tv_start;
	struct timeval tv_interval;
	struct timeval tv_begin;
    int timeIntervalUs = 1000000 / p_pitcher->tps;

    /* relocate l_stat and update share memory */
    stat_st *l_stat = g_stat + p_pitcher->index;
    l_stat->tag = RUNNING;

    /* load template */
	memset(&mytpl , 0x00 , sizeof(mytpl));
	if (get_template(p_pitcher->tpl_fp , &mytpl)){
		log_write(SYSLOG , LOGERR , "load template file fail");
		fclose(p_pitcher->tpl_fp);
		exit(-1);
	}
    /* load rules */
	ruleHead = get_rule(p_pitcher->rule_fp);
	if (ruleHead == NULL){
		log_write(SYSLOG , LOGERR , "load rules fail");
		fclose(p_pitcher->rule_fp);
		exit(-1);
	}

	msgs.type = 1;
	gettimeofday(&tv_begin,NULL);
	while( l_stat->left_num > 0 ){
	    gettimeofday(&tv_start,NULL);
		memset(msgs.text , 0x00 , sizeof(msgs.text));
		memcpy(msgs.text , mytpl.text , mytpl.len + 4);
		currule = ruleHead;
        /* apply substitution rules */
		while (currule != NULL){
			memset(replace , 0x00 , sizeof(replace));
			memset(temp , 0x00 , sizeof(temp));

			strcpy(temp , currule->rep_head->text);
			currule->rep_head = currule->rep_head->next;

			memcpy(msgs.text + currule->start - 1 + 4 , temp , currule->length);
			
			currule = currule->next;
		}
        /* send to out queue */
		ret = msgsnd((key_t)p_pitcher->qid , &msgs , sizeof(msg_st) - sizeof(long) , 0);
		if (ret < 0){
			log_write(SYSLOG , LOGERR , "pitcher msgsnd error");
			exit(1);
		}
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

int pack_cat_start(cat_proc_st *p_catcher)
{
	int pid;
    int i , j;
    char hex[MAX_MSG_LEN];
	pid = fork();
	if ( pid < 0 ){
		return -1;
	} else if ( pid > 0 ){
		return pid;
	}
	
	signal(SIGTERM, pack_cat_signal_handler);
	signal(SIGALRM, pack_cat_signal_handler);

	FILE *fp = NULL;
	msg_st msgs;
	int ret;
	char pathname[MAX_PATHNAME_LEN];

	memset(pathname , 0x00 , sizeof(pathname));
	sprintf(pathname , "%s/data/result.txt" , getenv("PRESS_HOME"));
	fp = fopen(pathname , "w+");
	if (fp == NULL){
		log_write(SYSLOG , LOGERR , "can not open result.txt");
		exit(1);
	}

	while (1){
		alarm(20);
		ret = msgrcv(	(key_t)p_catcher->qid , \
						&msgs , \
						sizeof(msg_st) - sizeof(long) , \
						0, \
						0 );
		if (ret < 0){
			log_write(SYSLOG , LOGERR , "msgrcv error");
			exit(1);
		}
		alarm(0);

        if ( msgs.flag == 'O' ){
            fprintf( fp , "%ld.%06d >>>" , msgs.ts.tv_sec , msgs.ts.tv_usec );
        } else if ( msgs.flag == 'I' ){
            fprintf( fp , "%ld.%06d <<<" , msgs.ts.tv_sec , msgs.ts.tv_usec );
        }
		int len = get_length(msgs.text);
        if ( ENCODING == 'H' ){
            memset( hex , 0x00 , MAX_MSG_LEN);
            j = 0;
            for ( i = 0 ; i < len+4 && j < MAX_MSG_LEN ; i ++ ){
                hex[j]   = msgs.text[i] / 16 + '0';
                hex[j+1] = msgs.text[i] % 16 + '0';
                j += 2;
            }
            fprintf( fp , "%s\n" , hex);
        } else {
		    fwrite(msgs.text , 1 , len + 4 , fp);
		    fwrite("\n" , 1, 1 , fp);
        }
		fflush(fp);
	}

	fclose(fp);
	exit(0);
}

void pack_cat_signal_handler(int signo)
{
	log_write(SYSLOG , LOGDBG , "catcher quit");
	exit(0);
}

rule_st *get_rule(FILE *fp)
{
	char 	line[150];
	char 	buf[100];
	char 	pathname[MAX_PATHNAME_LEN];

	char 	temp[MAX_LINE_LEN];
    char    replace[MAX_REP_LEN];
	FILE 	*rep_fp = NULL;
	rep_st	*rep_head = NULL;
	rep_st	*rep_tail = NULL;
	rep_st	*rep_cur = NULL;

	rule_st *cur = NULL;
	rule_st *ret = NULL;

	memset(line , 0x00 , sizeof(line));
	memset(buf , 0x00 , sizeof(buf));

	log_write(SYSLOG , LOGDBG , "enter get_rule");

	while( fgets(line , sizeof(line) , fp) != NULL ){

		if ( line[0] == '\n' || line[0] == '#' )
			continue;

		cur = (rule_st *)malloc(sizeof(rule_st));
		if ( cur == NULL ){
			log_write(SYSLOG , LOGERR , "malloc fails");
			fclose(fp);
			return NULL;
		}

		memset(cur , 0x00 , sizeof(rule_st));

		if (get_bracket(line , 1 , buf , 100))
		{
			log_write(SYSLOG , LOGERR , "rule file format error");
			return NULL;
		}
		cur->start = atoi(buf);
		log_write(SYSLOG , LOGDBG , "rule->start[%d]" , cur->start);

		if (get_bracket(line , 2 , buf , 100))
		{
			log_write(SYSLOG , LOGERR , "rule file format error");
			return NULL;
		}
		cur->length = atoi(buf);
		log_write(SYSLOG , LOGDBG , "rule->length[%d]" , cur->length);

		if (get_bracket(line , 4 , buf , 100))
		{
			log_write(SYSLOG , LOGERR , "rule file format error");
			return NULL;
		}
		cur->pad = atoi(buf);
		log_write(SYSLOG , LOGDBG , "pad[%d]" , cur->pad);

		if (get_bracket(line , 3 , buf , 100))
		{
			log_write(SYSLOG , LOGERR , "rule file format error");
			return NULL;
		}
		memset(pathname , 0x00 , sizeof(pathname));
		sprintf(pathname , "%s/data/rep/%s" , getenv("PRESS_HOME") , buf);
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
		cur->rep_head = rep_head;


		cur->next = ret;
		ret = cur;
	}
	log_write(SYSLOG , LOGDBG , "rule loaded");
	return ret;
}

int get_template(FILE *fp_tpl , tpl_st *mytpl)
{
	if ( fread(mytpl->text , 1 , 4 , fp_tpl) < 4){
		log_write(SYSLOG , LOGERR , "template file error");
		return -1;
	}
	mytpl->len = get_length(mytpl->text);
	if ( fread(mytpl->text + 4 , 1 , mytpl->len , fp_tpl) < mytpl->len ){
		log_write(SYSLOG , LOGERR , "template file error");
		return -1;
	}
	return 0;
}

void cleanRule(rule_st *ruleHead)
{
	rule_st *cur = ruleHead;
	rule_st *nex = ruleHead;
	rep_st	*rep_cur = NULL;

	while ( cur != NULL ){
		while( cur->rep_head != NULL){
			if( cur->rep_head->next == cur->rep_head ){
				free(cur->rep_head);
				cur->rep_head = NULL;
			} else {
				rep_cur = cur->rep_head->next->next;
				free(rep_cur->next);
				cur->rep_head->next = rep_cur;
			}
		}
		nex = cur->next;
		free(cur);
		cur = nex;
	}
	return;
}

void reply(char *rep)
{
	msg_st msgs;								/*message for command*/
    memset(&msgs , 0x00 , sizeof(msgs));
    msgs.type = presscmd_pid;
    msgs.pid = getpid();
    strcpy(msgs.text , rep);
    msgsnd((key_t)(QID_CMD) , &msgs , sizeof(msgs) - sizeof(long) , 0);
    return;
}

char *get_stat(conn_config_st *p_conn_conf , pack_config_st *p_pack_conf)
{
    char *ret = (char *)malloc(MAX_MSG_LEN);
    int offset = 0;
    char status[20];
    memset(ret , 0x00 , MAX_MSG_LEN);
    comm_proc_st *p_comm = p_conn_conf->process_head;
    pit_proc_st  *p_pack = p_pack_conf->pit_head;
    stat_st *l_stat;
    if ( p_conn_conf->status == RUNNING ){
        offset += sprintf(ret+offset , "======================communication status================================\n");
        offset += sprintf(ret+offset , "[TYPE][PID       ][IP             ][PORT  ][PERSIST][STATUS              ]\n");
        while ( p_comm != NULL ){
            memset( status , 0x00 , sizeof(status));
            if ( kill( p_comm->pid , 0 ) == 0 ){
                strcpy(status , "RUNNING");
            } else {
                strcpy(status , "DEAD");
            }
            offset += sprintf(ret+offset , 
                            "[%c   ][%-10u][%-15s][%-6d][%-7d][%-20s]\n" , \
                            p_comm->type , \
                            p_comm->pid , \
                            p_comm->ip , \
                            p_comm->port ,\
                            p_comm->persist ,\
                            status);
            p_comm = p_comm->next;
        }
        offset += sprintf(ret+offset , "==========================================================================\n");
    } else {
        offset += sprintf(ret+offset , "no conn process running\n");
    }
    if ( p_pack_conf->status == LOADED || p_pack_conf->status == RUNNING ){
        offset += sprintf(ret+offset , \
                "===============================packing status=============================================\n");
        offset += sprintf(ret+offset , \
                "[INDEX][TPLFILENAME][STATUS    ][TPS     ][SEND_NUM  ][LEFT_NUM  ][TIME_LAST ][TIME_LEFT ]\n");
        while ( p_pack != NULL ) {
            l_stat = g_stat + p_pack->index;
            memset( status , 0x00 , sizeof(status));
            if ( l_stat->tag != NOTLOADED ){
                if ( l_stat->tag == RUNNING ){
                    strcpy(status , "RUNNING");
                } else if ( l_stat->tag == LOADED ){
                    strcpy(status , "LOADED");
                } else if ( l_stat->tag == FINISHED ){
                    strcpy(status , "FINISHED");
                }
                offset += sprintf(ret+offset , \
                                "[%-5d][%-11s][%-10s][%-8d][%-10d][%-10d][%-10d][%-10d]\n" , \
                                p_pack->index,\
                                p_pack->tplFileName,\
                                status,\
                                l_stat->tps,\
                                l_stat->send_num,\
                                l_stat->left_num,\
                                l_stat->timelast,\
                                l_stat->timeleft);
            }
            p_pack = p_pack->next;
        }
        offset += sprintf(ret+offset , \
                "===============================packing status=============================================\n");
    } else {
        offset += sprintf(ret+offset , "no packing process running");
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
                offset += sprintf(ret+offset , "ERROR: invalid adjustment1.[%s]",msg);
                return ret;
            }
            break;
        }
        if ( msg[i] < '0' || msg[i] > '9' ){
            offset += sprintf(ret+offset , "ERROR: invalid adjustment2.[%s]",msg);
            return ret;
        }
        adjustment*=10;
        adjustment+=(msg[i] - '0');
    }
    if ( msg[i] == '%' ){
        if ( direc == 0 ){
            offset += sprintf(ret+offset , "ERROR: invalid adjustment3.[%s]",msg);
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
            offset += sprintf(ret+offset , "Modified tps for INDEX[%d] , [%d]--->[%d]" , id , before , after);
        else
            offset += sprintf(ret+offset , "Modified totaltime for INDEX[%d] , [%d]--->[%d]" , id , before , after);
    } else {
        while ( p_pack != NULL ){
            status_op(flag , p_pack->index , adjustment , percent , direc , &before , &after);
            if ( flag == 1 )
                offset += sprintf(ret+offset , "Modified tps for INDEX[%d] , [%d]--->[%d]\n" , p_pack->index , before , after);
            else
                offset += sprintf(ret+offset , "Modified totaltime INDEX[%d] , [%d]--->[%d]\n" , p_pack->index , before , after);
            p_pack = p_pack->next;
        }
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

int main(int argc , char *argv[])
{
	/*check for environments*/
	if ( getenv("PRESS_HOME") == NULL ){
		printf("missing environment PRESS_HOME\n");
		exit(1);
	}

	/*become daemon*/
	daemon_start();

	/*init log module*/
	log_init(SYSLOG , "system");

    /* check if already running */
    if ( check_deamon() ){
        log_write(SYSLOG , LOGERR , "deamon already running");
        exit(1);
    }

    FILE *fp = fopen( PIDFILE , "wb");
    if ( fp == NULL ){
        log_write(SYSLOG , LOGERR , "fail to open file %s , errno = %d" , errno , PIDFILE) ;
    } else {
        fprintf(fp , "%d" , getpid());
        fclose(fp);
    }

	log_write(SYSLOG , LOGINF , "daemon start");

	/*deal with signals*/
	signal(SIGCHLD , SIG_IGN);

	conn_config_st *p_conn_conf = NULL;			/*configs for conn*/
    pack_config_st *p_pack_conf = NULL;			/*configs for pack*/
	msg_st msgs;								/*message for command*/
    char *retmsg = NULL;

	/*LOAD QID FOR COMMAND*/
	if ( (QID_CMD = get_qid("MSGKEY_CMD")) < 0){
		log_write(SYSLOG , LOGERR , "MSGKEY_CMD not found");
		return -1;
	}

	p_conn_conf = (conn_config_st *)malloc(sizeof(conn_config_st));
	if ( p_conn_conf == NULL ){
		log_write(SYSLOG , LOGERR , "malloc fail");
		return -1;
	}
    p_conn_conf->status = NOTLOADED;

	p_pack_conf = (pack_config_st *)malloc(sizeof(pack_config_st));
	if ( p_pack_conf == NULL ){
		log_write(SYSLOG , LOGERR , "malloc fail");
		return -1;
	}
    p_pack_conf->status = NOTLOADED;

    /* create share memory */
    g_shmid = shmget( (key_t)18000 , MAX_PROC_NUM*sizeof(stat_st) , IPC_CREAT|0660);
    if ( g_shmid < 0 ){
		log_write(SYSLOG , LOGERR , "create share memory fail");
		return -1;
    }
	log_write(SYSLOG , LOGINF , "create share memory OK , shmid = %d" , g_shmid);
    g_stat = (stat_st *)shmat(g_shmid , NULL,  0);
    if ( g_stat == NULL ){
		log_write(SYSLOG , LOGERR , "shmat fail");
		return -1;
    }
	log_write(SYSLOG , LOGINF , "shmat OK , g_stat = %u" , g_stat);

	/*wait for command*/
	while (1){
		log_write(SYSLOG , LOGDBG , "DAEMON: wait for command");

		memset(&msgs , 0x00 , sizeof(msgs));
		msgrcv((key_t)(QID_CMD) , &msgs , sizeof(msgs) - sizeof(long) , getpid() , 0);
		log_write(SYSLOG , LOGDBG ,"receive command [%s] from presscmd[%d]" , msgs.text , msgs.pid);
        presscmd_pid = msgs.pid;

		if ( strncmp(msgs.text , "init" , 4) == 0 ){
			if (p_conn_conf->status == RUNNING){
				log_write(SYSLOG , LOGINF ,"conn already running");
                reply("conn already running , input \"stop\" to stop communication");
				continue;
			}
			conn_start(p_conn_conf);
            reply("press init OK");
		} else if ( strncmp(msgs.text , "stop" , 4) == 0 ){
			if (p_conn_conf->status != RUNNING){
				log_write(SYSLOG , LOGINF ,"conn not running ");
                reply("conn not running , input \"init\" to start communication");
				continue;
			}
			conn_stop(p_conn_conf);
            reply("press stop OK");
		} else if ( strncmp(msgs.text , "send" , 4) == 0 ) {
            if ( p_conn_conf->status != RUNNING ) {
				log_write(SYSLOG , LOGINF ,"conn not running ");
                reply("conn not running , input \"init\" to start communication");
				continue;
            }
            if ( p_pack_conf->status != LOADED ){
                reply("packing process config not loaded , input \"load\" to load config");
				continue;
            }
			pack_send(p_pack_conf);
            reply("press send OK");
		} else if ( strncmp(msgs.text , "shut" , 4) == 0){
			pack_shut(p_pack_conf);
            reply("press shut OK");
		} else if ( strncmp(msgs.text , "kill" , 4) == 0 ){
			if (p_conn_conf->status != RUNNING){
				log_write(SYSLOG , LOGINF ,"daemon exit");
                reply("deamon exit");
				break;
			}
			else {
				log_write(SYSLOG , LOGINF ,"conn still running , can not kill");
                reply("conn still running , input \"stop\" to stop communication");
			}
		} else if ( strncmp(msgs.text , "stat" , 4) == 0){
            retmsg = get_stat(p_conn_conf , p_pack_conf);
            reply(retmsg);
            free(retmsg);
        } else if ( strncmp(msgs.text , "load" , 4) == 0){
            if ( pack_load(msgs.text , p_pack_conf) ){
                reply("config load fail");
                continue;
            } else {
                reply("config loaded");
                continue;
            }
        } else if ( strncmp( msgs.text , "tps" , 3) == 0){
            if ( p_pack_conf->status == NOTLOADED  || p_pack_conf->status == FINISHED ){
                reply("packing process not loaded, input \"load\" to start sending");
                continue;
            }
            retmsg = adjust_status( 1 , msgs.text , p_pack_conf);
            reply(retmsg);
            free(retmsg);
        } else if ( strncmp( msgs.text , "time" , 4) == 0 ){
            if ( p_pack_conf->status == NOTLOADED || p_pack_conf->status == FINISHED ){
                reply("packing process not loaded, input \"load\" to start sending");
                continue;
            }
            retmsg = adjust_status( 2 , msgs.text , p_pack_conf);
            reply(retmsg);
            free(retmsg);
        }
	}
    /* clean everything */
    sleep(1);
	log_clear();
    pack_config_free(p_pack_conf);
	conn_config_free(p_conn_conf);
	msgctl((key_t)QID_CMD , IPC_RMID , NULL);
	msgctl((key_t)p_conn_conf->qid_in , IPC_RMID , NULL);
	msgctl((key_t)p_conn_conf->qid_out , IPC_RMID , NULL);
    shmdt((void *)g_stat);
    shmctl(g_shmid , IPC_RMID , 0);
    remove("/tmp/press.pid");
	return 0;
}
