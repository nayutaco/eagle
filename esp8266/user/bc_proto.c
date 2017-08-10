/**************************************************************************
 * @file    bc_proto.c
 * @brief   Bitcoinパケット制御
 * @note
 *          - ESP8266_NONOS_SDK_V1.5.1_16_01_08
 *              - 受信バッファサイズ : 1460byte(受信コールバックより)
 *              - 送信バッファサイズ : 2920byte(espconn_get_packet_info()より)
 **************************************************************************/

#ifdef __XTENSA__
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "user_config.h"
#include "espconn.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#endif

#include "bc_ope.h"
#include "bc_proto.h"
#include "bc_flash.h"
#include "picocoin/bloom.h"


/**************************************************************************
 * macros
 **************************************************************************/
#define SZ_SEND_BUF             (3096)

#define BC_PROTOCOL_VERSION     ((int32_t)70001)
#define BC_MAGIC_TESTNET3       ((uint32_t)0x0709110B)
#define BC_PORT_TESTNET3        (18333)
#define BC_CMD_LEN              (12)
#define BC_CHKSUM_LEN           (4)
#define BC_VER_UA               "/kumacoinc:0.00/test:0.0/"

//Elements=200, Rate=0.00001で、600バイト程度
//Elements=700, Rate=0.00001で、2096バイト程度
//Elements=700, Rate=0.0001で、1677バイト程度
#define BLOOM_ELEMENTS              (700)           //bitcoinjのwallettemplate参考
#define BLOOM_RATE                  (0.0001)        //bitcoinjのwallettemplate参考
#define BLOOM_TWEAK                 rand()

#define BLOOM_UPDATE_NONE           (0)
#define BLOOM_UPDATE_ALL            (1)
#define BLOOM_UPDATE_P2PUBKEY_ONLY  (2)

#define INV_MSG_ERROR               (0)
#define INV_MSG_TX                  (1)
#define INV_MSG_BLOCK               (2)
#define INV_MSG_FILTERED_BLOCK      (3)

#define GETDATA_NUM                 (60)            ///< 1回のheadersでgetdataする最大件数

/** @def    BC_PACKET_LEN()
 *
 * パケット長取得
 */
#define BC_PACKET_LEN(pProto)   (sizeof(struct bc_proto_t) + pProto->length)

/** @def    BC_LEN_CHECK()
 *
 * 受信データ長がpayload長を満たしているか
 */
#define BC_LEN_CHECK(p,l)   \
    if (l < p.length) {     \
        DBG_PRINTF("not enough length (%d < %d)\n", l, p.length);   \
        HALT();                                                     \
    }

/**************************************************************************
 * types
 **************************************************************************/
#pragma pack(1)
/** @struct bc_proto_t
 *
 *
 */
struct bc_proto_t {
    uint32_t    magic;
    char        command[BC_CMD_LEN];
    uint32_t    length;
    uint8_t     checksum[BC_CHKSUM_LEN];
    uint8_t     payload[0];
};

/** @struct net_addr
 *
 *
 */
struct net_addr_t {
    //timestampは自分でやる
    uint64_t    services;
    uint8_t     ipaddr[16];
    uint16_t    port;
};

/** @struct inv_t
 *
 *
 */
struct inv_t {
    uint32_t    type;
    uint8_t     hash[BC_SZ_HASH256];
};

/** @struct headers_t
 *
 *
 */
struct headers_t {
    int32_t     version;
    uint8_t     prev_block[BC_SZ_HASH256];
    uint8_t     merkle_root[BC_SZ_HASH256];
    uint32_t    timestamp;
    uint32_t    bits;
    uint32_t    nonce;
    uint8_t     txn_count;      //getheaders向けに確保しておく
};
#pragma pack()

typedef int (*read_function_t)(struct espconn *pConn, const uint8_t *p, int *pLen);


/**************************************************************************
 * prototypes
 **************************************************************************/
static int ICACHE_FLASH_ATTR send_data(struct espconn *pConn, struct bc_proto_t *pProto);
static void ICACHE_FLASH_ATTR set_header(struct bc_proto_t *pProto, const char *pCmd);
static sint64_t ICACHE_FLASH_ATTR get_current_time(void);
static void ICACHE_FLASH_ATTR print_time(uint64_t tm);

static void ICACHE_FLASH_ATTR add_netaddr(uint8_t **pp, uint64_t serv, int ip0, int ip1, int ip2, int ip3, uint16_t port);
static void ICACHE_FLASH_ATTR add_varint(uint8_t **pp, int Len);

static inline int ICACHE_FLASH_ATTR get32(const uint8_t *p, uint32_t *pVal);
static inline int ICACHE_FLASH_ATTR get64(const uint8_t *p, uint64_t *pVal);
static int ICACHE_FLASH_ATTR getstr(const uint8_t *p, char *pStr);
static inline int ICACHE_FLASH_ATTR get_netaddr(const uint8_t *p, struct net_addr_t *pAddr);
static inline int ICACHE_FLASH_ATTR get_headers(const uint8_t *p, struct headers_t *pHead);
//static inline int ICACHE_FLASH_ATTR get_inv(const uint8_t *p, struct inv_t *pInv);

static void ICACHE_FLASH_ATTR print_netaddr(const struct net_addr_t *pAddr);
static void ICACHE_FLASH_ATTR print_inv(const struct inv_t *pInv);
static void ICACHE_FLASH_ATTR print_headers(const struct headers_t* pHead);

static uint8_t *ICACHE_FLASH_ATTR read_nbyte(const uint8_t *pData, int *pLen, int nByte);
static int ICACHE_FLASH_ATTR read_varint(const uint8_t *pData, int *pLen, uint64_t *pVal);
static int ICACHE_FLASH_ATTR read_version(struct espconn *pConn, const uint8_t *p, int *pLen);
static int ICACHE_FLASH_ATTR read_verack(struct espconn *pConn, const uint8_t *p, int *pLen);
static int ICACHE_FLASH_ATTR read_ping(struct espconn *pConn, const uint8_t *p, int *pLen);
static int ICACHE_FLASH_ATTR read_pong(struct espconn *pConn, const uint8_t *p, int *pLen);
//static int ICACHE_FLASH_ATTR read_addr(struct espconn *pConn, const uint8_t *p, int *pLen);
static int ICACHE_FLASH_ATTR read_inv(struct espconn *pConn, const uint8_t *p, int *pLen);
static int ICACHE_FLASH_ATTR read_block(struct espconn *pConn, const uint8_t *p, int *pLen);
static int ICACHE_FLASH_ATTR read_tx(struct espconn *pConn, const uint8_t *p, int *pLen);
static int ICACHE_FLASH_ATTR read_headers(struct espconn *pConn, const uint8_t *pData, int *pLen);
static int ICACHE_FLASH_ATTR read_merkleblock(struct espconn *pConn, const uint8_t *pData, int *pLen);
static int ICACHE_FLASH_ATTR read_unknown(struct espconn *pConn, const uint8_t *pData, int *pLen);

static int ICACHE_FLASH_ATTR send_version(struct espconn *pConn);
static int ICACHE_FLASH_ATTR send_verack(struct espconn *pConn);
//static int ICACHE_FLASH_ATTR send_ping(struct espconn *pConn);
static int ICACHE_FLASH_ATTR send_pong(struct espconn *pConn, uint64_t Nonce);
//static int ICACHE_FLASH_ATTR send_getblocks(struct espconn *pConn, const uint8_t *pHash);
static int ICACHE_FLASH_ATTR send_getheaders(struct espconn *pConn, const uint8_t *pHash);
//static int ICACHE_FLASH_ATTR send_getdata(struct espconn *pConn, const uint8_t *pInv, int Len);
static int ICACHE_FLASH_ATTR send_filterload(struct espconn *pConn);
static int ICACHE_FLASH_ATTR send_mempool(struct espconn *pConn);


/**************************************************************************
 * const variables
 **************************************************************************/
const char kCMD_VERSION[] = "version";              ///< [message]version
const char kCMD_VERACK[] = "verack";                ///< [message]verack
const char kCMD_PING[] = "ping";                    ///< [message]ping
const char kCMD_PONG[] = "pong";                    ///< [message]pong
const char kCMD_ADDR[] = "addr";                    ///< [message]addr
const char kCMD_INV[] = "inv";                      ///< [message]inv
const char kCMD_GETBLOCKS[] = "getblocks";          ///< [message]getblocks
const char kCMD_GETHEADERS[] = "getheaders";        ///< [message]getheaders
const char kCMD_GETDATA[] = "getdata";              ///< [message]getdata
const char kCMD_BLOCK[] = "block";                  ///< [message]block
const char kCMD_HEADERS[] = "headers";              ///< [message]headers
const char kCMD_FILTERLOAD[] = "filterload";        ///< [message]filterload
const char kCMD_TX[] = "tx";                        ///< [message]tx
const char kCMD_MEMPOOL[] = "mempool";              ///< [message]mempool
const char kCMD_MERKLEBLOCK[] = "merkleblock";      ///< [message]merkleblock

