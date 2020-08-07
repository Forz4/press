#ifndef _PUBFUNC_H_
#define _PUBFUNC_H_

#include "common.h"

#define MAX_CFG_KEY_LEN 30
#define MAX_CFG_VAL_LEN 50
#define MAX_CFG_LINE_LEN 100
#define MAX_MSG_LEN 2000
#define PIDFILE "/tmp/press.pid"

#define	timersub(tvp, uvp, vvp)						        \
	do {								                    \
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				            \
			(vvp)->tv_sec--;				                \
			(vvp)->tv_usec += 1000000;			            \
		}							                        \
	} while (0)

typedef struct message_struct{
	long type;
	char text[MAX_MSG_LEN];
    char flag;  /* to tell a sender or receiver*/
    int  pid;   /* to indicate to pid of sender */
	struct timeval ts;
}msg_st;

extern int get_bracket(const char *line , int no , char *value , int val_size);
extern int get_qid(char *key);
extern int get_length(const char *);
extern int padding(char *s , char dir , char sub , char *d , int d_len);
extern void rTrim(char *str);
extern void lTrim(char *str);
extern int loadConfig(char *key , char *value  ,int val_len);
void daemon_start();
extern int check_deamon();
extern int _pow(int , int);

#endif
