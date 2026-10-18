/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* M-Vave SMK-37: the FM-1's SARADC readings, as the SMK-37 has them (BOARD_SMK37; boards/smk37). The SMK-37 has no
 * MASTER pot: knob K5 is a virtual one; the battery divider is on mux C (fm1_input.h reads both in its scan, which
 * owns the converter). Include after fm1_input.h. */
#pragma once
#include <stdint.h>
#include "fm1_input.h"

/* the battery comes from mux C; the FM-1's MASTER pot is knob K5 here, as a
 * virtual pot (0..1023, 24 a detent; starts at 724: master_q12 2048, the FM-1 default) */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
#define SMK_ENC_MASTER 4u          /* K5: K1..K4 are the page's knobs (KNOB 1..4), K5..K8 the FM-1's top row */
static int32_t smk_master = 724;
static void fm1_adc_init(void) {}
static int32_t fm1_enc_take(uint32_t e);
static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_BATT)
        return fm1_in.batt ? (int32_t)fm1_in.batt : -1;
    if (ch == FM1_ADC_MASTER) {
        smk_master += fm1_enc_take(SMK_ENC_MASTER) * 24;
        smk_master = smk_master < 0 ? 0 : smk_master > 1023 ? 1023 : smk_master;
        return smk_master;
    }
    return -1;
}
