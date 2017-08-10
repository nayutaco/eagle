//////////////////////////////////////////////////
// Simple NTP client for ESP8266.
// Copyright 2015 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.
//////////////////////////////////////////////////

#include "bc_misc.h"
//#include "c_types.h"
//#include "user_interface.h"
#include "espconn.h"
//#include "osapi.h"
//#include "mem.h"
#include <time.h>

#include "ntp/ntp.h"


static os_timer_t ntp_timeout;
static struct espconn *pCon = 0;
static FuncNtpCb_t pFunc = 0;
static ip_addr_t mDnsIp;

static void ICACHE_FLASH_ATTR ntp_udp_timeout(void *arg)
{
    os_timer_disarm(&ntp_timeout);
    DBG_PRINTF("ntp timout\n");

    // clean up connection
    if (pCon) {
        espconn_delete(pCon);
        os_free(pCon->proto.udp);
        os_free(pCon);
        pCon = 0;
    }

    FuncNtpCb_t f = pFunc;
    pFunc = 0;
    if (f) {
        (*f)(0);
    }
}

static void ICACHE_FLASH_ATTR ntp_udp_recv(void *arg, char *pdata, unsigned short len)
{
//    struct tm *dt;
//    time_t timestamp;
    ntp_t *ntp;
    uint32_t timestamp;

    os_timer_disarm(&ntp_timeout);

    // extract ntp time
    ntp = (ntp_t*)pdata;
    timestamp = ntp->trans_time[0] << 24 | ntp->trans_time[1] << 16 |ntp->trans_time[2] << 8 | ntp->trans_time[3];
    // convert to unix time
    timestamp -= 2208988800UL;
    DBG_PRINTF("ntp : %lu\n", timestamp);
//    // create tm struct
//    dt = gmtime(&timestamp);
//
//    // do something with it, like setting an rtc
//    //ds1307_setTime(dt);
//    // or just print it out
//    char timestr[11];
//    DBG_PRINTF(timestr, "%02d:%02d:%02d\n", dt->tm_hour, dt->tm_min, dt->tm_sec);

    // clean up connection
    if (pCon) {
        espconn_delete(pCon);
        os_free(pCon->proto.udp);
        os_free(pCon);
        pCon = 0;
    }

    FuncNtpCb_t f = pFunc;
    pFunc = 0;
    if (f) {
        (*f)(timestamp);
    }
}


static void ICACHE_FLASH_ATTR dns_ntpdonecb(const char *pName, ip_addr_t *pIpAddr, void *pArg)
{
    ntp_t ntp;

    if (pArg == NULL) {
        DBG_PRINTF("DNS lookup fail.\n");
        if (pFunc) {
            (*pFunc)(0);
        }
        return;
    }

    // set up the udp "connection"
    pCon->type = ESPCONN_UDP;
    pCon->state = ESPCONN_NONE;
    pCon->proto.udp = (esp_udp*)os_zalloc(sizeof(esp_udp));
    pCon->proto.udp->local_port = espconn_port();
    pCon->proto.udp->remote_port = 123;
    os_memcpy(pCon->proto.udp->remote_ip, &pIpAddr->addr, 4);

    // create a really simple ntp request packet
    os_memset(&ntp, 0, sizeof(ntp_t));
    ntp.options = 0b00100011; // leap = 0, version = 4, mode = 3 (client)

    // set timeout timer
    os_timer_disarm(&ntp_timeout);
    os_timer_setfn(&ntp_timeout, (os_timer_func_t*)ntp_udp_timeout, pCon);
    os_timer_arm(&ntp_timeout, NTP_TIMEOUT_MS, 0);

    // send the ntp request
    espconn_create(pCon);
    espconn_regist_recvcb(pCon, ntp_udp_recv);
    espconn_sent(pCon, (uint8*)&ntp, sizeof(ntp_t));
}


err_t ICACHE_FLASH_ATTR start_ntp(FuncNtpCb_t func)
{
    err_t err;

    pCon = (struct espconn*)os_zalloc(sizeof(struct espconn));
    err = espconn_gethostbyname(pCon, "ntp.nict.jp", &mDnsIp, dns_ntpdonecb);
    if ((err != ESPCONN_OK) && (err != ESPCONN_INPROGRESS)) {
        DBG_PRINTF("espconn_gethostbyname fail : %d\n", err);
    }

    pFunc = func;

    return err;
}
