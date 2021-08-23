#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <hiredis/hiredis.h>

void print_help()
{
	printf("press_report snapname_start snapname_end trannum,...\n");
	return;
}

void parse_trannum( char *input , char trannum[10][4+1] , int *numOfTran)
{
	int len = strlen(input);
	if ( len % 5 != 4 ){
		*numOfTran = 0;
		return;
	}
	int i = 0;
	for (i = 0 ; i <= len/5 ; i ++){
		if ( i > 0 && input[i*5-1] != ','){
			*numOfTran = 0;
			return; 
		}
		memcpy( trannum[i] , input+5*i , 4);
	}
	*numOfTran = i;
	return ;
}

int main( int argc , char *argv[])
{
	if ( argc < 3 ){
		printf("invalid arguments\n");
		print_help();
	  	exit(1);
	}
  
	if ( getenv("PRESS_HOME") == NULL ){
	  	printf("environment [PRESS_HOME] not found\n");
	  	exit(1);
	 }
  
	redisContext *c = redisConnect("127.0.0.1" , 6379);
	if(c->err) {
		printf("fail to connect to redis : %s\n" , c->errstr);
	  	redisFree(c);
    	exit(1);
    }

    char  		snapname_start[30];
    char  		snapname_end[30];
	char		filename_res[100];
	char		filename_log[100];
	int   		starttime = 0;
	int   		endtime = 0;
	char  		trannum[10][4+1];
	int   		numOfTran = 0;
	int   		i = 0;
	int   		j = 0;
	time_t		time = 0;
	struct tm   *temp = NULL;
	FILE 	    *fp_res = NULL;
	FILE 	    *fp_log = NULL;

	memset( snapname_start , 0x00 , sizeof(snapname_start) );
	memset( snapname_end , 0x00 , sizeof(snapname_end) );
    memset( filename_res , 0x00 , sizeof(filename_res) );
    memset( trannum , 0x00 , sizeof(trannum) );

    strcpy(snapname_start, argv[1]);
    strcpy(snapname_end, argv[2]);
    sprintf( filename_res, "%s/data/result/report_%s_%s.csv" ,\
              getenv("PRESS_HOME") , snapname_start , snapname_end );
    sprintf( filename_log, "%s/data/result/report_%s_%s.log" ,\
              getenv("PRESS_HOME") , snapname_start , snapname_end );
    fp_res = fopen(filename_res, "wb");
    if ( fp_res == NULL ){
    	printf("fopen[%s] error\n" , filename_res);
    	goto err;
    }
    fp_log = fopen(filename_log, "wb");
    if ( fp_log == NULL ){
    	printf("fopen[%s] error\n" , filename_log);
    	goto err;
    }

    fprintf(fp_log, "[start]press_report %s %s\n", snapname_start , snapname_end);

	if ( argc == 4 )	parse_trannum( argv[3] , trannum , &numOfTran);
	fprintf(fp_log, "[numOfTran]%d\n",numOfTran);
	for( i = 0 ; i < numOfTran ; i ++){
		fprintf(fp_log, "[trannum][%d]%s\n", i , trannum[i]);
	}
    redisReply *reply = NULL;
    reply = redisCommand( c , "zscore snap %s" , snapname_start);
    if ( reply->type == REDIS_REPLY_NIL ){
    	fprintf(fp_log, "[error]snapshot[%s] not found\n", snapname_start);
        freeReplyObject(reply);
    	goto err;
    } else {
    	starttime = atoi(reply->str);
    }
    freeReplyObject(reply);
    fprintf(fp_log, "[starttime]%d\n",starttime);

    reply = redisCommand( c , "zscore snap %s" , snapname_end);
    if ( reply->type == REDIS_REPLY_NIL ){
    	fprintf(fp_log, "[error]snapshot[%s] not found\n", snapname_end);
        freeReplyObject(reply);
    	goto err;
    } else {
    	endtime = atoi(reply->str);
    }
    freeReplyObject(reply);
    fprintf(fp_log, "[endtime]%d\n",endtime);

    if( starttime >= endtime ){
    	fprintf(fp_log, "[error]endtime[%d] is less than starttime[%d]\n", endtime , starttime );
    	goto err;
    }

    fprintf( fp_res, "TIME,SENDTPS,RECVTPS");
    for( i = 0 ; i < numOfTran ; i ++){
    	fprintf(fp_res , ",tran%s",trannum[i]);
    }
    fprintf(fp_res,"\n");

    int sendtps = 0;
    int recvtps = 0;
    int total = 0;
    int duration = 0;
    double response = 0;

    for ( i = starttime ; i <= endtime ; i ++ ){
    	time = i;
    	temp = localtime(&time);
    	fprintf( fp_res , "%02d:%02d:%02d" , temp->tm_hour , temp->tm_min , temp->tm_sec);

    	reply = redisCommand(c , "get sendtps|%d" , time);
    	if ( reply->type != REDIS_REPLY_NIL ){
    		sendtps = atoi(reply->str);
    	} else {
    		sendtps = 0;
    	}
    	fprintf( fp_res , ",%d" , sendtps);
    	freeReplyObject(reply);

    	reply = redisCommand(c , "get recvtps|%d" , time);
    	if ( reply->type != REDIS_REPLY_NIL ){
    		recvtps = atoi(reply->str);
    	} else {
    		recvtps = 0;
    	}
    	fprintf( fp_res , ",%d" , recvtps);
    	freeReplyObject(reply);

    	for ( j = 0 ; j < numOfTran ; j ++){
    		reply = redisCommand(c , "get trac|%s|total|%d" , trannum[j] , i);
    		if ( reply->type != REDIS_REPLY_NIL ){
    			total = atoi(reply->str);
    		} else {
    			total = 0;
    		}
    		freeReplyObject(reply);

    		reply = redisCommand(c , "get trac|%s|duration|%d" , trannum[j] , i);
    		if ( reply->type != REDIS_REPLY_NIL ){
    			duration = atoi(reply->str);
    		} else {
    			duration = 0;
    		}
    		freeReplyObject(reply);

    		if ( total > 0 )
    			response = (double)duration/total/1000;
    		else
    			response = 0;

    		fprintf( fp_res , ",%.2f" , response);
    	}

    	fprintf( fp_res , "\n" );
    }

    fprintf(fp_log, "[FINISHED]\n");
    if( fp_res )	fclose(fp_res);
    if( fp_log )	fclose(fp_log);
    redisFree(c);
	exit(0);
err:
    if( fp_res )	fclose(fp_res);
    if( fp_log )	fclose(fp_log);
	redisFree(c);
	exit(1);
}