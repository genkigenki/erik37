/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))

/* The board: 0 = M-Vave FM-1 (default), 1 = M-Vave SMK-37 (the same AC791N, SPL and update loader; another panel,
 * screen wiring and flash map: boards/smk37/). The build sets it (fm1-base tools/build.py BOARD=smk37) and puts
 * boards/smk37 on the include path in place of boards/fm1. */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
/* SMK_MODEL: the model name an SMK-37 build reports (its identity, its update loader's): the SMK-37 Pro, SMK-37
 * Elite and MKE-P37 are one keyboard under three names, and M-Vave's updater takes only a unit's own package */
#if BOARD_SMK37 && !defined(SMK_MODEL)
#define SMK_MODEL "SMK-37 Pro"
#endif
