#pragma once

#include "core/Transaction.hpp"
#include <memory>
#include <openssl/evp.h>
#include <openssl/ec.h>
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif


namespace forgechain::crypto {

    struct EVPPKeyCtxDeleter {
        void operator()(EVP_PKEY_CTX* ctx) const { EVP_PKEY_CTX_free(ctx); }
    };
    struct EVPPKeyDeleter {
        void operator()(EVP_PKEY* pkey) const { EVP_PKEY_free(pkey); }
    };
    struct ECKeyDeleter {
        void operator()(EC_KEY* key) const { EC_KEY_free(key); }
    };

    using unique_evp_pkey_ctx = std::unique_ptr<EVP_PKEY_CTX, EVPPKeyCtxDeleter>;
    using unique_evp_pkey = std::unique_ptr<EVP_PKEY, EVPPKeyDeleter>;
    using unique_ec_key = std::unique_ptr<EC_KEY, ECKeyDeleter>;

    struct KeyPair {
        core::bytes public_key;
        core::bytes private_key;
    };

    KeyPair generate_keypair();
}
