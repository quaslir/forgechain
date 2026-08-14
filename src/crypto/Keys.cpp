#include "crypto/Keys.hpp"
#include "crypto/Hash.hpp"
#include <cstddef>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>
#include <stdexcept>

namespace forgechain::crypto {
    KeyPair generate_keypair() {
      unique_evp_pkey_ctx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr));
      if(!ctx) {
          throw std::runtime_error("EVP_PKEY_CTX_new_id failed");
      }
      if(EVP_PKEY_keygen_init(ctx.get()) != 1) {
          throw std::runtime_error("keygen init failed");
      }
      if(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx.get(), NID_secp256k1) != 1) {
          throw std::runtime_error("set curve failed");
      }
      EVP_PKEY * raw_pkey = nullptr;
      if(EVP_PKEY_keygen(ctx.get(), &raw_pkey) != 1) {
          throw std::runtime_error("EVP_PKEY_keygen failed");
      }

      unique_evp_pkey pkey(raw_pkey);

      unique_ec_key ec_key(EVP_PKEY_get1_EC_KEY(pkey.get()));
      if(!ec_key) {
          throw std::runtime_error("EVP_PKEY_get1_EC_KEY failed");
      }
      const BIGNUM* priv_bn = EC_KEY_get0_private_key(ec_key.get());
      int priv_len = BN_num_bytes(priv_bn);

      bytes private_key(static_cast<size_t>(priv_len));
      BN_bn2bin(priv_bn, private_key.data());

      const EC_POINT* pub_point = EC_KEY_get0_public_key(ec_key.get());

      const EC_GROUP*  group = EC_KEY_get0_group(ec_key.get());

      size_t pub_len = EC_POINT_point2oct(group, pub_point, POINT_CONVERSION_UNCOMPRESSED, nullptr, 0, nullptr);
      bytes public_key(pub_len);
      EC_POINT_point2oct(group, pub_point, POINT_CONVERSION_UNCOMPRESSED, public_key.data(), pub_len, nullptr);
      return KeyPair(public_key, private_key);
    }
}
