#include "crypto/Signature.hpp"
#include "crypto/Keys.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <cstddef>
#include "crypto/CommonTypes.hpp"
using namespace forgechain::crypto;

namespace {

bytes toBytes(const std::string& s) {
    return bytes{s.begin(), s.end()};
}

}

TEST(SignAndVerify, ValidSignatureVerifiesTrue) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("forgechain transaction payload");

    bytes signature = sign(message, kp.private_key);
    EXPECT_TRUE(verify(message, signature, kp.public_key));
}

TEST(SignAndVerify, SignatureIsNonEmpty) {
    KeyPair kp = generate_keypair();
    bytes signature = sign(toBytes("hello"), kp.private_key);
    EXPECT_FALSE(signature.empty());
}

TEST(SignAndVerify, SignatureSizeIsWithinExpectedDerRange) {
    KeyPair kp = generate_keypair();
    for (int i = 0; i < 20; ++i) {
        bytes signature = sign(toBytes("size check"), kp.private_key);
        EXPECT_GE(signature.size(), 68u) << "iteration " << i;
        EXPECT_LE(signature.size(), 72u) << "iteration " << i;
    }
}

TEST(SignAndVerify, TwoSignaturesOverSameInputDifferButBothVerify) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("randomized ECDSA check");

    bytes sig1 = sign(message, kp.private_key);
    bytes sig2 = sign(message, kp.private_key);

    EXPECT_NE(sig1, sig2) << "ECDSA nonce reuse or unexpected determinism";
    EXPECT_TRUE(verify(message, sig1, kp.public_key));
    EXPECT_TRUE(verify(message, sig2, kp.public_key));
}

TEST(SignAndVerify, WrongPublicKeyFailsVerification) {
    KeyPair a = generate_keypair();
    KeyPair b = generate_keypair();
    bytes message = toBytes("cross-key check");

    bytes signature = sign(message, a.private_key);
    EXPECT_FALSE(verify(message, signature, b.public_key));
}

TEST(SignAndVerify, TamperedMessageFailsVerification) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("original message");
    bytes signature = sign(message, kp.private_key);

    bytes tampered = message;
    tampered[0] ^= 0x01;

    EXPECT_FALSE(verify(tampered, signature, kp.public_key));
}

TEST(SignAndVerify, TamperedSignatureMiddleByteFailsVerification) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("tamper check");
    bytes signature = sign(message, kp.private_key);

    bytes tampered = signature;
    tampered[tampered.size() / 2] ^= 0x01;

    EXPECT_FALSE(verify(message, tampered, kp.public_key));
}

TEST(SignAndVerify, TamperedSignatureLastByteFailsVerification) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("tamper check tail");
    bytes signature = sign(message, kp.private_key);

    bytes tampered = signature;
    tampered.back() ^= 0x01;

    EXPECT_FALSE(verify(message, tampered, kp.public_key));
}

TEST(SignAndVerify, EmptySignatureFailsVerification) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("empty sig check");
    EXPECT_FALSE(verify(message, bytes{}, kp.public_key));
}

TEST(SignAndVerify, GarbageSignatureFailsVerification) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("garbage sig check");
    bytes garbage(70, 0xAB);
    EXPECT_FALSE(verify(message, garbage, kp.public_key));
}

TEST(SignAndVerify, TruncatedSignatureFailsVerification) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("truncated sig check");
    bytes signature = sign(message, kp.private_key);

    bytes truncated(signature.begin(), signature.begin() + static_cast<long>(signature.size() / 2));
    EXPECT_FALSE(verify(message, truncated, kp.public_key));
}

TEST(SignAndVerify, EmptyPublicKeyThrows) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("malformed pubkey check");
    bytes signature = sign(message, kp.private_key);

    EXPECT_THROW((void)verify(message, signature, bytes{}), std::runtime_error);
}

TEST(SignAndVerify, GarbagePublicKeyOfCorrectLengthThrows) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("garbage pubkey check");
    bytes signature = sign(message, kp.private_key);

    bytes garbagePubKey(65, 0xAB);
    EXPECT_THROW((void)verify(message, signature, garbagePubKey), std::runtime_error);
}

TEST(SignAndVerify, WrongPrefixByteOnPublicKeyThrows) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("wrong prefix check");
    bytes signature = sign(message, kp.private_key);

    bytes corrupted = kp.public_key;
    corrupted[0] = 0x05;

    EXPECT_THROW((void)verify(message, signature, corrupted), std::runtime_error);
}

TEST(SignAndVerify, TooShortPublicKeyThrows) {
    KeyPair kp = generate_keypair();
    bytes message = toBytes("short pubkey check");
    bytes signature = sign(message, kp.private_key);

    bytes tooShort(10, 0x04);
    EXPECT_THROW((void)verify(message, signature, tooShort), std::runtime_error);
}

TEST(SignAndVerify, EmptyMessageSignsAndVerifies) {
    KeyPair kp = generate_keypair();
    bytes signature = sign(bytes{}, kp.private_key);
    EXPECT_TRUE(verify(bytes{}, signature, kp.public_key));
}

TEST(SignAndVerify, LargeMessageSignsAndVerifies) {
    KeyPair kp = generate_keypair();
    bytes largeMessage(1'000'000, 0xCD);
    bytes signature = sign(largeMessage, kp.private_key);
    EXPECT_TRUE(verify(largeMessage, signature, kp.public_key));
}

TEST(SignAndVerify, ManyIndependentKeypairsAllSignAndVerifyCorrectly) {
    constexpr int kIterations = 50;
    for (int i = 0; i < kIterations; ++i) {
        KeyPair kp = generate_keypair();
        bytes message = toBytes("breadth check " + std::to_string(i));
        bytes signature = sign(message, kp.private_key);
        EXPECT_TRUE(verify(message, signature, kp.public_key))
            << "failed at iteration " << i;
    }
}

TEST(SignAndVerify, CrossKeyMatrixNeverFalsePositive) {
    constexpr int kKeyCount = 5;
    std::vector<KeyPair> keys;
    std::vector<bytes> signatures;
    bytes message = toBytes("cross key matrix");

    for (int i = 0; i < kKeyCount; ++i) {
        keys.push_back(generate_keypair());
        signatures.push_back(sign(message, keys.back().private_key));
    }

    for (int i = 0; i < kKeyCount; ++i) {
        for (int j = 0; j < kKeyCount; ++j) {
            bool result = verify(message, signatures[static_cast<size_t>(i)],
                                  keys[static_cast<size_t>(j)].public_key);
            if (i == j) {
                EXPECT_TRUE(result) << "diagonal (" << i << "," << j << ") should verify";
            } else {
                EXPECT_FALSE(result) << "off-diagonal (" << i << "," << j << ") should NOT verify";
            }
        }
    }
}

TEST(SignAndVerify, RejectsEmptyPrivateKey) {
    bytes message = toBytes("invalid key check");
    EXPECT_THROW((void)sign(message, bytes{}), std::runtime_error)
        << "sign() accepted an empty (zero-value) private key";
}

TEST(SignAndVerify, RejectsAllZeroPrivateKey) {
    bytes message = toBytes("invalid key check");
    bytes zeroKey(32, 0x00);
    EXPECT_THROW((void)sign(message, zeroKey), std::runtime_error)
        << "sign() accepted an all-zero private key";
}

TEST(SignAndVerify, RejectsPrivateKeyExceedingCurveOrder) {
    bytes message = toBytes("invalid key check");
    bytes oversizedKey(32, 0xFF);
    EXPECT_THROW((void)sign(message, oversizedKey), std::runtime_error)
        << "sign() accepted a private key exceeding the curve order";
}