//Genesis Hash
//000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943
//const uint8_t kGenesisBhash[] = {
//    0x43, 0x49, 0x7f, 0xd7, 0xf8, 0x26, 0x95, 0x71, 
//    0x08, 0xf4, 0xa3, 0x0f, 0xd9, 0xce, 0xc3, 0xae, 
//    0xba, 0x79, 0x97, 0x20, 0x84, 0xe9, 0x0e, 0xad, 
//    0x01, 0xea, 0x33, 0x09, 0x00, 0x00, 0x00, 0x00, 
//};
//Height #1
//00000000b873e79784647a6c82962c70d228557d24a747ea4d1b8bbe878e1206
const uint8_t kBhash1[] = {
    0x06, 0x12, 0x8e, 0x87, 0xbe, 0x8b, 0x1b, 0x4d, 
    0xea, 0x47, 0xa7, 0x24, 0x7d, 0x55, 0x28, 0xd2, 
    0x70, 0x2c, 0x96, 0x82, 0x6c, 0x7a, 0x64, 0x84, 
    0x97, 0xe7, 0x73, 0xb8, 0x00, 0x00, 0x00, 0x00, 
};

/** 受信解析用 */
static const struct {
    const char          *pCmd;                  ///< メッセージ
    read_function_t     pFunc;                  ///< 処理関数
    int                 longPayload;            /**< 1:long payload対応
                                                 *      payload長があまり長くないと思われるメッセージは、
                                                 *      MALLOC()で動的にメモリを確保してでも一度に処理する。
                                                 *      あらかじめpayload長が長くなると思われるメッセージは、
                                                 *      各処理関数内で何度も受信が呼び出されることを想定した
                                                 *      実装を行う。
                                                 */
} kReplyFunc[] = {
    {   kCMD_PING,              read_ping,          0   },
    {   kCMD_HEADERS,           read_headers,       1   },
    {   kCMD_MERKLEBLOCK,       read_merkleblock,   1   },
    {   kCMD_INV,               read_inv,           1   },
    {   kCMD_TX,                read_tx,            0   },
    {   kCMD_BLOCK,             read_block,         0   },
    {   kCMD_PONG,              read_pong,          0   },
//    {   kCMD_ADDR,              read_addr,          0   },
    {   kCMD_VERSION,           read_version,       0   },
    {   kCMD_VERACK,            read_verack,        0   },
    {   NULL,                   read_unknown,       1   },
};


/**************************************************************************
 * private variables
 **************************************************************************/
static uint8_t mBufferCnt = 0;                  /**< 送信要求数 */
static uint8_t mMerkleCnt = 0;                  /**< getheaders-->headers-->getdata後のmerkleblock数(カウントダウン) */
static uint8_t mCurrentProto = 0xff;            /**< 現在処理中の受信メッセージ */
static uint8_t mBuffer[SZ_SEND_BUF];            /**< 送信バッファ
                                                 *      espconn_send()は送信バッファにためていき、
                                                 *      送信は非同期で行われる。
                                                 *      しかし、送信完了コールバックが来るまでは
                                                 *      espconn_send()を呼び出してもうまく動かない(エラーにはならない)。
                                                 *      そのため、送信完了コールバックが来るまでの送信を
                                                 *      このバッファにためる。
                                                 */
static uint8_t *mBufferWPnt = mBuffer;          /**< 送信データ書込みポイント */
static uint8_t *mBufferRPnt = mBuffer;          /**< 送信データ読込みポイント */
static uint8_t mLastHeadersBhash[BC_SZ_HASH256] __attribute__ ((aligned (4)));  /**< headersで最後に読んだBlock Hash */
static uint8_t mLastInvBhash[BC_SZ_HASH256] __attribute__ ((aligned (4)));      /**< invで最後に読んだBlock Hash
                                                                                 *      getheadersで使用する。
                                                                                 *      MSG_BLOCKで更新したか判定するため、
                                                                                 *      最後の要素を0xffにしておく。
                                                                                 */
static struct bc_proto_t    mProto;             /**< 現在処理中のプロトコルヘッダ */
static uint8_t *mpPayload = NULL;               /**< getdataメッセージのペイロード(mBufferWPnt内)
                                                 *      read_inv(), read_headers()用
                                                 */

static int8_t mStatus = -1;                     /**< TODO:用途を決め切れてないフラグ */
                                                //0: 送信完了時にgetheadersを投げ、1にする
                                                //1: getheaders中。全部投げてmempool投げると同時に2にする
static int8_t mHasPing = 0;                     /**< 0:ping受信あり */
static uint64_t mPingNonce;                     /**< 最後に受信したpingのnonce */


/**************************************************************************
 * public functions
 **************************************************************************/
int ICACHE_FLASH_ATTR bc_start(struct espconn *pConn)
{
    DBG_FUNCNAME();

    bc_flash_update_txinfo(BC_FLASH_TYPE_FLASH, NULL);

    //末尾に0x00以外を書込んでおく(read_headersでの更新判定のため)
    mLastHeadersBhash[BC_SZ_HASH256 - 1] = 0xff;
    //末尾に0x00以外を書込んでおく(read_invでの更新判定のため)
    mLastInvBhash[BC_SZ_HASH256 - 1] = 0xff;

    return send_version(pConn);
}


