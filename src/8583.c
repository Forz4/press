#include "include/8583.h"
#include "include/log.h"
/*
 CONSTANTS
 */
ISO8583_FILED_DEF_t ISO8583_FIELDS_DEF[128] = {
    /* FIELD 1 */  {"Bit Map                             ", 16 , 0 },
    /* FIELD 2 */  {"Primary Account Number (PAN)        ", 19 , 2 },
    /* FIELD 3 */  {"Processing Code                     ", 6  , 0 },
    /* FIELD 4 */  {"Amount, Transaction                 ", 12 , 0 },
    /* FIELD 5 */  {"Amount, Settlement                  ", 12 , 0 },
    /* FIELD 6 */  {"Amount, Cardholder Billing          ", 12 , 0 },
    /* FIELD 7 */  {"Transaction Date/Time               ", 10 , 0 },
    /* FIELD 8 */  {""                                    , 0  , 0 },
    /* FIELD 9 */  {"Conversion Rate, Settlement         ", 8  , 0 },
    /* FIELD 10 */ {"Conversion Rate, Cardholder Billing ", 8  , 0 },
    /* FIELD 11 */ {"System Trace Audit Number           ", 6  , 0 },
    /* FIELD 12 */ {"Time, Local Transaction             ", 6  , 0 },
    /* FIELD 13 */ {"Date, Local Transaction             ", 4  , 0 },
    /* FIELD 14 */ {"Date, Expiration                    ", 4  , 0 },
    /* FIELD 15 */ {"Date, Settlement                    ", 4  , 0 },
    /* FIELD 16 */ {"Date, Conversion                    ", 4  , 0 },
    /* FIELD 17 */ {""                                    , 0  , 0 },
    /* FIELD 18 */ {"Merchant's Type                     ", 4  , 0 },
    /* FIELD 19 */ {"Merchant Country Code               ", 3  , 0 },
    /* FIELD 20 */ {""                                    , 0  , 0 },
    /* FIELD 21 */ {""                                    , 0  , 0 },
    /* FIELD 22 */ {"Point Of Service Entry Mode Code    ", 3  , 0 },
    /* FIELD 23 */ {"Card Sequence Number                ", 3  , 0 },
    /* FIELD 24 */ {""                                    , 0  , 0 },
    /* FIELD 25 */ {"Point Of Service Condition Code     ", 2  , 0 },
    /* FIELD 26 */ {"Point Of Service PIN Capture Code   ", 2  , 0 },
    /* FIELD 27 */ {""                                    , 0  , 0 },
    /* FIELD 28 */ {"Amount,Transaction Fee              ", 9  , 0 },
    /* FIELD 29 */ {""                                    , 9  , 0 },
    /* FIELD 30 */ {""                                    , 9  , 0 },
    /* FIELD 31 */ {""                                    , 9  , 0 },
    /* FIELD 32 */ {"Acquiring Institution Id. Code      ", 11 , 2 },
    /* FIELD 33 */ {"Forwarding Institution Id. Code     ", 11 , 2 },
    /* FIELD 34 */ {""                                    , 0  , 0 },
    /* FIELD 35 */ {"Track 2 Data                        ", 37 , 2 },
    /* FIELD 36 */ {"Track 3 Data                        ", 104, 3 },
    /* FIELD 37 */ {"Retrieval Reference Number          ", 12 , 0 },
    /* FIELD 38 */ {"Authorization Id. Response          ", 6  , 0 },
    /* FIELD 39 */ {"Response Code                       ", 2  , 0 },
    /* FIELD 40 */ {""                                    , 0  , 0 },
    /* FIELD 41 */ {"Card Acceptor Terminal Id           ", 8  , 0 },
    /* FIELD 42 */ {"Card Acceptor Identification Code   ", 15 , 0 },
    /* FIELD 43 */ {"Card Acceptor Name/Location         ", 40 , 0 },
    /* FIELD 44 */ {"Additional Response Data            ", 25 , 2 },
    /* FIELD 45 */ {"Track 1 Data                        ", 76 , 2 },
    /* FIELD 46 */ {""                                    , 0  , 3 },
    /* FIELD 47 */ {""                                    , 0  , 3 },
    /* FIELD 48 */ {"Additional Data Private             ", 512, 3 },
    /* FIELD 49 */ {"Currency Code, Transaction          ", 3  , 0 },
    /* FIELD 50 */ {"Currency Code, Settlement"           , 3  , 0 },
    /* FIELD 51 */ {"Currency Code, Cardholder Billing   ", 3  , 0 },
    /* FIELD 52 */ {"Pin Data                            ", 8  , 0 },
    /* FIELD 53 */ {"Security Related Control Information", 16 , 0 },
    /* FIELD 54 */ {"Addtional Amounts                   ", 40 , 3 },
    /* FIELD 55 */ {"ICC System Related Data             ", 255, 3 },
    /* FIELD 56 */ {"Additional Data                     ", 512, 3 },
    /* FIELD 57 */ {"Additional Data Private             ", 100, 3 },
    /* FIELD 58 */ {""                                    , 0. , 0 },
    /* FIELD 59 */ {"Detail Inquiring                    ", 600, 3 },
    /* FIELD 60 */ {"Reserved                            ", 100, 3 },
    /* FIELD 61 */ {"Cardholder Authentication Info      ", 200, 3 },
    /* FIELD 62 */ {"Switching Data                      ", 200, 3 },
    /* FIELD 63 */ {"Finacial Network Data               ", 512, 3 },
    /* FIELD 64 */ {""                                    , 0  , 0 },
    /* FIELD 65 */ {""                                    , 0  , 0 },
    /* FIELD 66 */ {""                                    , 0  , 0 },
    /* FIELD 67 */ {""                                    , 0  , 0 },
    /* FIELD 68 */ {""                                    , 0  , 0 },
    /* FIELD 69 */ {""                                    , 0  , 0 },
    /* FIELD 70 */ {"Network Management Information Code ", 3  , 0 },
    /* FIELD 71 */ {""                                    , 0  , 0 },
    /* FIELD 72 */ {""                                    , 0  , 0 },
    /* FIELD 73 */ {""                                    , 0  , 0 },
    /* FIELD 74 */ {""                                    , 0  , 0 },
    /* FIELD 75 */ {""                                    , 0  , 0 },
    /* FIELD 76 */ {""                                    , 0  , 0 },
    /* FIELD 77 */ {""                                    , 0  , 0 },
    /* FIELD 78 */ {""                                    , 0  , 0 },
    /* FIELD 79 */ {""                                    , 0  , 0 },
    /* FIELD 80 */ {""                                    , 0  , 0 },
    /* FIELD 81 */ {""                                    , 0  , 0 },
    /* FIELD 82 */ {""                                    , 0  , 0 },
    /* FIELD 83 */ {""                                    , 0  , 0 },
    /* FIELD 84 */ {""                                    , 0  , 0 },
    /* FIELD 85 */ {""                                    , 0  , 0 },
    /* FIELD 86 */ {""                                    , 0  , 0 },
    /* FIELD 87 */ {""                                    , 0  , 0 },
    /* FIELD 88 */ {""                                    , 0  , 0 },
    /* FIELD 89 */ {""                                    , 0  , 0 },
    /* FIELD 90 */ {"Original Data Elements              ", 42 , 0 },
    /* FIELD 91 */ {""                                    , 0  , 0 },
    /* FIELD 92 */ {""                                    , 0  , 0 },
    /* FIELD 93 */ {""                                    , 0  , 0 },
    /* FIELD 94 */ {""                                    , 0  , 0 },
    /* FIELD 95 */ {""                                    , 0  , 0 },
    /* FIELD 96 */ {"Message Security Code               ", 8  , 0 },
    /* FIELD 97 */ {""                                    , 0  , 0 },
    /* FIELD 98 */ {""                                    , 0  , 0 },
    /* FIELD 99 */ {""                                    , 0  , 0 },
    /* FIELD 100*/ {"Receiving Institution Id Code       ", 11 , 2 },
    /* FIELD 101*/ {""                                    , 0  , 0 },
    /* FIELD 102*/ {"Account Identification 1            ", 28 , 2 },
    /* FIELD 103*/ {"Account Identification 2            ", 28 , 2 },
    /* FIELD 104*/ {"Additional Data                     ", 512, 3 },
    /* FIELD 105*/ {""                                    , 0  , 0 },
    /* FIELD 106*/ {""                                    , 0  , 0 },
    /* FIELD 107*/ {""                                    , 0  , 0 },
    /* FIELD 108*/ {""                                    , 0  , 0 },
    /* FIELD 109*/ {""                                    , 0  , 0 },
    /* FIELD 110*/ {""                                    , 0  , 0 },
    /* FIELD 111*/ {""                                    , 0  , 0 },
    /* FIELD 112*/ {""                                    , 0  , 0 },
    /* FIELD 113*/ {"Additional Data                     ", 512, 3 },
    /* FIELD 114*/ {""                                    , 0  , 0 },
    /* FIELD 115*/ {""                                    , 0  , 0 },
    /* FIELD 116*/ {"Additional Data                     ", 512, 3 },
    /* FIELD 117*/ {"Additional Data                     ", 256, 3 },
    /* FIELD 118*/ {""                                    , 0  , 0 },
    /* FIELD 119*/ {""                                    , 0  , 0 },
    /* FIELD 120*/ {""                                    , 0  , 0 },
    /* FIELD 121*/ {"CUPS Reserved                       ", 100, 3 },
    /* FIELD 122*/ {"Acquiring Institution Reserved      ", 100, 3 },
    /* FIELD 123*/ {"Issuer Institution Reserved         ", 100, 3 },
    /* FIELD 124*/ {""                                    , 0  , 0 },
    /* FIELD 125*/ {"Reserved                            ", 256, 3 },
    /* FIELD 126*/ {"Reserved                            ", 256, 3 },
    /* FIELD 127*/ {""                                    , 0  , 0 },
    /* FIELD 128*/ {"Message Authentication Code         ", 8  , 0 }
};
/*
 Parse message from pchBuf , a pointer to CUPS_MESSGE_t is returned if succeeded
 NULL is returned if failed and error message is written to pchErrmsg.
 */
