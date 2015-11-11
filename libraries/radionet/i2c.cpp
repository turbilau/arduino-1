/*
 Copyright (C) 2015 Dave Berkeley projects2@rotwang.co.uk

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 USA
*/

#include <stdint.h>

#include "i2c.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))

    /*
     *  I2C library.
     *
     *  Intended to be used in bootloader, so needs to be in C, not C++,
     *  and independent of the excellent jeelib library,
     *  from which it borrows.
     */

    /*
     *  Pin IO functions.
     */

static inline void pin_change(volatile uint8_t* reg, uint8_t mask, bool state)
{
    if (state)
        *reg |= mask;
    else
        *reg &= ~mask;
}

void pin_mode(const Pin* pin, bool output)
{
    pin_change(pin->ddr, pin->mask, output);
}

void pin_set(const Pin* pin, bool state)
{
    pin_change(pin->data, pin->mask, state);
}

bool pin_get(const Pin* pin)
{
    return (pin->mask & *(pin->pin)) ? true : false;
}

void pin_pulse(const Pin* pin)
{
    if (pin) {
        pin_set(pin, false);
        pin_set(pin, true);
    }
}

extern "C" {
extern unsigned long millis(void);
}

    /*
     *  I2C pin manipulation primitives
     */

void i2c_sda(I2C* i2c, bool data)
{
    // pull low, float hi
    pin_mode(i2c->sda, !data);
    pin_set(i2c->sda, data);
}

bool i2c_get(I2C* i2c)
{
    return pin_get(i2c->sda);
}

void i2c_scl(I2C* i2c, bool state)
{
    if (i2c->scl_delay)
        delay_us(i2c->scl_delay);
    pin_set(i2c->scl, state);
}

    /*
     *  I2C Communication.
     */

void i2c_init(I2C* i2c)
{
    i2c_sda(i2c, true);
    pin_mode(i2c->scl, true);
    i2c_scl(i2c, true);
    if (i2c->debug) {
        pin_set(i2c->debug, true);
        pin_mode(i2c->debug, true);
    }
    if (i2c->trig) {
        pin_set(i2c->trig, true);
        pin_mode(i2c->trig, true);
    }
}

bool i2c_start(I2C* i2c, uint8_t addr)
{
    pin_pulse(i2c->trig);
    if (i2c->trig) delay_us(2);
    i2c_scl(i2c, true);
    i2c_sda(i2c, false);
    return i2c_write(i2c, addr);
}

void i2c_stop(I2C* i2c)
{
    i2c_sda(i2c, false);
    i2c_scl(i2c, true);
    i2c_sda(i2c, true);
}

bool i2c_write(I2C* i2c, uint8_t data)
{
    i2c_scl(i2c, false);
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        i2c_sda(i2c, data & mask);
        i2c_scl(i2c, true);
        i2c_scl(i2c, false);
    }
    i2c_sda(i2c, true);
    i2c_scl(i2c, true);
    const uint8_t ack = ! i2c_get(i2c);
    i2c_scl(i2c, false);
    return ack;
}

uint8_t i2c_read(I2C* i2c, bool last)
{
    uint8_t data = 0;
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        i2c_scl(i2c, true);
        if (i2c_get(i2c))
            data |= mask;
        i2c_scl(i2c, false);
    }
    i2c_sda(i2c, last);
    i2c_scl(i2c, true);
    i2c_scl(i2c, false);
    if (last)
        i2c_stop(i2c);
    else
        i2c_sda(i2c, true);
    return data;
}

bool i2c_is_present(I2C* i2c)
{
    const bool ok = i2c_start(i2c, i2c->addr);
    i2c_stop(i2c);
    return ok;
}

    /*
     *
     */

static uint8_t dev_select(I2C* i2c, uint16_t page)
{
    return i2c->addr + ((page > 255) ? 0x02 : 0x00);
}

void i2c_load(I2C* i2c, uint16_t page, uint8_t offset, void* buff, int count)
{
    //while (get_ms() < i2c->next_save)
    //    ;

    // Address Write
    while (!i2c_start(i2c, dev_select(i2c, page)))
    {
        // TODO : Implement timeout
        pin_pulse(i2c->debug);
    }
    i2c_write(i2c, page);
    i2c_write(i2c, offset);

    // Address Read
    i2c_start(i2c, 0x01 | i2c->addr);

    uint8_t* b = (uint8_t*) buff;
    while (--count >= 0) {
        *b++ = i2c_read(i2c, count == 0);
    }
}

void i2c_save(I2C* i2c, uint16_t page, uint8_t offset, const void* buff, int count)
{
    // don't do back-to-back saves, last one must have had time to finish!
    //while (millis() < i2c->next_save)
    //    ;

    // Address Write
    while (!i2c_start(i2c, dev_select(i2c, page)))
    {
        // TODO : Implement timeout
        pin_pulse(i2c->debug);
    }
    i2c_write(i2c, page);
    i2c_write(i2c, offset);

    const uint8_t* b = (const uint8_t*) buff;
    while (count--) {
        i2c_write(i2c, *b++);
    }
    i2c_stop(i2c);

    //i2c->next_save = millis() + 10;
}

    /*
     *  Block Iterator to handle Flash page boundaries.
     */

void flash_block(const FlashIO* io, void* obj, uint32_t addr, uint16_t bytes, uint8_t* data, flash_iter fn)
{
    //  calculate block / offset etc.
    while (bytes) {
        const uint16_t bsize = io->info->block_size;
        const uint16_t block = addr / bsize;
        const uint16_t offset = addr % bsize;
        const uint16_t size = min(bytes, bsize - offset);

        if (block >= io->info->blocks)
            return;

        fn(io, obj, block, offset, size, data);

        data += size;
        bytes -= size;
        addr += size;
    }
} 

    /*
     *  Save data to Flash
     */

static void saver(const FlashIO* io, void* obj, uint16_t block, uint16_t offset, uint16_t bytes, uint8_t* data)
{
    uint16_t* xfered = (uint16_t*) obj;

    io->save(block, offset, data, bytes);
    *xfered += bytes;
}

void flash_save(const FlashIO* io, uint16_t* xfered, uint32_t addr, uint16_t bytes, uint8_t* data)
{
    flash_block(io, xfered, addr, bytes, data, saver);
}

    /*
     *  Read data from Flash
     */

static void reader(const FlashIO* io, void* obj, uint16_t block, uint16_t offset, uint16_t bytes, uint8_t* data)
{
    uint16_t* xfered = (uint16_t*) obj;

    io->load(block, offset, data, bytes);
    *xfered += bytes;
}

void flash_read(const FlashIO* io, uint16_t* xfered, uint32_t addr, uint16_t bytes, uint8_t* data)
{
    flash_block(io, xfered, addr, bytes, data, reader);
}

// FIN
