#ifndef BC_MISC_H__
#define BC_MISC_H__

#ifdef __XTENSA__


/**************************************************************************
 * includes
 **************************************************************************/

#include "c_types.h"
#include "osapi.h"
#include "ets_sys.h"
#include "esp8266_headers.h"
#include "mem.h"
#include "driver/uart.h"


/**************************************************************************
 * [esp8266]macros
 **************************************************************************/

#define CALLOC      os_calloc
#define REALLOC     os_realloc
#define MALLOC      os_malloc
#define FREE        os_free
#define MEMCPY      os_memcpy
#define MEMSET      os_memset
#define MEMCMP      os_memcmp
#define MEMMOVE     os_memmove
#define BZERO       os_bzero
#define STRCPY      os_strcpy
#define STRCMP      os_strcmp
#define STRLEN      os_strlen
#define DBG_PRINTF(...)     os_printf(__VA_ARGS__)
#define CMD_MBED_SEND(b,l)  uart0_tx_buffer((uint8 *)b,(uint16)l)

//0:lowest 1:middle 2:high
#define TASK_PRIOR_UART     USER_TASK_PRIO_1
#define TASK_PRIOR_MAIN     USER_TASK_PRIO_0


/**************************************************************************
 * [esp8266]types
 **************************************************************************/

/** @enum taskreq_t
 * 
 * system_os_post()での処理要求
 */
enum taskreq_t {
    TASK_REQ_WIFI_CONNECT,      ///< WiFi接続要求
    TASK_REQ_DNS_RESOLVE,       ///< 名前解決要求
    TASK_REQ_TCP_CONNECT,       ///< TCP接続要求
    TASK_REQ_MBED_HANDSHAKE,    ///< mbed通信開始要求
    TASK_REQ_MBED_ACK,          ///< mbedからの通信開始応答
    TASK_REQ_BC_START,          ///< Bitcoin処理開始要求
    TASK_REQ_TCP_RECONNECT,     ///< TCP再接続要求
    TASK_REQ_NTP,               ///< NTP要求
    TASK_REQ_REBOOT,            ///< 再起動要求
    TASK_REQ_DATA_ERASE,        ///< データ消去要求(BcAddrと公開鍵は残す)
    TASK_REQ_FLASH_ERASE,       ///< FLASH消去要求
    TASK_REQ_IGNORE             ///< 何もしない
};

struct bc_flash_tx_t;


/**************************************************************************
 * [esp8266]prototypes
 **************************************************************************/

/** 時刻更新開始
 * 
 * @param[in]   epoch   更新開始時間(epoch time)
 */
void ICACHE_FLASH_ATTR bc_misc_time_start(uint32_t epoch);

/** 現在時刻取得
 * 
 * @return      現在時刻(epoch time)
 */
uint32_t ICACHE_FLASH_ATTR bc_misc_time_get(void);

/** 通電開始要求
 * 
 * @param[in]   pProtoTx    通電情報
 * @retval      0           通電開始
 * @retval      -1          通電未実施(過去)
 * @retval      -2          通電未実施(未来)
 */
int ICACHE_FLASH_ATTR bc_misc_powon(const struct bc_flash_tx_t *pProtoTx);


#else   //__XTENSA__

/**************************************************************************
 * [linux]includes
 **************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <unistd.h>


/**************************************************************************
 * [linux]macros
 **************************************************************************/

#define CALLOC      calloc
#define REALLOC     realloc
#define MALLOC      malloc
//#define MALLOC(s)           { printf("malloc[%s() %d]\n", __func__, __LINE__); malloc(s); }
#define FREE        free
#define MEMCPY      memcpy
//#define MEMCPY(d,s,n)       { printf("[mcp:%d]%p,%d\n", __LINE__, d, n);   memcpy(d,s,n); }
#define MEMSET      memset
//#define MEMSET(a,v,n)       { printf("[mst:%d]%p,%d\n", __LINE__, a, n); memset(a,v,n); }
#define MEMCMP      memcmp
#define MEMMOVE     memmove
#define BZERO       bzero
#define STRCPY      strcpy
#define STRCMP      strcmp
#define STRLEN      strlen
#define DBG_PRINTF  printf

