#pragma once

#include <libsystem/Common.h>

struct Random
{
    uint64_t state;
    uint64_t inc;
};

Random random_create();

uint32_t random_uint32(Random *random);

uint32_t random_uint32_max(Random *random, uint32_t max);

double random_double(Random *random);
