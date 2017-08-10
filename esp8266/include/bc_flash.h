/**
 * @file    bc_flash.h
 * @brief   FLASH管理ヘッダ
 */
/* 以下、BC_FLASH_STARTからの相対FLASHセクタ番号
 *          0=0x200
 *        507=0x3fb
 * 
 *      +-----------------------------------------+
 *    0 | bc_flash_tx_t[4000]                     |
 *      =                                         =
 *  499 |                                         |
 *      +-----------------------------------------+
 *  500 | bc_flash_blk_t                          |
 *  501 | bc_flash_blk_t                          |
 *      +-----------------------------------------+
 *  502 | 退避セクタ                              |
 *  503 | 退避セクタ番号                          |
 *      +-----------------------------------------+
 *      | 空き                                    |
 *      |                                         |
 *      +-----------------------------------------+
 *  507 | bc_wallet_t                             |
 *      +-----------------------------------------+
 */
#ifndef BC_FLASH_H__
#define BC_FLASH_H__

#include "bc_misc.h"
#include "bc_proto.h"


/**************************************************************************
 * macros
 **************************************************************************/

#define BC_FLASH_START          (0x200)             ///< ユーザ用FLASH開始セクタ番号
#define BC_FLASH_END            (0x3fb)             ///< ユーザ用FLASH終了セクタ番号
#define BC_FLASH_WALLET_SZ32    (14)                ///< 4byte alignでメモリ確保する時のサイズ

#define BC_FLASH_WRT_IGNORE     (1)                 ///< FLASH書込み未実施
#define BC_FLASH_WRT_DONE       (0)                 ///< FLASH書込み正常
#define BC_FLASH_WRT_FAIL       (-1)                ///< FLASH書込み失敗

#define BC_FLASH_TYPE_TXA       (0)                 ///< TX(a)
#define BC_FLASH_TYPE_TXB       (1)                 ///< TX(b)
#define BC_FLASH_TYPE_FLASH     (2)                 ///< FLASH


/**************************************************************************
 * types
 **************************************************************************/

#pragma pack(1)

/** @struct bc_flash_tx_t
 * 
 * 所有者→使用者情報
 * 
 * @attention
 *      - 実装簡略のため、構造体サイズを128byte alignに調整すること
 */
struct bc_flash_tx_t {
    //TX(a)
    uint8_t     txa_hash[BC_SZ_HASH256];        ///< TX(a)のhash
    uint32_t    start_time;                     ///< 利用可能期間開始(epoch time)
    uint32_t    end_time;                       ///< 利用可能期間終了(epoch time)
    uint32_t    use_min;                        ///< 利用時間(分)
    uint8_t     use_ch;                         ///< 利用CH
    //TX(b)
    uint8_t     txb_hash[BC_SZ_HASH256];        ///< TX(b)のhash
    uint32_t    started_time;                   ///< 利用開始時間(epoch time)
    //FLASH alignment
    uint8_t     reserved[47];                   ///< padding(4KBアラインメント用)
};


/** @struct bc_flash_blk_t
 * 
 * @attention
 *      - 実装簡略のため、構造体サイズを4byte alignに調整すること
 */
struct bc_flash_blk_t {
    uint8_t     bhash[BC_SZ_HASH256];           ///< 最後に受信したblock hash
    uint32_t    update_time;                    ///< 更新時間(epoch time)
};


/** @struct bc_flash_wlt_t
 * 
 * @attention
 *      - 実装簡略のため、構造体サイズを4byte alignに調整すること
 */
struct bc_flash_wlt_t {
    uint8_t     bcaddr[BC_SZ_HASH160];          ///< プラグBitcoinアドレス(HASH160)
    uint8_t     pubkey[BC_SZ_PUBKEY];           ///< 所有者公開鍵
    uint8_t     reserved[3];                    ///< padding(4byteアラインメント用)
};

#pragma pack()


/**************************************************************************
 * prototypes
 **************************************************************************/

/** Bitcoinアドレスの保存
 * 
 * @param[in]   pData           Bitcoinアドレス[20], 公開鍵[33]
 * @retval      BC_FLASH_WRT_IGNORE     同値のため保存せず終了
 * @retval      BC_FLASH_WRT_DONE       値保存して終了
 * @retval      BC_FLASH_WRT_FAIL       エラー
 * @note
 *      - pDataはFLASH APIにあわせてuint32_t型で確保すること
 */
int ICACHE_FLASH_ATTR bc_flash_save_bcaddr(const uint32_t *pData);


/** Bitcoinアドレスの保存
 * 
 * @param[out]  pAddr       Bitcoinアドレス[20], 公開鍵[33]
 */
void ICACHE_FLASH_ATTR bc_flash_get_bcaddr(struct bc_flash_wlt_t *pAddr);


/** @brief  TX情報消去
 * 
 * Bitcoinアドレスと公開鍵以外消去
 */
void ICACHE_FLASH_ATTR bc_flash_erase_txinfo(void);


/** @brief  TX情報更新
 *
 * @param[in]   Type        更新対象
 * @param[in]   pProtoTx    TX情報
 */
void ICACHE_FLASH_ATTR bc_flash_update_txinfo(uint8_t Type, const struct bc_proto_tx *pProtoTx);


/** @brief  最後に取得したBlock Hash更新
 * 
 * @param[in]   pHash       保存するBlock Hash
 */
void ICACHE_FLASH_ATTR bc_flash_save_last_bhash(const uint8_t *pHash);


/** @brief  最後に取得したBlock Hash取得
 * 
 * @param[out]  pHash       [戻り値]Block Hash
 */
void ICACHE_FLASH_ATTR bc_flash_get_last_bhash(uint8_t *pHash);


/** @brief  有効なBlock Hash消去
 * 
 * @retval  0       消去未実施(Block Hashが初期値)
 * @retval  -1      消去実施
 */
int ICACHE_FLASH_ATTR bc_flash_erase_last_bhash(void);


#endif /* BC_FLASH_H__ */
