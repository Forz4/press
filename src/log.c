#include "include/log.h"

log_t *log_head = NULL;

int log_init(log_type_t type , char *filename)
{
	log_t *cur = NULL;
	char pathname[MAX_PATHNAME_LEN];
	memset(pathname , 0x00 , sizeof(pathname));
	sprintf(pathname , "%s/log/%s.log" , getenv("PRESS_HOME") , filename);
    /*
	char key[]="LOGLEVEL";
	char value[MAX_CFG_VAL_LEN];

	if (log_head == NULL){
		if (loadConfig(key , value , MAX_CFG_VAL_LEN)){
			LOGLEVEL = 0;
		} else {
			LOGLEVEL = atoi(value);
		}
	}
    */

	cur = (log_t *)malloc(sizeof(log_t));
	if ( cur == NULL )
	{
		printf("malloc fail , exit\n");
		exit(-1);
	}
	cur->type = type;
	cur->p_file = fopen(pathname , "a+");
	if (cur->p_file == NULL)
	{
		printf("fopen fail , exit\n");
		exit(-2);
	}

	cur->next = log_head;
	log_head = cur;

	return 0;
}

void log_clear()
{
	log_t *cur = log_head;
	log_t *del = NULL;
	while (cur != NULL)
	{
		del = cur;
		cur = cur->next;
		fclose(del->p_file);
		free(del);
	}
	return ;
}

void log_write(log_type_t type, log_level_t level , char *fmt , ...)
{
	if (level > LOGLEVEL)
		return;

	char level_print[7][10] = { "LOGNON" , "LOGFAT" , "LOGWAN" , "LOGERR" , "LOGINF" , "LOGADT" , "LOGDBG" };
	va_list ap;
	time_t t;
	struct tm *timeinfo;
	char message[MAX_LOG_LINE];
	int sz = 0;
	FILE *fp = NULL;
	struct timeval tv_now;
	memset(message , 0x00 , sizeof(message));

	/*get file pointer*/
	log_t *cur = log_head;
	while (cur != NULL)
	{
		if (type == cur->type){
			fp = cur->p_file;
			break;
		}
		cur = cur->next;
	}
	if (fp == NULL)
	{
		return;
	}

	t = time(NULL);
	gettimeofday(&tv_now , NULL);

	time(&t);
	timeinfo = localtime(&t);
	sz = sprintf(message , \
			"[%02d:%02d:%02d:%06d][PID<%-8d>][%6s]" ,\
			timeinfo->tm_hour , \
			timeinfo->tm_min, \
			timeinfo->tm_sec, \
			tv_now.tv_usec, \
			getpid(), \
			level_print[level] );
	va_start(ap , fmt);
	vsnprintf(message + sz , MAX_LOG_LINE - sz , fmt , ap);
	va_end(ap);

	fprintf(fp , "%s\n" , message);
	fflush(fp);
	return ;
}

