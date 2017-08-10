/**
 * @file    bc_flash.c
 * @brief   FLASH管理
 */

#include "bc_flash.h"
#include "bc_proto.h"


/**************************************************************************
 * macros
 **************************************************************************/

#define SEC_TX_START    (BC_FLASH_START + 0)
#define SEC_TX_END      (BC_FLASH_START + 499)
#define SEC_BLOCK1      (BC_FLASH_START + 500)
#define SEC_BLOCK2      (BC_FLASH_START + 501)
#define SEC_RESTORE     (BC_FLASH_START + 502)
#define SEC_RESTORE_NUM (BC_FLASH_START + 503)
#define SEC_WALLET      (BC_FLASH_START + 507)

#define M_FLASH_EMPTY8   ((uint8_t)0xff)
#define M_FLASH_EMPTY16  ((uint8_t)0xffff)
#define M_FLASH_EMPTY32  ((uint32_t)0xffffffff)

#define M_FLASH_OPECHK(fret) \
    if (fret != SPI_FLASH_RESULT_OK) {                          \
        DBG_PRINTF("flash fail[%d] : %d\n", __LINE__, fret);    \
        HALT();                                                 \
    }

/**************************************************************************
 * types
 **************************************************************************/

/** @struct txpos_t
 * 
 * 
 */
struct txpos_t {
    uint16_t                sec;            ///< セクタ
    int8_t                  pos;            ///< bc_flash_tx_t要素番号
    uint8_t                 edit;           ///< 0:p_txの更新無し  1:p_txの更新あり
    struct bc_flash_tx_t    *p_tx;          ///< 検索データ
    const uint8_t           *p_hash;        ///< 検索するTX(a) hash
    uint8_t                 type;           ///< 検索方法
};


/**************************************************************************
 * const variables
 **************************************************************************/

//エンディアンを逆順にし忘れないように注意！
//Height : 685351
//000000000079667b6264c468a4f8b12549815994583be1398d4682d9c3f83535
const uint8_t kBlockHashStart[] __attribute__ ((aligned (4))) = {
    0x35, 0x35, 0xf8, 0xc3, 0xd9, 0x82, 0x46, 0x8d, 
    0x39, 0xe1, 0x3b, 0x58, 0x94, 0x59, 0x81, 0x49, 
    0x25, 0xb1, 0xf8, 0xa4, 0x68, 0xc4, 0x64, 0x62, 
    0x7b, 0x66, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 
};


/**************************************************************************
 * prototypes
 **************************************************************************/

#ifdef __XTENSA__
static int ICACHE_FLASH_ATTR search_txinfo(struct txpos_t *pPos, uint32_t timestamp);
static void ICACHE_FLASH_ATTR show_bcaddr(const struct bc_flash_wlt_t *pAddr);
#endif  //__XTENSA__


/**************************************************************************
 * public functions
 **************************************************************************/

int ICACHE_FLASH_ATTR bc_flash_save_bcaddr(const uint32_t *pData)
{
#ifdef __XTENSA__
    DBG_PRINTF("Input: ");
    show_bcaddr((const struct bc_flash_wlt_t *)pData);
    DBG_PRINTF("\n--------------------------\n");

    int ret;
    struct bc_flash_wlt_t wlt;

    bc_flash_get_bcaddr(&wlt);
    int cmp = memcmp(pData, &wlt, BC_SZ_HASH160 + BC_SZ_PUBKEY);
    if (cmp == 0) {
        DBG_PRINTF("same data.\nnot saved.\n");
        return BC_FLASH_WRT_IGNORE;
    }

    //書込み
    spi_flash_erase_sector(SEC_WALLET);
    SpiFlashOpResult fret = spi_flash_write(
            (uint32)(SPI_FLASH_SEC_SIZE * SEC_WALLET),
            (uint32 *)pData,
            (uint32)sizeof(struct bc_flash_wlt_t));
    M_FLASH_OPECHK(fret);

    //ok
    DBG_PRINTF("bcaddr save.\n");
    ret = BC_FLASH_WRT_DONE;

    //アドレス情報が変化した場合、保持している情報をクリアする
    bc_flash_erase_txinfo();

    return ret;

#else   //__XTENSA__
    return BC_FLASH_WRT_IGNORE;
#endif  //__XTENSA__
}


