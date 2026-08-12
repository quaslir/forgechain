#include "crypto/Hash.hpp"
#include <openssl/evp.h>

#include <stdexcept>
namespace forgechain::crypto {
HashBytes sha256(const bytes& data) {
    HashBytes hash;
    unique_evp_md_ctx ctx(EVP_MD_CTX_new());
     if(!ctx) {
         throw std::runtime_error("EVP_MD_CTX_new failed");
     }

     unsigned int out_len = 0;

     if(EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1 ||
         EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1 ||
         EVP_DigestFinal_ex(ctx.get(), hash.data(), &out_len) != 1) {
              throw std::runtime_error("SHA-256 computation failed");
         }

     return hash;
}
}
