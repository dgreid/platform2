// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_METRICS_H_
#define CRYPTOHOME_TPM_METRICS_H_

#include "cryptohome/tpm.h"

namespace cryptohome {

// WARNING: The values in the following enum are written to logs and the
// elements must not be renumbered or deleted (and hence possibly reused).
enum TpmResult {
  kTpmSuccess = 1,  // TSS_SUCCESS.

  // TSS_LAYER_TPM errors:
  kTpmErrorAuthenticationFail = 2,             // TPM_E_AUTHFAIL.
  kTpmErrorBadParameter = 3,                   // TPM_E_BAD_PARAMETER.
  kTpmErrorBadIndex = 4,                       // TPM_E_BADINDEX.
  kTpmErrorAuditFail = 5,                      // TPM_E_AUDITFAILURE.
  kTpmErrorClearDisabled = 6,                  // TPM_E_CLEAR_DISABLED.
  kTpmErrorTpmDeactivated = 7,                 // TPM_E_DEACTIVATED.
  kTpmErrorTpmDisabled = 8,                    // TPM_E_DISABLED.
  kTpmErrorFailed = 9,                         // TPM_E_FAIL.
  kTpmErrorBadOrdinal = 10,                    // TPM_E_BAD_ORDINAL.
  kTpmErrorOwnerInstallDisabled = 11,          // TPM_E_INSTALL_DISABLED.
  kTpmErrorInvalidKeyHandle = 12,              // TPM_E_INVALID_KEYHANDLE.
  kTpmErrorKeyNotFound = 13,                   // TPM_E_KEYNOTFOUND.
  kTpmErrorBadEncryptionScheme = 14,           // TPM_E_INAPPROPRIATE_ENC.
  kTpmErrorMigrationAuthorizationFail = 15,    // TPM_E_MIGRATEFAIL.
  kTpmErrorInvalidPcrInfo = 16,                // TPM_E_INVALID_PCR_INFO.
  kTpmErrorNoSpaceToLoadKey = 17,              // TPM_E_NOSPACE.
  kTpmErrorNoSrk = 18,                         // TPM_E_NOSRK.
  kTpmErrorInvalidEncryptedBlob = 19,          // TPM_E_NOTSEALED_BLOB.
  kTpmErrorOwnerAlreadySet = 20,               // TPM_E_OWNER_SET.
  kTpmErrorNotEnoughTpmResources = 21,         // TPM_E_RESOURCES.
  kTpmErrorRandomStringTooShort = 22,          // TPM_E_SHORTRANDOM.
  kTpmErrorTpmOutOfSpace = 23,                 // TPM_E_SIZE.
  kTpmErrorWrongPcrValue = 24,                 // TPM_E_WRONGPCRVAL.
  kTpmErrorBadParamSize = 25,                  // TPM_E_BAD_PARAM_SIZE.
  kTpmErrorNoSha1Thread = 26,                  // TPM_E_SHA_THREAD.
  kTpmErrorSha1Error = 27,                     // TPM_E_SHA_ERROR.
  kTpmErrorTpmSelfTestFailed = 28,             // TPM_E_FAILEDSELFTEST.
  kTpmErrorSecondAuthorizationFailed = 29,     // TPM_E_AUTH2FAIL.
  kTpmErrorBadTag = 30,                        // TPM_E_BADTAG.
  kTpmErrorIOError = 31,                       // TPM_E_IOERROR.
  kTpmErrorEncryptionError = 32,               // TPM_E_ENCRYPT_ERROR.
  kTpmErrorDecryptionError = 33,               // TPM_E_DECRYPT_ERROR.
  kTpmErrorInvalidAuthorizationHandle = 34,    // TPM_E_INVALID_AUTHHANDLE.
  kTpmErrorNoEndorsement = 35,                 // TPM_E_NO_ENDORSEMENT.
  kTpmErrorInvalidKeyUsage = 36,               // TPM_E_INVALID_KEYUSAGE.
  kTpmErrorWrongEntityType = 37,               // TPM_E_WRONG_ENTITYTYPE.
  kTpmErrorInvalidPostInitSequence = 38,       // TPM_E_INVALID_POSTINIT.
  kTpmErrorInvalidSignatureFormat = 39,        // TPM_E_INAPPROPRIATE_SIG.
  kTpmErrorBadKeyProperty = 40,                // TPM_E_BAD_KEY_PROPERTY.
  kTpmErrorBadMigration = 41,                  // TPM_E_BAD_MIGRATION.
  kTpmErrorBadScheme = 42,                     // TPM_E_BAD_SCHEME.
  kTpmErrorBadDataSize = 43,                   // TPM_E_BAD_DATASIZE.
  kTpmErrorBadModeParameter = 44,              // TPM_E_BAD_MODE.
  kTpmErrorBadPresenceValue = 45,              // TPM_E_BAD_PRESENCE.
  kTpmErrorBadVersion = 46,                    // TPM_E_BAD_VERSION.
  kTpmErrorWrapTransportNotAllowed = 47,       // TPM_E_NO_WRAP_TRANSPORT.
  kTpmErrorAuditFailCommandUnsuccessful = 48,  // TPM_E_AUDITFAIL_UNSUCCESSFUL.
  kTpmErrorAuditFailCommandSuccessful = 49,    // TPM_E_AUDITFAIL_SUCCESSFUL.
  kTpmErrorPcrRegisterNotResetable = 50,       // TPM_E_NOTRESETABLE.
  kTpmErrorPcrRegisterResetRequiresLocality = 51,  // TPM_E_NOTLOCAL.
  kTpmErrorBadTypeOfIdentityBlob = 52,             // TPM_E_BAD_TYPE.
  kTpmErrorBadResourceType = 53,                   // TPM_E_INVALID_RESOURCE.
  kTpmErrorCommandAvailableOnlyInFipsMode = 54,    // TPM_E_NOTFIPS.
  kTpmErrorInvalidFamilyId = 55,                   // TPM_E_INVALID_FAMILY.
  kTpmErrorNoNvRamPermission = 56,                 // TPM_E_NO_NV_PERMISSION.
  kTpmErrorSignedCommandRequired = 57,             // TPM_E_REQUIRES_SIGN.
  kTpmErrorNvRamKeyNotSupported = 58,              // TPM_E_KEY_NOTSUPPORTED.
  kTpmErrorAuthorizationConflict = 59,             // TPM_E_AUTH_CONFLICT.
  kTpmErrorNvRamAreaLocked = 60,                   // TPM_E_AREA_LOCKED.
  kTpmErrorBadLocality = 61,                       // TPM_E_BAD_LOCALITY.
  kTpmErrorNvRamAreaReadOnly = 62,                 // TPM_E_READ_ONLY.
  kTpmErrorNvRamAreaNoWriteProtection = 63,        // TPM_E_PER_NOWRITE.
  kTpmErrorFamilyCountMismatch = 64,               // TPM_E_FAMILYCOUNT.
  kTpmErrorNvRamAreaWriteLocked = 65,              // TPM_E_WRITE_LOCKED.
  kTpmErrorNvRamAreaBadAttributes = 66,            // TPM_E_BAD_ATTRIBUTES.
  kTpmErrorInvalidStructure = 67,                  // TPM_E_INVALID_STRUCTURE.
  kTpmErrorKeyUnderOwnerControl = 68,              // TPM_E_KEY_OWNER_CONTROL.
  kTpmErrorBadCounterHandle = 69,                  // TPM_E_BAD_COUNTER.
  kTpmErrorNotAFullWrite = 70,                     // TPM_E_NOT_FULLWRITE.
  kTpmErrorContextGap = 71,                        // TPM_E_CONTEXT_GAP.
  kTpmErrorMaxNvRamWrites = 72,                    // TPM_E_MAXNVWRITES.
  kTpmErrorNoOperator = 73,                        // TPM_E_NOOPERATOR.
  kTpmErrorResourceMissing = 74,                   // TPM_E_RESOURCEMISSING.
  kTpmErrorDelagteLocked = 75,                     // TPM_E_DELEGATE_LOCK.
  kTpmErrorDelegateFamily = 76,                    // TPM_E_DELEGATE_FAMILY.
  kTpmErrorDelegateAdmin = 77,                     // TPM_E_DELEGATE_ADMIN.
  kTpmErrorTransportNotExclusive = 78,     // TPM_E_TRANSPORT_NOTEXCLUSIVE.
  kTpmErrorOwnerControl = 79,              // TPM_E_OWNER_CONTROL.
  kTpmErrorDaaResourcesNotAvailable = 80,  // TPM_E_DAA_RESOURCES.
  kTpmErrorDaaInputData0 = 81,             // TPM_E_DAA_INPUT_DATA0.
  kTpmErrorDaaInputData1 = 82,             // TPM_E_DAA_INPUT_DATA1.
  kTpmErrorDaaIssuerSettings = 83,         // TPM_E_DAA_ISSUER_SETTINGS.
  kTpmErrorDaaTpmSettings = 84,            // TPM_E_DAA_TPM_SETTINGS.
  kTpmErrorDaaStage = 85,                  // TPM_E_DAA_STAGE.
  kTpmErrorDaaIssuerValidity = 86,         // TPM_E_DAA_ISSUER_VALIDITY.
  kTpmErrorDaaWrongW = 87,                 // TPM_E_DAA_WRONG_W.
  kTpmErrorBadHandle = 88,                 // TPM_E_BAD_HANDLE.
  kTpmErrorBadDelegate = 89,               // TPM_E_BAD_DELEGATE.
  kTpmErrorBadContextBlob = 90,            // TPM_E_BADCONTEXT.
  kTpmErrorTooManyContexts = 91,           // TPM_E_TOOMANYCONTEXTS.
  kTpmErrorMigrationAuthoritySignatureFail = 92,  // TPM_E_MA_TICKET_SIGNATURE.
  kTpmErrorMigrationDestinationNotAuthenticated = 93,  // TPM_E_MA_DESTINATION.
  kTpmErrorBadMigrationSource = 94,                    // TPM_E_MA_SOURCE.
  kTpmErrorBadMigrationAuthority = 95,                 // TPM_E_MA_AUTHORITY.
  kTpmErrorPermanentEk = 96,                           // TPM_E_PERMANENTEK.
  kTpmErrorCmkTicketBadSignature = 97,                 // TPM_E_BAD_SIGNATURE.
  kTpmErrorNoContextSpace = 98,                        // TPM_E_NOCONTEXTSPACE.
  kTpmErrorTpmBusyRetryLater = 99,                     // TPM_E_RETRY.
  kTpmErrorNeedsSelfTest = 100,                        // TPM_E_NEEDS_SELFTEST.
  kTpmErrorDoingSelfTest = 101,                        // TPM_E_DOING_SELFTEST.
  kTpmErrorDefendLockRunning = 102,   // TPM_E_DEFEND_LOCK_RUNNING.
  kTpmErrorTpmCommandDisabled = 103,  // TPM_E_DISABLED_CMD.
  kTpmErrorUnknownError = 104,        //  Unknown TPM error.