void ICACHE_FLASH_ATTR bc_flash_get_bcaddr(struct bc_flash_wlt_t *pAddr)
{
#ifdef __XTENSA__
    uint32 buff[BC_FLASH_WALLET_SZ32];

    MEMSET(buff, 0x00, sizeof(buff));
    SpiFlashOpResult fret = spi_flash_read(
            (uint32)(SPI_FLASH_SEC_SIZE * SEC_WALLET),
            buff,
            (uint32)sizeof(buff));
    M_FLASH_OPECHK(fret);

    //ok
    memcpy(pAddr, buff, sizeof(buff));

    //show_bcaddr(pAddr);
#else   //__XTENSA__
    //Bitconアドレス : mneKtaZTBsYhTZRxx9qV2z5XvZ82Z5Jmzx
    //公開鍵
    const uint8_t kPubKey[] __attribute__ ((aligned (4))) = {
        //036a2342adcb976377fad49e417e12e35cd0878ba1ce9128ec2c650007aa84d00f
        0x03, 0x6a, 0x23, 0x42, 0xad, 0xcb, 0x97, 0x63, 
        0x77, 0xfa, 0xd4, 0x9e, 0x41, 0x7e, 0x12, 0xe3, 
        0x5c, 0xd0, 0x87, 0x8b, 0xa1, 0xce, 0x91, 0x28, 
        0xec, 0x2c, 0x65, 0x00, 0x07, 0xaa, 0x84, 0xd0, 
        0x0f, 
    };
    //Bitconアドレス : mpgVrHz442iqi3KzmP4omoMz1Y8VJTvnce
    //公開鍵ハッシュ
    const uint8_t kPubKeyHash[] __attribute__ ((aligned (4))) = {
        //6487379b2fa67bd14064eae192f8aa7008edb07d
        0x64, 0x87, 0x37, 0x9B, 0x2F, 0xA6, 0x7B, 0xD1,
        0x40, 0x64, 0xEA, 0xE1, 0x92, 0xF8, 0xAA, 0x70,
        0x08, 0xED, 0xB0, 0x7D
    };

    MEMCPY(pAddr->bcaddr, kPubKeyHash, sizeof(kPubKeyHash));
    MEMCPY(pAddr->pubkey, kPubKey, sizeof(kPubKey));
#endif  //__XTENSA__
}


void ICACHE_FLASH_ATTR bc_flash_erase_txinfo(void)
{
#ifdef __XTENSA__
    DBG_FUNCNAME();

    for (int sec = SEC_TX_START; sec <= SEC_BLOCK2; sec++) {
        //DBG_PRINTF("erase %d\n", sec);
        spi_flash_erase_sector(sec);
    }
    DBG_PRINTF("%s() done.\n", __func__);

#if 0
    //消去確認
    for (int sec = SEC_TX_START; sec <= SEC_BLOCK2; sec++) {
        uint32 tmp[128 / sizeof(uint32)];      //128byte
        const uint8_t *ptmp = (const uint8_t *)tmp;
        for (int addr = 0; addr < SPI_FLASH_SEC_SIZE / sizeof(tmp); addr++) {
            SpiFlashOpResult fret = spi_flash_read(
                    (uint32)(SPI_FLASH_SEC_SIZE * sec + sizeof(tmp) * addr),
                    tmp,
                    (uint32)sizeof(tmp));
            M_FLASH_OPECHK(fret);

            for (int lp = 0; lp < sizeof(tmp); lp++) {
                if (ptmp[lp] != M_FLASH_EMPTY8) {
                    DBG_PRINTF("&");
                }
            }
            system_soft_wdt_feed();
        }
    }
#endif

#endif  //__XTENSA__
}


