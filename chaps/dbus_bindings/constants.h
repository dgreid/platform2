// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_DBUS_BINDINGS_CONSTANTS_H_
#define CHAPS_DBUS_BINDINGS_CONSTANTS_H_

namespace chaps {
// chaps
const char kChapsInterface[] = "org.chromium.Chaps";
const char kChapsServicePath[] = "/org/chromium/Chaps";
const char kChapsServiceName[] = "org.chromium.Chaps";
// Methods exposed by chaps.
const char kOpenIsolateMethod[] = "OpenIsolate";
const char kCloseIsolateMethod[] = "CloseIsolate";
const char kLoadTokenMethod[] = "LoadToken";
const char kUnloadTokenMethod[] = "UnloadToken";
const char kChangeTokenAuthDataMethod[] = "ChangeTokenAuthData";
const char kGetTokenPathMethod[] = "GetTokenPath";
const char kSetLogLevelMethod[] = "SetLogLevel";
const char kGetSlotListMethod[] = "GetSlotList";
const char kGetSlotInfoMethod[] = "GetSlotInfo";
const char kGetTokenInfoMethod[] = "GetTokenInfo";
const char kGetMechanismListMethod[] = "GetMechanismList";
const char kGetMechanismInfoMethod[] = "GetMechanismInfo";
const char kInitTokenMethod[] = "InitToken";
const char kInitPINMethod[] = "InitPIN";
const char kSetPINMethod[] = "SetPIN";
const char kOpenSessionMethod[] = "OpenSession";
const char kCloseSessionMethod[] = "CloseSession";
const char kGetSessionInfoMethod[] = "GetSessionInfo";
const char kGetOperationStateMethod[] = "GetOperationState";
const char kSetOperationStateMethod[] = "SetOperationState";
const char kLoginMethod[] = "Login";
const char kLogoutMethod[] = "Logout";
const char kCreateObjectMethod[] = "CreateObject";
const char kCopyObjectMethod[] = "CopyObject";
const char kDestroyObjectMethod[] = "DestroyObject";
const char kGetObjectSizeMethod[] = "GetObjectSize";
const char kGetAttributeValueMethod[] = "GetAttributeValue";
const char kSetAttributeValueMethod[] = "SetAttributeValue";
const char kFindObjectsInitMethod[] = "FindObjectsInit";
const char kFindObjectsMethod[] = "FindObjects";
const char kFindObjectsFinalMethod[] = "FindObjectsFinal";
const char kEncryptInitMethod[] = "EncryptInit";
const char kEncryptMethod[] = "Encrypt";
const char kEncryptUpdateMethod[] = "EncryptUpdate";
const char kEncryptFinalMethod[] = "EncryptFinal";
const char kEncryptCancelMethod[] = "EncryptCancel";
const char kDecryptInitMethod[] = "DecryptInit";
const char kDecryptMethod[] = "Decrypt";
const char kDecryptUpdateMethod[] = "DecryptUpdate";
const char kDecryptFinalMethod[] = "DecryptFinal";
const char kDecryptCancelMethod[] = "DecryptCancel";
const char kDigestInitMethod[] = "DigestInit";
const char kDigestMethod[] = "Digest";
const char kDigestUpdateMethod[] = "DigestUpdate";
const char kDigestKeyMethod[] = "DigestKey";
const char kDigestFinalMethod[] = "DigestFinal";
const char kDigestCancelMethod[] = "DigestCancel";
const char kSignInitMethod[] = "SignInit";
const char kSignMethod[] = "Sign";
const char kSignUpdateMethod[] = "SignUpdate";
const char kSignFinalMethod[] = "SignFinal";
const char kSignCancelMethod[] = "SignCancel";
const char kSignRecoverInitMethod[] = "SignRecoverInit";
const char kSignRecoverMethod[] = "SignRecover";
const char kVerifyInitMethod[] = "VerifyInit";
const char kVerifyMethod[] = "Verify";
const char kVerifyUpdateMethod[] = "VerifyUpdate";
const char kVerifyFinalMethod[] = "VerifyFinal";
const char kVerifyCancelMethod[] = "VerifyCancel";
const char kVerifyRecoverInitMethod[] = "VerifyRecoverInit";
const char kVerifyRecoverMethod[] = "VerifyRecover";
const char kDigestEncryptUpdateMethod[] = "DigestEncryptUpdate";
const char kDecryptDigestUpdateMethod[] = "DecryptDigestUpdate";
const char kSignEncryptUpdateMethod[] = "SignEncryptUpdate";
const char kDecryptVerifyUpdateMethod[] = "DecryptVerifyUpdate";
const char kGenerateKeyMethod[] = "GenerateKey";
const char kGenerateKeyPairMethod[] = "GenerateKeyPair";
const char kWrapKeyMethod[] = "WrapKey";
const char kUnwrapKeyMethod[] = "UnwrapKey";
const char kDeriveKeyMethod[] = "DeriveKey";
const char kSeedRandomMethod[] = "SeedRandom";
const char kGenerateRandomMethod[] = "GenerateRandom";
}  // namespace chaps

#endif  // CHAPS_DBUS_BINDINGS_CONSTANTS_H_
