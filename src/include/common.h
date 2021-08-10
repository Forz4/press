#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <math.h>
#include <stdarg.h>
#include <hiredis/hiredis.h>

#define MAX_PATHNAME_LEN 200
#define MAX_FILENAME_LEN 20
#define MAX_LINE_LEN 4000
#define BASE_DIR "PRESS_HOME"

#endif

