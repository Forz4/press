#ifndef _PRESS_CMD_H_
#define _PRESS_CMD_H_

#include "common.h"
#include "pubfunc.h"
#include "version.h"
#define  MAX_NODE_NUM  50

static void print_usage();
static void print_usage();
void sig_handler(int signo);
int parse_ip_port( char *optarg , char *ip , int *port);
#endif

