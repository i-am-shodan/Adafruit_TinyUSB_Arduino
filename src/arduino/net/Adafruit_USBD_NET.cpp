/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 hathach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <cstdint>

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/ethip6.h"

#include "class/net/net_device.h"

#include "dhserver.h"
#include "dnserver.h"

#include <HardwareSerial.h>

#include "Adafruit_USBD_NET.h"

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+
//#define CFG_TUD_NET_ENDPOINT_SIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_NET_ENDPOINT_SIZE 64

static Adafruit_USBD_NET *_net_dev = NULL;

static struct netif netif_data;

/* shared between tud_network_recv_cb() and service_traffic() */
static struct pbuf *received_frame;

#define INIT_IP4(a,b,c,d) { PP_HTONL(LWIP_MAKEU32(a,b,c,d)) }

/* network parameters of this MCU */
static const ip4_addr_t ipaddr  = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to store assigned MAC addresses */
static dhcp_entry_t entries[] =
{
    /* mac ip address                          lease time */
    { {0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60 },
    { {0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60 },
    { {0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_config =
{
    INIT_IP4(0, 0, 0, 0),            /* router address (if any) */
    67,                                /* listen port */
    INIT_IP4(192, 168, 7, 1),           /* dns server (if any) */
    "usb",                                     /* dns suffix */
    TU_ARRAY_SIZE(entries),                    /* num entry */
    entries                                    /* entries */
};

uint8_t tud_network_mac_address[6] = {0x02,0x02,0x84,0x6A,0x96,0x00};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
{
  (void)netif;

  for (;;)
  {
    /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
    if (!tud_ready())
      return ERR_USE;

    /* if the network driver can accept another packet, we make it happen */
    if (tud_network_can_xmit(p->tot_len))
    {
      tud_network_xmit(p, 0 /* unused for this example */);
      return ERR_OK;
    }

    /* transfer execution to TinyUSB in the hopes that it will finish transmitting the prior packet */
    tud_task();
  }
}

#if LWIP_IPV6
static err_t ip6_output_fn(struct netif *netif, struct pbuf *p, const ip6_addr_t *addr)
{
  return ethip6_output(netif, p, addr);
}
#endif

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
  return etharp_output(netif, p, addr);
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
  /* this shouldn't happen, but if we get another packet before
  parsing the previous, we must signal our inability to accept it */
  if (received_frame) return false;

  if (size)
  {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);

    if (p)
    {
      /* pbuf_alloc() has already initialized struct; all we need to do is copy the data */
      memcpy(p->payload, src, size);

      /* store away the pointer for service_traffic() to later handle */
      received_frame = p;
    }
  }

  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
  struct pbuf *p = (struct pbuf *)ref;

  //(void)arg; /* unused for this example */

  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void)
{
  /* if the network is re-initializing and we have a leftover packet, we must do a cleanup */
  if (received_frame)
  {
    pbuf_free(received_frame);
    received_frame = NULL;
  }
}

/* handle any DNS requests from dns-server */
bool dns_query_proc(const char *name, ip4_addr_t *addr)
{
  if (0 == strcmp(name, "tiny.usb"))
  {
    *addr = ipaddr;
    return true;
  }
  return false;
}

static err_t netif_init_cb(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  netif->mtu = CFG_TUD_NET_MTU;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
  netif->state = NULL;
  netif->name[0] = 'E';
  netif->name[1] = 'X';
  netif->linkoutput = linkoutput_fn;
  netif->output = ip4_output_fn;
#if LWIP_IPV6
  netif->output_ip6 = ip6_output_fn;
#endif
  return ERR_OK;
}

Adafruit_USBD_NET::Adafruit_USBD_NET() {
  struct netif *netif = &netif_data;

  lwip_init();

  netif->hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
  netif->hwaddr[5] ^= 0x01;

  netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ip_input);
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(netif, 1);
#endif
  netif_set_default(netif);

  _net_dev = this;

  dhserv_init(&dhcp_config);
  dnserv_init(IP_ADDR_ANY, 53, dns_query_proc);
}

uint16_t Adafruit_USBD_NET::getInterfaceDescriptor(uint8_t itfnum_deprecated,
                                                    uint8_t *buf,
                                                    uint16_t bufsize) {
  (void)itfnum_deprecated;
  //_strid = 4; // STRID_INTERFACE; todo
  uint8_t STRID_MAC = 5; // todo

  // null buffer is used to get the length of descriptor only
  if (!buf) {
    return TUD_CDC_NCM_DESC_LEN;
  }

  if (bufsize < TUD_CDC_NCM_DESC_LEN) {
    return 0;
  }

  uint8_t const itfnum = TinyUSBDevice.allocInterface(2);
  uint8_t const ep_notif = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
  uint8_t const ep_in = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
  uint8_t const ep_out = TinyUSBDevice.allocEndpoint(TUSB_DIR_OUT);

  uint8_t strid = TinyUSBDevice.addStringDescriptor("TinyUSB Network Interface");

  uint8_t const desc[] = {
      TUD_CDC_NCM_DESCRIPTOR(0, strid, STRID_MAC, 0x81, 64, 0x02, 0x82, CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU),
  };

  uint16_t const len = sizeof(desc);

  // null buffer is used to get the length of descriptor only
  memcpy(buf, desc, len);

  return len;
}

bool Adafruit_USBD_NET::begin(void) {
  if (!TinyUSBDevice.addInterface(*this)) {
    return false;
  }

  _net_dev = this;
  return true;
}

bool Adafruit_USBD_NET::loop()
{
  bool ret = false;
  tud_task();

  /* handle any packet received by tud_network_recv_cb() */
  if (received_frame)
  { 
    ethernet_input(received_frame, &netif_data);
    pbuf_free(received_frame);
    received_frame = NULL;
    tud_network_recv_renew();
    ret = true;
  }

  return ret;
}