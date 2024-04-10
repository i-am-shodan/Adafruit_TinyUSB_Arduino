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
#include "Adafruit_USBD_NET.h"

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+
#define CFG_TUD_NET_ENDPOINT_SIZE 64

static Adafruit_USBD_NET *_net_dev = nullptr;

uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

uint8_t *received_frame = nullptr;
uint32_t recieved_frame_size = 0;

bool usbnet_hasNewPacket()
{
    return received_frame != nullptr && recieved_frame_size != 0;
}

uint8_t *usbnet_getPacket(uint32_t *len)
{
    if (received_frame == nullptr || received_frame == 0)
    {
        return nullptr;
    }

    *len = recieved_frame_size;
    return received_frame;
}

void usbnet_releasePacket()
{
    recieved_frame_size = 0;
    free(received_frame);
    received_frame = nullptr;

    tud_network_recv_renew();
}

extern "C" bool usbnet_transmitPacket(uint8_t *buffer, uint32_t l)
{
    uint16_t len = (uint16_t)l;

    if (l != len)
    {
        return false;
    }

    if (buffer == nullptr || len == 0)
    {
        return false;
    }

    if (tud_network_can_xmit(len))
    {
        tud_network_xmit(buffer, len);
        return true;
    }

    return false;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    uint16_t len = arg;
    memcpy(dst, ref, len);
    return len;
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    if (received_frame != nullptr)
    {
        return false;
    }

    bool err = true;

    if (size)
    {
        uint8_t *p = (uint8_t *)malloc(size);

        if (p)
        {
            memcpy(p, src, size);
            received_frame = p;
            recieved_frame_size = size;
            err = false;
        }
    }

    if (err)
    {
        tud_network_recv_renew();
    }

    return true;
}

void tud_network_init_cb(void)
{
    /* if the network is re-initializing and we have a leftover packet, we must do a cleanup */
    if (received_frame)
    {
        recieved_frame_size = 0;
        free(received_frame);
        received_frame = nullptr;
    }
}

char macAddrBuffer[32] = {0};

Adafruit_USBD_NET::Adafruit_USBD_NET()
{
}

uint16_t Adafruit_USBD_NET::getInterfaceDescriptor(uint8_t itfnum_deprecated,
                                                   uint8_t *buf,
                                                   uint16_t bufsize)
{
    return 0;
}

bool Adafruit_USBD_NET::begin(void)
{
    if (!TinyUSBDevice.addInterface(*this))
    {
        return false;
    }

    _net_dev = this;
    return true;
}