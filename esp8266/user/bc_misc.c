#include "bc_misc.h"
#include "bc_flash.h"

#ifdef __XTENSA__
#else
#include <openssl/sha.h>    //SHA256
#endif


/**************************************************************************
 * [common]public functions
 **************************************************************************/

void ICACHE_FLASH_ATTR bc_misc_hash256(uint8_t *pHash, const uint8_t *pData, size_t Size)
{
#ifdef __XTENSA__
    const uint8_t *ARRAY[1];
    size_t len[1];
    uint8_t hash1[BC_SZ_HASH256];
    int ret;

    ARRAY[0] = pData;
    len[0] = Size;
    ret = sha256_vector(1, ARRAY, len, hash1);
    if (ret != 0) {
        //TODO:
        DBG_PRINTF("hash1 err\n");
    }
    ARRAY[0] = hash1;
    len[0] = sizeof(hash1);
    ret = sha256_vector(1, ARRAY, len, pHash);
    if (ret != 0) {
        //TODO:
        DBG_PRINTF("hash2 err\n");
    }
#else
    SHA256_CTX ctx;
    uint8_t hash1[BC_SZ_HASH256];

    SHA256_Init(&ctx);
    SHA256_Update(&ctx, pData, Size);
    SHA256_Final(hash1, &ctx);
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, hash1, sizeof(hash1));
    SHA256_Final(pHash, &ctx);
#endif
}


void ICACHE_FLASH_ATTR bc_misec_add_varint(uint8_t **pp, uint16_t Len)
{
    if (Len < 0xfd) {
        bc_misc_add(pp, Len, 1);
    }
    else if (Len < 0xffff) {
        uint32_t len2 = (uint32_t)(Len | 0xfd0000);
        bc_misc_add(pp, len2, 3);
    }
    else {
        //TODO:
        DBG_PRINTF("string too long!\n");
        return;
    }
}


/** varint長取得
 * 
 * @param[in]   pp          受信データ
 * @parent[out] pLen        verint長
 * @return      解析データ長
 * 
 * @note
 *      - 16bitデータまでしか対応しない
 */
int ICACHE_FLASH_ATTR bc_misc_get_varint(const uint8_t *p, int *pLen)
{
    int rlen;

    if (*p < 0xfd) {
        *pLen = *p;
        rlen = 1;
    }
    else if (*p == 0xfd) {
        *pLen = *(p + 1) | (*(p + 2) << 8);
        rlen = 3;
    }
    else {
        DBG_PRINTF("too large varint !\n");
        rlen = 0;
        return -1;
    }

    return rlen;
}


#ifdef __XTENSA__

/**************************************************************************
 * [esp8266]private variables
 **************************************************************************/

static os_timer_t   sRtcUpdate;
static uint32       sEpochTime;         //最後に更新したepoch time
static uint32       sLastSysTime = BC_TIME_INVALID;
static uint32       sDiff;              //秒未満の蓄積

/**************************************************************************
 * [esp8266]prototypes
 **************************************************************************/

static void ICACHE_FLASH_ATTR time_update(void *pArg);


/**************************************************************************
 * [esp8266]public functions
 **************************************************************************/

uint32_t ICACHE_FLASH_ATTR bc_misc_time_get(void)
{
    if (sLastSysTime == BC_TIME_INVALID) {
        return BC_TIME_INVALID;
    }

    uint32 sysTime = system_get_time();
    uint64 diff;

    if (sysTime > sLastSysTime) {
        //値が回っていない
        diff = (uint64)sDiff + (uint64)sysTime - (uint64)sLastSysTime;
    }
    else {
        //値が回った
        diff = (uint64)sDiff + (uint64)sysTime + (uint64)(0 - sLastSysTime);
    }
    uint32 diff_num = (uint32)(diff / 1000000);
    sEpochTime += diff_num;
    sDiff = (uint32)(diff - (uint64)diff_num * (uint64)1000000);
    sLastSysTime = sysTime;

    //DBG_PRINTF("now : %u (%u)\n", sEpochTime, sysTime);

    return sEpochTime;
}


void ICACHE_FLASH_ATTR bc_misc_time_start(uint32_t epoch)
{
    DBG_FUNCNAME();

    sLastSysTime = system_get_time();    //ブートしてからの時間(usec)
    sEpochTime = epoch;

    os_timer_disarm(&sRtcUpdate);                           //タイマ停止
    os_timer_setfn(&sRtcUpdate, time_update, NULL);
    os_timer_arm(&sRtcUpdate, 60000, 1);                    //タイマ開始(1分繰り返し)
}


int ICACHE_FLASH_ATTR bc_misc_powon(const struct bc_flash_tx_t *pProtoTx)
{
    uint8_t buff[8 + 1 + 1 + 5];
    uint32_t now = bc_misc_time_get();

    DBG_FUNCNAME();

    DBG_PRINTF("  * now          : %u\n", now);
    DBG_PRINTF("  * start time   : %u\n", pProtoTx->start_time);
    DBG_PRINTF("  * end time     : %u\n", pProtoTx->end_time);
    DBG_PRINTF("  * use min      : %u\n", pProtoTx->use_min);
    DBG_PRINTF("  * use ch       : %u\n", pProtoTx->use_ch);
    DBG_PRINTF("  * started time : %u\n", pProtoTx->started_time);

    if (now < pProtoTx->started_time) {
        //使用開始が、現在よりも未来
        DBG_PRINTF("start time yet(%u < %u)\n", now, pProtoTx->started_time);
        return -2;
    }

    // 使用トークンの終了時間
    uint32_t end_time;
    if (pProtoTx->started_time + pProtoTx->use_min * 60 <= pProtoTx->end_time) {
        end_time = pProtoTx->started_time + pProtoTx->use_min * 60;
    }
    else {
        end_time = pProtoTx->end_time;
    }

    if (now >= end_time) {
        //既に時間外
        DBG_PRINTF("end time over(%u >= %u)\n", now, pProtoTx->end_time);
        return -1;
    }
    int32_t ontime = end_time - now;       //過ぎた時間を引く
    DBG_PRINTF("[%s()] ontime=%d\n", __func__, ontime);
    if (ontime <= 0) {
        DBG_PRINTF("no ontime\n");
        return -1;
    }
    ontime = (ontime + 59) / 60;        //分変換(切り上げ)
    MEMCPY(buff, "NaYuTaCo", 8);
    buff[8] = (uint8_t)(1 + 5);
    buff[9] = (uint8_t)BC_MBED_CMD_POWON;
    buff[10] = pProtoTx->use_ch;
    buff[11] = (uint8_t)(ontime & 0xff);
    buff[12] = (uint8_t)((ontime >> 8) & 0xff);
    buff[13] = (uint8_t)((ontime >> 16) & 0xff);
    buff[14] = (uint8_t)((ontime >> 24) & 0xff);
    CMD_MBED_SEND(buff, sizeof(buff));      //C:通電

    return 0;
}


/**************************************************************************
 * [esp8266]private functions
 **************************************************************************/

static void ICACHE_FLASH_ATTR time_update(void *pArg)
{
    (void)bc_misc_time_get();
}

#endif  //__XTENSA__