void ICACHE_FLASH_ATTR bc_flash_update_txinfo(uint8_t Type, const struct bc_proto_tx *pProtoTx)
{
#if __XTENSA__
    SpiFlashOpResult fret;
    struct txpos_t txpos;
    struct txpos_t freepos = { .sec = M_FLASH_EMPTY16, .pos = -1 };
    uint8_t hash[BC_SZ_HASH256];

//    DBG_PRINTF("max heap size=%u\n", system_get_free_heap_size());
//    if (system_get_free_heap_size() < SPI_FLASH_SEC_SIZE) {
//        M_FLASH_OPECHK(-1);
//    }
    uint32 *p_buff = (uint32 *)MALLOC(SPI_FLASH_SEC_SIZE);
//    DBG_PRINTF("max heap size=%u\n", system_get_free_heap_size());

    DBG_FUNCNAME();

    //現在時刻は最初に保持する(処理に時間がかかるため)
    uint32_t timestamp = bc_misc_time_get();

    switch (Type) {
    case BC_FLASH_TYPE_TXA:
        //inputのHASH256
        DBG_PRINTF("  TX(a)\n");
        bc_misc_hash256(hash, pProtoTx->pTx, pProtoTx->Len);
        break;
    case BC_FLASH_TYPE_TXB:
        //prev_output
        DBG_PRINTF("  TX(b)\n");
        MEMCPY(hash, pProtoTx->pPrevOutput, BC_SZ_HASH256);
        break;
    case BC_FLASH_TYPE_FLASH:
        DBG_PRINTF("  FLASH\n");
        MEMSET(hash, M_FLASH_EMPTY8, sizeof(hash));
        break;
    }
    DBG_PRINTF("  search hash(%u): ", timestamp);
    for (int i = 0; i < BC_SZ_HASH256; i++) {
        DBG_PRINTF("%02x", hash[BC_SZ_HASH256 - i - 1]);
    }
    DBG_PRINTF("\n");

    int sret;
    int sec;
    for (sec = SEC_TX_START; sec <= SEC_TX_END; sec++) {
        //DBG_PRINTF("[%s()]sec=%d\n", __func__, sec);

        //1セクタ分読込む
        fret = spi_flash_read(
                (uint32)(SPI_FLASH_SEC_SIZE * sec),
                p_buff,
                (uint32)SPI_FLASH_SEC_SIZE);
        M_FLASH_OPECHK(fret);

        txpos.edit = 0;
        txpos.pos = 0;      //先頭から
        do {
            txpos.sec = sec;
            txpos.p_tx = (struct bc_flash_tx_t *)p_buff;
            txpos.p_hash = hash;
            txpos.type = Type;
            sret = search_txinfo(&txpos, timestamp);

            if (sret == 0) {
                if (Type == BC_FLASH_TYPE_TXA) {
                    //TX(a)が見つかった→これ以上検索する必要なし
                    DBG_PRINTF("[%s()] find TX(a) sec=%d, pos=%d\n", __func__, txpos.sec, txpos.pos);
                    sret = -2;
                    sec = SEC_TX_END + 1;
                    break;
                }
                else {
                    //TX(a)が見つかった→TX(b)
                    DBG_PRINTF(" [TX(b)]\n");
                    if ((Type == BC_FLASH_TYPE_TXB) && (txpos.p_tx[txpos.pos].started_time == M_FLASH_EMPTY32)) {
                        //TX(b)未保存 --> TX(b)保存
                        /*
                         * OP_RETURN解析:TX(b)
                         * 
                         * +----------------------------+
                         * | Len(1)                     |
                         * +----------------------------+
                         * | Started Time(4)            |
                         * +----------------------------+
                         */
                        const uint8_t *p = pProtoTx->pOpReturn;
                        if (*p == 4) {
                            p++;
                            uint32_t started_time = GET_BE32(p);

                            //使用トークンの有効性
                            if (started_time < txpos.p_tx[txpos.pos].start_time) {
                                DBG_PRINTF("  started < start\n");
                            }
                            else if (started_time > txpos.p_tx[txpos.pos].end_time) {
                                DBG_PRINTF("  started > end\n");
                            }
                            else {
                                //check ok
                                txpos.edit = 1;
                                txpos.p_tx[txpos.pos].started_time = started_time;
                                bc_misc_hash256(txpos.p_tx[txpos.pos].txb_hash, pProtoTx->pTx, pProtoTx->Len);

                                DBG_PRINTF("  * [%s()] add TX(b) sec=%d, pos=%d\n", __func__, txpos.sec, txpos.pos);
                                DBG_PRINTF("  * hash: ");
                                for (int i = 0; i < BC_SZ_HASH256; i++) {
                                    DBG_PRINTF("%02x", txpos.p_tx[txpos.pos].txb_hash[BC_SZ_HASH256 - i -1]);
                                }
                                DBG_PRINTF("\n");
                            }
                        }
                        else {
                            DBG_PRINTF("  invalid size=%d\n", *p);
                        }
                    }
                    DBG_PRINTF("  * started_time: %u\n", txpos.p_tx[txpos.pos].started_time);
                    
                    //通電開始
                    DBG_PRINTF("Power On!\n");
                    bc_misc_powon(&txpos.p_tx[txpos.pos]);

                    //次の場所から再開
                    txpos.pos++;
                }
            }
            else if (sret == 1) {
                //空きだけ見つかった
                if (freepos.sec == M_FLASH_EMPTY16) {
                    //初回だけ更新
                    freepos.sec = txpos.sec;
                    freepos.pos = txpos.pos;
                    DBG_PRINTF("[%s()]empty sec=%d, pos=%d\n", __func__, freepos.sec, freepos.pos);
                }
            }
            else {
                //検索失敗 --> 継続
            }
        } while (sret == 0);

        //更新があれば書き換える
        if (txpos.edit != 0) {
            //FLASH更新
            DBG_PRINTF("[%s()] update TX sec=%d\n", __func__, sec);
            spi_flash_erase_sector(sec);
            fret = spi_flash_write(
                    (uint32)(SPI_FLASH_SEC_SIZE * sec),
                    p_buff,
                    (uint32)SPI_FLASH_SEC_SIZE);
            M_FLASH_OPECHK(fret);
        }
    }

    if ((Type == BC_FLASH_TYPE_TXA) && (sret != -2)) {
        //TX(a)検索で一致無し
        if (freepos.sec != M_FLASH_EMPTY16) {
            //空きあり
            DBG_PRINTF("[%s()] add TX(a) sec=%d, pos=%d\n", __func__, freepos.sec, freepos.pos);

            //空きのあるセクタを読む
            fret = spi_flash_read(
                    (uint32)(SPI_FLASH_SEC_SIZE * freepos.sec),
                    p_buff,
                    (uint32)SPI_FLASH_SEC_SIZE);
            M_FLASH_OPECHK(fret);

            //空きにTX(a)情報を詰める
            freepos.p_tx = (struct bc_flash_tx_t *)p_buff;
            MEMSET(&freepos.p_tx[freepos.pos], 0xff, sizeof(struct bc_flash_tx_t));
            MEMCPY(freepos.p_tx[freepos.pos].txa_hash, hash, BC_SZ_HASH256);
            /*
             * OP_RETURN解析:TX(a)
             * 
             * +----------------------------+
             * | Len(1)                     |
             * +----------------------------+
             * | Start Time(4)              |
             * +----------------------------+
             * | End Time(4)                |
             * +----------------------------+
             * | Use Minute(2)              |
             * +----------------------------+
             * | Use Ch(1)                  |
             * +----------------------------+
             */
            const uint8_t *p = pProtoTx->pOpReturn;
            if (*p == 11) {
                p++;
                freepos.p_tx[freepos.pos].start_time = GET_BE32(p);
                p += 4;
                freepos.p_tx[freepos.pos].end_time = GET_BE32(p);
                p += 4;
                freepos.p_tx[freepos.pos].use_min = GET_BE16(p);
                p += 2;
                freepos.p_tx[freepos.pos].use_ch = *p;

                DBG_PRINTF(" [TX(a)]\n");
                DBG_PRINTF("  * txa_hash : ");
                for (int i = 0; i < BC_SZ_HASH256; i++) {
                    DBG_PRINTF("%02x", freepos.p_tx[freepos.pos].txa_hash[BC_SZ_HASH256 - i -1]);
                }
                DBG_PRINTF("\n");
                DBG_PRINTF("  * start_time : %u\n", freepos.p_tx[freepos.pos].start_time);
                DBG_PRINTF("  * end_time : %u\n", freepos.p_tx[freepos.pos].end_time);
                DBG_PRINTF("  * use_min : %u\n", freepos.p_tx[freepos.pos].use_min);
                DBG_PRINTF("  * use_ch : %u\n", freepos.p_tx[freepos.pos].use_ch);

                if (freepos.p_tx[freepos.pos].start_time >= freepos.p_tx[freepos.pos].end_time) {
                    DBG_PRINTF("  start >= end\n");
                }
                else if (freepos.p_tx[freepos.pos].end_time <= timestamp) {
                    DBG_PRINTF("  end <= now\n");
                }
                else {
                    //check ok
                    //FLASH更新 : 権利トークン TX(a)
                    spi_flash_erase_sector(freepos.sec);
                    fret = spi_flash_write(
                            (uint32)(SPI_FLASH_SEC_SIZE * freepos.sec),
                            p_buff,
                            (uint32)SPI_FLASH_SEC_SIZE);
                    M_FLASH_OPECHK(fret);
                }
            }
            else {
                DBG_PRINTF("  invalid size=%d\n", *p);
            }
        }
        else {
            //空きなし
            DBG_PRINTF("[%s()] no empty\n", __func__);
        }
    }

    FREE(p_buff);

    DBG_PRINTF("%s() end\n", __func__);
#endif  //__XTENSA__
}


