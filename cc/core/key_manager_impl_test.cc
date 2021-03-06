// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////////////

#include "tink/core/key_manager_impl.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tink/aead.h"
#include "tink/subtle/aes_gcm_boringssl.h"
#include "tink/subtle/random.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"
#include "tink/util/test_matchers.h"
#include "tink/util/test_util.h"
#include "tink/util/validation.h"
#include "proto/aes_gcm.pb.h"

namespace crypto {
namespace tink {
namespace internal {
namespace {

using ::crypto::tink::test::StatusIs;
using ::google::crypto::tink::AesGcmKey;
using ::google::crypto::tink::AesGcmKeyFormat;
using ::google::crypto::tink::KeyData;
using ::testing::_;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::SizeIs;

// A class for testing. We will construct objects from an aead key, so that we
// can check that a keymanager can handle multiple primitives. It is really
// insecure, as it does nothing except provide access to the key.
class AeadVariant {
 public:
  explicit AeadVariant(std::string s) : s_(s) {}

  std::string get() { return s_; }

 private:
  std::string s_;
};

class ExampleInternalKeyManager
    : public InternalKeyManager<AesGcmKey, AesGcmKeyFormat,
                                std::tuple<Aead, AeadVariant>> {
 public:
  class AeadFactory : public PrimitiveFactory<Aead> {
   public:
    crypto::tink::util::StatusOr<std::unique_ptr<Aead>> Create(
        const AesGcmKey& key) const override {
      // Ignore the key and returned one with a fixed size for this test.
      return {subtle::AesGcmBoringSsl::New(key.key_value())};
    }
  };

  class AeadVariantFactory : public PrimitiveFactory<AeadVariant> {
   public:
    crypto::tink::util::StatusOr<std::unique_ptr<AeadVariant>> Create(
        const AesGcmKey& key) const override {
      return absl::make_unique<AeadVariant>(key.key_value());
    }
  };

  ExampleInternalKeyManager()
      : InternalKeyManager(absl::make_unique<AeadFactory>(),
                           absl::make_unique<AeadVariantFactory>()) {}

  google::crypto::tink::KeyData::KeyMaterialType key_material_type()
      const override {
    return google::crypto::tink::KeyData::SYMMETRIC;
  }

  MOCK_CONST_METHOD0(get_version, uint32_t());

  // We mock out ValidateKey and ValidateKeyFormat so that we can easily test
  // proper behavior in case they return an error.
  MOCK_CONST_METHOD1(ValidateKey,
                     crypto::tink::util::Status(const AesGcmKey& key));
  MOCK_CONST_METHOD1(ValidateKeyFormat,
                     crypto::tink::util::Status(const AesGcmKeyFormat& key));

  const std::string& get_key_type() const override { return kKeyType; }


  crypto::tink::util::StatusOr<AesGcmKey> CreateKey(
      const AesGcmKeyFormat& key_format) const override {
    AesGcmKey result;
    result.set_key_value(subtle::Random::GetRandomBytes(key_format.key_size()));
    return result;
  }

 private:
  const std::string kKeyType = "type.googleapis.com/google.crypto.tink.AesGcmKey";
};

TEST(KeyManagerImplTest, FactoryNewKeyFromMessage) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  auto key = key_manager->get_key_factory().NewKey(key_format).ValueOrDie();

  EXPECT_THAT(dynamic_cast<AesGcmKey&>(*key).key_value(), SizeIs(16));
}

TEST(KeyManagerImplTest, FactoryNewKeyFromStringView) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  auto key = key_manager->get_key_factory()
                 .NewKey(key_format.SerializeAsString())
                 .ValueOrDie();

  EXPECT_THAT(dynamic_cast<AesGcmKey&>(*key).key_value(), SizeIs(16));
}

TEST(KeyManagerImplTest, FactoryNewKeyFromKeyData) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  auto key_data = *key_manager->get_key_factory()
                       .NewKeyData(key_format.SerializeAsString())
                       .ValueOrDie();

  AesGcmKey key;
  key.ParseFromString(key_data.value());
  EXPECT_THAT(key.key_value(), SizeIs(16));
}

TEST(KeyManagerImplTest, FactoryNewKeyFromMessageCallsValidate) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  EXPECT_CALL(internal_km, ValidateKeyFormat(_))
      .WillOnce(Return(ToStatusF(util::error::OUT_OF_RANGE,
                                 "FactoryNewKeyFromMessageCallsValidate")));
  EXPECT_THAT(key_manager->get_key_factory().NewKey(key_format).status(),
              StatusIs(util::error::OUT_OF_RANGE,
                       HasSubstr("FactoryNewKeyFromMessageCallsValidate")));
}

TEST(KeyManagerImplTest, FactoryNewKeyFromStringViewCallsValidate) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  EXPECT_CALL(internal_km, ValidateKeyFormat(_))
      .WillOnce(Return(ToStatusF(util::error::OUT_OF_RANGE,
                                 "FactoryNewKeyFromStringViewCallsValidate")));
  EXPECT_THAT(key_manager->get_key_factory()
                  .NewKey(key_format.SerializeAsString())
                  .status(),
              StatusIs(util::error::OUT_OF_RANGE,
                       HasSubstr("FactoryNewKeyFromStringViewCallsValidate")));
}

