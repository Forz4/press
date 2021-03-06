#ifndef _PRESS_H_
#define _PRESS_H_

#include "common.h"
#include "log.h"
#include "pubfunc.h"

#define MAX_TPL_LEN 3000
#define MAX_REP_LEN	50
#define NOTLOADED 	    0
#define LOADED 	        1
#define RUNNING		    2
#define FINISHED        3
#define MAX_PROC_NUM    40

const char *PRESS_VERSION = "V1.0";

typedef struct comm_proc{
	char 	type;					/*comm type , S or R*/
	pid_t 	pid;					/*process id*/
	char 	ip[20];					/*ip address*/
	int 	port;					/*port number*/
	int 	qidSend;	    		/*qid */
	int 	qidRead;				/*qid */
    int     persist;                /*wheathe writes to file*/
	struct 	comm_proc *next;		/*next process*/
}comm_proc_st;

typedef struct conn_config{
	int 	status;					/*0: config not loaded , 1: config loaded but not started , 2: process started*/
	int 	qid_in;
	int 	qid_out;
	struct 	comm_proc *process_head;
}conn_config_st;

typedef struct pit_proc{
    int     index;
	pid_t 	pid;
    char    tplFileName[MAX_FILENAME_LEN];
	FILE 	*tpl_fp;
	FILE 	*rule_fp;
	struct  REPLACE	*rep_head;
	int 	tps; 
	int		time;
	int 	qid;
	struct 	pit_proc *next;
}pit_proc_st;

typedef struct cat_proc{
	pid_t 	pid;
	int 	qid;
    struct  cat_proc *next;
}cat_proc_st;

typedef struct pack_config{
	int 	status;
	int 	qid_in;
	int 	qid_out;
	struct 	pit_proc *pit_head;
	struct 	cat_proc *cat_head;
}pack_config_st;

typedef struct REPLACE{
	char	text[MAX_REP_LEN];
	struct 	REPLACE	*next;
}rep_st;

typedef struct TEMPLATE{
	int 	len;
	char 	text[MAX_TPL_LEN];
}tpl_st;

typedef struct RULE{
	int 	start;          /* start postion */
	int 	length;         /* substitution length */
	rep_st	*rep_head;      /* link list for replacements */
	int 	pad;            /* padding method , */
	struct 	RULE *next;
}rule_st;

typedef struct mystat{
    int     tag;            /* 0:empty   1:running   2:finished*/
    int     send_num;       /* number of packages already sent*/
    int     left_num;       /* number of packages left to send*/
    int     tps;            /* speed */
    int     timelast;       /* time already send */
    int     timeleft;       /* time left */
    int     timetotal;      /* time in total */
}stat_st;

typedef struct monstat{
    int     send_num;       /* number of packages sent by conn*/
    int     recv_num;       /* number of packages received by conn*/
    int     real_send_tps;  /* real send tps */
    int     real_recv_tps;  /* real recv tps */
}monstat_st;

int conn_config_load(conn_config_st *);
void conn_config_free(conn_config_st *);
int conn_start(conn_config_st *);
int conn_receiver_start(comm_proc_st *);
void conn_receiver_signal_handler(int no);
int conn_sender_start(comm_proc_st *);
void conn_sender_signal_handler(int no);
void send_idle(int);
int conn_stop(conn_config_st *);
int pack_config_load(char *filename , pack_config_st *p_pack_conf);
int pack_config_free(pack_config_st *p_pack_conf);
int pack_load(char *msg , pack_config_st *p_pack_conf);
int pack_send(pack_config_st *p_pack_conf);
int pack_shut(pack_config_st *p_pack_conf);
void pack_pit_load(stat_st *l_stat , pit_proc_st *p_pitcher);
int pack_pit_start(pit_proc_st *p_pitcher);
void cal_time_ns(int tps , struct timespec *ts);
int pack_cat_start(cat_proc_st *p_catcher);
void pack_cat_signal_handler(int signo);
rule_st *get_rule(FILE *fp);
int get_template(FILE *fp_tpl , tpl_st *mytpl);
void cleanRule(rule_st *ruleHead);
void reply(char *reply);
char *get_stat(conn_config_st * , pack_config_st *);
char *adjust_status(int , char * , pack_config_st *);
void status_op(int flag , int id , int adjustment , int percent , int direc , int *before , int *after);

#endif