void ICACHE_FLASH_ATTR bc_flash_save_last_bhash(const uint8_t *pHash)
{
#ifdef __XTENSA__
    SpiFlashOpResult fret;

    uint32 blk1[sizeof(struct bc_flash_blk_t) / sizeof(uint32)];
    uint32 blk2[sizeof(struct bc_flash_blk_t) / sizeof(uint32)];

    fret = spi_flash_read(
            (uint32)(SPI_FLASH_SEC_SIZE * SEC_BLOCK1),
            blk1,
            (uint32)sizeof(blk1));
    M_FLASH_OPECHK(fret);

    fret = spi_flash_read(
            (uint32)(SPI_FLASH_SEC_SIZE * SEC_BLOCK2),
            blk2,
            (uint32)sizeof(blk2));
    M_FLASH_OPECHK(fret);

    int sec;
    struct bc_flash_blk_t *p1 = (struct bc_flash_blk_t *)blk1;
    struct bc_flash_blk_t *p2 = (struct bc_flash_blk_t *)blk2;
    DBG_PRINTF("(blk1:%u, blk2:%u)\n", p1->update_time, p2->update_time);
    if (p1->update_time == M_FLASH_EMPTY32) {
        //blk1更新
        DBG_PRINTF("[%s()] new blk1\n", __func__);
        sec = SEC_BLOCK1;
    }
    else if (p2->update_time == M_FLASH_EMPTY32) {
        //blk2更新
        DBG_PRINTF("[%s()] new blk2\n", __func__);
        sec = SEC_BLOCK2;
    }
    else {
        if (p1->update_time <= p2->update_time) {
            //blk1の方が古い
            DBG_PRINTF("[%s()] update blk1\n", __func__);
            sec = SEC_BLOCK1;
        }
        else {
            DBG_PRINTF("[%s()] update blk2\n", __func__);
            sec = SEC_BLOCK2;
        }
    }

    if (MEMCMP(p1->bhash, pHash, BC_SZ_HASH256) != 0) {
        //不一致の場合
        MEMCPY(p1->bhash, pHash, BC_SZ_HASH256);
        p1->update_time = bc_misc_time_get();

        spi_flash_erase_sector(sec);
        fret = spi_flash_write(
                (uint32)(SPI_FLASH_SEC_SIZE * sec),
                blk1,
                (uint32)sizeof(blk1));
        M_FLASH_OPECHK(fret);


        DBG_PRINTF("saved block hash[%u]: ", p1->update_time);
        for (int i = 0; i < BC_SZ_HASH256; i++) {
            DBG_PRINTF("%02x", p1->bhash[BC_SZ_HASH256 - i - 1]);
        }
        DBG_PRINTF("\n");
    }
    else {
        DBG_PRINTF("[%s()] same hash. not saved.\n", __func__);
    }
#endif  //__XTENSA__
}


