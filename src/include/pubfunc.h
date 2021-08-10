#ifndef _PUBFUNC_H_
#define _PUBFUNC_H_

#include "common.h"

#define MAX_CFG_KEY_LEN 30
#define MAX_CFG_VAL_LEN 50
#define MAX_CFG_LINE_LEN 100
#define MAX_MSG_LEN 2000

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
    int  length;
	char text[MAX_MSG_LEN];
	struct timeval ts;
}msg_st;

union semun1 {
   	int              val;    /* Value for SETVAL */
   	struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
   	unsigned short  *array;  /* Array for GETALL, SETALL */
   	struct seminfo  *__buf;  /* Buffer for IPC_INFO*/
};

int         get_bracket(const char *line , int no , char *value , int val_size);
int         get_length(const char *);
int         padding(char *s , char dir , char sub , char *d , int d_len);
void        rTrim(char *str);
void        lTrim(char *str);
int         loadConfig(char *key , char *value  ,int val_len);
int         _pow(int , int);

#endif
