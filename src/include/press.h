#ifndef _PRESS_H_
#define _PRESS_H_

#include "common.h"
#include "log.h"
#include "pubfunc.h"
#include "version.h"

#define MAX_CMD_LEN 5000
#define MAX_TPL_LEN 3000
#define MAX_REP_LEN	50
#define NOTLOADED 	    0
#define LOADED 	        1
#define RUNNING		    2
#define FINISHED        3
#define MAX_PROC_NUM    40
#define MAX_PARA_NUM    500

#define CONN_NOTREADY       0
#define CONN_ESTABLISHED	1
#define CONN_REDISERROR     2
#define CONN_CONNECTING		3
#define CONN_BINDFAIL       4
#define CONN_LISTENFAIL     5
#define CONN_ACCEPTFAIL     6
#define CONN_RECVERROR      7
#define CONN_MSGRCVERROR    8
#define CONN_SENDERROR      9
#define CONN_ACCEPTING      10

#define MONITOR_TPS			1
#define MONITOR_RESPONSE	2

#define REPTYPE_RANDOM  1
#define REPTYPE_FILE    2
#define REPTYPE_F7      3
#define REPTYPE_PIN     4

#define STAT_CONN       0x1
#define STAT_PACK       0x10
#define STAT_MONI       0x100

#define LONG_SEND       1 
#define LONG_RECV       2
#define SHORTCONN       3
#define JIPSCONN        4 

typedef struct comm_proc{
	char 	type;					/*comm type , S or R*/
	pid_t 	pid;					/*process id*/
	char 	ip[20];					/*ip address*/
	int 	port;					/*port number*/
    int     persist;                /*wheathe writes to file*/
    int     parallel;               /*parallel number*/
    pid_t   para_pids[MAX_PARA_NUM];/*pids*/
    int     status;					/*connection status*/
    int     monitor;				/*monitor switch*/
	struct 	comm_proc *next;		/*next process*/
}comm_proc_st;

typedef struct conn_config{
	int 	status;					/*0: config not loaded , 1: config loaded but not started , 2: process started*/
	struct 	comm_proc *process_head;
}conn_config_st;

typedef struct pit_proc{
    int     index;
	pid_t 	pid;
    char    tplFileName[MAX_FILENAME_LEN];
    char    trannum[4+1];
	FILE 	*tpl_fp;
	FILE 	*rule_fp;
	struct  REPLACE	*rep_head;
	int 	tps; 
	int		time;
	int 	qid;
	struct 	pit_proc *next;
}pit_proc_st;

typedef struct pack_config{
	int 	status;
	struct 	pit_proc *pit_head;
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
    int     type;           /* type */
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

int			getTranInfo( char *message  , char *key  , char *trannum , char *retcode );
int         persist(char *text , int len , char type , struct timeval ts);
rule_st    *get_rule(FILE *fp);
void        clean_rule(rule_st *ruleHead);
int         get_template(FILE *fp_tpl , tpl_st *mytpl);
int         conn_config_load(conn_config_st *);
void        conn_config_free(conn_config_st *);
int         conn_receiver_start(comm_proc_st *);
void        conn_receiver_signal_handler(int no);
int         conn_sender_start(comm_proc_st *);
void        conn_sender_signal_handler(int no);
int         conn_jips_start(comm_proc_st *p_jips);
void        conn_jips_signal_handler(int signo);
void        conn_report_status( int );
void        conn_send_idle(int);
int         pack_config_load(char *filename , pack_config_st *p_pack_conf);
int         pack_config_free(pack_config_st *p_pack_conf);
void        pack_pit_load(stat_st *l_stat , pit_proc_st *p_pitcher);
int         pack_pit_start(pit_proc_st *p_pitcher);
void        pack_pit_signal_handler(int signo);
int         command_conn_stop(conn_config_st *);
int         command_conn_start(conn_config_st *);
int         command_pack_load(char *msg , pack_config_st *p_pack_conf);
int         command_pack_send(pack_config_st *p_pack_conf);
int         command_pack_shut(pack_config_st *p_pack_conf);
char       *command_get_stat(int, conn_config_st * , pack_config_st *);
char       *command_adjust_status(int , char * , pack_config_st *);
char       *command_adjust_para( char *msg , conn_config_st *p_conn_config);
void 		command_update_conn_status( char *buffer_cmd , conn_config_st *p_conn_conf );
void        deamon_status_op(int flag , int id , int adjustment , int percent , int direc , int *before , int *after);
int         deamon_check();
void        deamon_start();
void        deamon_exit();
void        deamon_signal_handler( int signo);
void        deamon_print_help();

#endif