void ICACHE_FLASH_ATTR bc_read_message(struct espconn *pConn, const uint8_t *pBuffer, int *pLen)
{
    static enum {
        STAGE0,         //ヘッダ受信前
        STAGE1,         //ヘッダ受信済み
        STAGE2,         //ペイロード判定
        STAGE2_5,       //ペイロード受信(longPayload非対応かつ受信済みが小さい場合)
        STAGE3          //ペイロード処理
    } sStage = STAGE0;
    static uint8_t sProtoLen;           //mProtoに詰めたデータ長(Stage0の受信不足かStage1のMAGIC不正)
    static uint8_t *spBuffer = NULL;

    int ret;

    //DBG_FUNCNAME();

    switch (sStage) {
    case STAGE0:
        //DBG_PRINTF("*** STAGE 0 ***[sProtoLen=%d]\n", sProtoLen);
        //DBG_PRINTF("0");
        //ヘッダ分以上のサイズがあるならヘッダだけ、それ未満なら全部読込む
        if (sProtoLen + *pLen >= sizeof(struct bc_proto_t)) {
            MEMCPY((uint8_t *)&mProto + sProtoLen, pBuffer, sizeof(struct bc_proto_t) - sProtoLen);
            sStage = STAGE1;
        }
        else {
            //受信不足
            MEMCPY((uint8_t *)&mProto + sProtoLen, pBuffer, *pLen);
            sProtoLen += *pLen;
            *pLen = 0;
//            DBG_PRINTF("[%s()]  ... more read ...\n", __func__);
//            for (int lp = 0; lp < sProtoLen; lp++) {
//                DBG_PRINTF("%02x", *((uint8_t *)&mProto + lp));
//            }
//            DBG_PRINTF("\n");
        }
        break;

    case STAGE1:
        //DBG_PRINTF("*** STAGE 1 ***\n");
        //DBG_PRINTF("1");
        if (mProto.magic == BC_MAGIC_TESTNET3) {
            //DBG_PRINTF("  magic : %08x\n", mProto.magic);
            //DBG_PRINTF("  cmd   : %s\n", mProto.command);
            //DBG_PRINTF("  len   : %d\n", mProto.length);
            //DBG_PRINTF("  hash  : %02x %02x %02x %02x\n", mProto.length.checksum[0], mProto.length.checksum[1], mProto.length.checksum[2], mProto.length.checksum[3]);

            sStage = STAGE2;
            *pLen -= sizeof(struct bc_proto_t) - sProtoLen;
            sProtoLen = 0;
        }
        else {
            DBG_PRINTF("[%s()]  invalid magic(%08x)\n", __func__, mProto.magic);
            for (int lp = 0; lp < *pLen; lp++) {
                DBG_PRINTF("%02x ", mBuffer[lp]);
            }
            DBG_PRINTF("\n");

            //不一致 --> 1byte詰める
            sProtoLen = sizeof(struct bc_proto_t) - 1;
            MEMMOVE(&mProto, (uint8_t *)&mProto + 1, sProtoLen);
            (*pLen)--;
            sStage = STAGE0;
        }
        break;

    case STAGE2:
        //DBG_PRINTF("*** STAGE 2 ***\n");
        //DBG_PRINTF("2");
        mCurrentProto = 0;
        while (kReplyFunc[mCurrentProto].pCmd != NULL) {
            if (STRCMP(mProto.command, kReplyFunc[mCurrentProto].pCmd) == 0) {
                break;
            }
            mCurrentProto++;
        }
        if ((kReplyFunc[mCurrentProto].longPayload == 0) &&
          (mProto.length > *pLen)) {
            //longPayload非対応で、受信済みサイズがペイロード長よりも短い
            spBuffer = read_nbyte(pBuffer, pLen, mProto.length);
            sStage = STAGE2_5;
            DBG_PRINTF("*** STAGE 2 --> 2.5 *** : spBuffer=%p\n", spBuffer);
        }
        else {
            spBuffer = NULL;
            sStage = STAGE3;
        }
        break;

    case STAGE2_5:
        DBG_PRINTF("*** STAGE 2.5 *** : spBuffer=%p\n", spBuffer);
        if (spBuffer == NULL) {
            spBuffer = read_nbyte(pBuffer, pLen, mProto.length);
        }
        if (spBuffer != NULL) {
            DBG_PRINTF("*** STAGE 2.5 --> analyze ***\n");
//            for (int lp = 0; lp < mProto.length; lp++) {
//                DBG_PRINTF("%02x ", spBuffer[lp]);
//            }
//            DBG_PRINTF("\n");
            //解析は1回で終了する
            int len = mProto.length;
            (void)((*kReplyFunc[mCurrentProto].pFunc)(pConn, spBuffer, &len));
            FREE(spBuffer);
            spBuffer = NULL;
            sStage = STAGE0;
        }
        break;

    case STAGE3:
        //DBG_PRINTF("*** STAGE 3 *** : <<<< length:%d >>>>\n", *pLen);
        //DBG_PRINTF("3");
        ret = (*kReplyFunc[mCurrentProto].pFunc)(pConn, pBuffer, pLen);
        if (ret == BC_PROTO_FIN) {
            //解析完了
            sStage = STAGE0;
            mCurrentProto = 0xff;
            //DBG_PRINTF("  <<< fin >>>\n\n");
            //DBG_PRINTF(";");
        }
        else {
            //解析継続あり
            //DBG_PRINTF("  <<< cont >>>\n\n");
        }
        break;

    default:
        DBG_PRINTF("invalid stage : %d\n", (int)sStage);
        HALT();
        break;
    }
}


void ICACHE_FLASH_ATTR bc_sent(struct espconn *pConn, uint16_t sent_length)
{
    DBG_PRINTF("\n[%s(%u)]mBufferCnt=%d, sent_length=%u\n", __func__, bc_misc_time_get(), mBufferCnt, sent_length);

    if (mBufferCnt) {
        const struct bc_proto_t *pProto = (const struct bc_proto_t *)mBufferRPnt;
        int ret = espconn_send(pConn, mBufferRPnt, BC_PACKET_LEN(pProto));
        if (ret == 0) {
            mBufferRPnt += BC_PACKET_LEN(pProto);
            mBufferCnt--;

            if (mBufferCnt == 0) {
                //DBG_PRINTF("[%s()] buff init\n", __func__);
                mBufferRPnt = mBuffer;
                mBufferWPnt = mBuffer;
            }
        }
        else {
            DBG_PRINTF("[%s()]ret = %d\n", __func__, ret);
            HALT();
        }
    }
    else {
        //DBG_PRINTF("mBufferCnt : 0\n");

        if (mStatus == 0) {
            //初回のgetheaders送信
            mStatus = 1;
            uint8_t hash[BC_SZ_HASH256];
            bc_flash_get_last_bhash(hash);
            send_getheaders(pConn, hash);
        }
        else {
            if ((mHasPing) && (mpPayload == NULL)) {
                //ping受信済みで、Long Payloadの処理をしていないとき
                send_pong(pConn, mPingNonce);
                mHasPing = 0;
            }
        }
    }
}


void ICACHE_FLASH_ATTR bc_finish(void)
{
    DBG_FUNCNAME();

    if (mStatus == 1) {
        //FLASHの初期処理中であれば、現状を保持する
        if (mLastHeadersBhash[BC_SZ_HASH256 - 1] != 0xff) {
            DBG_PRINTF("save current block : ");
            for (int i = 0; i < BC_SZ_HASH256; i++) {
                DBG_PRINTF("%02x", mLastHeadersBhash[BC_SZ_HASH256 - i - 1]);
            }
            DBG_PRINTF("\n");
            bc_flash_save_last_bhash(mLastHeadersBhash);
            mLastHeadersBhash[BC_SZ_HASH256 - 1] = 0xff;
        }
        mStatus = -1;
    }
}


/**************************************************************************
 * private functions
 **************************************************************************/

///////////////
// 環境依存
///////////////

/** 現在時刻の取得(epoch)
 *
 * @return  現在時刻(epoch時間)
 */
static sint64_t ICACHE_FLASH_ATTR get_current_time(void)
{
#ifdef __XTENSA__
    //TODO:
    return 0;
#else
    return time(NULL);
#endif
}


/** (コンソール)時刻出力
 *
 * @param[in]   tm       時刻データ(epoch:Little Endian)
 */
static void ICACHE_FLASH_ATTR print_time(uint64_t tm)
{
#ifdef __XTENSA__
    DBG_PRINTF("%08x%08x\n", (uint32_t)(tm >> 32), (uint32_t)(tm & 0xffffffff));
#else
    DBG_PRINTF("%s", ctime((time_t *)&tm));
#endif
}


/** TCP送信
 *
 * @param[in]       pConn       管理データ
 * @param[in]       pProto      Bitcoinプロトコルデータ
 * @return          送信結果(0...OK)
 */
static int ICACHE_FLASH_ATTR send_data(struct espconn *pConn, struct bc_proto_t *pProto)
{
    int ret = ESPCONN_MAXNUM;

    //checksum
    uint8_t hash[BC_SZ_HASH256];
    bc_misc_hash256(hash, pProto->payload, pProto->length);
    MEMCPY(pProto->checksum, hash, BC_CHKSUM_LEN);

    if (mBufferCnt == 0) {
        //戻り値0は成功時
        ret = espconn_send(pConn, (uint8_t *)pProto, BC_PACKET_LEN(pProto));
        DBG_PRINTF("[%s(%u)] ret=%d, len=%d\n", __func__, bc_misc_time_get(), ret, BC_PACKET_LEN(pProto));
    }
#ifdef __XTENSA__
    if (ret == 0) {
        //OK
    }
    else if (ret == ESPCONN_MAXNUM) {
        //ESP8266の送信バッファあふれ
        mBufferCnt++;
        mBufferWPnt += BC_PACKET_LEN(pProto);
        DBG_PRINTF("  add send buffer(mBufferCnt=%d[%s]len=%d, %p)\n", mBufferCnt, pProto->command, BC_PACKET_LEN(pProto), mBufferWPnt);
        if (mBufferWPnt - mBuffer >= sizeof(mBuffer)) {
            //バッファへの書込みは各関数でやっているので、実はこの時点で既に領域破壊している。
            //ならば、本来はバッファの書込み中にあふれないかどうかをチェックするのが正しいだろう。
            //しかし、だ。
            //書込みがあふれるような作りにしないことが先なのだ。
            //だから、あえてここは目をつぶろう。
            DBG_PRINTF(" oops! send buffer already FULL!!!!\n");
            HALT();
        }
    }
    else {
        DBG_PRINTF("[%s()] ret=%d\n", __func__, ret);
        HALT();
    }
#else
    else {
    }
    //Linuxでは同期送信なので、ためない
    ret = 0;
    mBufferCnt = 0;

    if (BC_PACKET_LEN(pProto) >= sizeof(mBuffer)) {
        //バッファへの書込みは各関数でやっているので、実はこの時点で既に領域破壊している。
        //ならば、本来はバッファの書込み中にあふれないかどうかをチェックするのが正しいだろう。
        //しかし、だ。
        //書込みがあふれるような作りにしないことが先なのだ。
        //だから、あえてここは目をつぶろう。
        DBG_PRINTF(" oops! send buffer already FULL!!!!\n");
        HALT();
    }
#endif
    return ret;
}


