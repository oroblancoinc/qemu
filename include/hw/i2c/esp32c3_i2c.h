/*
 * ESP32-C3 I2C controller emulation
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32C3_I2C_H
#define ESP32C3_I2C_H

#include "hw/sysbus.h"
#include "qemu/fifo8.h"
#include "hw/i2c/i2c.h"
#include "hw/registerfields.h"
#include "hw/gpio/esp32c3_gpio.h"

#define TYPE_ESP32C3_I2C "esp32c3.i2c"
#define ESP32C3_I2C(obj) OBJECT_CHECK(Esp32C3I2CState, (obj), TYPE_ESP32C3_I2C)

#define ESP32C3_I2C_MEM_SIZE    0x1000
#define ESP32C3_I2C_FIFO_LENGTH 32
/* The C3 only has 8 command registers, unlike the 16 of the original ESP32 */
#define ESP32C3_I2C_CMD_COUNT   8

/*
 * GPIO matrix input signal indices for I2C0 (soc/gpio_sig_map.h). The IDF
 * driver routes the controller to pads with esp_rom_gpio_connect_in_signal(),
 * so these are the signals the GPIO device must be queried with to learn
 * which pads the controller is currently wired to.
 */
#define ESP32C3_I2CEXT0_SCL_IN_IDX  53
#define ESP32C3_I2CEXT0_SDA_IN_IDX  54

/*
 * Read-only integer properties holding the pad each line is currently routed
 * to, or -1 when unrouted. Slaves on the bus read these to decide whether
 * they are physically wired to the lines being driven.
 */
#define ESP32C3_I2C_SCL_PAD_PROP    "scl-pad"
#define ESP32C3_I2C_SDA_PAD_PROP    "sda-pad"

typedef struct Esp32C3I2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    I2CBus *bus;
    Fifo8 rx_fifo;
    Fifo8 tx_fifo;
    bool trans_ongoing;

    /* GPIO device used to resolve which pads the controller is routed to */
    ESP32C3GPIOState *gpio;

    uint32_t ctr_reg;
    uint32_t timeout_reg;
    uint32_t slave_addr_reg;
    uint32_t int_ena_reg;
    uint32_t int_raw_reg;
    uint32_t sda_hold_reg;
    uint32_t sda_sample_reg;
    uint32_t high_period_reg;
    uint32_t low_period_reg;
    uint32_t start_hold_reg;
    uint32_t rstart_setup_reg;
    uint32_t stop_hold_reg;
    uint32_t stop_setup_reg;
    uint32_t filter_cfg_reg;
    uint32_t clk_conf_reg;
    uint32_t scl_st_timeout_reg;
    uint32_t scl_main_st_timeout_reg;
    uint32_t scl_sp_conf_reg;
    uint32_t date_reg;
    uint32_t cmd_reg[ESP32C3_I2C_CMD_COUNT];
} Esp32C3I2CState;


REG32(I2C_LOW_PERIOD, 0x00);

REG32(I2C_CTR, 0x04);
    FIELD(I2C_CTR, MS_MODE, 4, 1);
    FIELD(I2C_CTR, TRANS_START, 5, 1);
    /* C3-only, no equivalent on the original ESP32 */
    FIELD(I2C_CTR, FSM_RST, 10, 1);
    FIELD(I2C_CTR, CONF_UPGATE, 11, 1);

REG32(I2C_STATUS, 0x08);
    FIELD(I2C_STATUS, BUS_BUSY, 4, 1);
    FIELD(I2C_STATUS, RXFIFO_CNT, 8, 6);
    FIELD(I2C_STATUS, TXFIFO_CNT, 18, 6);

REG32(I2C_TIMEOUT, 0x0c);
REG32(I2C_SLAVE_ADDR, 0x10);
REG32(I2C_FIFO_ST, 0x14);

REG32(I2C_FIFO_CONF, 0x18);
    FIELD(I2C_FIFO_CONF, NONFIFO_EN, 10, 1);
    FIELD(I2C_FIFO_CONF, RX_FIFO_RST, 12, 1);
    FIELD(I2C_FIFO_CONF, TX_FIFO_RST, 13, 1);

REG32(I2C_FIFO_DATA, 0x1c);

REG32(I2C_INT_RAW, 0x20);
    FIELD(I2C_INT_RAW, RXFIFO_WM, 0, 1);
    FIELD(I2C_INT_RAW, TXFIFO_WM, 1, 1);
    FIELD(I2C_INT_RAW, END_DETECT, 3, 1);
    FIELD(I2C_INT_RAW, ARBITRATION_LOST, 5, 1);
    FIELD(I2C_INT_RAW, TRANS_COMPLETE, 7, 1);
    FIELD(I2C_INT_RAW, TIME_OUT, 8, 1);
    /* The original ESP32 model calls this same bit ACK_ERR */
    FIELD(I2C_INT_RAW, NACK, 10, 1);

REG32(I2C_INT_CLR, 0x24);
REG32(I2C_INT_ENA, 0x28);
REG32(I2C_INT_ST, 0x2c);

REG32(I2C_SDA_HOLD, 0x30);
REG32(I2C_SDA_SAMPLE, 0x34);
REG32(I2C_HIGH_PERIOD, 0x38);
REG32(I2C_START_HOLD, 0x40);
REG32(I2C_RSTART_SETUP, 0x44);
REG32(I2C_STOP_HOLD, 0x48);
REG32(I2C_STOP_SETUP, 0x4c);
REG32(I2C_FILTER_CFG, 0x50);
REG32(I2C_CLK_CONF, 0x54);

REG32(I2C_CMD, 0x58);
    FIELD(I2C_CMD, BYTE_NUM, 0, 8);
    FIELD(I2C_CMD, ACK_CHECK_EN, 8, 1);
    FIELD(I2C_CMD, ACK_EXP, 9, 1);
    FIELD(I2C_CMD, ACK_VAL, 10, 1);
    FIELD(I2C_CMD, OPCODE, 11, 3);
    FIELD(I2C_CMD, DONE, 31, 1);
/* 7 more command registers, up to 0x74 */

REG32(I2C_SCL_ST_TIME_OUT, 0x78);
REG32(I2C_SCL_MAIN_ST_TIME_OUT, 0x7c);
REG32(I2C_SCL_SP_CONF, 0x80);
REG32(I2C_DATE, 0xf8);

/*
 * I2C_CMD.OPCODE values. These differ from the original ESP32 (which uses
 * RSTART=0, WRITE=1, READ=2, STOP=3, END=4); see
 * components/hal/esp32c3/include/hal/i2c_ll.h. Getting these wrong
 * mis-decodes the command list silently.
 */
typedef enum {
    I2C_OPCODE_WRITE   = 1,
    I2C_OPCODE_STOP    = 2,
    I2C_OPCODE_READ    = 3,
    I2C_OPCODE_END     = 4,
    I2C_OPCODE_RSTART  = 6,
} esp32c3_i2c_opcode_t;

#endif /* ESP32C3_I2C_H */
