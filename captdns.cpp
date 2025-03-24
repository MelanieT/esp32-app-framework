/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain
 * this notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 *
 * modified for ESP32 by Cornelis
 *
 * ----------------------------------------------------------------------------
 */


/*
This is a 'captive portal' DNS server: it basically replies with a fixed IP (in this case:
the one of the SoftAP interface of this ESP module) for any and all DNS queries. This can
be used to send mobile phones, tablets etc which connect to the ESP in AP mode directly to
the internal webserver.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "cstring"
#include "esp_netif.h"
#include "captdns.h"

#define DNS_LEN 512

#define FLAG_QR (1<<7)
#define FLAG_AA (1<<2)
#define FLAG_TC (1<<1)
#define FLAG_RD (1<<0)

#define QTYPE_A  1
#define QTYPE_NS 2
#define QTYPE_CNAME 5
#define QTYPE_SOA 6
#define QTYPE_WKS 11
#define QTYPE_PTR 12
#define QTYPE_HINFO 13
#define QTYPE_MINFO 14
#define QTYPE_MX 15
#define QTYPE_TXT 16
#define QTYPE_URI 256

#define QdnsClass_IN 1
#define QdnsClass_ANY 255
#define QdnsClass_URI 256

//Function to put unaligned 16-bit network values
void CaptiveDns::setn16(void *pp, int16_t n)
{
    char *p = (char *)pp;
    *p++ = (char)(n >> 8);
    *p = (char)(n & 0xff);
}

//Function to put unaligned 32-bit network values
void CaptiveDns::setn32(void *pp, int32_t n)
{
    char *p = (char *)pp;
    *p++ = (char)((n >> 24) & 0xff);
    *p++ = (char)((n >> 16) & 0xff);
    *p++ = (char)((n >> 8) & 0xff);
    *p = (char)((n & 0xff));
}

uint16_t CaptiveDns::my_ntohs(uint16_t *in)
{
    char *p = (char *) in;
    return ((p[0] << 8) & 0xff00) | (p[1] & 0xff);
}

//Parses a label into a C-string containing a dotted
//Returns pointer to start of next fields in packet
char *CaptiveDns::labelToStr(char *packet, char *labelPtr, int packetSz, char *res, int resMaxLen)
{
    int i, k;
    char j;
    char *endPtr = nullptr;
    i = 0;
    do
    {
        if ((*labelPtr & 0xC0) == 0)
        {
            j = (*labelPtr++); //skip past length
            //Add separator period if there already is data in res
            if (i < resMaxLen && i != 0)
                res[i++] = '.';
            //Copy label to res
            for (k = 0; k < j; k++)
            {
                if ((labelPtr - packet) > packetSz)
                    return nullptr;
                if (i < resMaxLen)
                    res[i++] = *labelPtr++;
            }
        }
        else if ((*labelPtr & 0xC0) == 0xC0)
        {
            //Compressed label pointer
            endPtr = labelPtr + 2;
            int offset = my_ntohs(((uint16_t *) labelPtr)) & 0x3FFF;
            //Check if offset points to somewhere outside of the packet
            if (offset > packetSz)
                return nullptr;
            labelPtr = &packet[offset];
        }
        //check for out-of-bound-ness
        if ((labelPtr - packet) > packetSz)
            return nullptr;
    } while (*labelPtr != 0);
    res[i] = 0; //zero-terminate
    if (endPtr == nullptr)
        endPtr = labelPtr + 1;
    return endPtr;
}

//Converts a dotted hostname to the weird label form dns uses.
char *CaptiveDns::strToLabel(char *str, char *label, int maxLen)
{
    char *len = label; //ptr to len byte
    char *p = label + 1; //ptr to next label byte to be written
    while (true)
    {
        if (*str == '.' || *str == 0)
        {
            *len = (char)((p - len) - 1);    //write len of label bit
            len = p;                //pos of len for next part
            p++;                //data ptr is one past len
            if (*str == 0)
                break;    //done
            str++;
        }
        else
        {
            *p++ = *str++;    //copy byte
            //if ((p-label)>maxLen) return nullptr;	//check out of bounds
        }
    }
    *len = 0;
    return p; //ptr to first free byte in resp
}

//Receive a DNS packet and maybe send a response back
void CaptiveDns::captdnsRecv(struct sockaddr_in *premote_addr, char *pusrdata, unsigned short length)
{

    char buff[DNS_LEN];
    char reply[DNS_LEN];
    int i;
    char *rend = &reply[length];
    char *p = pusrdata;
    auto *hdr = (DnsHeader *) p;
    auto *rhdr = (DnsHeader *) &reply[0];
    p += sizeof(DnsHeader);
    //	printf("DNS packet: id 0x%X flags 0x%X rcode 0x%X qcnt %d ancnt %d nscount %d arcount %d len %d\n",
    //		my_ntohs(&hdr->id), hdr->flags, hdr->rcode, my_ntohs(&hdr->qdcount), my_ntohs(&hdr->ancount), my_ntohs(&hdr->nscount), my_ntohs(&hdr->arcount), length);
    //Some sanity checks:
    if (length > DNS_LEN)
        return;                                //Packet is longer than DNS implementation allows
    if (length < sizeof(DnsHeader))
        return;                        //Packet is too short
    if (hdr->ancount || hdr->nscount || hdr->arcount)
        return;    //this is a reply, don't know what to do with it
    if (hdr->flags & FLAG_TC)
        return;                                //truncated, can't use this
    //Reply is basically the request plus the needed data
    memcpy(reply, pusrdata, length);
    rhdr->flags |= FLAG_QR;

    for (i = 0; i < my_ntohs(&hdr->qdcount); i++)
    {
        //Grab the labels in the q string
        p = labelToStr(pusrdata, p, length, buff, sizeof(buff));
        if (p == nullptr)
            return;
        auto *qf = (DnsQuestionFooter *) p;
        p += sizeof(DnsQuestionFooter);

        printf("DNS: Q (type 0x%X dnsClass 0x%X) for %s\n", my_ntohs(&qf->type), my_ntohs(&qf->
        dnsClass), buff);


        if (my_ntohs(&qf->type) == QTYPE_A)
        {
            //They want to know the IPv4 address of something.
            //Build the response.

            rend = strToLabel(buff, rend, (int)sizeof(reply) - (rend - reply)); //Add the label
            if (rend == nullptr)
                return;
            auto *rf = (DnsResourceFooter *) rend;
            rend += sizeof(DnsResourceFooter);
            setn16(&rf->type, QTYPE_A);
            setn16(&rf->
            dnsClass, QdnsClass_IN);
            setn32(&rf->ttl, 0);
            setn16(&rf->rdlength, 4); //IPv4 addr is 4 bytes;
            //Grab the current IP of the softap interface

            esp_netif_ip_info_t info;
            //tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &info);
            esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &info);
            //struct ip_info info;
            //wifi_get_ip_info(SOFTAP_IF, &info);
            *rend++ = ip4_addr1(&info.ip);
            *rend++ = ip4_addr2(&info.ip);
            *rend++ = ip4_addr3(&info.ip);
            *rend++ = ip4_addr4(&info.ip);
            setn16(&rhdr->ancount, (int16_t)(my_ntohs(&rhdr->ancount) + 1));
            printf("IP Address:  %s\n", ip4addr_ntoa((const ip4_addr_t *) &info.ip));
            printf("Added A rec to resp. Resp len is %d\n", (rend - reply));

        }
        else if (my_ntohs(&qf->type) == QTYPE_NS)
        {
            //Give ns server. Basically can be whatever we want because it'll get resolved to our IP later anyway.
            rend = strToLabel(buff, rend, (int)sizeof(reply) - (rend - reply)); //Add the label
            auto *rf = (DnsResourceFooter *) rend;
            rend += sizeof(DnsResourceFooter);
            setn16(&rf->type, QTYPE_NS);
            setn16(&rf->
            dnsClass, QdnsClass_IN);
            setn16(&rf->ttl, 0);
            setn16(&rf->rdlength, 4);
            *rend++ = 2;
            *rend++ = 'n';
            *rend++ = 's';
            *rend++ = 0;
            setn16(&rhdr->ancount, (int16_t )(my_ntohs(&rhdr->ancount) + 1));
            //printf("Added NS rec to resp. Resp len is %d\n", (rend-reply));
        }
        else if (my_ntohs(&qf->type) == QTYPE_URI)
        {
            //Give uri to us
            rend = strToLabel(buff, rend, (int)(sizeof(reply) - (rend - reply))); //Add the label
            auto *rf = (DnsResourceFooter *) rend;
            rend += sizeof(DnsResourceFooter);
            auto *uh = (DnsUriHdr *) rend;
            rend += sizeof(DnsUriHdr);
            setn16(&rf->type, QTYPE_URI);
            setn16(&rf->
            dnsClass, QdnsClass_URI);
            setn16(&rf->ttl, 0);
            setn16(&rf->rdlength, 4 + 16);
            setn16(&uh->prio, 10);
            setn16(&uh->weight, 1);
            memcpy(rend, "http://esp.nonet", 16);
            rend += 16;
            setn16(&rhdr->ancount, (int16_t)(my_ntohs(&rhdr->ancount) + 1));
            //printf("Added NS rec to resp. Resp len is %d\n", (rend-reply));
        }
    }
    //Send the response
    //printf("Send response\n");
    sendto(m_socket, (uint8_t *) reply, rend - reply, 0, (struct sockaddr *) premote_addr, sizeof(struct sockaddr_in));

}


void CaptiveDns::captdnsTask()
{
    struct sockaddr_in server_addr = {};
    uint32_t ret;
    struct sockaddr_in from = {};
    socklen_t fromlen;
    //struct tcpip_adapter_ip_info_t ipconfig;
    char udp_msg[DNS_LEN];

    //memset(&ipconfig, 0, sizeof(ipconfig));
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(53);
    server_addr.sin_len = sizeof(server_addr);

    printf("Opening DNS listening socket\n");

    do
    {
        m_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_socket == -1)
        {
            printf("captdns_task failed to create sock!\n");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while (m_socket == -1);

    do
    {
        ret = bind(m_socket, (struct sockaddr *) &server_addr, sizeof(server_addr));
        if (ret != 0)
        {
            printf("captdns_task failed to bind sock, err=%d!\n", errno);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while (ret != 0);

    printf("CaptDNS inited.\n");

    while (m_run)
    {
        memset(&from, 0, sizeof(from));
        fromlen = sizeof(struct sockaddr_in);
        ret = recvfrom(m_socket, (uint8_t *) udp_msg, DNS_LEN, 0, (struct sockaddr *) &from, (socklen_t *) &fromlen);
        if (ret > 0)
            captdnsRecv(&from, udp_msg, ret);
    }

    close(m_socket);
    m_socket = -1;
    vTaskDelete(nullptr);
}

void CaptiveDns::start()
{
    printf("CaptDNS start\n");
    if (m_handle)
        return;

    m_run = true;
    xTaskCreate(taskThunk, (const char *) "captdns_task", 10000, this, 3, &m_handle);
    printf("CaptDNS started. handle %08lx\n", m_handle);
}

void CaptiveDns::stop()
{
    if (!m_handle)
        return;

    m_run = false;

    vTaskDelete(m_handle);
    m_handle = nullptr;

    if (m_socket != -1)
        close(m_socket);
    m_socket = -1;
}

void CaptiveDns::taskThunk(void *data)
{
    auto obj = (CaptiveDns *)data;

    obj->captdnsTask();
}