/** Bitcoinパケットヘッダ設定
 *
 * @param[in]   pProto      Bitconプロトコルデータ
 * @param[in]   pCmd        送信コマンド
 */
static void ICACHE_FLASH_ATTR set_header(struct bc_proto_t *pProto, const char *pCmd)
{
    pProto->magic = BC_MAGIC_TESTNET3;
    BZERO(pProto->command, BC_CMD_LEN);
    STRCPY(pProto->command, pCmd);
}


/** net_addr設定
 *
 * @param[in,out]   pp      設定先バッファ
 * @param[in]       serv    サービス
 * @param[in]       ip0     IPv4[0]
 * @param[in]       ip1     IPv4[1]
 * @param[in]       ip2     IPv4[2]
 * @param[in]       ip3     IPv4[03]
 * @param[in]       port    ポート番号
 *
 * @note
 *      - ポインタを進める
 */
static void ICACHE_FLASH_ATTR add_netaddr(uint8_t **pp, uint64_t serv, int ip0, int ip1, int ip2, int ip3, uint16_t port)
{
    struct net_addr_t addr;

    BZERO(&addr, 10);
    addr.services = serv;
    addr.ipaddr[10] = addr.ipaddr[11] = 0xff;
    addr.ipaddr[12] = ip0;
    addr.ipaddr[13] = ip1;
    addr.ipaddr[14] = ip2;
    addr.ipaddr[15] = ip3;
    addr.port = (uint16_t)(((port & 0xff00) >> 8) | ((port & 0xff) << 8));     //big endian
    MEMCPY(*pp, &addr, sizeof(addr));
    *pp += sizeof(addr);
}


/** データ設定(varint)
 *
 * @param[in,out]   pp      設定先バッファ
 * @param[in]       Len     値
 *
 * @note
 *      - ポインタを進める
 */
static void ICACHE_FLASH_ATTR add_varint(uint8_t **pp, int Len)
{
    if (Len < 0xfd) {
        bc_misc_add(pp, Len, 1);
    }
    else if (Len < 0xffff) {
        bc_misc_add(pp, 0xfd, 1);
        bc_misc_add(pp, Len, 2);
    }
    else {
        //TODO:
        DBG_PRINTF("string too long!\n");
        return;
    }
}


/** データ取得(32bit)
 *
 * @param[in]   pp          受信データ
 * @return      データ(32bit)
 *
 * @note
 *      - ポインタを進める
 */
static inline int ICACHE_FLASH_ATTR get32(const uint8_t *p, uint32_t *pVal)
{
    MEMCPY(pVal, p, sizeof(*pVal));
    return sizeof(uint32_t);
}


/** データ取得(64bit)
 *
 * @param[in]   pp          受信データ
 * @return      データ(64bit)
 *
 * @note
 *      - ポインタを進める
 */
static inline int ICACHE_FLASH_ATTR get64(const uint8_t *p, uint64_t *pVal)
{
    MEMCPY(pVal, p, sizeof(*pVal));
    return sizeof(uint64_t);
}


/** データ取得(文字列)
 *
 * @param[in]   p       受信データ
 * @param[out]  pStr    文字列
 * @return          解析データ長
 */
static int ICACHE_FLASH_ATTR getstr(const uint8_t *p, char *pStr)
{
    int len;
    int rlen = bc_misc_get_varint(p, &len);
    p += rlen;
    MEMCPY(pStr, p, len);
    *(pStr + len) = '\0';
    return rlen + len;
}


/** データ取得(net_addr)
 *
 * @param[in]   p       受信データ
 * @param[out]  pAddr   net_addr
 * @return      解析データ長
 */
static inline int ICACHE_FLASH_ATTR get_netaddr(const uint8_t *p, struct net_addr_t *pAddr)
{
    MEMCPY(pAddr, p, sizeof(struct net_addr_t));
    pAddr->port = (uint16_t)((pAddr->port >> 8) | ((pAddr->port & 0xff) << 8));
    return sizeof(struct net_addr_t);
}


/** データ取得(header)
 *
 * @param[in]   p       受信データ
 * @param[out]  pHead   block header
 * @return      解析データ長
 */
static inline int ICACHE_FLASH_ATTR get_headers(const uint8_t *p, struct headers_t *pHead)
{
    MEMCPY(pHead, p, sizeof(struct headers_t));
    return sizeof(struct headers_t);
}


/** データ取得(inv)
 *
 * @param[in]   p       受信データ
 * @param[out]  pInv    inv
 * @return      解析データ長
 */
//static inline int ICACHE_FLASH_ATTR get_inv(const uint8_t *p, struct inv_t *pInv)
//{
//    MEMCPY(pInv, p, sizeof(struct inv_t));
//    return sizeof(struct inv_t);
//}


/** (コンソール)net_addr出力
 *
 * @param[in]   pAddr       net_addrデータ
 */
static void ICACHE_FLASH_ATTR print_netaddr(const struct net_addr_t *pAddr)
{
    DBG_PRINTF("    services : %llu\n", pAddr->services);
    DBG_PRINTF("    addr : ");
    for (int i = 0; i < sizeof(pAddr->ipaddr); i++) {
        DBG_PRINTF("%02x ", pAddr->ipaddr[i]);
    }
    DBG_PRINTF("\n");
    DBG_PRINTF("    port : %d\n", pAddr->port);
}


/** (コンソール)inv出力
 *
 * @param[in]   pInv    invデータ
 */
static void ICACHE_FLASH_ATTR print_inv(const struct inv_t *pInv)
{
    const char *pTypeName;

    switch (pInv->type) {
    case INV_MSG_ERROR:
        pTypeName = "ERROR";
        break;
    case INV_MSG_TX:
        pTypeName = "MSG_TX";
        break;
    case INV_MSG_BLOCK:
        pTypeName = "MSG_BLOCK";
        break;
    case INV_MSG_FILTERED_BLOCK:
        pTypeName = "MSG_FILTERED_BLOCK";
        break;
    default:
        DBG_PRINTF("unknown type\n");
        HALT();
        break;
    }

    DBG_PRINTF("    type : %s(%d)\n", pTypeName, pInv->type);
    DBG_PRINTF("    hash(inv) : ");
    for (int i = 0; i < BC_SZ_HASH256; i++) {
        DBG_PRINTF("%02x", pInv->hash[BC_SZ_HASH256 - i - 1]);
    }
    DBG_PRINTF("\n");
}


/** (コンソール)headers出力
 *
 * @param[in]   p       headersデータ
 * @return      解析データ長
 */
static void ICACHE_FLASH_ATTR print_headers(const struct headers_t* pHead)
{
//    //version
//    DBG_PRINTF("    version : %d\n", pHead->version);
//    //prev_block
//    DBG_PRINTF("    prev_block(LE) : ");
//    for (int i = 0; i < BC_SZ_HASH256; i++) {
//        DBG_PRINTF("%02x", pHead->prev_block[BC_SZ_HASH256 - i - 1]);
//    }
//    DBG_PRINTF("\n");
//    //merkle_root
//    DBG_PRINTF("    merkle_root(LE) : ");
//    for (int i = 0; i < BC_SZ_HASH256; i++) {
//        DBG_PRINTF("%02x", pHead->merkle_root[BC_SZ_HASH256 - i - 1]);
//    }
//    DBG_PRINTF("\n");
//    //timestamp
//    DBG_PRINTF("    timestamp : ");
//    print_time(pHead->timestamp);
//    //bits
//    DBG_PRINTF("    bits : %08x\n", pHead->bits);
//    //nonce
//    DBG_PRINTF("    nonce : %08x\n", pHead->nonce);

    //block hash
    uint8_t hash[BC_SZ_HASH256];
    bc_misc_hash256(hash, (const uint8_t *)pHead, sizeof(struct headers_t) - 1); //block hash
    DBG_PRINTF("    block hash(%u) : ", bc_misc_time_get());
    for (int i = 0; i < BC_SZ_HASH256; i++) {
        DBG_PRINTF("%02x", hash[BC_SZ_HASH256 - i - 1]);
    }
    DBG_PRINTF("\n");
}


/** @brief  MALLOC()して指定サイズまでためる
 *
 * @param[in]       pData       入力データ
 * @param[in,out]   pLen        [in]pDataサイズ, [out]残りサイズ
 * @param[in]       nByte       指定サイズ
 * @retval          NULL        指定サイズまでたまっていない
 * @retval          それ以外
 * @note
 *      - 戻り値が非NULLだった場合、呼び出し元でFREE()すること
 */
