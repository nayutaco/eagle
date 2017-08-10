#include <stdlib.h>     //srand()
#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"

#include "user_interface.h"
#include "user_config.h"
#include "espconn.h"

#include "bc_misc.h"
#include "bc_proto.h"
#include "bc_flash.h"

#include "ntp/ntp.h"

//bitcoind
#define MY_HOST     "some.host.name"
#define MY_HOST1        aa
#define MY_HOST2        bb
#define MY_HOST3        cc
#define MY_HOST4        dd

#define PORT            (18333)         //bitcoin testnet3

enum Status_t {
    ST_INIT,                ///< 初期状態
    ST_WIFI_CONNECTING,     ///< WiFi接続要求中
    ST_WIFI_CONNECTED,      ///< WiFi接続済み
    ST_DHCP_RESOLVED,       ///< IPアドレス取得
    ST_DNS_RESOLVING,       ///< 名前解決中
    ST_DNS_RESOLVED,        ///< 名前解決済み
    ST_TCP_CONNECTING,      ///< TCP接続要求中
    ST_TCP_CONNECTED,       ///< TCP接続済み
    ST_MBED_HANDSHAKING,    ///< mbed通信開始中
    ST_BITCOIN,             ///< Bitcoin動作中
    ST_TCP_DISCONNECT,      ///< TCP切断中
};

static const char M_SSID[] = MY_SSID;
static const char M_PASSWD[] = MY_PASSWD;

#define M_SZ_QUEUE      (3)
static os_event_t queue[M_SZ_QUEUE];
static void ICACHE_FLASH_ATTR event_handler(os_event_t *pEvent);


static struct espconn mConn;
static ip_addr_t mDnsIp;
static esp_tcp mTcp;
static enum Status_t mStatus = ST_INIT;

//////////////////////////////////////

static void ICACHE_FLASH_ATTR wifi_eventcb(System_Event_t *evt);
//static void ICACHE_FLASH_ATTR dns_donecb(const char *pName, ip_addr_t *pIpAddr, void *pArg);
static void ICACHE_FLASH_ATTR tcp_connectedcb(void *pArg);
static void ICACHE_FLASH_ATTR tcp_disconnectedcb(void *pArg);
static void ICACHE_FLASH_ATTR data_sentcb(void *pArg);
static void ICACHE_FLASH_ATTR data_receivedcb(void *pArg, char *pData, unsigned short Len);

static void ICACHE_FLASH_ATTR event_handler(os_event_t *pEvent);


////////////////////////////////////////////////////////////////
// public
////////////////////////////////////////////////////////////////

/******************************************************************************
 * FunctionName : uart1_tx_one_char
 * Description  : Internal used function
 *                Use uart1 interface to transfer one char
 * Parameters   : uint8 TxChar - character to tx
 * Returns      : OK
*******************************************************************************/
static void ICACHE_FLASH_ATTR uart1_tx_one_char(uint8 TxChar)
{
    while (READ_PERI_REG(UART_STATUS(UART1)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S)) {
    }

    WRITE_PERI_REG(UART_FIFO(UART1) , TxChar);
}

/******************************************************************************
 * FunctionName : uart1_write_char
 * Description  : Internal used function
 *                Do some special deal while tx char is '\r' or '\n'
 * Parameters   : char c - character to tx
 * Returns      : NONE
*******************************************************************************/
static void ICACHE_FLASH_ATTR uart1_write_char(char c)
{
    if (c == '\n') {
        uart1_tx_one_char('\r');
        uart1_tx_one_char('\n');
    } else if (c == '\r') {
    } else {
        uart1_tx_one_char(c);
    }
}


void user_rf_pre_init(void)
{
}


/** エントリポイント
 *
 *
 */
