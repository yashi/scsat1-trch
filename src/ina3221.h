/*
 * Space Cubics OBC TRCH Software
 *  Definitions for INA3221
 *
 * (C) Copyright 2021-2022
 *         Space Cubics, LLC
 *
 */

#pragma once

#include <stdint.h>

#include "fpga.h"

#define REG_VOLTAGE_BASE 0x01

struct ina3221_data {
        int  master;
        uint8_t addr;
        int  channel;
        uint8_t shunt[2];
        uint8_t bus[2];
        int  error;
};

extern int ina3221_data_read (struct ina3221_data *id, enum FpgaState fpga_state, int type);