static uint8_t *ICACHE_FLASH_ATTR read_nbyte(const uint8_t *pData, int *pLen, int nByte)
{
    static uint8_t *spBuf = NULL;       //呼び出し元でFREE()してもらうこと！
    static int sBytes = 0;

    uint8_t *pRet = NULL;

    if (sBytes == 0) {
        spBuf = (uint8_t *)MALLOC(nByte);
    }
    if (sBytes + *pLen >= nByte) {
        MEMCPY(spBuf + sBytes, pData, nByte - sBytes);
        *pLen -= nByte - sBytes;
        pRet = spBuf;       //呼び出し元でFREE()してもらうこと！
        sBytes = 0;
    }
    else {
        MEMCPY(spBuf + sBytes, pData, *pLen);
        sBytes += *pLen;
        *pLen = 0;
    }

    return pRet;
}


/** varint数値変換
 * 
 * 初回のpDataは、varintデータの先頭から始まることを想定.
 * 先頭データから残りの必要サイズを決定し、処理したサイズを引いてpLenに戻す.
 * よって、BC_PROTO_CONTを返す場合や、BC_PROTO_FINでちょうどvarint分のデータだった場合、*pLenは0になる.
 * 
 * pDataは位置を変更させないので、pLenと統一性がない.
 * いっそのこと、pLenは「残りサイズ」ではなく「処理したサイズ」のほうがすっきりするかもしれないが、
 * 全体的に*pLenは残りサイズを返すように作っているため、それもやりづらい.
 * 
 * @param[in]       pData       入力データ
 * @param[in,out]   pLen        [in]pDataサイズ, [out]残りサイズ
 * @param[out]      pVal        変換結果
 * @retval      BC_PROTO_FIN    解析完了
 * @retval      BC_PROTO_CONT   データ不足
 * 
 * @note        内部で#read_nbyte()を呼び出す
 */
static int ICACHE_FLASH_ATTR read_varint(const uint8_t *pData, int *pLen, uint64_t *pVal)
{
    static uint8_t *spBuf;
    static int sCount = 0;

    int ret = BC_PROTO_CONT;

    if (sCount == 0) {
        if (*pData < 0xfd) {
            *pVal = (uint64_t)*pData;
            ret = BC_PROTO_FIN;
        }
        else if (*pData == 0xfd) {
            sCount = 2;
        }
        else if (*pData == 0xfe) {
            sCount = 4;
        }
        else {
            sCount = 8;
        }
        pData++;
        (*pLen)--;
    }
    if (sCount > 0) {
        spBuf = read_nbyte(pData, pLen, sCount);
        if (spBuf != NULL) {
            switch (sCount) {
            case 2:
                *pVal = (uint64_t)*(uint16_t *)spBuf;
                break;
            case 4:
                *pVal = (uint64_t)*(uint32_t *)spBuf;
                break;
            case 8:
                *pVal = (uint64_t)*(uint64_t *)spBuf;
                break;
            default:
                break;
            }
            FREE(spBuf);
            sCount = 0;
            ret = BC_PROTO_FIN;
        }
    }

    return ret;
}


/** 受信データ解析(version)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 */
static int ICACHE_FLASH_ATTR read_version(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    const uint8_t *p = pData;

    BC_LEN_CHECK(mProto, *pLen);

    DBG_PRINTF("  [version]\n");
    //version
    int32_t version;
    p += get32(p, (uint32_t *)&version);
    DBG_PRINTF("   version : %d\n", version);
    //services
    uint64_t services;
    p += get64(p, &services);
    DBG_PRINTF("   services : %llu\n", services);
    //timestamp
    DBG_PRINTF("   timestamp : ");
    uint64_t timestamp;
    p += get64(p, &timestamp);
    print_time(timestamp);
    //addr_recv
    DBG_PRINTF("   addr_recv:\n");
    struct net_addr_t addr;
    p += get_netaddr(p, &addr);
    print_netaddr(&addr);
    //addr_from
    DBG_PRINTF("   addr_from:\n");
    p += get_netaddr(p, &addr);
    print_netaddr(&addr);
    //nonce
    uint64_t nonce;
    p += get64(p, &nonce);
    DBG_PRINTF("   nonce : %08x%08x\n", (uint32_t)(nonce >> 32), (uint32_t)(nonce & 0xffffffff));
    //UserAgent
    char buf[50];
    p += getstr(p, buf);
    DBG_PRINTF("   user_agent : %s\n", buf);
    //height
    uint32_t height;
    p += get32(p, (uint32_t *)&height);
    DBG_PRINTF("   height : %d\n", height);
    //relay
    DBG_PRINTF("   relay : %d\n", *p);
    p++;

    *pLen -= p - pData;
    return BC_PROTO_FIN;
}


/** 受信データ解析(verack)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 *
 * @note
 *          - verackを送信する
 */
static int ICACHE_FLASH_ATTR read_verack(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    DBG_PRINTF("  [verack]\n");

    send_verack(pConn);
    send_filterload(pConn);

#ifdef __XTENSA__
    //ESP8266は送信完了してからgetheadersし始める
    mStatus = 0;
#else
    //Linux版は送信が同期なので、ここで送ってしまう。
    uint8_t hash[BC_SZ_HASH256];

    bc_flash_get_last_bhash(hash);
    send_getheaders(pConn, hash);
#endif

    return BC_PROTO_FIN;
}


/** 受信データ解析(ping)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 *
 * @note
 *          - pongを送信する
 */
static int ICACHE_FLASH_ATTR read_ping(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    const uint8_t *p = pData;

    DBG_PRINTF("  [ping]\n");

    MEMCPY(&mPingNonce, p, sizeof(uint64_t));
    if (mpPayload == NULL) {
        //即時送信
        send_pong(pConn, mPingNonce);
    }
    else {
        //今の処理が終わってから送信
        mHasPing = 1;
    }

    //DBG_PRINTF("    LAST INV(LE)  : ");
    //for (int i = 0; i < BC_SZ_HASH256; i++) {
    //    DBG_PRINTF("%02x", mLastInvBhash[BC_SZ_HASH256 - i - 1]);
    //}
    //DBG_PRINTF("\n");
    //DBG_PRINTF("    LAST HEAD(LE) : ");
    //for (int i = 0; i < BC_SZ_HASH256; i++) {
    //    DBG_PRINTF("%02x", mLastHeadersBhash[BC_SZ_HASH256 - i - 1]);
    //}
    //DBG_PRINTF("\n");

    uint64_t nonce;
    p += get64(p, &nonce);

    *pLen -= p - pData;
    return BC_PROTO_FIN;
}


/** 受信データ解析(pong)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 */
static int ICACHE_FLASH_ATTR read_pong(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    const uint8_t *p = pData;

    DBG_PRINTF("  [pong]\n");
    uint64_t nonce;
    p += get64(p, &nonce);
    DBG_PRINTF("   nonce : %08x%08x\n", (uint32_t)(nonce >> 32), (uint32_t)(nonce & 0xffffffff));

    *pLen -= p - pData;
    return BC_PROTO_FIN;
}


#if 0
/** 受信データ解析(addr)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 */
static int ICACHE_FLASH_ATTR read_addr(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    const uint8_t *p = pData;

    int lp;
    int count;

    p += bc_misc_get_varint(p, &count);

    DBG_PRINTF("  [addr]\n");
    DBG_PRINTF("   count : %d\n", count);
    for (lp = 0; lp < count; lp++) {
        DBG_PRINTF("   addr_list[%4d] :\n", lp);
        //timestamp
        DBG_PRINTF("    timestamp : ");
        uint32_t timestamp;
        p += get32(p, &timestamp);
        print_time(timestamp);
        //addr
        DBG_PRINTF("    addr:\n");
        struct net_addr_t addr;
        p += get_netaddr(p, &addr);
        print_netaddr(&addr);
    }

    *pLen -= p - pData;
    return BC_PROTO_FIN;
}
#endif


/** 受信データ解析(inv)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 * 
 * @note        内部で#read_nbyte()を呼び出す
 */