#define CMD_MBED_SEND(b,l)  //none

/**************************************************************************
 * [linux]types
 **************************************************************************/

typedef int64_t     sint64_t;
typedef int         err_t;
#define ICACHE_FLASH_ATTR
struct espconn {
    int socket;
} mConn;


/**************************************************************************
 * [linux]prototypes
 **************************************************************************/

int espconn_send(struct espconn *pConn, uint8_t *psent, uint16_t length);
void system_soft_wdt_feed(void);

#endif  //__XTENSA__


/**************************************************************************
 * [common]macros
 **************************************************************************/

#define BC_SZ_HASH256       (32)            ///< HASH256サイズ
#define BC_SZ_HASH160       (20)            ///< HASH160サイズ
#define BC_SZ_PUBKEY        (33)            ///< 公開鍵サイズ
#define BC_TIME_INVALID     ((uint32)-1)    ///< #bc_misc_time_get()の時間が有効では無い

#define BC_MBED_CMD_STARTED         "NaYuTaCo" "\x01" "A"   ///< 起動完了
#define BC_MBED_CMD_STARTED_LEN     (8 + 1 + 1)
#define BC_MBED_CMD_PREPARED        "NaYuTaCo" "\x01" "B"   ///< 準備完了
#define BC_MBED_CMD_PREPARED_LEN    (8 + 1 + 1)
#define BC_MBED_CMD_POWON           'C'                     ///< 通電要求
#define BC_MBED_CMD_REBOOT          "NaYuTaCo" "\x01" "R"   ///< 再起動要求
#define BC_MBED_CMD_REBOOT_LEN      (8 + 1 + 1)

#define DBG_FUNCNAME()      DBG_PRINTF("[[ %s ]]\n", __func__)
#define ARRAY_SIZE(a)       (sizeof(a) / sizeof(a[0]))
#define GET_BE16(p)         ((uint16_t)((p[0] << 8) | p[1]))
#define GET_BE32(p)         ((uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]))
#define HALT()              while (1) { system_soft_wdt_feed(); }


/**************************************************************************
 * [common]prototypes
 **************************************************************************/

/** HASH256(HASH256(data))の取得
 * 
 * @param[out]      pHash       計算結果(32byte)
 * @param[in]       pData       計算元データ
 * @param[in]       Size        データサイズ
 */
void ICACHE_FLASH_ATTR bc_misc_hash256(uint8_t *pHash, const uint8_t *pData, size_t Size);


/** データ設定(1byte～8byteの整数)
 * 
 * @param[in,out]   pp      設定先バッファ
 * @param[in]       val     設定値
 * @param[in]       sz      設定値サイズ
 * 
 * @note
 *      - ポインタを進める
 */
static inline void bc_misc_add(uint8_t **pp, uint64_t val, size_t sz)
{
    MEMCPY(*pp, &val, sz);
    *pp += sz;
}


/** データ設定(ポインタ)
 * 
 * @param[in,out]   pp      設定先バッファ
 * @param[in]       pval    設定値
 * @param[in]       sz      設定値サイズ
 * 
 * @note
 *      - ポインタを進める
 */
//inline void ICACHE_FLASH_ATTR bc_misc_addp(uint8_t **pp, const void *pval, size_t sz)
//{
//    MEMCPY(*pp, pval, sz);
//    *pp += sz;
//}


/** varint長取得
 * 
 * @param[in]   p           受信データ
 * @param[out]  pLen        verint長
 * @return      解析データ長
 * 
 * @note
 *      - 16bitデータまでしか対応しない
 */
int ICACHE_FLASH_ATTR bc_misc_get_varint(const uint8_t *p, int *pLen);


#endif /* BC_MISC_H__ */