TEST(KeyManagerImplTest, FactoryNewKeyFromKeyDataCallsValidate) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  EXPECT_CALL(internal_km, ValidateKeyFormat(_))
      .WillOnce(Return(ToStatusF(util::error::OUT_OF_RANGE,
                                 "FactoryNewKeyFromKeyDataCallsValidate")));
  EXPECT_THAT(key_manager->get_key_factory()
                  .NewKeyData(key_format.SerializeAsString())
                  .status(),
              StatusIs(util::error::OUT_OF_RANGE,
                       HasSubstr("FactoryNewKeyFromKeyDataCallsValidate")));
}

TEST(KeyManagerImplTest, GetPrimitiveAead) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<Aead>> key_manager =
      MakeKeyManager<Aead>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);

  auto key_data = *key_manager->get_key_factory()
                       .NewKeyData(key_format.SerializeAsString())
                       .ValueOrDie();

  auto aead = key_manager->GetPrimitive(key_data).ValueOrDie();
  std::string encryption = aead->Encrypt("Hi", "aad").ValueOrDie();
  std::string decryption = aead->Decrypt(encryption, "aad").ValueOrDie();
  EXPECT_THAT(decryption, Eq("Hi"));
}

TEST(KeyManagerImplTest, GetPrimitiveAeadVariant) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  auto key_data = *key_manager->get_key_factory()
                       .NewKeyData(key_format.SerializeAsString())
                       .ValueOrDie();

  AesGcmKey key;
  key.ParseFromString(key_data.value());
  auto aead_variant = key_manager->GetPrimitive(key_data).ValueOrDie();
  EXPECT_THAT(aead_variant->get(), Eq(key.key_value()));
}

TEST(KeyManagerImplTest, GetPrimitiveFromKey) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<Aead>> key_manager =
      MakeKeyManager<Aead>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  auto key = key_manager->get_key_factory()
                 .NewKey(key_format.SerializeAsString())
                 .ValueOrDie();

  auto aead = key_manager->GetPrimitive(*key).ValueOrDie();
  std::string encryption = aead->Encrypt("Hi", "aad").ValueOrDie();
  std::string decryption = aead->Decrypt(encryption, "aad").ValueOrDie();
  EXPECT_THAT(decryption, Eq("Hi"));
}

TEST(KeyManagerImplTest, GetKeyType) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<Aead>> key_manager =
      MakeKeyManager<Aead>(&internal_km);
  EXPECT_THAT(key_manager->get_key_type(), Eq(internal_km.get_key_type()));
}

TEST(KeyManagerImplTest, GetVersion) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<Aead>> key_manager =
      MakeKeyManager<Aead>(&internal_km);
  EXPECT_CALL(internal_km, get_version()).WillOnce(Return(121351));
  EXPECT_THAT(121351, Eq(internal_km.get_version()));
}

TEST(KeyManagerImplTest, DoesSupport) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<Aead>> key_manager =
      MakeKeyManager<Aead>(&internal_km);
  EXPECT_TRUE(key_manager->DoesSupport(internal_km.get_key_type()));
  // Check with first and last letter removed.
  EXPECT_FALSE(key_manager->DoesSupport(
      "type.googleapis.com/google.crypto.tink.AesGcmKe"));
  EXPECT_FALSE(key_manager->DoesSupport(
      "ype.googleapis.com/google.crypto.tink.AesGcmKey"));
}

TEST(KeyManagerImplTest, GetPrimitiveCallsValidate) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<Aead>> key_manager =
      MakeKeyManager<Aead>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  auto key_data = *key_manager->get_key_factory()
                       .NewKeyData(key_format.SerializeAsString())
                       .ValueOrDie();

  AesGcmKey key;
  key.ParseFromString(key_data.value());

  EXPECT_CALL(internal_km, ValidateKey(_))
      .WillOnce(
          Return(ToStatusF(util::error::OUT_OF_RANGE,
                           "GetPrimitiveCallsValidate")));
  EXPECT_THAT(key_manager->GetPrimitive(key_data).status(),
              StatusIs(util::error::OUT_OF_RANGE,
                       HasSubstr("GetPrimitiveCallsValidate")));
}

TEST(KeyManagerImplTest, GetPrimitiveFromKeyCallsValidate) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<Aead>> key_manager =
      MakeKeyManager<Aead>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  auto key_data = *key_manager->get_key_factory()
                       .NewKeyData(key_format.SerializeAsString())
                       .ValueOrDie();

  AesGcmKey key;
  key.ParseFromString(key_data.value());

  EXPECT_CALL(internal_km, ValidateKey(_))
      .WillOnce(Return(
          ToStatusF(util::error::OUT_OF_RANGE,
                    "GetPrimitiveFromKeyCallsValidate")));
  EXPECT_THAT(key_manager->GetPrimitive(key).status(),
              StatusIs(util::error::OUT_OF_RANGE,
                       HasSubstr("GetPrimitiveFromKeyCallsValidate")));
}

