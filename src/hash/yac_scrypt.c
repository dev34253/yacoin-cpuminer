/*
 * yacoin-cpuminer: scrypt-jane build unit.
 *
 * This file #includes the unmodified copy of yacoin's scrypt-jane.c (see
 * third_party/scrypt-jane/SOURCE.md), so the mix code is compiled exactly once,
 * with the node's defines (SCRYPT_KECCAK512, SCRYPT_CHACHA,
 * SCRYPT_CHOOSE_COMPILETIME, set by CMake), and adds a few entry points.
 *
 * Copyright (c) 2026 yacoin-cpuminer contributors. MIT licence (see LICENSE).
 */
#include "scrypt-jane.c"

#include "yac_scrypt.h"

int yac_scrypt_self_test(void)
{
    /* res == 7 means mix, hash and full scrypt vectors all passed. */
    return scrypt_power_on_self_test() == 7;
}

const char *yac_scrypt_mix_name(void)
{
    return SCRYPT_MIX;
}
