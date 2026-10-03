// yacoin-cpuminer: entry point. MIT licence.
#include <cstdio>

#include "hash/yac_scrypt.h"

int main()
{
    if (!yac_scrypt_self_test()) {
        std::fprintf(stderr, "scrypt-jane self-test failed\n");
        return 1;
    }
    std::printf("yacoin-cpuminer %s (%s)\n", YAC_MINER_VERSION, yac_scrypt_mix_name());
    return 0;
}
