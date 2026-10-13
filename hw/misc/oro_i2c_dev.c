/*
 * Generic simulated I2C peripheral with a flat register file
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * Covers the register protocol shared by the GPIO expanders, LED drivers and
 * touch controllers found on the Oroblanco boards: the first byte written
 * after a START selects a register index, subsequent writes store with
 * auto-increment, and reads return the register file with auto-increment.
 *
 * Instantiate one per simulated chip, e.g.
 *     -device oro-i2c-dev,address=0x58,name=aw9523b,regs=10=23
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_ORO_I2C_DEV "oro-i2c-dev"
OBJECT_DECLARE_SIMPLE_TYPE(OroI2CDevState, ORO_I2C_DEV)

#define ORO_I2C_DEV_NREGS 256

/* Debug output */
#define ORO_I2C_DEV_DEBUG 0

struct OroI2CDevState {
    I2CSlave parent_obj;

    /* Properties */
    char *name;
    char *regs_preset;

    uint8_t regs[ORO_I2C_DEV_NREGS];
    uint8_t ptr;
    bool expect_reg;
};

static const char *oro_i2c_dev_name(OroI2CDevState *s)
{
    return s->name ? s->name : TYPE_ORO_I2C_DEV;
}

static int oro_i2c_dev_event(I2CSlave *i2c, enum i2c_event event)
{
    OroI2CDevState *s = ORO_I2C_DEV(i2c);

    switch (event) {
    case I2C_START_SEND:
        /* The first byte of a write selects the register to access */
        s->expect_reg = true;
        break;
    case I2C_START_RECV:
        /*
         * A read continues from the register selected by the preceding write,
         * so the pointer is deliberately left alone here. The same goes for
         * I2C_FINISH: the controller model ends the transfer on a repeated
         * start, and the pointer must survive that.
         */
        break;
    case I2C_FINISH:
    case I2C_NACK:
        break;
    default:
        return -1;
    }

    return 0;
}

static uint8_t oro_i2c_dev_recv(I2CSlave *i2c)
{
    OroI2CDevState *s = ORO_I2C_DEV(i2c);
    uint8_t value = s->regs[s->ptr];

#if ORO_I2C_DEV_DEBUG
    info_report("[%s @0x%02x] READ reg 0x%02x = 0x%02x",
                oro_i2c_dev_name(s), i2c->address, s->ptr, value);
#endif

    s->ptr++;
    return value;
}

static int oro_i2c_dev_send(I2CSlave *i2c, uint8_t data)
{
    OroI2CDevState *s = ORO_I2C_DEV(i2c);

    if (s->expect_reg) {
        s->ptr = data;
        s->expect_reg = false;
#if ORO_I2C_DEV_DEBUG
        info_report("[%s @0x%02x] SELECT reg 0x%02x",
                    oro_i2c_dev_name(s), i2c->address, s->ptr);
#endif
        return 0;
    }

#if ORO_I2C_DEV_DEBUG
    info_report("[%s @0x%02x] WRITE reg 0x%02x = 0x%02x",
                oro_i2c_dev_name(s), i2c->address, s->ptr, data);
#endif

    s->regs[s->ptr] = data;
    s->ptr++;
    return 0;
}

/*
 * Parse the "regs" property: a comma separated list of hexadecimal
 * "<index>=<value>" pairs, e.g. "10=23,00=18".
 */
static void oro_i2c_dev_parse_regs(OroI2CDevState *s, Error **errp)
{
    g_auto(GStrv) entries = NULL;

    if (!s->regs_preset || s->regs_preset[0] == '\0') {
        return;
    }

    entries = g_strsplit(s->regs_preset, ",", -1);
    for (int i = 0; entries[i] != NULL; i++) {
        g_auto(GStrv) pair = g_strsplit(entries[i], "=", -1);
        uint64_t index;
        uint64_t value;

        if (g_strv_length(pair) != 2) {
            error_setg(errp, "%s: malformed regs entry '%s', "
                       "expected <index>=<value>", oro_i2c_dev_name(s),
                       entries[i]);
            return;
        }
        if (qemu_strtou64(g_strstrip(pair[0]), NULL, 16, &index) < 0 ||
            index >= ORO_I2C_DEV_NREGS) {
            error_setg(errp, "%s: invalid regs index '%s'",
                       oro_i2c_dev_name(s), pair[0]);
            return;
        }
        if (qemu_strtou64(g_strstrip(pair[1]), NULL, 16, &value) ||
            value > 0xff) {
            error_setg(errp, "%s: invalid regs value '%s'",
                       oro_i2c_dev_name(s), pair[1]);
            return;
        }

        s->regs[index] = value;
    }
}

static void oro_i2c_dev_realize(DeviceState *dev, Error **errp)
{
    OroI2CDevState *s = ORO_I2C_DEV(dev);

    oro_i2c_dev_parse_regs(s, errp);
}

static void oro_i2c_dev_reset_hold(Object *obj, ResetType type)
{
    OroI2CDevState *s = ORO_I2C_DEV(obj);

    s->ptr = 0;
    s->expect_reg = false;
}

static const VMStateDescription vmstate_oro_i2c_dev = {
    .name = TYPE_ORO_I2C_DEV,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, OroI2CDevState),
        VMSTATE_UINT8_ARRAY(regs, OroI2CDevState, ORO_I2C_DEV_NREGS),
        VMSTATE_UINT8(ptr, OroI2CDevState),
        VMSTATE_BOOL(expect_reg, OroI2CDevState),
        VMSTATE_END_OF_LIST()
    }
};

static Property oro_i2c_dev_properties[] = {
    /* The "address" property comes from TYPE_I2C_SLAVE */
    DEFINE_PROP_STRING("name", OroI2CDevState, name),
    DEFINE_PROP_STRING("regs", OroI2CDevState, regs_preset),
    DEFINE_PROP_END_OF_LIST()
};

static void oro_i2c_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    dc->realize = oro_i2c_dev_realize;
    dc->vmsd = &vmstate_oro_i2c_dev;
    device_class_set_props(dc, oro_i2c_dev_properties);
    rc->phases.hold = oro_i2c_dev_reset_hold;

    sc->event = oro_i2c_dev_event;
    sc->recv = oro_i2c_dev_recv;
    sc->send = oro_i2c_dev_send;
}

static const TypeInfo oro_i2c_dev_info = {
    .name = TYPE_ORO_I2C_DEV,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(OroI2CDevState),
    .class_init = oro_i2c_dev_class_init,
};

static void oro_i2c_dev_register_types(void)
{
    type_register_static(&oro_i2c_dev_info);
}

type_init(oro_i2c_dev_register_types)