CUPS_MESSAGE_t *CUPS8583_parseMessage( BYTE *pchBuf , char *pchErrmsg )
{
    /* allocate memory */
    CUPS_MESSAGE_t  *pmessage = (CUPS_MESSAGE_t *)malloc(sizeof(CUPS_MESSAGE_t));
    if ( pmessage == NULL ){
        if ( pchErrmsg )    sprintf( pchErrmsg , "malloc fail");
        return NULL;        
    }
    memset( pmessage , 0x00 , sizeof(CUPS_MESSAGE_t));

    int ret = 0;
    /* parse header */
    ret = CUPS8583_parseHeader( pchBuf, &(pmessage->header) );
    if ( ret != CUPS_HEADER_LENGTH && ret != BANK_HEADER_LENGTH ){
        if ( pchErrmsg )    sprintf( pchErrmsg , "parse header fail");
        goto parse_fail;
    }
    pchBuf += ret;

    /* Message Type */
    memcpy( pmessage->pbytMsgtype , pchBuf , 4 );
    pchBuf += 4;

    /* parse bitmap */
    ret = CUPS8583_parseBitmap(pchBuf, &(pmessage->bitmap));
    pchBuf += ret;

    /* parse fields */
    ret = CUPS8583_parseFields(pchBuf , &(pmessage->bitmap) , pmessage->fields);
    if ( ret < 0 ){
        if ( pchErrmsg ) sprintf( pchErrmsg , "parse header fail");
        goto parse_fail;
    }
    
    return pmessage;

parse_fail:

    CUPS8583_freeMessage( pmessage );
    return NULL;
}