void ICACHE_FLASH_ATTR bc_flash_get_last_bhash(uint8_t *pHash)
{
#ifdef __XTENSA__
    SpiFlashOpResult fret;

    uint32 blk1[sizeof(struct bc_flash_blk_t) / sizeof(uint32)];
    uint32 blk2[sizeof(struct bc_flash_blk_t) / sizeof(uint32)];

    fret = spi_flash_read(
            (uint32)(SPI_FLASH_SEC_SIZE * SEC_BLOCK1),
            blk1,
            (uint32)sizeof(blk1));
    M_FLASH_OPECHK(fret);

    fret = spi_flash_read(
            (uint32)(SPI_FLASH_SEC_SIZE * SEC_BLOCK2),
            blk2,
            (uint32)sizeof(blk2));
    M_FLASH_OPECHK(fret);

    const uint8_t *p;
    struct bc_flash_blk_t *p1 = (struct bc_flash_blk_t *)blk1;
    struct bc_flash_blk_t *p2 = (struct bc_flash_blk_t *)blk2;
    DBG_PRINTF("(blk1:%u, blk2:%u)\n", p1->update_time, p2->update_time);
    if ((p1->update_time == M_FLASH_EMPTY32) && (p2->update_time == M_FLASH_EMPTY32)) {
        //どちらも初めて
        DBG_PRINTF("[%s()] first\n", __func__);
        p = kBlockHashStart;
    }
    else if (p1->update_time == M_FLASH_EMPTY32) {
        //blk2有効
        DBG_PRINTF("[%s()] use blk2\n", __func__);
        p = p2->bhash;
    }
    else if (p2->update_time == M_FLASH_EMPTY32) {
        //blk1有効
        DBG_PRINTF("[%s()] use blk1\n", __func__);
        p = p1->bhash;
    }
    else {
        if (p1->update_time > p2->update_time) {
            //blk1の方が新しい
            DBG_PRINTF("[%s()] use new blk1\n", __func__);
            p = p1->bhash;
        }
        else {
            DBG_PRINTF("[%s()] use new blk2\n", __func__);
            p = p2->bhash;
        }
    }
    MEMCPY(pHash, p, BC_SZ_HASH256);
#else   //__XTENSA__
    MEMCPY(pHash, kBlockHashStart, BC_SZ_HASH256);
#endif  //__XTENSA__
}


