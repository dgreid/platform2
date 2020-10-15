# Homedirs

[TOC]

## Overview

Cryptohome makes use of `ecryptfs` for the underlying file and filename
encryption. Keys for a user are maintained in an encrypted vault keyset that is
protected using a key derived from the user's password. That protection may be
either through the TPM or through the use of [`scrypt`].

Cryptohome manages directories as follows:

*   `/home/.shadow`: Location for the system salt and individual users'
    salt/key/vault

*   `/home/.shadow/<salted_hash_of_username>`: Each Chrome OS user gets a
    directory in the shadow root where their salts, keys, and vault are stored
    (s_h_o_u).

*   `/home/.shadow/<s_h_o_u>/vault`: The user's vault (the encrypted version of
    their home directory)

*   `/home/.shadow/<s_h_o_u>/master.0`: Vault keyset for the user. The vault
    keyset contains the encrypted file encryption key and encrypted filename
    encryption key. It also contains the salt used to convert the user's passkey
    to an AES key, and may contain the TPM-encrypted intermediate key when TPM
    protection is enabled (see tpm.h for details).

*   `/home/.shadow/<s_h_o_u>/mount`: On successful login, the user's vault
    directory is mounted here using the symmetric key decrypted from master.X by
    the user's passkey.

*   `/home/user/<s_h_o_u>`: bind mount of `/home/.shadow/<s_h_o_u>/mount/user`

*   `/home/root/<s_h_o_u>`: bind mount of `/home/.shadow/<s_h_o_u>/mount/root`

*   `/home/chronos/u<s_h_o_u>`: bind mount of
    `/home/.shadow/<s_h_o_u>/mount/user` used for multi-user support.

*   `/home/chronos/user`: bind mount of the active user
    `/home/.shadow/<s_h_o_u>/mount/user`, for backward compatibility.

Offline login and screen unlock is processed through cryptohome using a test
decryption of the user's keyset using the passkey provided. If the user
currently has their cryptohome mounted, then the credentials may be verified
against their session object instead, which provides quick credentials
verification without access to the key material. This latter method uses the
UserSession object in user_session.cc. A user session is established on
successful completion of MountCryptohome() in mount.cc. The user session is torn
down when a call to UnmmountCryptohome, MountCryptohome, MountGuestCryptohome,
or MigratePasskey is made (regardless of success status). If no user session
exists for the Mount instance, offline credential test falls back to attempting
to decrypt the user's stored vault keyset. The session method is preferred
because it does not attempt to decrypt key material, and because it does not
require a round-trip to the TPM when the TPM is used for further protecting the
keyset (which can be ~.7s, or close to 3s if the RSA key was evicted from the
TPM key slot it occupies).

A user's cryptohome is automatically created when the vault directory for the
user does not exist and the cryptohome service gets a call to mount the user's
home directory. This assumes that the call to MountCryptohome contains the
correct user password--no verification can be done if the vault keyset for the
user does not exist. TestCredentials should be used if implicit creation is not
desired (an of course, explicit mount).

Passkey change is implemented through a call to MigratePasskey. MigratePasskey
will attempt to decrypt the vault keyset using the old credentials supplied, and
if successful, will re-save the vault keyset using the new credentials.
MigratePasskey can be called regardless of whether a user's (any user)
cryptohome is mounted. However, it will always clear the current user's session
as described above.

## Typical Mount Flow

*   Cryptohome's Mount() D-Bus API is called with valid user credentials.
*   If a cryptohome does not exist for that user name, a new one is created.
    *   The cryptohome is located at `/home/.shadow/<salted hash of user name>`,
        where the encrypted hash is a SHA1 hash of the system salt concatenated
        to the user name.
    *   The cryptohome vault (the directory that is mounted via ecryptfs) is
        created at `/home/.shadow/<s_h_o_u>/vault`
    *   The vault keyset is stored at `/home/.shadow/<s_h_o_u>/master.0`
        *   The vault keyset stores an encrypted blob containing the ecryptfs
            file encryption key (`FEK`) and file name encryption key (`FNEK`).
        *   The vault keyset stores non-sensitive information about the
            protection mechanism used to encrypt the keys, such as salt,
            password rounds, etc. See the later discussion on protection
            mechanisms.