/*
 Free message
 */
void CUPS8583_freeMessage( CUPS_MESSAGE_t *pmessage )
{
    for( int i = 1 ; i < (pmessage->bitmap.bytIsExtend == 1 ? 128 : 64) ; i ++){
        if ( pmessage->bitmap.pbytFlags[i] == '1' && pmessage->fields[i].pchData != NULL ){
            free(pmessage->fields[i].pchData);
        }
    }
    free(pmessage);
    return;
}
/* 
 Parse header from pchBuf
 returns total offset if succeeded ,for CUPS header offset should be 50
 returns -1 if failed.
*/
int CUPS8583_parseHeader( BYTE *pchBuf , CUPS_HEADER_t *pheader )
{
    int     offset = 0;

    if ( pheader->pbytHeaderLength[0] == BANK_HEADER_LENGTH ){
        return BANK_HEADER_LENGTH;
    }

    /* Header Legnth */
    memcpy( pheader->pbytHeaderLength , pchBuf+offset , 1);
    if ( pheader->pbytHeaderLength[0] != CUPS_HEADER_LENGTH )   return -1;
    offset += 1;

    /* Header Flag and Version */
    memcpy( pheader->pbytHeaderFlagVersion , pchBuf+offset , 1);
    offset += 1;

    /* Total Message Length */
    memcpy( pheader->pbytTotalMessageLength , pchBuf+offset , 4);
    offset += 4;

    /* Destination ID */
    memcpy( pheader->pbytDestID , pchBuf+offset , 11 );
    offset += 11;

    /* Source ID */
    memcpy( pheader->pbytSourID , pchBuf+offset , 11 );
    offset += 11;    

    /* Reserved */
    memcpy( pheader->pbytReserved , pchBuf+offset , 3 );
    offset += 3;

    /* Batch Number */
    memcpy( pheader->pbytBatchNum , pchBuf+offset , 1);
    offset += 1;

    /* Trasaction Information */
    memcpy( pheader->pbytTransactionInfo , pchBuf+offset , 8 );
    offset += 8;

    /* User Information */
    memcpy( pheader->pbytUserInfo , pchBuf+offset , 1);
    offset += 1;

    /* Reject Code */
    memcpy( pheader->pbytRejectCode , pchBuf+offset , 5 );
    offset += 5;

    return offset;
}
/* 
 Parse bitmap from pchBuf ， returns total offset.
*/
int CUPS8583_parseBitmap( BYTE *pchBuf , CUPS_BITMAP_t *pbitmap )
{
    int offset = 0;
    int i = 0;
    int j = 0;
    /* check extended flag */
    if ( pchBuf[0] & 0x80 ) {
        pbitmap->bytIsExtend = 1;
        offset = 16;
    } else {
        pbitmap->bytIsExtend = 0;
        offset = 8;
    }
    memcpy( pbitmap->bytRaw , pchBuf , offset);
    for ( i = 0 ; i < offset ; i ++ )
        for ( j = 0 ; j < 8 ; j ++ )
            pbitmap->pbytFlags[i*8+j] = (pchBuf[i] & 1<<(7-j) ) == 0 ? '0' : '1';
    return offset;
}