void user_init(void)
{
    uart_init(BIT_RATE_115200, BIT_RATE_115200);
    //出力をUART1に変更
    os_install_putc1((void *)uart1_write_char);
    DBG_PRINTF("\nReady.\n");

    struct rst_info *pInfo = system_get_rst_info();
    switch (pInfo->reason) {
    case REASON_DEFAULT_RST:
        os_printf("  REASON_DEFAULT_RST\n");
        break;
    case REASON_WDT_RST:
        os_printf("  REASON_WDT_RST\n");
        break;
    case REASON_EXCEPTION_RST:
        os_printf("  REASON_EXCEPTION_RST\n");
        break;
    case REASON_SOFT_WDT_RST:
        os_printf("  REASON_SOFT_WDT_RST\n");
        break;
    case REASON_SOFT_RESTART:
        os_printf("  REASON_SOFT_RESTART\n");
        break;
    case REASON_DEEP_SLEEP_AWAKE:
        os_printf("  REASON_DEEP_SLEEP_AWAKE\n");
        break;
    case REASON_EXT_SYS_RST:
        os_printf("  REASON_EXT_SYS_RST\n");
        break;
    default:
        os_printf("  unknown reason : %x\n", pInfo->reason);
        break;
    }

    //system log output : OFF
    //これをすると、自分のos_printfも止まってしまう。
    //即座に止めるのか、上のログも"REASON_"くらいで終わってしまっていた。
    //system_set_os_print(0);

    //os_printf()をUART1に割り振る(os_install_putc1)、というのもあるし、
    //UART0のピンを変更し、RX/TX→MTCK/MTDOにする(system_uart_swap)、というのもある。
    //しかし、システムが出力するログだけを停止する、というのはないようだ。

    //system_set_os_print()でログを停止させて、
    //自分で文字列を加工して、uart0_tx_buffer()でUART0に直接送信する方が確実か。
    //しかし、ここでuart0_tx_buffer()を使うと、REASONのログも正しく出力されなくなった。
    //出力されなくなったと言うよりは、ゴミになったというべきか。
    //system_set_os_print()をコメントアウトしても正しく出力されないので、os_printf()との相性が悪いのか。

    //タスク追加
    system_os_task(event_handler, TASK_PRIOR_MAIN, queue, M_SZ_QUEUE);

    //WiFi接続開始
    system_os_post(TASK_PRIOR_MAIN, TASK_REQ_WIFI_CONNECT, 0);
}


////////////////////////////////////////////////////////////////
// private
////////////////////////////////////////////////////////////////


/** WiFiイベントハンドラ
 *
 * @param[in]   evt     WiFiイベント
 */
