# ESP32-C3 I2C controller test

Register-level test for `hw/i2c/esp32c3_i2c.c` and `hw/misc/oro_i2c_dev.c`.

```sh
tests/esp32c3-i2c/run.sh
```

Exit status: 0 pass, 1 fail, 77 skip (no cross compiler). Every check prints
its own line, so a failure names itself:

```
I2CTEST START
probe 0x58 INT_RAW=0x0080 ACK  ok
probe 0x08 INT_RAW=0x0400 NACK ok
read 0x58[10] INT_RAW=0x0080 val=0x23 expect=0x23 ok
end-split 0x58[10] INT_RAW=0x0008 busy=0 then INT_RAW=0x0080 val=0x23 ok
clr-bus SCL_SP_CONF=0x000a ok
I2CTEST ALL OK
```

15 checks, in four groups:

- **`probe`** — the exact command list ESP-IDF's `i2c_master_probe()` emits
  (`RESTART(6), WRITE(1) ack_en, STOP(2)`). A configured address must raise
  TRANS_COMPLETE, an unconfigured one must raise NACK, and a NACK must not
  wedge the controller for the next probe.
- **`read`** / write-readback — register reads and writes against two chips at
  different addresses, including pointer auto-increment and the pointer not
  leaking between transfers.
- **`end-split`** — a register read split across an END command, the way the
  driver splits transfers that do not fit the FIFO. `SR.BUS_BUSY` must read 0
  in the gap, because the driver spins on it with no timeout in interrupt
  context (`s_i2c_send_command_async`), while the transfer itself must stay
  open so the resumed command list does not re-read its payload as an address.
  Those two requirements pull in opposite directions on a single flag; this is
  the only coverage for the split.
- **`clr-bus`** — `SCL_SP_CONF.SCL_RST_SLV_EN` is `R/W/SC` and must read back
  as 0 with `SCL_RST_SLV_NUM` preserved. `s_i2c_master_clear_bus()` polls it
  for 50ms and then fails every later transaction.

## Why this is not a qtest

`-accel qtest` segfaults during `-machine esp32c3` init, with an empty script
and no I2C in the picture, so the usual `tests/qtest` approach is unavailable.
QEMU's gdbstub is not an alternative either: `M` packets go through
`address_space_write_rom`, which skips MMIO side effects, so register writes
silently no-op. Hence a real guest, loaded with `-kernel`.

The test writes its output straight to the UART0 FIFO rather than linking
anything from ESP-IDF, so it needs only a bare-metal riscv32 compiler. It is
not wired into meson because that compiler is not a QEMU build dependency.