/*
 Parse fields from pchBuf , returns total offset
 */
int CUPS8583_parseFields( BYTE *pchBuf , CUPS_BITMAP_t *bitmap , CUPS_FIELD_t  *pfields )
{
    int  i = 0;
    char temp[4];
    BYTE *start = pchBuf;
    for ( i = 1 ; i < 128 ; i ++ ){
        if ( bitmap->pbytFlags[i] == '1' ){
            /* check if field has variable length */
            if ( ISO8583_FIELDS_DEF[i].intVarFlag != 0 ){
                memset( temp , 0x00 , sizeof(temp));
                memcpy( temp , pchBuf , ISO8583_FIELDS_DEF[i].intVarFlag );
                int varLen = atoi(temp);
                if ( varLen > ISO8583_FIELDS_DEF[i].intMaxLengthInBytes ){
                    return -1;
                } else {
                    pfields[i].intDataLength = varLen;
                    pchBuf += ISO8583_FIELDS_DEF[i].intVarFlag;
                }
            } else {
                pfields[i].intDataLength  = ISO8583_FIELDS_DEF[i].intMaxLengthInBytes;
            }
            pfields[i].pchData = (BYTE *)malloc(pfields[i].intDataLength);
            if( pfields[i].pchData == NULL )    return -1;
            memcpy( pfields[i].pchData , pchBuf , pfields[i].intDataLength);
            pchBuf += pfields[i].intDataLength;
        }
    }
    return pchBuf - start;
}
/*
 Get a field from message , returns NULL if field not found
 index should be [2-128]
 */
