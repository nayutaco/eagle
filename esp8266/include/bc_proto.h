/**************************************************************************
 * @file    bc_proto.c
 * @brief   Bitcoinパケット制御
 **************************************************************************/
#ifndef BC_PROTO_H__
#define BC_PROTO_H__

#include "bc_misc.h"


/**************************************************************************
 * macros
 **************************************************************************/

#define BC_PROTO_FIN    (0)         ///< パケット解析完了
#define BC_PROTO_CONT   (-1)        ///< パケット解析継続


/**************************************************************************
 * types
 **************************************************************************/

/** @struct bc_proto_tx
 * 
 * 受信したtxのうち、FLASH保存に必要なデータを集約する
 * ポインタは、受信バッファ内を指しているので、壊さないこと
 */
struct bc_proto_tx {
    const uint8_t       *pTx;                   ///< TX全体
    const uint8_t       *pPrevOutput;           ///< prev_output
    const uint8_t       *pOpReturn;             ///< [TxOut]output2のpk_script(length+script)
    uint16_t            Len;                    ///< pTx長
};


/**************************************************************************
 * prototypes
 **************************************************************************/

/** 開始
 * 
 * @param[in]   pConn       管理データ
 * @return      開始結果
 */
int ICACHE_FLASH_ATTR bc_start(struct espconn *pConn);


/** 受信データ処理
 * 
 * @param[in]       pConn       管理データ
 * @param[in]       pBuffer     受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 */
void ICACHE_FLASH_ATTR bc_read_message(struct espconn *pConn, const uint8_t *pBuffer, int *pLen);


/** 送信完了処理
 * 
 * @param[in]   pConn       管理データ
 * @param[in]   sent_length 送信済みサイズ
 */
void ICACHE_FLASH_ATTR bc_sent(struct espconn *pConn, uint16_t sent_length);


/** 終了処理
 * 再起動前に呼ばれることを想定
 * 
 */
void ICACHE_FLASH_ATTR bc_finish(void);

#endif /* BC_PROTO_H__ */