  // TSS_LAYER_TDDL errors:
  kTddlErrorGeneralFail = 105,                     // TSS_E_FAIL.
  kTddlErrorBadParameter = 106,                    // TSS_E_BAD_PARAMETER.
  kTddlErrorInternalSoftwareError = 107,           // TSS_E_INTERNAL_ERROR.
  kTddlErrorNotImplemented = 108,                  // TSS_E_NOTIMPL.
  kTddlErrorKeyNotFoundInPersistentStorage = 109,  // TSS_E_PS_KEY_NOTFOUND.
  kTddlErrorKeyAlreadyRegistered = 110,     // TSS_E_KEY_ALREADY_REGISTERED.
  kTddlErrorActionCanceledByRequest = 111,  // TSS_E_CANCELED.
  kTddlErrorTimeout = 112,                  // TSS_E_TIMEOUT.
  kTddlErrorOutOfMemory = 113,              // TSS_E_OUTOFMEMORY.
  kTddlErrorUnexpectedTpmOutput = 114,      // TSS_E_TPM_UNEXPECTED.
  kTddlErrorCommunicationFailure = 115,     // TSS_E_COMM_FAILURE.
  kTddlErrorTpmUnsupportedFeature = 116,    // TSS_E_TPM_UNSUPPORTED_FEATURE.
  kTddlErrorConnectionToTpmDeviceFailed = 117,  // TDDL_E_COMPONENT_NOT_FOUND.
  kTddlErrorDeviceAlreadyOpened = 118,          // TDDL_E_ALREADY_OPENED.
  kTddlErrorBadTag = 119,                       // TDDL_E_BADTAG.
  kTddlErrorReceiveBufferTooSmall = 120,        // TDDL_E_INSUFFICIENT_BUFFER.
  kTddlErrorCommandAlreadyCompleted = 121,      // TDDL_E_COMMAND_COMPLETED.
  kTddlErrorCommandAborted = 122,               // TDDL_E_COMMAND_ABORTED.
  kTddlErrorDeviceDriverAlreadyClosed = 123,    // TDDL_E_ALREADY_CLOSED.
  kTddlErrorIOError = 124,                      // TDDL_E_IOERROR.
  kTddlErrorUnknownError = 125,                 // Unknown TDDL error.

