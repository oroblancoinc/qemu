/*
 * Bare-metal ESP32-C3 I2C controller test for the QEMU model.
 *
 * Run it with ./run.sh; see README.md for what it covers and why it is not a
 * qtest.
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define UART0        0x60000000u
#define I2C0         0x60013000u
#define A_CTR        0x04
#define A_SR         0x08
#define A_FIFO_CONF  0x18
#define A_DATA       0x1c
#define A_INT_RAW    0x20
#define A_INT_CLR    0x24
#define A_CMD        0x58

#define CTR_MS_MODE      (1u << 4)
#define CTR_TRANS_START  (1u << 5)
#define CTR_CONF_UPGATE  (1u << 11)
#define INT_TRANS_COMPLETE (1u << 7)
#define INT_NACK           (1u << 10)

#define OP_WRITE  1
#define OP_STOP   2
#define OP_READ   3
#define OP_END    4
#define OP_RSTART 6

static void putc_(char c) { REG(UART0) = (uint32_t)(unsigned char)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex2(uint32_t v)
{
    const char *d = "0123456789abcdef";
    putc_(d[(v >> 4) & 0xf]);
    putc_(d[v & 0xf]);
}
static void hex4(uint32_t v) { hex2(v >> 8); hex2(v); }

static uint32_t cmd(uint32_t op, uint32_t byte_num, uint32_t ack_en)
{
    return (byte_num & 0xff) | (ack_en << 8) | (op << 11);
}

static void begin(void)
{
    REG(I2C0 + A_INT_CLR) = 0xffffffffu;
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_CONF_UPGATE;
    REG(I2C0 + A_FIFO_CONF) = (1u << 12) | (1u << 13);
    REG(I2C0 + A_FIFO_CONF) = 0;
}

/* Exactly the command list ESP-IDF's i2c_master_probe() builds. */
static uint32_t probe(uint8_t addr)
{
    begin();
    REG(I2C0 + A_DATA) = (uint32_t)addr << 1;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 1, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_STOP, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
    return REG(I2C0 + A_INT_RAW);
}

static int read_reg(uint8_t addr, uint8_t reg, uint32_t *raw)
{
    begin();
    REG(I2C0 + A_DATA) = (uint32_t)addr << 1;
    REG(I2C0 + A_DATA) = reg;
    REG(I2C0 + A_DATA) = ((uint32_t)addr << 1) | 1;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 2, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 12) = cmd(OP_WRITE, 1, 1);
    REG(I2C0 + A_CMD + 16) = cmd(OP_READ, 1, 0);
    REG(I2C0 + A_CMD + 20) = cmd(OP_STOP, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
    *raw = REG(I2C0 + A_INT_RAW);
    if (((REG(I2C0 + A_SR) >> 8) & 0x3f) == 0) {
        return -1;
    }
    return (int)(REG(I2C0 + A_DATA) & 0xff);
}

static void write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    begin();
    REG(I2C0 + A_DATA) = (uint32_t)addr << 1;
    REG(I2C0 + A_DATA) = reg;
    REG(I2C0 + A_DATA) = val;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 3, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_STOP, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
}

static int fails;

static void check_probe(uint8_t addr, int expect_ack)
{
    uint32_t raw = probe(addr);
    int ack = (raw & INT_TRANS_COMPLETE) && !(raw & INT_NACK);
    puts_("probe 0x"); hex2(addr);
    puts_(" INT_RAW=0x"); hex4(raw);
    puts_(ack ? " ACK " : " NACK");
    if (ack == expect_ack) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

static void check_read(uint8_t addr, uint8_t reg, int expect)
{
    uint32_t raw;
    int v = read_reg(addr, reg, &raw);
    puts_("read 0x"); hex2(addr); puts_("["); hex2(reg);
    puts_("] INT_RAW=0x"); hex4(raw); puts_(" val=");
    if (v < 0) {
        puts_("none");
    } else {
        puts_("0x"); hex2((uint32_t)v);
    }
    puts_(" expect=0x"); hex2((uint32_t)expect);
    if (v == expect) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

/*
 * Split a register read across an END, the way the driver does for transfers
 * that do not fit the FIFO. BUS_BUSY must read 0 in the gap (the driver spins
 * on it with no timeout) while the transfer itself stays open, so the
 * resumed command list must not re-interpret its first byte as an address.
 */
static void check_end_split(uint8_t addr, uint8_t reg, int expect)
{
    uint32_t raw1, busy1, raw2;
    int v;

    begin();
    REG(I2C0 + A_DATA) = (uint32_t)addr << 1;
    REG(I2C0 + A_DATA) = reg;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 2, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_END, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
    raw1 = REG(I2C0 + A_INT_RAW);
    busy1 = (REG(I2C0 + A_SR) >> 4) & 1;

    /* Resume: no FIFO/INT reset, exactly as the driver refills the regs */
    REG(I2C0 + A_INT_CLR) = 0xffffffffu;
    REG(I2C0 + A_DATA) = ((uint32_t)addr << 1) | 1;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 1, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_READ, 1, 0);
    REG(I2C0 + A_CMD + 12) = cmd(OP_STOP, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
    raw2 = REG(I2C0 + A_INT_RAW);
    v = (((REG(I2C0 + A_SR) >> 8) & 0x3f) == 0) ? -1 : (int)(REG(I2C0 + A_DATA) & 0xff);

    puts_("end-split 0x"); hex2(addr); puts_("["); hex2(reg);
    puts_("] INT_RAW=0x"); hex4(raw1); puts_(" busy=");
    putc_(busy1 ? '1' : '0');
    puts_(" then INT_RAW=0x"); hex4(raw2); puts_(" val=");
    if (v < 0) {
        puts_("none");
    } else {
        puts_("0x"); hex2((uint32_t)v);
    }
    if ((raw1 & (1u << 3)) && busy1 == 0 && v == expect) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

/* SCL_SP_CONF.SCL_RST_SLV_EN is R/W/SC: it must read back as 0. */
static void check_clr_bus(void)
{
    uint32_t v;

    REG(I2C0 + 0x80) = (5u << 1) | 1u;
    v = REG(I2C0 + 0x80);
    puts_("clr-bus SCL_SP_CONF=0x"); hex4(v);
    if ((v & 1u) == 0 && ((v >> 1) & 0x1f) == 5) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

void test_main(void)
{
    puts_("I2CTEST START\r\n");

    check_probe(0x58, 1);
    check_probe(0x3a, 1);
    check_probe(0x08, 0);
    check_probe(0x5b, 0);
    /* A NACK must not wedge the controller: the next probe still works. */
    check_probe(0x58, 1);

    check_read(0x58, 0x10, 0x23);
    check_read(0x3a, 0x00, 0x18);
    check_read(0x58, 0x00, 0x00);

    write_reg(0x58, 0x02, 0x5a);
    write_reg(0x58, 0x03, 0xa5);
    check_read(0x58, 0x02, 0x5a);
    check_read(0x58, 0x03, 0xa5);
    /* The auto-incremented pointer must not leak into the next transfer */
    check_read(0x58, 0x10, 0x23);

    check_end_split(0x58, 0x10, 0x23);
    check_clr_bus();
    /* The controller must still be usable after both of those */
    check_probe(0x58, 1);
    check_read(0x3a, 0x00, 0x18);

    puts_(fails ? "I2CTEST FAILURES\r\n" : "I2CTEST ALL OK\r\n");
    for (;;) {
    }
}
