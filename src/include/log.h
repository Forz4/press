#ifndef _LOG_H_
#define _LOG_H_

#include "common.h"
#include "pubfunc.h"
#include <stdarg.h>

#define MAX_LOG_LINE 2000

typedef enum log_type{
	SYSLOG,
	PCKLOG,
    CONLOG
}log_type_t;

typedef enum log_level{
	LOGNON=0,
	LOGFAT=1,
	LOGWAN=2,
	LOGERR=3,
	LOGINF=4,
	LOGADT=5,
	LOGDBG=6
}log_level_t;

typedef struct log_st{
	log_type_t type;
	FILE *p_file;
	struct log_st *next;
}log_t;

log_level_t	LOGLEVEL;           /* 日志级别 */
extern int log_init(log_type_t type , char *filename);
extern void log_clear();
extern void log_write(log_type_t type, log_level_t level , char *fmt , ...);

#endif
