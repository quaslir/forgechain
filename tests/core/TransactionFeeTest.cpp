#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"

#include <gtest/gtest.h>
#include <cstdint>

using namespace forgechain::core;
using namespace forgechain::crypto;

namespace {

struct Wallet {
  KeyPair keys;
  str address;
};

Wallet make_wallet() {
  KeyPair kp = generate_keypair();
  return Wallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_tx(const Wallet &sender, const str &recipient,
                            uint64_t amount, uint64_t fee) {
  Transaction tx(sender.address, recipient, amount, sender.keys.public_key, fee);
  tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
  return tx;
}

}  // namespace

TEST(TransactionFee, RoundTripPreservesFee) {
  Wallet alice = make_wallet();
  Transaction tx = make_signed_tx(alice, "bob", 100, 7);

  auto serialized = tx.serialize();
  auto restored = Transaction::deserialize(serialized);

  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->fee_, 7u);
  EXPECT_EQ(restored->amount_, 100u);
}

TEST(TransactionFee, RoundTripPreservesZeroFee) {
  Wallet alice = make_wallet();
  Transaction tx = make_signed_tx(alice, "bob", 100, 0);

  auto serialized = tx.serialize();
  auto restored = Transaction::deserialize(serialized);

  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->fee_, 0u);
}

TEST(TransactionFee, TamperingWithFeeAfterSigningInvalidatesSignature) {
  Wallet alice = make_wallet();
  Transaction tx = make_signed_tx(alice, "bob", 100, 5);

  Transaction tampered = tx;
  tampered.fee_ = 0;

  EXPECT_FALSE(verify(tampered.serialize_for_signing(), tampered.signature_,
                      alice.keys.public_key))
      << "signature must not verify once fee_ has been altered post-signing";
}

TEST(TransactionFee, TamperingWithFeeUpwardAlsoInvalidatesSignature) {
  Wallet alice = make_wallet();
  Transaction tx = make_signed_tx(alice, "bob", 100, 5);

  Transaction tampered = tx;
  tampered.fee_ = 1000;

  EXPECT_FALSE(verify(tampered.serialize_for_signing(), tampered.signature_,
                      alice.keys.public_key));
}

TEST(TransactionFee, OriginalSignatureStillValidWhenFeeUnchanged) {
  Wallet alice = make_wallet();
  Transaction tx = make_signed_tx(alice, "bob", 100, 5);

  EXPECT_TRUE(verify(tx.serialize_for_signing(), tx.signature_,
                     alice.keys.public_key));
}

TEST(TransactionFee, EqualityConsidersFee) {
  Wallet alice = make_wallet();
  Transaction low_fee = make_signed_tx(alice, "bob", 100, 1);
  Transaction high_fee = make_signed_tx(alice, "bob", 100, 2);

  EXPECT_FALSE(low_fee == high_fee);
}