static void ICACHE_FLASH_ATTR wifi_eventcb(System_Event_t *evt)
{
    DBG_FUNCNAME();

    DBG_PRINTF("evt->event : %x\n", evt->event);

    uint8_t req;

    switch (evt->event) {
    case EVENT_STAMODE_CONNECTED:
        DBG_PRINTF("[CONN] SSID[%s] CH[%d]\n", evt->event_info.connected.ssid, evt->event_info.connected.channel);
        req = TASK_REQ_IGNORE;
        mStatus = ST_WIFI_CONNECTED;
        break;
    case EVENT_STAMODE_DISCONNECTED:
        /*
            disconnected.reason
                REASON_UNSPECIFIED              = 1,
                REASON_AUTH_EXPIRE              = 2,
                REASON_AUTH_LEAVE               = 3,
                REASON_ASSOC_EXPIRE             = 4,
                REASON_ASSOC_TOOMANY            = 5,
                REASON_NOT_AUTHED               = 6,
                REASON_NOT_ASSOCED              = 7,
                REASON_ASSOC_LEAVE              = 8,
                REASON_ASSOC_NOT_AUTHED         = 9,
                REASON_DISASSOC_PWRCAP_BAD      = 10
                REASON_DISASSOC_SUPCHAN_BAD     = 11
                REASON_IE_INVALID               = 13
                REASON_MIC_FAILURE              = 14
                REASON_4WAY_HANDSHAKE_TIMEOUT   = 15
                REASON_GROUP_KEY_UPDATE_TIMEOUT = 16
                REASON_IE_IN_4WAY_DIFFERS       = 17
                REASON_GROUP_CIPHER_INVALID     = 18
                REASON_PAIRWISE_CIPHER_INVALID  = 19
                REASON_AKMP_INVALID             = 20
                REASON_UNSUPP_RSN_IE_VERSION    = 21
                REASON_INVALID_RSN_IE_CAP       = 22
                REASON_802_1X_AUTH_FAILED       = 23
                REASON_CIPHER_SUITE_REJECTED    = 24
         */
        DBG_PRINTF("[DISC] SSID[%s] REASON[%d]\n", evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
        req = TASK_REQ_TCP_RECONNECT;
        mStatus = ST_INIT;
        break;
    case EVENT_STAMODE_AUTHMODE_CHANGE:
        DBG_PRINTF("[CHG_AUTH]\n");
        req = TASK_REQ_IGNORE;
        break;
    case EVENT_STAMODE_GOT_IP:
        DBG_PRINTF("[GOT_IP] IP[" IPSTR "] MASK[" IPSTR "] GW[" IPSTR "]\n",
                        IP2STR(&evt->event_info.got_ip.ip),
                        IP2STR(&evt->event_info.got_ip.mask),
                        IP2STR(&evt->event_info.got_ip.gw));

        req = TASK_REQ_DNS_RESOLVE;
        mStatus = ST_DHCP_RESOLVED;
        break;
    case EVENT_STAMODE_DHCP_TIMEOUT:
        DBG_PRINTF("[DHCP timeout]\n");
        req = TASK_REQ_REBOOT;
        break;
    default:
        req = TASK_REQ_IGNORE;
        break;
    }

    system_os_post(TASK_PRIOR_MAIN, req, 0);
}


#if 0
/** 名前解決完了コールバック
 *
 * @param[in]   pName
 * @param[in]   pIpAddr
 * @param[in]   pArg            struct espconn
 */
static void ICACHE_FLASH_ATTR dns_donecb(const char *pName, ip_addr_t *pIpAddr, void *pArg)
{
    struct espconn *pConn = (struct espconn *)pArg;

    DBG_FUNCNAME();

    if (pConn == NULL) {
        DBG_PRINTF("DNS lookup fail.\n");
        return;
    }

    mStatus = ST_DNS_RESOLVED;

    system_os_post(TASK_PRIOR_MAIN, TASK_REQ_TCP_CONNECT, 0);
}
#endif


/** TCP接続完了コールバック
 *
 * @param[in]   pArg            struct espconn
 */
static void ICACHE_FLASH_ATTR tcp_connectedcb(void *pArg)
{
    struct espconn *pConn = (struct espconn *)pArg;

    DBG_FUNCNAME();

    espconn_set_opt(pConn, (enum espconn_option)(ESPCONN_REUSEADDR | ESPCONN_NODELAY));

    mStatus = ST_TCP_CONNECTED;

    system_os_post(TASK_PRIOR_MAIN, TASK_REQ_MBED_HANDSHAKE, 0);
}


/** TCP切断完了コールバック
 *
 * @param[in]   pArg            struct espconn
 */
static void ICACHE_FLASH_ATTR tcp_disconnectedcb(void *pArg)
{
    DBG_FUNCNAME();

    mStatus = ST_TCP_DISCONNECT;

    system_os_post(TASK_PRIOR_MAIN, TASK_REQ_TCP_RECONNECT, 0);
}


/** 送信完了コールバック
 *
 * @param[in]   pArg            struct espconn
 */
static void ICACHE_FLASH_ATTR data_sentcb(void *pArg)
{
    struct espconn *pConn = (struct espconn *)pArg;
    struct espconn_packet infoarg;

    //DBG_FUNCNAME();

    espconn_get_packet_info(pConn, &infoarg);
    //DBG_PRINTF("[%s()] sent_length=%d\n", __func__, infoarg.sent_length);
    bc_sent(pConn, infoarg.sent_length);
}


/** 受信完了コールバック
 *
 * @param[in]   pArg            struct espconn
 * @param[in]   pData           受信データ
 * @param[in]   Len             受信データ長
 */
static void ICACHE_FLASH_ATTR data_receivedcb(void *pArg, char *pData, unsigned short Len)
{
//    DBG_PRINTF("[[ data_receivedcb : Len=%d ]]\n", Len);
    DBG_PRINTF("%%");

//    DBG_PRINTF("----------\n");
    int lp;
//    for (lp = 0; lp < Len; lp++) {
//        DBG_PRINTF("%02x ", pData[lp]);
//    }
    int sz = (int)Len;
    while (sz > 0) {
//        DBG_PRINTF("------ sz : %d\n", sz);
        int prev_sz = sz;
        bc_read_message(&mConn, (uint8_t *)pData, &sz);
        pData += prev_sz - sz;
    }
//    DBG_PRINTF("-------data_receivedcb fin---\n");
    DBG_PRINTF("~\n");
}


/** NTP完了コールバック
 *
 * @param[in]   epoch       取得時間(Epoch Time)
 */
static void ntpcb(uint32_t epoch)
{
    bc_misc_time_start(epoch);
    uint32_t timestamp = bc_misc_time_get();
    DBG_PRINTF("NTP : %u\n", timestamp);
}


/** イベントハンドラ
 *
 * system_os_post(TASK_PRIOR_MAIN, req, param)で処理要求を受け付ける。
 *
 * @param[in]   pEvent      イベント
 */
static void ICACHE_FLASH_ATTR event_handler(os_event_t *pEvent)
{
    switch (pEvent->sig) {
    case TASK_REQ_WIFI_CONNECT:
        {
            struct station_config stconf;

            os_memcpy(stconf.ssid, M_SSID, sizeof(M_SSID));
            os_memcpy(stconf.password, M_PASSWD, sizeof(M_PASSWD));
            stconf.bssid_set = 0;

            wifi_set_opmode(STATION_MODE);
            //wifi_set_opmode(STATIONAP_MODE);
            wifi_station_set_auto_connect(1);
            wifi_station_set_config(&stconf);
            wifi_set_event_handler_cb(wifi_eventcb);
        }
        mStatus = ST_WIFI_CONNECTING;
        break;

    case TASK_REQ_DNS_RESOLVE:
#if 0
        err = espconn_gethostbyname(&mConn, MY_HOST, &mDnsIp, dns_donecb);
        if ((err != ESPCONN_OK) && (err != ESPCONN_INPROGRESS)) {
            DBG_PRINTF("espconn_gethostbyname fail : %d\n", err);
        }
        mStatus = ST_DNS_RESOLVING;
#else
        //IP直接
        IP4_ADDR(&mDnsIp, MY_HOST1, MY_HOST2, MY_HOST3, MY_HOST4);
        mStatus = ST_DNS_RESOLVED;
        system_os_post(TASK_PRIOR_MAIN, TASK_REQ_TCP_CONNECT, 0);
#endif
        break;

    case TASK_REQ_TCP_CONNECT:
        mConn.type = ESPCONN_TCP;
        mConn.state = ESPCONN_NONE;
        mConn.proto.tcp = &mTcp;
        mConn.proto.tcp->local_port = espconn_port();
        mConn.proto.tcp->remote_port = PORT;
        os_memcpy(mConn.proto.tcp->remote_ip, &mDnsIp.addr, 4);

        espconn_regist_connectcb(&mConn, tcp_connectedcb);
        espconn_regist_disconcb(&mConn, tcp_disconnectedcb);
        espconn_connect(&mConn);
        mStatus = ST_TCP_CONNECTING;
        break;

    case TASK_REQ_MBED_HANDSHAKE:
        DBG_PRINTF("connected IP[" IPSTR "]\n", IP2STR(mConn.proto.tcp->remote_ip));

        //TODO:バッファ確認用
        {
            struct espconn_packet infoarg;
            espconn_get_packet_info(&mConn, &infoarg);
            DBG_PRINTF("@@@ info @@@\n");
            DBG_PRINTF("sent_length : %u\n", infoarg.sent_length);
            DBG_PRINTF("snd_buf_size : %u\n", infoarg.snd_buf_size);
            DBG_PRINTF("snd_queuelen : %u\n", infoarg.snd_queuelen);
            DBG_PRINTF("total_queuelen : %u\n", infoarg.total_queuelen);
            DBG_PRINTF("packseqno : %lu\n", infoarg.packseqno);
            DBG_PRINTF("packseq_nxt : %lu\n", infoarg.packseq_nxt);
            DBG_PRINTF("packnum : %lu\n\n", infoarg.packnum);

            //TODO:ほどほどに乱数のようなので、これを使ってみる
            srand(infoarg.packseq_nxt);
        }

        // NTP
        system_os_post(TASK_PRIOR_MAIN, TASK_REQ_NTP, 0);

        // mbed Handshake start
        DBG_PRINTF("\nwait mbed ...\n");
        mStatus = ST_MBED_HANDSHAKING;
        CMD_MBED_SEND(BC_MBED_CMD_STARTED, BC_MBED_CMD_STARTED_LEN);    //起動完了
        break;

    case TASK_REQ_MBED_ACK:     //uart.cより
        {
            int result = (int)pEvent->par;
            switch (result) {
            case BC_FLASH_WRT_DONE:
                DBG_PRINTF("NEW BcAddr\n");
                break;
            case BC_FLASH_WRT_IGNORE:
                DBG_PRINTF("BcAddr not changed.\n");
                break;
            case BC_FLASH_WRT_FAIL:
            default:
                DBG_PRINTF("FLASH fail.\n");
                HALT();
                return;
            }
        }
        DBG_PRINTF("\nmbed OK\n");
        system_os_post(TASK_PRIOR_MAIN, TASK_REQ_BC_START, 0);
        break;

    case TASK_REQ_BC_START:
        if (bc_misc_time_get() == BC_TIME_INVALID) {
            //NTPがまだ終わっていないなら、待つ
            os_delay_us(10000);
            system_os_post(TASK_PRIOR_MAIN, TASK_REQ_BC_START, 0);
            break;
        }

        espconn_regist_recvcb(&mConn, data_receivedcb);
        espconn_regist_sentcb(&mConn, data_sentcb);

        err_t err = bc_start(&mConn);
        if (err == 0) {
            mStatus = ST_BITCOIN;
        }
        else {
            DBG_PRINTF("bc_start fail: %d\n", err);
            system_os_post(TASK_PRIOR_MAIN, TASK_REQ_REBOOT, 0);
        }
        break;

    case TASK_REQ_TCP_RECONNECT:
#if 0
        //TODO: 再起動
        DBG_PRINTF("recoonect --> REBOOT\n");
        system_os_post(TASK_PRIOR_MAIN, TASK_REQ_REBOOT, 0);
#else
        //TODO: 1秒待って接続
        DBG_PRINTF("recoonect --> CONNECT\n");
        bc_finish();
        for (int lp = 0; lp < 100; lp++) {
            os_delay_us(10000);     //10msec
        }
        system_os_post(TASK_PRIOR_MAIN, TASK_REQ_TCP_CONNECT, 0);
#endif
        break;

    case TASK_REQ_NTP:
        start_ntp(ntpcb);
        break;

    case TASK_REQ_REBOOT:
        //再起動
        DBG_PRINTF("*** RESATART ***\n");
        if (pEvent->par == 0) {
            bc_finish();
        }
        CMD_MBED_SEND(BC_MBED_CMD_REBOOT, BC_MBED_CMD_REBOOT_LEN);  //reboot
        os_delay_us(10000);
        system_restart();
        while (1) { }
        break;

    case TASK_REQ_DATA_ERASE:
        //データ消去
        bc_flash_erase_txinfo();
        if (pEvent->par == 1) {
            //リセットも行う
            system_os_post(TASK_PRIOR_MAIN, TASK_REQ_REBOOT, 1);    //bc_finish()しない
        }
        break;

    case TASK_REQ_IGNORE:
        //do nothing
        break;

    default:
        DBG_PRINTF("unknown cmd=%d, status=%d\n", pEvent->sig, mStatus);
        HALT();

    }
}