  // TSS_LAYER_TCS errors:
  kTcsErrorGeneralFail = 126,                     // TSS_E_FAIL.
  kTcsErrorBadParameter = 127,                    // TSS_E_BAD_PARAMETER.
  kTcsErrorInternalSoftwareError = 128,           // TSS_E_INTERNAL_ERROR.
  kTcsErrorNotImplemented = 129,                  // TSS_E_NOTIMPL.
  kTcsErrorKeyNotFoundInPersistentStorage = 130,  // TSS_E_PS_KEY_NOTFOUND.
  kTcsErrorKeyAlreadyRegistered = 131,     // TSS_E_KEY_ALREADY_REGISTERED.
  kTcsErrorActionCanceledByRequest = 132,  // TSS_E_CANCELED.
  kTcsErrorTimeout = 133,                  //               // TSS_E_TIMEOUT.
  kTcsErrorOutOfMemory = 134,              // TSS_E_OUTOFMEMORY.
  kTcsErrorUnexpectedTpmOutput = 135,      // TSS_E_TPM_UNEXPECTED.
  kTcsErrorCommunicationFailure = 136,     // TSS_E_COMM_FAILURE.
  kTcsErrorTpmUnsupportedFeature = 137,    // TSS_E_TPM_UNSUPPORTED_FEATURE.
  kTcsErrorKeyMismatch = 138,              // TCS_E_KEY_MISMATCH.
  kTcsErrorKeyLoadFail = 139,              // TCS_E_KM_LOADFAILED.
  kTcsErrorKeyContextReloadFail = 140,     // TCS_E_KEY_CONTEXT_RELOAD.
  kTcsErrorBadMemoryIndex = 141,           // TCS_E_BAD_INDEX.
  kTcsErrorBadContextHandle = 142,         // TCS_E_INVALID_CONTEXTHANDLE.
  kTcsErrorBadKeyHandle = 143,             // TCS_E_INVALID_KEYHANDLE.
  kTcsErrorBadAuthorizationHandle = 144,   // TCS_E_INVALID_AUTHHANDLE.
  kTcsErrorAuthorizationSessionClosedByTpm = 145,  // TCS_E_INVALID_AUTHSESSION.
  kTcsErrorInvalidKey = 146,                       // TCS_E_INVALID_KEY.
  kTcsErrorUnknownError = 147,                     //  Unknown TCS error.

