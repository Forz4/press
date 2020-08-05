#include "include/pubfunc.h"
extern int errno;

extern int get_bracket(const char *line , int no , char *value , int val_size)
{
	int i = 0;
	int j = 0;
	int count = 0;
	memset(value , 0x00 , val_size);
	while ( count != no){
		if ( line[i] == '\0' || line[i] == '\n' )
			break;
		if ( line[i] == '[' )
			count ++;
		i ++;
	}
	if ( count != no )
		return 1;
	j = 0;
	while ( line[i] != ']' ){
		if ( line[i] == '\0' || line[i] == '\n' )
			break;
		value[j] = line[i];
		j ++;
		i ++;
	}
	if ( line[i] != ']' )
		return 1;

	return 0;
}

extern int get_qid(char *key)
{
	char value[MAX_CFG_VAL_LEN];
	int MSGKEY , qid;
	if ( loadConfig(key , value , MAX_CFG_VAL_LEN) ){
		return -1;
	}
	MSGKEY = atoi(value);
	qid = msgget(MSGKEY , IPC_CREAT|0660);
	if ( qid < 0 ){
	    return -1;
    }
	return qid;
}

extern int get_length(const char *str)
{
	char buf[5];
	int i = 0;

	memset(buf , 0x00 , 5);
	memcpy(buf,str,4);

	for (i = 0 ; i < 4 ; i ++){
		if ( buf[i] < '0' || buf[i] > '9')
			return -1;
	}

	return atoi(buf);
}

extern int padding(char *s , char dir , char sub , char *d , int d_len)
{
	int i = 0;
	int j = 0;
	int s_len = strlen(s);
	memset(d , 0x00 , d_len);
	
	if ( s_len >= d_len ){
		memcpy(d , s , d_len);
		return 0;
	}

	if (dir == 'l'){
		for ( i = 0 ; i < d_len - s_len ; i ++ ) 	d[i] = sub;
		for ( j = 0 ; i < d_len ; i ++ , j ++)		d[i] = s[j];
	} else if (dir == 'r') {
		for ( i = 0 ; i < s_len ; i ++ , j ++ )		d[i] = s[j];
		for ( i = s_len ; i < d_len ; i ++)			d[i] = sub;
	} else {
		return -1;
	}
	return 0;
}

extern void rTrim(char *str)
{
	int i = 0;
	for(i = strlen(str) - 1 ; i >= 0 ; i --){
		if( str[i] != ' ' && str[i] != '\n' && str[i] != '\t' )
			break;
		str[i] = '\0';
	}
}

extern void lTrim(char *str)
{
	int i = 0;
	int j = 0;
	for(i = 0 ; i < strlen(str) ; i ++){
		if(str[i] == ' ' || str[i] == '\n' || str[i] == '\t')
			continue;
		else
			str[j ++] = str[i];
	}
	str[j] = '\0';
}

extern int loadConfig(char *key , char *value , int val_len)
{
	FILE *fp = NULL;
	int keylen = strlen(key);
	char tmpkey[MAX_CFG_KEY_LEN];
	char tmpvalue[MAX_CFG_VAL_LEN];
	char buf[MAX_CFG_LINE_LEN];
	char cfgFilename[MAX_PATHNAME_LEN];
	int i = 0;
	int j = 0;
	int flag = 0;

	memset(buf , 0x00 , sizeof(buf));
	memset(value , 0x00 , val_len);
	memset(cfgFilename , 0x00 , MAX_PATHNAME_LEN);

	sprintf(cfgFilename , "%s/cfg/press.cfg" , getenv("PRESS_HOME"));
	if ( (fp = fopen(cfgFilename,"r")) == NULL )	return -1;
	if ( keylen > MAX_CFG_KEY_LEN )	return -1;

	while (fgets(buf , MAX_CFG_LINE_LEN , fp) != NULL) {
		if (strlen(buf) == 0 || buf[0] == '#' || buf[0] == '\n' )	continue;
		rTrim(buf);
		lTrim(buf);
		i = 0;
		j = 0;
		flag = 0;
		memset(tmpkey , 0x00 , sizeof(tmpkey));
		memset(tmpvalue , 0x00 , sizeof(tmpvalue));
		for (i = 0 ; i < strlen(buf) ; i ++) {
			if ( flag == 0 ) {
				if ( buf[i] == '=' ) {
					flag = 1;
					j = 0;
					continue;
				}else
					tmpkey[j ++] = buf[i];
			}else
				tmpvalue[j ++] = buf[i];
		}
		int cmplen = (keylen > strlen(tmpkey) ? keylen : strlen(tmpkey));
		if (strncmp( key , tmpkey , cmplen) == 0)
		{
			strncpy(value , tmpvalue , val_len);
			return 0;
		}
	}
	return 1;
}

void daemon_start()
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
extern int check_deamon()
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
