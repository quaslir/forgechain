#pragma once
#include <memory>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>

namespace forgechain::crypto {

struct EVPPKeyCtxDeleter {
  void operator()(EVP_PKEY_CTX *ctx) const { EVP_PKEY_CTX_free(ctx); }
};
struct EVPPKeyDeleter {
  void operator()(EVP_PKEY *pkey) const { EVP_PKEY_free(pkey); }
};
struct ECKeyDeleter {
  void operator()(EC_KEY *key) const { EC_KEY_free(key); }
};
struct BNDeleter {
  void operator()(BIGNUM *bn) const { BN_free(bn); }
};
struct ECPointDeleter {
  void operator()(EC_POINT *point) const { EC_POINT_free(point); }
};
struct EVPMDCTXDeleter {
  void operator()(EVP_MD_CTX *ptr) const {
    if (ptr)
      EVP_MD_CTX_free(ptr);
  }
};
using unique_ec_point = std::unique_ptr<EC_POINT, ECPointDeleter>;
using unique_bn = std::unique_ptr<BIGNUM, BNDeleter>;
using unique_evp_pkey_ctx = std::unique_ptr<EVP_PKEY_CTX, EVPPKeyCtxDeleter>;
using unique_evp_pkey = std::unique_ptr<EVP_PKEY, EVPPKeyDeleter>;
using unique_ec_key = std::unique_ptr<EC_KEY, ECKeyDeleter>;
using unique_evp_md_ctx = std::unique_ptr<EVP_MD_CTX, EVPMDCTXDeleter>;
} // namespace forgechain::crypto
