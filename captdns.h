#ifndef CAPTDNS_H
#define CAPTDNS_H

#include "FreeRTOS.h"
#include "freertos/task.h"

class CaptiveDns
{
private:
    typedef struct __attribute__ ((packed))
    {
        uint16_t id;
        uint8_t flags;
        uint8_t rcode;
        uint16_t qdcount;
        uint16_t ancount;
        uint16_t nscount;
        uint16_t arcount;
    } DnsHeader;


    typedef struct __attribute__ ((packed))
    {
        uint8_t len;
        uint8_t data;
    } DnsLabel;


    typedef struct __attribute__ ((packed))
    {
        //before: label
        uint16_t type;
        uint16_t dnsClass;
    } DnsQuestionFooter;


    typedef struct __attribute__ ((packed))
    {
        //before: label
        uint16_t type;
        uint16_t dnsClass;
        uint32_t ttl;
        uint16_t rdlength;
        //after: rdata
    } DnsResourceFooter;

    typedef struct __attribute__ ((packed))
    {
        uint16_t prio;
        uint16_t weight;
    } DnsUriHdr;

public:
    CaptiveDns() = default;

    void start();
    void stop();

private:
    static void taskThunk(void *data);
    void captdnsTask();
    void captdnsRecv(struct sockaddr_in *premote_addr, char *pusrdata, unsigned short length);
    static void setn16(void *pp, int16_t n);
    static void setn32(void *pp, int32_t n);
    static uint16_t my_ntohs(uint16_t *in);
    static char *labelToStr(char *packet, char *labelPtr, int packetSz, char *res, int resMaxLen);
    static char *strToLabel(char *str, char *label, int maxLen);


    TaskHandle_t m_handle;
    volatile bool m_run = true;
    int m_socket = -1;
};

#endif