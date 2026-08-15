#pragma once

#include "core/Transaction.hpp"


#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif


namespace forgechain::crypto {



    struct KeyPair {
        core::bytes public_key;
        core::bytes private_key;
    };

    KeyPair generate_keypair();
}