int ICACHE_FLASH_ATTR bc_flash_erase_last_bhash(void)
{
#ifdef __XTENSA__
    SpiFlashOpResult fret;

    uint32 blk1[sizeof(struct bc_flash_blk_t) / sizeof(uint32)];
    uint32 blk2[sizeof(struct bc_flash_blk_t) / sizeof(uint32)];

    fret = spi_flash_read(
            (uint32)(SPI_FLASH_SEC_SIZE * SEC_BLOCK1),
            blk1,
            (uint32)sizeof(blk1));
    M_FLASH_OPECHK(fret);

    fret = spi_flash_read(
            (uint32)(SPI_FLASH_SEC_SIZE * SEC_BLOCK2),
            blk2,
            (uint32)sizeof(blk2));
    M_FLASH_OPECHK(fret);

    int sec;
    struct bc_flash_blk_t *p1 = (struct bc_flash_blk_t *)blk1;
    struct bc_flash_blk_t *p2 = (struct bc_flash_blk_t *)blk2;
    DBG_PRINTF("(blk1:%u, blk2:%u)\n", p1->update_time, p2->update_time);
    if ((p1->update_time == M_FLASH_EMPTY32) && (p2->update_time == M_FLASH_EMPTY32)) {
        //どちらも初めて
        DBG_PRINTF("[%s()] none\n", __func__);
        return 0;
    }
    else if (p1->update_time == M_FLASH_EMPTY32) {
        //blk2有効
        DBG_PRINTF("[%s()] erase blk2\n", __func__);
        sec = SEC_BLOCK1;
    }
    else if (p2->update_time == M_FLASH_EMPTY32) {
        //blk1有効
        DBG_PRINTF("[%s()] erase blk1\n", __func__);
        sec = SEC_BLOCK2;
    }
    else {
        if (p1->update_time > p2->update_time) {
            //blk1の方が新しい
            DBG_PRINTF("[%s()] erase new blk1\n", __func__);
            sec = SEC_BLOCK1;
        }
        else {
            DBG_PRINTF("[%s()] erase new blk2\n", __func__);
            sec = SEC_BLOCK2;
        }
    }

    //有効 or 新しい方を消去
    spi_flash_erase_sector(sec);

    return 1;
#else   //__XTENSA__
    return 0;
#endif  //__XTENSA__
}