  // Other TSS errors:
  kTssErrorGeneralFail = 148,                     // TSS_E_FAIL.
  kTssErrorBadParameter = 149,                    // TSS_E_BAD_PARAMETER.
  kTssErrorInternalSoftwareError = 150,           // TSS_E_INTERNAL_ERROR.
  kTssErrorNotImplemented = 151,                  // TSS_E_NOTIMPL.
  kTssErrorKeyNotFoundInPersistentStorage = 152,  // TSS_E_PS_KEY_NOTFOUND.
  kTssErrorKeyAlreadyRegistered = 153,         // TSS_E_KEY_ALREADY_REGISTERED.
  kTssErrorActionCanceledByRequest = 154,      // TSS_E_CANCELED.
  kTssErrorTimeout = 155,                      // TSS_E_TIMEOUT.
  kTssErrorOutOfMemory = 156,                  // TSS_E_OUTOFMEMORY.
  kTssErrorUnexpectedTpmOutput = 157,          // TSS_E_TPM_UNEXPECTED.
  kTssErrorCommunicationFailure = 158,         // TSS_E_COMM_FAILURE.
  kTssErrorTpmUnsupportedFeature = 159,        // TSS_E_TPM_UNSUPPORTED_FEATURE.
  kTssErrorBadObjectType = 160,                // TSS_E_INVALID_OBJECT_TYPE.
  kTssErrorBadObjectInitFlag = 161,            // TSS_E_INVALID_OBJECT_INITFLAG.
  kTssErrorInvalidHandle = 162,                // TSS_E_INVALID_HANDLE.
  kTssErrorNoCoreServiceConnection = 163,      // TSS_E_NO_CONNECTION.
  kTssErrorCoreServiceConnectionFail = 164,    // TSS_E_CONNECTION_FAILED.
  kTssErrorCoreServiceConnectionBroken = 165,  // TSS_E_CONNECTION_BROKEN.
  kTssErrorInvalidHashAlgorithm = 166,         // TSS_E_HASH_INVALID_ALG.
  kTssErrorBadHashLength = 167,                // TSS_E_HASH_INVALID_LENGTH.
  kTssErrorHashObjectHasNoValue = 168,         // TSS_E_HASH_NO_DATA.
  kTssErrorSilentContextNeedsUserInput = 169,  // TSS_E_SILENT_CONTEXT.
  kTssErrorBadAttributeFlag = 170,             // TSS_E_INVALID_ATTRIB_FLAG.
  kTssErrorBadAttributeSubFlag = 171,          // TSS_E_INVALID_ATTRIB_SUBFLAG.
  kTssErrorBadAttributeData = 172,             // TSS_E_INVALID_ATTRIB_DATA.
  kTssErrorNoPcrRegistersSet = 173,            // TSS_E_NO_PCRS_SET.
  kTssErrorKeyNotLoaded = 174,                 // TSS_E_KEY_NOT_LOADED.
  kTssErrorKeyNotSet = 175,                    // TSS_E_KEY_NOT_SET.
  kTssErrorValidationFailed = 176,             // TSS_E_VALIDATION_FAILED.
  kTssErrorTspAuthorizationRequired = 177,     // TSS_E_TSP_AUTHREQUIRED.
  kTssErrorTspMultipleAuthorizationRequired = 178,  // TSS_E_TSP_AUTH2REQUIRED.
  kTssErrorTspAuthorizationFailed = 179,            // TSS_E_TSP_AUTHFAIL.
  kTssErrorTspMultipleAuthorizationFailed = 180,    // TSS_E_TSP_AUTH2FAIL.
  kTssErrorKeyHasNoMigrationPolicy = 181,   // TSS_E_KEY_NO_MIGRATION_POLICY.
  kTssErrorPolicyHasNoSecret = 182,         // TSS_E_POLICY_NO_SECRET.
  kTssErrorBadObjectAccess = 183,           // TSS_E_INVALID_OBJ_ACCESS.
  kTssErrorBadEncryptionScheme = 184,       // TSS_E_INVALID_ENCSCHEME.
  kTssErrorBadSignatureScheme = 185,        // TSS_E_INVALID_SIGSCHEME.
  kTssErrorEncryptedObjectBadLength = 186,  // TSS_E_ENC_INVALID_LENGTH.
  kTssErrorEncryptedObjectHasNoData = 187,  // TSS_E_ENC_NO_DATA.
  kTssErrorEncryptedObjectBadType = 188,    // TSS_E_ENC_INVALID_TYPE.
  kTssErrorBadKeyUsage = 189,               // TSS_E_INVALID_KEYUSAGE.
  kTssErrorVerificationFailed = 190,        // TSS_E_VERIFICATION_FAILED.
  kTssErrorNoHashAlgorithmId = 191,         // TSS_E_HASH_NO_IDENTIFIER.
  kTssErrorNvRamAreaAlreadyExists = 192,    // TSS_E_NV_AREA_EXIST.
  kTssErrorNvRamAreaDoesntExist = 193,      // TSS_E_NV_AREA_NOT_EXIST.
  kTssErrorUnknownError = 194,              //  Unknown TSS error.

  // Must be the last entry.
  kTpmResultNumberOfBuckets = 195,
};

// Returns the corresponding TpmResult enum value to be used to report a
// "Cryptohome.TpmResults" histogram sample.
TpmResult GetTpmResultSample(TpmReturnCode result);

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_METRICS_H_