*   The encrypted blob in the vault keyset is decrypted using the user
    credentials supplied to the Mount() API.
    *   On failure one of several actions is taken:
        *   If the blob was protected using the TPM, and the TPM has been
            cleared and re-owned, then the user's cryptohome vault is removed
            and re-created. This is because it is not possible to recover a
            keyset that is TPM-protected if the TPM has been cleared. A status
            code is returned in this case, but the Mount() call returns success.
            This is the only case where cryptohome will automatically re-create
            a user's home.
        *   If the blob was protected using the TPM, and the TPM is unavailable,
            for example, it has been disabled, then the call returns an error
            code indicating that either there was a failure communicating with
            the TPM or that the TPM is in defend lock (a state where the TPM
            believes that a brute-force attack is happening, and so it
            temporarily blocks most API calls). Communications failures are
            usually transient, and can be fixed by calling the API a second
            time. (For example, some chips on resume from S3 require us to
            re-establish our long-lived session.) If the device allows manual
            disabling of the TPM, this error would not be transient, and the
            user would have to re-enable the TPM. Defend lock errors are always
            transient, but the back-off period is variable. While sometimes it
            may be seconds, other times it is best to reboot the system to clear
            this state.
        *   If the blob was protected using the TPM, and there is a failure in
            the TSS API (couldn't load the cryptohome TPM key, etc.) that
            doesn't correspond to a TPM clear, then a communications failure
            error is returned as described in the last section.
        *   If there is a disk failure, specifically, the vault keyset file is
            corrupt, there is undefined behavior.
        *   If the following errors occur on creation, cryptohome returns an
            error and cannot remedy the problem:
            *   If the TPM is unavailable, cryptohome falls back to
                [`scrypt`]-based protection, and the call to `scryptenc_buf`
                fails for any reason.
            *   If writing the encrypted vault keyset to disk fails for any
                reason.
            *   If creating the user's vault path fails for any reason.
            *   If setting the ownership of the user's vault path fails for any
                reason.
            *   If decrypting the vault keyset fails because the call to decrypt
                in the TPM fails, it is assumed that the password is incorrect.
                This may occur if the user changes their password, as the keyset
                would still be protected with the password used at last login,
                but the call to Mount() would presumably use the current
                credentials. In this case, the system is given a chance to
                migrate the keys by having the user supply the old password.
            *   If decrypting the vault keyset fails because the call to decrypt
                using scrypt fails, it is assumed that the password is
                incorrect, as with the TPM above.
            *   If adding the decrypted keyset to the kernel keyring before
                ecryptfs mount fails, it is assumed that the key material was
                decrypted properly but some other problem exists outside of the
                control of cryptohome.
            *   If the call to mount the user's cryptohome fails, it is assumed
                that some other problem exists outside of the control of
                cryptohome.
            *   If the user's cryptohome must be re-created due to condition
                (i), and the cryptohome cannot be removed, then some other
                problem exists outside of the control of cryptohome.

## Protection Mechanisms

The vault keyset (vault_keyset.cc/h) contains the file encryption key and file
name encryption key used by ecryptfs. This keyset encrypted and persisted to
disk. Cryptohome may use either the TPM or [`scrypt`] as the
encryption/protection mechanism.

If the TPM is available, cryptohome will prefer to use it. If the TPM is not
available (either not present, not enabled, owned by another OS, or it is in the
middle of being owned), cryptohome will fall back to using scrypt-based
protection of the vault keyset. If the TPM becomes available at a later login,
cryptohome will transparently migrate a user's keyset to TPM-based protection.

The method when the TPM is enabled can be described using the decryption
workflow as an example:

```
  UP -
      |
      + AES decrypt (no padding) => IEVKK -
      |                                    |
EVKK -                                     |
                                           + RSA decrypt (in TPM) => VKK
                                           |
                                           |
                                  TPM_CHK -
```

Where:

*   `UP`: User Passkey
*   `EVKK`: Ecrypted vault keyset key (stored on disk)
*   `IEVKK`: Intermediate vault keyset key
*   `TPM_CHK`: TPM-wrapped system-wide Cryptohome Key
*   `VKK`: Vault Keyset Key

The end result, the Vault Keyset Key (VKK), is an AES key that is used to
decrypt the Vault Keyset, which holds the ecryptfs keys (filename encryption key
and file encryption key). The VKK, when using the TPM for protection, is a
randomly-generated key.

The User Passkey (UP) is used as an AES key to do an initial decrypt of the
Encrypted Vault Keyset Key (EVKK, or the "tpm_key" field in the
SerializedVaultKeyset, see vault_keyset.proto). This is done without padding as
the decryption is done in-place and the resulting buffer is the Intermediate
Vault Keyset Key (IEVKK), which is fed into an RSA decrypt on the TPM as the
cipher text. That RSA decrypt uses the system-wide TPM-wrapped cryptohome key.
In this manner, we can use a randomly-created system-wide key (the TPM has a
limited number of key slots), but still require the user's passkey during the
decryption phase. This also increases the brute-force cost of attacking the
SerializedVaultKeyset offline as it means that the attacker would have to do a
TPM cipher operation per password attempt (assuming that the wrapped key could
not be recovered).

After obtaining the VKK, it is used to recover the vault keyset by using it as
an AES key to decrypt the Encrypted Vault Keyset (EVK, or the "wrapped_keyset"
field in the SerializedVaultKeyset):

```
VKK -
     |
     + AES (PKCS#5 padding + SHA1 verification) => VK
     |
EVK -

Where:
  EVK - Encrypted vault keyset
  VK - Vault keyset

Presented another way:

+----------------------------------+
| EVKK (persisted as "tpm_key")    |
+----------------------------------+
                              \   /
                               \ /   Final 128-bits decrypted     +----------+
                                +--- in-place using the user's ---+ UP (mem) |
                               / \   passkey                      +----------+
                              /   \
+----------------------------------+
| IEVKK (mem)                      |
+----------------------------------+
 \                                 /
  \                               /
   \                             /
    -----------      ------------
                \   /
                 \ /                       +---------------------+
                  +--- Decrypted on-TPM ---+ TPM_CHK (persisted, |
                 / \                       | sealed by the TPM)  |
                /   \                      +---------------------+
               /     \
              /       \
             /         \            +-------------------------------------+
            /           \           | EVK (persisted as "wrapped_keyset") |
           /             \          +-------------------------------------+
          /               \          \                                   /
         /                 \          ----------------   ----------------
        +-------------------+                         \ /
        | VKK (mem)         +--- AES decrypt ----------+
        +-------------------+                         / \
                                      ----------------   ----------------
                                     /                                   \
                                    +-------------------------------------+
                                    | VK (mem)                            |
                                    +-------------------------------------+
```

Encryption of the Vault Keyset (VK) is the reverse method (see WrapVaultKeyset
in crypto.cc).

By comparison, when the TPM is not enabled, the UP is used as the password
supplied to scryptbuf_enc, which will use memory-bound key strengthening and AES
to encrypt the VK. The [`scrypt`] method is simpler because of the high-level
API that [`scrypt`] exposes for key strengthening and encryption in one function
call.

[`scrypt`]: http://www.tarsnap.com/scrypt.html