CUPS_FIELD_t *CUPS8583_getField( CUPS_MESSAGE_t *pmessage , int index )
{
    if ( index < 2 || index > 128 ) return NULL;
    if ( pmessage->bitmap.pbytFlags[index-1] == '1' ){
        return &(pmessage->fields[index-1]);
    } else {
        return NULL;
    }
}
/*
 Print message
 */
void CUPS8583_printMessage( CUPS_MESSAGE_t *pmessage)
{
    printf("=======================================================================================================================\n");
    printf( "%-4s|%-5s|%-4s|%-37s|%-48s|%-16s\n" , "TYPE" , "INDEX" , "SIZE" , "INFO" , "HEX" , "ASC");
    printf("=======================================================================================================================\n");
    CUPS8583_printHex( pmessage->header.pbytHeaderLength,       "HEAD" , 1  , 1  , "HEADER LENGTH"             );
    CUPS8583_printHex( pmessage->header.pbytHeaderFlagVersion,  "HEAD" , 2  , 1  , "HEADER FLAG VERSION"       );
    CUPS8583_printHex( pmessage->header.pbytTotalMessageLength, "HEAD" , 3  , 4  , "TOTAL MESSAGE LENGTH"      );
    CUPS8583_printHex( pmessage->header.pbytDestID,             "HEAD" , 4  , 11 , "DESTINATION ID"            );
    CUPS8583_printHex( pmessage->header.pbytSourID,             "HEAD" , 5  , 11 , "SOURCE ID"                 );
    CUPS8583_printHex( pmessage->header.pbytReserved,           "HEAD" , 6  , 3  , "RESERVED"                  );
    CUPS8583_printHex( pmessage->header.pbytBatchNum,           "HEAD" , 7  , 1  , "BATCH NUMBER"              );
    CUPS8583_printHex( pmessage->header.pbytTransactionInfo,    "HEAD" , 8  , 8  , "TRASACTION INFORMATION"    );
    CUPS8583_printHex( pmessage->header.pbytUserInfo,           "HEAD" , 9  , 1  , "USER INFORMATION"          );
    CUPS8583_printHex( pmessage->header.pbytRejectCode,         "HEAD" , 10 , 5  , "REJECT CODE"               );
    CUPS8583_printHex( pmessage->pbytMsgtype,                   "TYPE" , 0  , 4  , "MSGTYPE"                   );
    CUPS8583_printHex( pmessage->bitmap.bytRaw,"BITM" , 1 , pmessage->bitmap.bytIsExtend==1 ? 16 : 8 ,"BITMAP" );
    for( int i = 1 ; i < (pmessage->bitmap.bytIsExtend == 1 ? 128 : 64) ; i ++){
        if ( pmessage->bitmap.pbytFlags[i] == '1' ){
            CUPS8583_printHex( pmessage->fields[i].pchData , "BODY" , i+1 , pmessage->fields[i].intDataLength  , ISO8583_FIELDS_DEF[i].pbytInfo);
        }
    }
    printf("=======================================================================================================================\n");
    return;
}
/*
 Print A Field in HEX
*/
void CUPS8583_printHex( BYTE *buf , char *type , int index , int size , char *info)
{
    int   i = 0;
    int   j = 0;
    BYTE ch = 0;
    int   prefix = 0;
    for ( i = 0 ; i <= (size-1)/16 ; i ++){
        if (i == 0){
            prefix = printf( "%4s|%-5d|%-4d|%-37s" , type , index , size , info);

        } else {
            for( int k = 0 ; k < prefix ; k ++) 
                printf(" ");
        }
        printf("|");
        for ( j = 0 ; j+i*16 < size && j < 16 ;j ++){
            ch = (BYTE)buf[j+i*16];
            printf( "%02X " , ch);
        }
        while ( j++ < 16 ){
            printf( "   ");
        }
        printf( "|");
        for ( j = 0 ; j+i*16 < size && j < 16 ; j ++ ){
            int pos = j+i*16;
            if ( (buf[pos] >= 32 && buf[pos] <= 126) ){
                printf( "%c" , buf[pos]);
            } else {
                printf( ".");
            }
        }
        printf( "\n");
   }
   return;
}