static int ICACHE_FLASH_ATTR read_inv(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    static int sCount;                      //全部で回す回数(初回時に設定)
                                            //最大で50000(bitcoin仕様)

    int ret = BC_PROTO_FIN;
    const struct inv_t *pInv;

    DBG_PRINTF("  [inv]\n");

    if (*pLen == 0) {
        DBG_PRINTF("    ... more read inv_t = 0\n");
        return BC_PROTO_CONT;
    }

    //count
    if (sCount == 0) {
        //初回
        uint64_t val;
        int len = *pLen;
        ret = read_varint(pData, pLen, &val);
        if (ret == BC_PROTO_CONT) {
            *pLen = 0;
            //DBG_PRINTF("    ... more read inv count\n");
            return BC_PROTO_CONT;  //countが決まるまでは進まない
        }
        pData += len - *pLen;
        mProto.length -= len - *pLen;
        sCount = (int)val;
        DBG_PRINTF("   count : %d\n", sCount);
    }
    if (sCount == 0) {
        //あり得るのかわからないが、countが0だった場合はここで終わり
        return BC_PROTO_FIN;
    }

    //inv_t
    uint8_t *pPkt = read_nbyte(pData, pLen, sizeof(struct inv_t));
    if (pPkt == NULL) {
        *pLen = 0;
        //DBG_PRINTF("    ... more read inv_t\n");
        return BC_PROTO_CONT;  //inv_t不足
    }
    pInv = (const struct inv_t *)pPkt;
    print_inv(pInv);

    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;
    switch (pInv->type) {
    case INV_MSG_TX:
        //getdata
        if (mpPayload != NULL) {
            (*pProto->payload)++;
            MEMCPY(mpPayload, pInv, sizeof(struct inv_t));
            pProto->length += sizeof(struct inv_t);
            mpPayload += sizeof(struct inv_t);
            if (mpPayload - mBufferWPnt > SZ_SEND_BUF) {
                DBG_PRINTF("oops! Buffer full \n");
                HALT();
            }

            DBG_PRINTF("getdata - %d\n", *pProto->payload);
        }
        else {
            //getdataの準備
            set_header(pProto, kCMD_GETDATA);
            *pProto->payload = 1;     //1つ目
            mpPayload = pProto->payload + 1;        //var_int=1byte分
            MEMCPY(mpPayload, pInv, sizeof(struct inv_t));
            mpPayload += sizeof(struct inv_t);
            pProto->length = 1 + sizeof(struct inv_t);

            DBG_PRINTF("getdata - first\n");
        }
        break;
    case INV_MSG_BLOCK:
        //最後に通知されたBhash更新
        MEMCPY(mLastInvBhash, pInv->hash, BC_SZ_HASH256);
        break;
    }

    FREE(pPkt);
    sCount--;
    mProto.length -= sizeof(struct inv_t);
    if (mProto.length > 0) {
        //継続
        //DBG_PRINTF("    ... more read(%d)(%d) ...\n", sCount, mProto.length);
        ret = BC_PROTO_CONT;
    }
    else {
        //終了
        DBG_PRINTF("    ... end inv ...\n");
        sCount = 0;
        ret = BC_PROTO_FIN;

        //MSG_BLOCKがあるなら、次回のgetheaders負荷を減らすために更新
        if ((mStatus > 1) && (mLastInvBhash[BC_SZ_HASH256 - 1] != 0xff)) {
            //1はgetheaders中
            bc_flash_save_last_bhash(mLastInvBhash);
            mLastInvBhash[BC_SZ_HASH256 - 1] = 0xff;        //Bitcoinの仕様上、先頭は0x00のため
        }

        if (mpPayload != NULL) {
            //ここまでをgetdataする
            DBG_PRINTF("  *** send getdata[cnt:%d] ***\n", *pProto->payload);
            send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
            mpPayload = NULL;
        }
    }

    return ret;
}


/** 受信データ解析(block)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in]       Len         受信データ長
 * @return          処理バイト長
 */
static int ICACHE_FLASH_ATTR read_block(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    const uint8_t *p = pData;
    int lp;

    DBG_PRINTF("  [block]\n");
    struct headers_t head;
    p += get_headers(p, &head) - 1; //txn_countはこちらで処理する
    print_headers(&head);

    //tx
    int txn_count;
    p += bc_misc_get_varint(p, &txn_count);
    DBG_PRINTF("   txn_count : %d\n", txn_count);
    for (lp = 0; lp < txn_count; lp++) {
        DBG_PRINTF("%02x", *p);
        p++;
    }
    DBG_PRINTF("\n");

    *pLen -= p - pData;
    return BC_PROTO_FIN;
}


/** 受信データ解析(tx)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 */
static int ICACHE_FLASH_ATTR read_tx(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    const uint8_t *p = pData;
    uint8_t flg_pubkey = 0;
    uint8_t flg_bcaddr = 0;
    uint8_t flg_opret = 0;
    int lp;
    struct bc_flash_wlt_t wlt;
    struct bc_proto_tx proto_tx;

    DBG_PRINTF("  [tx]\n");

    bc_flash_get_bcaddr(&wlt);

    //check size
    BC_LEN_CHECK(mProto, *pLen);

    proto_tx.pTx = pData;
    proto_tx.Len = (uint16_t)mProto.length;

    //version
//int32_t version;
//get32(p, (uint32_t *)&version);
//DBG_PRINTF("   version : %d\n", version);
    p += sizeof(int32_t);
    //tx_in count
    int txn_in_count;
    p += bc_misc_get_varint(p, &txn_in_count);

    for (lp = 0; lp  < txn_in_count; lp++) {
        //previous_output
//DBG_PRINTF("     prev_output :");
//for (int i = 0; i < BC_SZ_HASH256; i++) {
//    DBG_PRINTF("%02x", *(p + BC_SZ_HASH256 - i -1));
//}
//DBG_PRINTF("\n");
        proto_tx.pPrevOutput = p;
        p += BC_SZ_HASH256;
        //  index
//uint32_t index;
//get32(p, &index);
//DBG_PRINTF("       index : %u\n", index);
        p += sizeof(uint32_t);
        //script length
        int scr_len;
        p += bc_misc_get_varint(p, &scr_len);
        if (scr_len > 255) {
            //[len+署名][len+公開鍵]
            DBG_PRINTF("     script length : %d\n", scr_len);
            goto func_end;
        }
        //signature script
        //  sign
        p += 1 + *p;        //1byte長
        //  pubkey
        if (*p != BC_SZ_PUBKEY) {
            //公開鍵長は33byte
            DBG_PRINTF("     pubkey length : %d\n", *p);
            goto func_end;
        }
        p++;
        if (MEMCMP(p, wlt.pubkey, BC_SZ_PUBKEY) == 0) {
            //公開鍵一致
            DBG_PRINTF("  match pubkey!\n");
            flg_pubkey = 1;
        }
        p += BC_SZ_PUBKEY;
////sequence
//uint32_t sequence;
//get32(p, &sequence);
//DBG_PRINTF("     sequence : %u\n", sequence);
        p += sizeof(uint32_t);
    }

    //tx_out count
    int txn_out_count;
    p += bc_misc_get_varint(p, &txn_out_count);
    if (txn_out_count < 2) {
        //outputは2以上
        DBG_PRINTF("    txn_out count : %d\n", txn_out_count);
        goto func_end;
    }
    //tx_out
    for (lp = 0; lp < txn_out_count; lp++) {
        //value
//uint64_t value;
//get64(p, &value);
//DBG_PRINTF("     value : %lld\n", value);
        p += sizeof(uint64_t);
        //pk_script length
        int pk_scr_len;
        p += bc_misc_get_varint(p, &pk_scr_len);
        if (pk_scr_len > 255) {
            DBG_PRINTF("     pk_script length : %d\n", pk_scr_len);
            goto func_end;
        }

        //signature script
//DBG_PRINTF("     pk_script[%d] : ", lp);
//for (int i = 0; i < pk_scr_len; i++) {
//    DBG_PRINTF("%02x", *(p + i));
//}
//DBG_PRINTF("\n");

        //OUTPUT1
        if ((lp == 0) && !flg_pubkey && (pk_scr_len >= 23)) {   //OP_DUP(1) OP_HASH160(1) LEN(1) BCADDR(20) 
            if ((p[0] != OP_DUP) && (p[1] != OP_HASH160) && (p[2] != (uint8_t)BC_SZ_HASH160)) {
                DBG_PRINTF("not [OP_DUP][OP_HASH160]\n");
                goto func_end;
            }
            if (MEMCMP(&p[3], wlt.bcaddr, sizeof(wlt.bcaddr)) == 0) {
                //output1のBitcoinアドレスが一致
                DBG_PRINTF("  match bcaddr!\n");
                flg_bcaddr = 1;
            }
            else {
                DBG_PRINTF("not match bcaddr\n");
                goto func_end;
            }
        }
        //OUTPUT2
        else if ((lp == 1) && (pk_scr_len >= 2)) {   //OP_RETURN(1) LEN(1)
            if (p[0] == OP_RETURN) {
                DBG_PRINTF("  detect OP_RETURN\n");
                flg_opret = 1;
                proto_tx.pOpReturn = p + 1;
            }
        }
        p += pk_scr_len;
    }
    //lock_time
//uint32_t lock_time;
//get32(p, &lock_time);
//DBG_PRINTF("    lock_time : %u\n", lock_time);
    //p += sizeof(uint32_t);

    if (flg_opret) {
        if (flg_pubkey) {
            //TX(a)
            bc_flash_update_txinfo(BC_FLASH_TYPE_TXA, &proto_tx);
        }
        else if (flg_bcaddr && (txn_in_count == 1)) {
            //TX(b)
            bc_flash_update_txinfo(BC_FLASH_TYPE_TXB, &proto_tx);
        }
        else {
            DBG_PRINTF("no match flags\n");
        }
    }
    else {
        DBG_PRINTF("no OP_RETURN\n");
    }

func_end:
    *pLen -= mProto.length;
    return BC_PROTO_FIN;
}


