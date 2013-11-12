// Copyright (c) 2013 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/aes_encryptor.h"

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const uint32 kAesBlockSize = 16;

// From NIST SP 800-38a test case: - F.5.1 CTR-AES128.Encrypt
// http://csrc.nist.gov/publications/nistpubs/800-38a/sp800-38a.pdf
const uint8 kAesCtrKey[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                            0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

const uint8 kAesCtrIv[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                           0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};

const uint8 kAesCtrPlaintext[] = {
    // Block #1
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    // Block #2
    0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
    0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
    // Block #3
    0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
    0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
    // Block #4
    0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
    0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};

const uint8 kAesCtrCiphertext[] = {
    // Block #1
    0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
    0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
    // Block #2
    0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
    0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
    // Block #3
    0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
    0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
    // Block #4
    0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
    0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee};

// Subsample test cases.
struct SubsampleTestCase {
  const uint8* subsample_sizes;
  uint32 subsample_count;
};

const uint8 kSubsampleTest1[] = {64};
const uint8 kSubsampleTest2[] = {13, 51};
const uint8 kSubsampleTest3[] = {52, 12};
const uint8 kSubsampleTest4[] = {16, 48};
const uint8 kSubsampleTest5[] = {3, 16, 45};
const uint8 kSubsampleTest6[] = {18, 16, 34};
const uint8 kSubsampleTest7[] = {8, 16, 2, 38};
const uint8 kSubsampleTest8[] = {10, 1, 33, 20};
const uint8 kSubsampleTest9[] = {7, 19, 6, 32};
const uint8 kSubsampleTest10[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 9};

const SubsampleTestCase kSubsampleTestCases[] = {
    {kSubsampleTest1, arraysize(kSubsampleTest1)},
    {kSubsampleTest2, arraysize(kSubsampleTest2)},
    {kSubsampleTest3, arraysize(kSubsampleTest3)},
    {kSubsampleTest4, arraysize(kSubsampleTest4)},
    {kSubsampleTest5, arraysize(kSubsampleTest5)},
    {kSubsampleTest6, arraysize(kSubsampleTest6)},
    {kSubsampleTest7, arraysize(kSubsampleTest7)},
    {kSubsampleTest8, arraysize(kSubsampleTest8)},
    {kSubsampleTest9, arraysize(kSubsampleTest9)},
    {kSubsampleTest10, arraysize(kSubsampleTest10)}, };

// IV test values.
const uint32 kTextSizeInBytes = 60;  // 3 full blocks + 1 partial block.

const uint8 kIv128Zero[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const uint8 kIv128Two[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
const uint8 kIv128Four[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
const uint8 kIv128Max64[] = {0,    0,    0,    0,    0,    0,    0,    0,
                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint8 kIv128OneAndThree[] = {0, 0, 0, 0, 0, 0, 0, 1,
                                   0, 0, 0, 0, 0, 0, 0, 3};
const uint8 kIv128MaxMinusOne[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0xff, 0xff, 0xff, 0xfe};

const uint8 kIv64Zero[] = {0, 0, 0, 0, 0, 0, 0, 0};
const uint8 kIv64One[] = {0, 0, 0, 0, 0, 0, 0, 1};
const uint8 kIv64MaxMinusOne[] = {0xff, 0xff, 0xff, 0xff,
                                  0xff, 0xff, 0xff, 0xfe};
const uint8 kIv64Max[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct IvTestCase {
  const uint8* iv_test;
  uint32 iv_size;
  const uint8* iv_expected;
};

// As recommended in ISO/IEC FDIS 23001-7: CENC spec,
// For 64-bit (8-byte) IV_Sizes, initialization vectors for subsequent samples
// can be created by incrementing the initialization vector of the previous
// sample. For 128-bit (16-byte) IV_Sizes, initialization vectors for subsequent
// samples should be created by adding the block count of the previous sample to
// the initialization vector of the previous sample.
const IvTestCase kIvTestCases[] = {
    {kIv128Zero, arraysize(kIv128Zero), kIv128Four},
    {kIv128Max64, arraysize(kIv128Max64), kIv128OneAndThree},
    {kIv128MaxMinusOne, arraysize(kIv128MaxMinusOne), kIv128Two},
    {kIv64Zero, arraysize(kIv64Zero), kIv64One},
    {kIv64MaxMinusOne, arraysize(kIv64MaxMinusOne), kIv64Max},
    {kIv64Max, arraysize(kIv64Max), kIv64Zero}, };

// We support AES 128, i.e. 16 bytes key only.
const uint8 kInvalidKey[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2,
                             0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, };

// We support Iv of size 8 or 16 only as defined in CENC spec.
const uint8 kInvalidIv[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                            0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, };

}  // namespace

namespace media {

class AesEncryptorTest : public testing::Test {
 public:
  virtual void SetUp() {
    key_.assign(kAesCtrKey, kAesCtrKey + arraysize(kAesCtrKey));
    iv_.assign(kAesCtrIv, kAesCtrIv + arraysize(kAesCtrIv));
    plaintext_.assign(kAesCtrPlaintext,
                      kAesCtrPlaintext + arraysize(kAesCtrPlaintext));
    ciphertext_.assign(kAesCtrCiphertext,
                       kAesCtrCiphertext + arraysize(kAesCtrCiphertext));

    ASSERT_TRUE(encryptor_.InitializeWithIv(key_, iv_));
  }

 protected:
  std::vector<uint8> key_;
  std::vector<uint8> iv_;
  std::vector<uint8> plaintext_;
  std::vector<uint8> ciphertext_;
  AesCtrEncryptor encryptor_;
};

TEST_F(AesEncryptorTest, NistTestCase) {
  std::vector<uint8> encrypted;
  EXPECT_TRUE(encryptor_.Encrypt(plaintext_, &encrypted));
  EXPECT_EQ(ciphertext_, encrypted);

  encryptor_.SetIv(iv_);
  std::vector<uint8> decrypted;
  EXPECT_TRUE(encryptor_.Decrypt(encrypted, &decrypted));
  EXPECT_EQ(plaintext_, decrypted);
}

TEST_F(AesEncryptorTest, NistTestCaseInplaceEncryptionDecryption) {
  std::vector<uint8> buffer = plaintext_;
  EXPECT_TRUE(encryptor_.Encrypt(&buffer[0], buffer.size(), &buffer[0]));
  EXPECT_EQ(ciphertext_, buffer);

  encryptor_.SetIv(iv_);
  EXPECT_TRUE(encryptor_.Decrypt(&buffer[0], buffer.size(), &buffer[0]));
  EXPECT_EQ(plaintext_, buffer);
}

TEST_F(AesEncryptorTest, EncryptDecryptString) {
  static const char kPlaintext[] = "normal plaintext of random length";
  static const char kExpectedCiphertextInHex[] =
      "82E3AD1EF90C5CC09EB37F1B9EFBD99016441A1C15123F0777CD57BB993E14DA02";

  std::string ciphertext;
  EXPECT_TRUE(encryptor_.Encrypt(kPlaintext, &ciphertext));
  EXPECT_EQ(kExpectedCiphertextInHex,
            base::HexEncode(ciphertext.data(), ciphertext.size()));

  std::string decrypted;
  encryptor_.SetIv(iv_);
  EXPECT_TRUE(encryptor_.Decrypt(ciphertext, &decrypted));
  EXPECT_EQ(kPlaintext, decrypted);
}

TEST_F(AesEncryptorTest, 128BitIVBoundaryCaseEncryption) {
  // There are four blocks of text in |plaintext_|. The first block should be
  // encrypted with IV = kIv128Max64, the subsequent blocks should be encrypted
  // with iv 0 to 3.
  std::vector<uint8> iv_max64(kIv128Max64,
                              kIv128Max64 + arraysize(kIv128Max64));
  ASSERT_TRUE(encryptor_.InitializeWithIv(key_, iv_max64));
  std::vector<uint8> encrypted;
  EXPECT_TRUE(encryptor_.Encrypt(plaintext_, &encrypted));

  std::vector<uint8> iv_one_and_three(
      kIv128OneAndThree, kIv128OneAndThree + arraysize(kIv128OneAndThree));
  encryptor_.UpdateIv();
  EXPECT_EQ(iv_one_and_three, encryptor_.iv());

  ASSERT_TRUE(encryptor_.InitializeWithIv(key_, iv_max64));
  std::vector<uint8> encrypted_verify(plaintext_.size(), 0);
  EXPECT_TRUE(
      encryptor_.Encrypt(&plaintext_[0], kAesBlockSize, &encrypted_verify[0]));
  std::vector<uint8> iv_zero(kIv128Zero, kIv128Zero + arraysize(kIv128Zero));
  ASSERT_TRUE(encryptor_.InitializeWithIv(key_, iv_zero));
  EXPECT_TRUE(encryptor_.Encrypt(&plaintext_[kAesBlockSize],
                                 kAesBlockSize * 3,
                                 &encrypted_verify[kAesBlockSize]));
  EXPECT_EQ(encrypted, encrypted_verify);
}

TEST_F(AesEncryptorTest, InitWithRandomIv) {
  const uint8 kIvSize = 8;
  ASSERT_TRUE(encryptor_.InitializeWithRandomIv(key_, kIvSize));
  ASSERT_EQ(kIvSize, encryptor_.iv().size());
  LOG(INFO) << "Random IV: " << base::HexEncode(&encryptor_.iv()[0],
                                                encryptor_.iv().size());
}

TEST_F(AesEncryptorTest, UnsupportedKeySize) {
  std::vector<uint8> key(kInvalidKey, kInvalidKey + arraysize(kInvalidKey));
  ASSERT_DEATH(encryptor_.InitializeWithIv(key, iv_), "");
}

TEST_F(AesEncryptorTest, UnsupportedIV) {
  std::vector<uint8> iv(kInvalidIv, kInvalidIv + arraysize(kInvalidIv));
  ASSERT_DEATH(encryptor_.InitializeWithIv(key_, iv), "");
}

TEST_F(AesEncryptorTest, IncorrectIvSize) {
  ASSERT_DEATH(encryptor_.InitializeWithRandomIv(key_, 15), "");
}

class AesCtrEncryptorSubsampleTest
    : public AesEncryptorTest,
      public ::testing::WithParamInterface<SubsampleTestCase> {};

TEST_P(AesCtrEncryptorSubsampleTest, NistTestCaseSubsamples) {
  const SubsampleTestCase* test_case = &GetParam();

  std::vector<uint8> encrypted(plaintext_.size(), 0);
  for (uint32 i = 0, offset = 0; i < test_case->subsample_count; ++i) {
    uint32 len = test_case->subsample_sizes[i];
    EXPECT_TRUE(
        encryptor_.Encrypt(&plaintext_[offset], len, &encrypted[offset]));
    offset += len;
    EXPECT_EQ(offset % kAesBlockSize, encryptor_.block_offset());
  }
  EXPECT_EQ(ciphertext_, encrypted);

  encryptor_.SetIv(iv_);
  std::vector<uint8> decrypted(encrypted.size(), 0);
  for (uint32 i = 0, offset = 0; i < test_case->subsample_count; ++i) {
    uint32 len = test_case->subsample_sizes[i];
    EXPECT_TRUE(
        encryptor_.Decrypt(&encrypted[offset], len, &decrypted[offset]));
    offset += len;
    EXPECT_EQ(offset % kAesBlockSize, encryptor_.block_offset());
  }
  EXPECT_EQ(plaintext_, decrypted);
}

INSTANTIATE_TEST_CASE_P(SubsampleTestCases,
                        AesCtrEncryptorSubsampleTest,
                        ::testing::ValuesIn(kSubsampleTestCases));

class AesCtrEncryptorIvTest : public ::testing::TestWithParam<IvTestCase> {};

TEST_P(AesCtrEncryptorIvTest, IvTest) {
  // Some dummy key and plaintext.
  std::vector<uint8> key(16, 1);
  std::vector<uint8> plaintext(kTextSizeInBytes, 3);

  std::vector<uint8> iv_test(GetParam().iv_test,
                             GetParam().iv_test + GetParam().iv_size);
  std::vector<uint8> iv_expected(GetParam().iv_expected,
                                 GetParam().iv_expected + GetParam().iv_size);

  AesCtrEncryptor encryptor;
  ASSERT_TRUE(encryptor.InitializeWithIv(key, iv_test));

  std::vector<uint8> encrypted;
  EXPECT_TRUE(encryptor.Encrypt(plaintext, &encrypted));
  encryptor.UpdateIv();
  EXPECT_EQ(iv_expected, encryptor.iv());
}

INSTANTIATE_TEST_CASE_P(IvTestCases,
                        AesCtrEncryptorIvTest,
                        ::testing::ValuesIn(kIvTestCases));

}  // namespace media
