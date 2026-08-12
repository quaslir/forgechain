#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <openssl/evp.h>
#include <memory>
#include <string>
namespace forgechain::crypto {
    struct EVPMDCTXDeleter {
      void operator()(EVP_MD_CTX *ptr) const {
        if (ptr)
          EVP_MD_CTX_free(ptr);
      }
    };
using HashBytes = std::array<uint8_t, 32>;
using bytes = std::vector<uint8_t>;
using str = std::string;
using unique_evp_md_ctx = std::unique_ptr<EVP_MD_CTX, EVPMDCTXDeleter>;



HashBytes sha256(const bytes& data);
HashBytes double_sha_256(const bytes& data);
str to_hex(const HashBytes& data);
}
