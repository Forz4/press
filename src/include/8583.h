#ifndef _CUPS8583_H_
#define _CUPS8583_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/*
 DEFINES
 */
#define CUPS_HEADER_LENGTH          46

/*
 TYPES
 */
typedef unsigned char                   BYTE;
typedef struct iso8583_field_def        ISO8583_FILED_DEF_t;
typedef struct cups_header              CUPS_HEADER_t;
typedef struct cups_bitmap              CUPS_BITMAP_t;
typedef struct cups_field               CUPS_FIELD_t;
typedef struct cups_message             CUPS_MESSAGE_t;

/*
 STRUCTURES
 */
struct iso8583_field_def {     
    char        pbytInfo[37];
    int         intMaxLengthInBytes;
    int         intVarFlag;
};
struct cups_header{
    BYTE        pbytHeaderLength[1+1];
    BYTE        pbytHeaderFlagVersion[1+1];
    BYTE        pbytTotalMessageLength[4+1];
    BYTE        pbytDestID[11+1];
    BYTE        pbytSourID[11+1];
    BYTE        pbytReserved[3+1];
    BYTE        pbytBatchNum[1+1];
    BYTE        pbytTransactionInfo[8+1];
    BYTE        pbytUserInfo[1+1];
    BYTE        pbytRejectCode[5+1];
    BYTE        pbytMsgtype[4+1];
};
struct cups_bitmap{
    BYTE        bytIsExtend;
    BYTE        bytRaw[16+1];
    BYTE        pbytFlags[128];
};
struct cups_field{
    int         intDataLength;
    BYTE        *pchData;
};
struct cups_message{
    CUPS_HEADER_t   header;
    CUPS_BITMAP_t   bitmap;
    CUPS_FIELD_t    fields[128];
};

/*
 FUNCTIONS
 */
CUPS_MESSAGE_t *CUPS8583_parseMessage( BYTE *pchBuf , char *pchErrmsg );
void            CUPS8583_freeMessage( CUPS_MESSAGE_t *pmessage );
int             CUPS8583_parseHeader( BYTE *pchBuf , CUPS_HEADER_t *pheader );
int             CUPS8583_parseBitmap( BYTE *pchBuf , CUPS_BITMAP_t *pbitmap );
int             CUPS8583_parseFields( BYTE *pchBuf , CUPS_BITMAP_t *bitmap , CUPS_FIELD_t  *pfields );
CUPS_FIELD_t   *CUPS8583_getField( CUPS_MESSAGE_t *pmessage , int index);
void            CUPS8583_printMessage( CUPS_MESSAGE_t *message);
void            CUPS8583_printHex( BYTE *buf , char *type , int index , int size , char *info);
#endif