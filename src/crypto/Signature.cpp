#include "crypto/Signature.hpp"
#include "crypto/OpenSSLTypes.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <stdexcept>
namespace forgechain::crypto {
    crypto::bytes sign(const crypto::bytes& message, const crypto::bytes& private_key) {
        unique_ec_key ec_key(EC_KEY_new_by_curve_name(NID_secp256k1));

        if(!ec_key) {
            throw std::runtime_error("EC_KEY_new_by_curve_name failed");
        }
       unique_bn bn(BN_bin2bn(private_key.data(), static_cast<int>(private_key.size()), nullptr));
        if(!bn) {
            throw std::runtime_error("BN_bin2bn failed");
        }

        if(EC_KEY_set_private_key(ec_key.get(), bn.get()) != 1) {
            throw std::runtime_error("EC_KEY_set_private_key failed");
        }

        const EC_GROUP* group = EC_KEY_get0_group(ec_key.get());

        if(!group) {
            throw std::runtime_error("EC_KEY_get0_group failed");
        }

        unique_ec_point pub_point(EC_POINT_new(group));

        if(!pub_point) {
            throw std::runtime_error("EC_POINT_new failed");
        }

        if(EC_POINT_mul(group, pub_point.get(), bn.get(),nullptr, nullptr, nullptr) != 1) {
            throw std::runtime_error("EC_POINT_mul failed");
        }

        if(EC_KEY_set_public_key(ec_key.get(), pub_point.get()) != 1) {
            throw std::runtime_error("EC_KEY_set_public_key failed");
        }

        unique_evp_pkey pkey(EVP_PKEY_new());
        if(!pkey) {
            throw std::runtime_error("EVP_PKEY_new failed");
        }

        if(EVP_PKEY_set1_EC_KEY(pkey.get(), ec_key.get()) != 1) {
            throw std::runtime_error("EVP_PKEY_set1_EC_KEY failed");
        }

        unique_evp_md_ctx ctx(EVP_MD_CTX_new());
        if(!ctx) {
            throw std::runtime_error("EVP_MD_CTX_new failed");
        }

        if(EVP_DigestSignInit(ctx.get(),nullptr, EVP_sha256(), nullptr, pkey.get()) != 1) {
            throw std::runtime_error("EVP_DigestSignInit failed");
        }

        size_t signature_length = 0;
        if(EVP_DigestSign(ctx.get(), nullptr, &signature_length, message.data(), message.size()) != 1) {
            throw std::runtime_error("EVP_DigestSign (size query) failed");
        }

        crypto::bytes signature(signature_length);

        if(EVP_DigestSign(ctx.get(), signature.data(), &signature_length,message.data(), message.size()) != 1) {
            throw std::runtime_error("EVP_DigestSign failed");
        }

        signature.resize(signature_length);
        return signature;
    }
}