/**************************************************************************
 * private functions
 **************************************************************************/

#ifdef __XTENSA__
/** TX(a)検索
 * 
 * @param[in,out]   pPos        [in]検索情報, [out]検索結果
 * @retval      0       対象が見つかった(対象検索までしか処理を行っていない)
 * @retval      1       対象はなかったが、空きは見つかった
 * @retval      -1      
 */
static int ICACHE_FLASH_ATTR search_txinfo(struct txpos_t *pPos, uint32_t timestamp)
{
    int ret = -1;
    int first_pos = pPos->pos;

    //DBG_PRINTF("[%s()]sec=%d pos=%d\n", __func__, pPos->sec, pPos->pos);

    pPos->pos = -1;

    for (int lp = first_pos; lp < SPI_FLASH_SEC_SIZE / sizeof(struct bc_flash_tx_t); lp++) {
        system_soft_wdt_feed();

        if (pPos->p_tx[lp].use_ch == M_FLASH_EMPTY8) {
            //空き
            if (pPos->pos == -1) {
                //初回はFLASH位置を保持
                pPos->pos = lp;
                ret = 1;            //空きはあった
                //DBG_PRINTF("[%s()]first empty: sec=%d, pos=%d\n", __func__, pPos->sec, pPos->pos);
            }
        }
        else {
            //TX(a)あり
            DBG_PRINTF("[%3d]s=%u e=%u m=%u ch=%d sd=%u\n", lp, pPos->p_tx[lp].start_time, pPos->p_tx[lp].end_time, pPos->p_tx[lp].use_min, pPos->p_tx[lp].use_ch, pPos->p_tx[lp].started_time);

            //利用期間と現在時刻のチェック
            if (timestamp >= pPos->p_tx[lp].end_time) {
                //時間切れ --> 情報削除して検索を続ける
                DBG_PRINTF("  [%s()]out of date: sec=%d, pos=%d, time=%u\n", __func__, pPos->sec, lp, pPos->p_tx[lp].end_time);
                MEMSET(&pPos->p_tx[lp], M_FLASH_EMPTY8, sizeof(struct bc_flash_tx_t));
                pPos->edit = 1;
                continue;
            }
            int cmp;
            if (pPos->type != BC_FLASH_TYPE_FLASH) {
                cmp = MEMCMP(pPos->p_tx[lp].txa_hash, pPos->p_hash, BC_SZ_HASH256);
            }
            else {
                //比較せずにTX(b)の有無をチェック
                cmp = (pPos->p_tx[lp].started_time != M_FLASH_EMPTY32) ? 0 : -1;
            }
            if (cmp != 0) {
                //何もしない
                continue;
            }

            //検索結果あり --> 関数を抜ける
            pPos->pos = lp;
            ret = 0;
            DBG_PRINTF("  [%s()]match: sec=%d, pos=%d\n", __func__, pPos->sec, pPos->pos);
            break;
        }
    }

    //DBG_PRINTF("\n[%s()]ret=%d\n", __func__, ret);

    return ret;
}


static void ICACHE_FLASH_ATTR show_bcaddr(const struct bc_flash_wlt_t *pAddr)
{
    DBG_PRINTF("BcAddr: ");
    for (int lp = 0; lp < sizeof(pAddr->bcaddr); lp++) {
        DBG_PRINTF("%02x", pAddr->bcaddr[lp]);
    }
    DBG_PRINTF("\n");
    DBG_PRINTF("PubKey: ");
    for (int lp = 0; lp < sizeof(pAddr->pubkey); lp++) {
        DBG_PRINTF("%02x", pAddr->pubkey[lp]);
    }
    DBG_PRINTF("\n");
}
#endif  //__XTENSA__