/** 受信データ解析(headers)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 * 
 * @note        内部で#read_nbyte()を呼び出す
 */
static int ICACHE_FLASH_ATTR read_headers(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    static int sCount = 0;                  //全部で回す回数(初回時に設定)
                                            //最大で2000(bitcoin仕様)
    static int sGetCnt;                     //続けてgetheadersするときのsCount

    int ret = BC_PROTO_FIN;

    if (*pLen == 0) {
        DBG_PRINTF("    ... more read headers_t = 0\n");
        return BC_PROTO_CONT;
    }

    //count
    if (sCount == 0) {
        //初回
        uint64_t val;
        int len = *pLen;

        //DBG_PRINTF("  [headers]");
        ret = read_varint(pData, pLen, &val);
        if (ret == BC_PROTO_CONT) {
            *pLen = 0;
            //DBG_PRINTF("    ... more read headers count\n");
            return BC_PROTO_CONT;  //countが決まるまでは進まない
        }
        pData += len - *pLen;
        mProto.length -= len - *pLen;
        sCount = (int)val;
        //DBG_PRINTF("   count : %d\n", sCount);

        if (sCount > 0) {
            //次にgetheadersするときのsCountを決める
            if (sCount >= GETDATA_NUM) {
                //送信バッファに収まらないため、件数を制限する
                sGetCnt = sCount - GETDATA_NUM;
            }
            else {
                //それ以外なら、最後のblock
                sGetCnt = 0;
            }
            //DBG_PRINTF("   get count : %d\n", sGetCnt);

            //getdataの準備
            struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;
            set_header(pProto, kCMD_GETDATA);
            mMerkleCnt = (uint8_t)(sCount - sGetCnt);
            mpPayload = pProto->payload;
            *mpPayload = mMerkleCnt;
            pProto->length = 1 + sizeof(struct inv_t) * (*mpPayload);
            mpPayload++;
        }
    }
    if (sCount == 0) {
        //countが0だった場合はここで終わり
        sGetCnt = -1;

//        DBG_PRINTF("    LAST HEAD(LE) : ");
//        for (int i = 0; i < BC_SZ_HASH256; i++) {
//            DBG_PRINTF("%02x", mLastHeadersBhash[BC_SZ_HASH256 - i - 1]);
//        }
//        DBG_PRINTF("\n");

        //最後にheadersで受信したblock hashを保存する
        if (mLastHeadersBhash[BC_SZ_HASH256 - 1] != 0xff) {
            //最新のBlock Hashで起動した場合、mLastHeadersBhash[]は未受信
            bc_flash_save_last_bhash(mLastHeadersBhash);
        }

        CMD_MBED_SEND(BC_MBED_CMD_PREPARED, BC_MBED_CMD_PREPARED_LEN);  //準備完了

        //全headersが終わったので、mempoolを受け付ける
        send_mempool(pConn);

        //2は起動時のgetheadersが終わった意味
        mStatus = 2;
        return BC_PROTO_FIN;
    }

    //headers_t
    uint8_t *pPkt = read_nbyte(pData, pLen, sizeof(struct headers_t));
    if (pPkt == NULL) {
        *pLen = 0;
        //DBG_PRINTF("    ... more read headers_t\n");
        DBG_PRINTF("_");
        return BC_PROTO_CONT;  //headers_t不足
    }

//    bc_misc_hash256(mLastHeadersBhash, pPkt, sizeof(struct headers_t) - 1);
//    DBG_PRINTF("[sCount=%d, sGetCnt=%d]\n", sCount, sGetCnt);
//    print_headers((const struct headers_t *)pPkt);
    sCount--;
    if (sCount >= sGetCnt) {
        //getdataにためる
        bc_misc_hash256(mLastHeadersBhash, pPkt, sizeof(struct headers_t) - 1); //block hash

        //もしBlock#1だったら、途中から開始したい
        if (MEMCMP(mLastHeadersBhash, kBhash1, BC_SZ_HASH256) == 0) {
            //Genesis!
            DBG_PRINTF("get #1 BHash!\n");
            int erase_ret = bc_flash_erase_last_bhash();
            if (erase_ret) {
                //再起動してやり直す
#ifdef __XTENSA__
                //FLASHの処理をせずに再起動
                system_os_post(TASK_PRIOR_MAIN, TASK_REQ_REBOOT, 1);
#endif  //__XTENSA__
                *pLen = 0;
                return BC_PROTO_FIN;
            }
        }

        //inv
        bc_misc_add(&mpPayload,  INV_MSG_FILTERED_BLOCK, sizeof(uint32_t));
        MEMCPY(mpPayload, mLastHeadersBhash, BC_SZ_HASH256);
        mpPayload += BC_SZ_HASH256;

        //print_headers((const struct headers_t *)pPkt);
        DBG_PRINTF("=");        //プログレスバー代わりのログ
    }
    else {
        DBG_PRINTF(".");        //プログレスバー代わりのログ
    }

    FREE(pPkt);
    mProto.length -= sizeof(struct headers_t);
    if (mProto.length > 0) {
        //継続
        //DBG_PRINTF("    ... more read(rest=%d)(%d) ...\n", sCount, mProto.length);
        ret = BC_PROTO_CONT;
    }
    else {
        //終了
//        DBG_PRINTF("    ... end headers\n");

        //ここまでをgetdataする
//        uint8_t *pRead = &mBufferWPnt[4 + 12 + 4 + 4 + 1];
//        for (int lp = 0; lp < mBufferWPnt[4 + 12 + 4 + 4]; lp++) {
//            DBG_PRINTF("\ngetdata[%2d]\n", lp);
//            print_inv((const struct inv_t *)pRead);
//            pRead += sizeof(struct inv_t);
//        }
        DBG_PRINTF("@@@ send getdata[cnt:%d] @@@\n", mBufferWPnt[4 + 12 + 4 + 4]);
        send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
        mpPayload = NULL;

        sCount = 0;
        ret = BC_PROTO_FIN;
    }

    return ret;
}


/** 受信データ解析(merkleblock)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 */
static int ICACHE_FLASH_ATTR read_merkleblock(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    int ret;

    //DBG_PRINTF("  [merkleblock]\n");

    //解析の必要あり？
    //今のところ感じていないので、ここで直接見てみる。
    //print_headers((const struct headers_t *)pData);

    if (mProto.length > *pLen) {
        //まだデータが来る
        DBG_PRINTF("m");
        mProto.length -= *pLen;
        *pLen = 0;
        ret = BC_PROTO_CONT;
        //DBG_PRINTF("   rest : %d\n", mProto.length);
    }
    else {
        //もうデータは来ない
        DBG_PRINTF("M");
        *pLen -= mProto.length;
        ret = BC_PROTO_FIN;
        //DBG_PRINTF("   rest : 0\n");
    }

    if (mMerkleCnt) {
        mMerkleCnt--;
        //DBG_PRINTF("  rest merkleblock : %d\n", mMerkleCnt);
        if (mMerkleCnt == 0) {
            //全部返ってきた --> 次のgetheaders
            send_getheaders(pConn, mLastHeadersBhash);
        }
    }

    return ret;
}


/** 受信データ解析(未処理)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       p           受信データ
 * @param[in,out]   pLen        [in]受信データ長, [out]処理サイズを引いたデータ長
 * @return          処理結果(BC_PROTO_FIN..解析完了, BC_PROTO_CONT..解析継続)
 */