// If we create a KeyManager for a not supported class, creating the key manager
// succeeds, but "GetPrimitive" will fail. Since MakeKeyManager is only supposed
// to be used internally, we are not doing extra work to make this a compile
// time error.
class NotSupported {};
TEST(KeyManagerImplTest, GetPrimitiveFails) {
  ExampleInternalKeyManager internal_km;
  std::unique_ptr<KeyManager<NotSupported>> key_manager =
      MakeKeyManager<NotSupported>(&internal_km);
  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  auto key_data = *key_manager->get_key_factory()
                       .NewKeyData(key_format.SerializeAsString())
                       .ValueOrDie();

  EXPECT_THAT(key_manager->GetPrimitive(key_data).status(),
              StatusIs(util::error::INVALID_ARGUMENT,
                       HasSubstr("No PrimitiveFactory was registered")));
}

// Next, we test some of the methods with an InternalKeyManager which has no
// factory.
class ExampleInternalKeyManagerWithoutFactory
    : public InternalKeyManager<AesGcmKey, void,
                                std::tuple<Aead, AeadVariant>> {
 public:
  class AeadFactory : public PrimitiveFactory<Aead> {
   public:
    crypto::tink::util::StatusOr<std::unique_ptr<Aead>> Create(
        const AesGcmKey& key) const override {
      // Ignore the key and returned one with a fixed size for this test.
      return {subtle::AesGcmBoringSsl::New(key.key_value())};
    }
  };

  class AeadVariantFactory : public PrimitiveFactory<AeadVariant> {
   public:
    crypto::tink::util::StatusOr<std::unique_ptr<AeadVariant>> Create(
        const AesGcmKey& key) const override {
      return absl::make_unique<AeadVariant>(key.key_value());
    }
  };

  ExampleInternalKeyManagerWithoutFactory()
      : InternalKeyManager(absl::make_unique<AeadFactory>(),
                           absl::make_unique<AeadVariantFactory>()) {}

  google::crypto::tink::KeyData::KeyMaterialType key_material_type()
      const override {
    return google::crypto::tink::KeyData::SYMMETRIC;
  }

  uint32_t get_version() const override { return kVersion; }

  const std::string& get_key_type() const override { return key_type_; }

  util::Status ValidateKey(const AesGcmKey& key) const override {
    util::Status status = ValidateVersion(key.version(), kVersion);
    if (!status.ok()) return status;
    return ValidateAesKeySize(key.key_value().size());
  }

 private:
  static const int kVersion = 0;
  const std::string key_type_ = "type.googleapis.com/google.crypto.tink.AesGcmKey";
};

TEST(KeyManagerImplTest, GetPrimitiveWithoutFactoryAead) {
  ExampleInternalKeyManagerWithoutFactory internal_km;
  std::unique_ptr<KeyManager<Aead>> key_manager =
      MakeKeyManager<Aead>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);

  KeyData key_data = test::AsKeyData(
      ExampleInternalKeyManager().CreateKey(key_format).ValueOrDie(),
      KeyData::SYMMETRIC);

  auto aead = key_manager->GetPrimitive(key_data).ValueOrDie();
  std::string encryption = aead->Encrypt("Hi", "aad").ValueOrDie();
  std::string decryption = aead->Decrypt(encryption, "aad").ValueOrDie();
  EXPECT_THAT(decryption, Eq("Hi"));
}

TEST(KeyManagerImplTest, NonexistentFactoryNewKeyFromMessage) {
  ExampleInternalKeyManagerWithoutFactory internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  EXPECT_THAT(key_manager->get_key_factory().NewKey(key_format).status(),
              StatusIs(util::error::UNIMPLEMENTED));
}

TEST(KeyManagerImplTest, NonexistentFactoryNewKeyFromStringView) {
  ExampleInternalKeyManagerWithoutFactory internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);

  EXPECT_THAT(key_manager->get_key_factory()
                  .NewKey(key_format.SerializeAsString())
                  .status(),
              StatusIs(util::error::UNIMPLEMENTED));
}

TEST(KeyManagerImplTest, NonexistentFactoryNewKeyFromKeyData) {
  ExampleInternalKeyManagerWithoutFactory internal_km;
  std::unique_ptr<KeyManager<AeadVariant>> key_manager =
      MakeKeyManager<AeadVariant>(&internal_km);

  AesGcmKeyFormat key_format;
  key_format.set_key_size(16);
  EXPECT_THAT(key_manager->get_key_factory()
                  .NewKeyData(key_format.SerializeAsString())
                  .status(),
              StatusIs(util::error::UNIMPLEMENTED));
}


}  // namespace

}  // namespace internal
}  // namespace tink
}  // namespace crypto