static int ICACHE_FLASH_ATTR read_unknown(struct espconn *pConn, const uint8_t *pData, int *pLen)
{
    int ret;

    DBG_PRINTF("  [%s] read out\n", mProto.command);
    if (mProto.length > *pLen) {
        //まだデータが来る
        mProto.length -= *pLen;
        *pLen = 0;
        ret = BC_PROTO_CONT;
        DBG_PRINTF("   rest : %d\n", mProto.length);
    }
    else {
        //もうデータは来ない
        *pLen -= mProto.length;
        ret = BC_PROTO_FIN;
        DBG_PRINTF("   rest : 0\n");
    }
    return ret;
}


/** Bitcoinパケット送信(version)
 *
 * @param[in]       pConn       管理データ
 * @return          送信結果(0..OK)
 */
static int ICACHE_FLASH_ATTR send_version(struct espconn *pConn)
{
    DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;
    uint8_t *p = pProto->payload;

    set_header(pProto, kCMD_VERSION);

    //version
    bc_misc_add(&p, BC_PROTOCOL_VERSION, sizeof(int32_t));
    //services
    bc_misc_add(&p, 0, sizeof(uint64_t));
    //timestamp
    bc_misc_add(&p, get_current_time(), sizeof(sint64_t));
    //addr_recv
    add_netaddr(&p, 0, 127, 0, 0, 1, BC_PORT_TESTNET3);
    //addr_from
    add_netaddr(&p, 0, 127, 0, 0, 1, BC_PORT_TESTNET3);
    //nonce
    uint64_t rnd = rand();
    rnd <<= 32;
    rnd |= rand();
    bc_misc_add(&p, rnd, sizeof(uint64_t));
    //user_agent
    int ua_len = STRLEN(BC_VER_UA);
    add_varint(&p, ua_len);
    MEMCPY(p, BC_VER_UA, ua_len);
    p += ua_len;
    //start_height(0固定)
    bc_misc_add(&p, 0, sizeof(int32_t));
    //relay
    bc_misc_add(&p, 0, 1);

    //payload length
    pProto->length = p - pProto->payload;

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}


/** Bitcoinパケット送信(verack)
 *
 * @param[in]       pConn       管理データ
 * @return          送信結果(0..OK)
 */
static int ICACHE_FLASH_ATTR send_verack(struct espconn *pConn)
{
    DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;

    set_header(pProto, kCMD_VERACK);
    pProto->length = 0;

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}


#if 0
/** Bitcoinパケット送信(ping)
 *
 * @param[in]       pConn       管理データ
 * @return          送信結果(0..OK)
 */
static int ICACHE_FLASH_ATTR send_ping(struct espconn *pConn)
{
    DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;
    uint8_t *p = pProto->payload;

    set_header(pProto, kCMD_PING);
    //nonce
    uint64_t rnd = rand();
    rnd <<= 32;
    rnd |= rand();
    bc_misc_add(&p, rnd, sizeof(uint64_t));
    pProto->length = sizeof(uint64_t);

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}
#endif


/** Bitcoinパケット送信(pong)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       Nonce       送信するnonce(通常はpingと同じ値)
 * @return          送信結果(0..OK)
 */
static int ICACHE_FLASH_ATTR send_pong(struct espconn *pConn, uint64_t Nonce)
{
    DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;

    set_header(pProto, kCMD_PONG);
    pProto->length = 8;
    //nonce
    MEMCPY(pProto->payload, &Nonce, pProto->length);

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}


#if 0
/** Bitcoinパケット送信(getblocks)
 *
 * @param[in]       pConn       管理データ
 * @return          送信結果(0..OK)
 */
static int ICACHE_FLASH_ATTR send_getblocks(struct espconn *pConn, const uint8_t *pHash)
{
    DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;
    uint8_t *p = pProto->payload;

    set_header(pProto, kCMD_GETBLOCKS);

    //version
    bc_misc_add(&p, BC_PROTOCOL_VERSION, sizeof(int32_t));
    //hash count
    bc_misc_add(&p, 1, sizeof(uint8_t));        //varintだが1byte固定なので省略
    //block locator hashes
    MEMCPY(p, pHash, BC_SZ_HASH256);
    p += BC_SZ_HASH256;
    //hash_stop             : 最大数
    MEMSET(p, 0, BC_SZ_HASH256);
    p += BC_SZ_HASH256;

    //payload length
    pProto->length = p - pProto->payload;

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}
#endif


/** Bitcoinパケット送信(getheaders)
 *
 * @param[in]       pConn       管理データ
 * @return          送信結果(0..OK)
 */
static int ICACHE_FLASH_ATTR send_getheaders(struct espconn *pConn, const uint8_t *pHash)
{
    //DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;
    uint8_t *p = pProto->payload;

    set_header(pProto, kCMD_GETHEADERS);

    //version
    bc_misc_add(&p, BC_PROTOCOL_VERSION, sizeof(int32_t));
    //hash count
    bc_misc_add(&p, 1, sizeof(uint8_t));        //varintだが1byte固定なので省略
    //block locator hashes
    MEMCPY(p, pHash, BC_SZ_HASH256);
    p += BC_SZ_HASH256;
    //hash_stop             : 最大数
    MEMSET(p, 0, BC_SZ_HASH256);
    p += BC_SZ_HASH256;

    DBG_PRINTF("    hash(getheaders) : ");
    for (int i = 0; i < BC_SZ_HASH256; i++) {
        DBG_PRINTF("%02x", pHash[BC_SZ_HASH256 - i - 1]);
    }
    DBG_PRINTF("\n");

    //payload length
    pProto->length = p - pProto->payload;

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}


#if 0
/** Bitcoinパケット送信(getdata)
 *
 * @param[in]       pConn       管理データ
 * @param[in]       pInv        取得要求するINV(varint + inv_vect[])
 * @param[in]       Len         pInv長
 * @return          送信結果(0..OK)
 */
static int ICACHE_FLASH_ATTR send_getdata(struct espconn *pConn, const uint8_t *pInv, int Len)
{
    DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;

    set_header(pProto, kCMD_GETDATA);

    //inv
    pProto->length = Len;
    MEMCPY(pProto->payload, pInv, pProto->length);

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}
#endif


/** Bitcoinパケット送信(filterload)
 *
 * @param[in]       pConn       管理データ
 * @return          送信結果(0..OK)
 */
static int ICACHE_FLASH_ATTR send_filterload(struct espconn *pConn)
{
    DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;
    struct bc_flash_wlt_t wlt;

    bc_flash_get_bcaddr(&wlt);

    set_header(pProto, kCMD_FILTERLOAD);

    struct bloom bloom;
    bloom_init(&bloom, BLOOM_ELEMENTS, BLOOM_RATE, BLOOM_TWEAK);
    bloom_insert(&bloom, wlt.pubkey, sizeof(wlt.pubkey));
    bloom_insert(&bloom, wlt.bcaddr, sizeof(wlt.bcaddr));

    //filter
    uint8_t *p = pProto->payload;
    add_varint(&p, bloom.vData->len);
    MEMCPY(p, bloom.vData->str, bloom.vData->len);
    p += bloom.vData->len;
    bloom_free(&bloom);
    //nHashFuncs
    bc_misc_add(&p, bloom.nHashFuncs, sizeof(uint32_t));
    //nTweak
    bc_misc_add(&p, bloom.nTweak, sizeof(uint32_t));
    //nFlags
    //  0: BLOOM_UPDATE_NONE means the filter is not adjusted when a match is found.
    //  1: BLOOM_UPDATE_ALL  means if the filter matches any data element in a scriptPubKey the outpoint is serialized and inserted into the filter.
    //  2: BLOOM_UPDATE_P2PUBKEY_ONLY means the outpoint is inserted into the filter only if a data element in the scriptPubKey is matched, and that script is of the standard "pay to pubkey" or "pay to multisig" forms.
    bc_misc_add(&p, BLOOM_UPDATE_ALL, sizeof(uint8_t));

    //payload length
    pProto->length = p - pProto->payload;

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}


static int ICACHE_FLASH_ATTR send_mempool(struct espconn *pConn)
{
    DBG_FUNCNAME();

    int ret;
    struct bc_proto_t *pProto = (struct bc_proto_t *)mBufferWPnt;

    set_header(pProto, kCMD_MEMPOOL);
    pProto->length = 0;

    ret = send_data(pConn, (struct bc_proto_t *)mBufferWPnt);
    return ret;
}
