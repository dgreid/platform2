// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chromeos/streams/tls_stream.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <base/bind.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <chromeos/secure_blob.h>
#include <chromeos/streams/openssl_stream_bio.h>
#include <chromeos/streams/stream_utils.h>
#include <chromeos/strings/string_utils.h>

namespace {

// SSL info callback which is called by OpenSSL when we enable logging level of
// at least 3. This logs the information about the internal TLS handshake.
void TlsInfoCallback(const SSL *ssl, int where, int ret) {
  std::string reason;
  std::vector<std::string> info;
  if (where & SSL_CB_LOOP)
    info.push_back("loop");
  if (where & SSL_CB_EXIT)
    info.push_back("exit");
  if (where & SSL_CB_READ)
    info.push_back("read");
  if (where & SSL_CB_WRITE)
    info.push_back("write");
  if (where & SSL_CB_ALERT) {
    info.push_back("alert");
    reason = ", reason: ";
    reason += SSL_alert_type_string_long(ret);
    reason += "/";
    reason += SSL_alert_desc_string_long(ret);
  }
  if (where & SSL_CB_HANDSHAKE_START)
    info.push_back("handshake_start");
  if (where & SSL_CB_HANDSHAKE_DONE)
    info.push_back("handshake_done");

  VLOG(3) << "TLS progress info: " << chromeos::string_utils::Join(",", info)
          << ", with status: " << ret << reason;
}

// Static variable to store the index of TlsStream private data in SSL context
// used to store custom data for OnCertVerifyResults().
int ssl_ctx_private_data_index = -1;

// Default trusted certificate store location.
const char kChromeOSCACertificatePath[] = "/usr/share/chromeos-ca-certificates";

}  // anonymous namespace

namespace chromeos {

// Helper implementation of TLS stream used to hide most of OpenSSL inner
// workings from the users of chromeos::TlsStream.
class TlsStream::TlsStreamImpl {
 public:
  TlsStreamImpl();
  ~TlsStreamImpl();

  bool Init(StreamPtr socket,
            const std::string& host,
            const base::Closure& success_callback,
            const Stream::ErrorCallback& error_callback,
            ErrorPtr* error);

  bool ReadNonBlocking(void* buffer,
                       size_t size_to_read,
                       size_t* size_read,
                       bool* end_of_stream,
                       ErrorPtr* error);

  bool WriteNonBlocking(const void* buffer,
                        size_t size_to_write,
                        size_t* size_written,
                        ErrorPtr* error);

  bool Flush(ErrorPtr* error);
  bool Close(ErrorPtr* error);
  bool WaitForData(AccessMode mode,
                   const base::Callback<void(AccessMode)>& callback,
                   ErrorPtr* error);
  bool WaitForDataBlocking(AccessMode in_mode,
                           AccessMode* out_mode,
                           ErrorPtr* error);
  void CancelPendingAsyncOperations();

 private:
  bool ReportError(ErrorPtr* error,
                   const tracked_objects::Location& location,
                   const std::string& message);
  void DoHandshake(const base::Closure& success_callback,
                   const Stream::ErrorCallback& error_callback);
  void RetryHandshake(const base::Closure& success_callback,
                      const Stream::ErrorCallback& error_callback,
                      Stream::AccessMode mode);

  int OnCertVerifyResults(int ok, X509_STORE_CTX* ctx);
  static int OnCertVerifyResultsStatic(int ok, X509_STORE_CTX* ctx);

  StreamPtr socket_;
  std::string host_;  // Expected host name used to verify the server identity.
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx_{nullptr, SSL_CTX_free};
  std::unique_ptr<SSL, decltype(&SSL_free)> ssl_{nullptr, SSL_free};
  BIO* stream_bio_{nullptr};
  bool need_more_read_{false};
  bool need_more_write_{false};

  Blob io_buffer;

  base::WeakPtrFactory<TlsStreamImpl> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(TlsStreamImpl);
};

TlsStream::TlsStreamImpl::TlsStreamImpl() {
  SSL_load_error_strings();
  SSL_library_init();
  if (ssl_ctx_private_data_index < 0) {
    ssl_ctx_private_data_index =
        SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  }
  io_buffer.resize(4096);
}

TlsStream::TlsStreamImpl::~TlsStreamImpl() {
  ssl_.reset();
  ctx_.reset();
}

bool TlsStream::TlsStreamImpl::ReadNonBlocking(void* buffer,
                                               size_t size_to_read,
                                               size_t* size_read,
                                               bool* end_of_stream,
                                               ErrorPtr* error) {
  const size_t max_int = std::numeric_limits<int>::max();
  int size_int = static_cast<int>(std::min(size_to_read, max_int));
  int ret = SSL_read(ssl_.get(), buffer, size_int);
  if (ret > 0) {
    *size_read = static_cast<size_t>(ret);
    if (end_of_stream)
      *end_of_stream = false;
    return true;
  }

  int err = SSL_get_error(ssl_.get(), ret);
  if (err == SSL_ERROR_ZERO_RETURN) {
    *size_read = 0;
    if (end_of_stream)
      *end_of_stream = true;
    return true;
  }

  if (err == SSL_ERROR_WANT_READ) {
    need_more_read_ = true;
  } else if (err == SSL_ERROR_WANT_WRITE) {
    // Writes might be required for SSL_read() because of possible TLS
    // re-negotiations which can happen at any time.
    need_more_write_ = true;
  } else {
    return ReportError(error, FROM_HERE, "Error reading from TLS socket");
  }
  *size_read = 0;
  if (end_of_stream)
    *end_of_stream = false;
  return true;
}

bool TlsStream::TlsStreamImpl::WriteNonBlocking(const void* buffer,
                                                size_t size_to_write,
                                                size_t* size_written,
                                                ErrorPtr* error) {
  const size_t max_int = std::numeric_limits<int>::max();
  int size_int = static_cast<int>(std::min(size_to_write, max_int));
  int ret = SSL_write(ssl_.get(), buffer, size_int);
  if (ret > 0) {
    *size_written = static_cast<size_t>(ret);
    return true;
  }

  int err = SSL_get_error(ssl_.get(), ret);
  if (err == SSL_ERROR_WANT_READ) {
    // Reads might be required for SSL_write() because of possible TLS
    // re-negotiations which can happen at any time.
    need_more_read_ = true;
  } else if (err == SSL_ERROR_WANT_WRITE) {
    need_more_write_ = true;
  } else {
    return ReportError(error, FROM_HERE, "Error writing to TLS socket");
  }
  *size_written = 0;
  return true;
}

bool TlsStream::TlsStreamImpl::Flush(ErrorPtr* error) {
  return socket_->FlushBlocking(error);
}

bool TlsStream::TlsStreamImpl::Close(ErrorPtr* error) {
  int retry_count = 0;
  while (retry_count < 2) {
    int ret = SSL_shutdown(ssl_.get());
    if (ret == 0) {
      // TLS shutdown hasn't completed yet. Retrying...;
      retry_count++;
      continue;
    }
    if (ret == 1)
      break;

    int err = SSL_get_error(ssl_.get(), ret);
    if (err == SSL_ERROR_WANT_READ) {
      if (!socket_->WaitForDataBlocking(AccessMode::READ, nullptr, error))
        break;
    } else if (err == SSL_ERROR_WANT_WRITE) {
      if (!socket_->WaitForDataBlocking(AccessMode::WRITE, nullptr, error))
        break;
    } else {
      ReportError(error, FROM_HERE, "Failed to shut down TLS socket");
      break;
    }
  }
  return socket_->CloseBlocking(error);
}

bool TlsStream::TlsStreamImpl::WaitForData(
    AccessMode mode,
    const base::Callback<void(AccessMode)>& callback,
    ErrorPtr* error) {
  bool is_read = stream_utils::IsReadAccessMode(mode);
  bool is_write = stream_utils::IsWriteAccessMode(mode);
  is_read |= need_more_read_;
  is_write |= need_more_write_;
  need_more_read_ = false;
  need_more_write_ = false;
  if (is_read && SSL_pending(ssl_.get()) > 0) {
    callback.Run(AccessMode::READ);
    return true;
  }
  mode = stream_utils::MakeAccessMode(is_read, is_write);
  return socket_->WaitForData(mode, callback, error);
}

bool TlsStream::TlsStreamImpl::WaitForDataBlocking(AccessMode in_mode,
                                                   AccessMode* out_mode,
                                                   ErrorPtr* error) {
  bool is_read = stream_utils::IsReadAccessMode(in_mode);
  bool is_write = stream_utils::IsWriteAccessMode(in_mode);
  is_read |= need_more_read_;
  is_write |= need_more_write_;
  need_more_read_ = need_more_write_ = false;
  if (is_read && SSL_pending(ssl_.get()) > 0) {
    if (out_mode)
      *out_mode = AccessMode::READ;
    return true;
  }
  in_mode = stream_utils::MakeAccessMode(is_read, is_write);
  return socket_->WaitForDataBlocking(in_mode, out_mode, error);
}

void TlsStream::TlsStreamImpl::CancelPendingAsyncOperations() {
  socket_->CancelPendingAsyncOperations();
  weak_ptr_factory_.InvalidateWeakPtrs();
}

bool TlsStream::TlsStreamImpl::ReportError(
    ErrorPtr* error,
    const tracked_objects::Location& location,
    const std::string& message) {
  const char* file = nullptr;
  int line = 0;
  const char* data = 0;
  int flags = 0;
  while (auto errnum = ERR_get_error_line_data(&file, &line, &data, &flags)) {
    char buf[256];
    ERR_error_string_n(errnum, buf, sizeof(buf));
    tracked_objects::Location ssl_location{"Unknown", file, line, nullptr};
    std::string ssl_message = buf;
    if (flags & ERR_TXT_STRING) {
      ssl_message += ": ";
      ssl_message += data;
    }
    Error::AddTo(error, ssl_location, "openssl", std::to_string(errnum),
                 ssl_message);
  }
  Error::AddTo(error, location, "tls_stream", "failed", message);
  return false;
}

int TlsStream::TlsStreamImpl::OnCertVerifyResults(int ok, X509_STORE_CTX* ctx) {
  // OpenSSL already performs a comprehensive check of the certificate chain
  // (using X509_verify_cert() function) and calls back with the result of its
  // verification.
  // |ok| is set to 1 if the verification passed and 0 if an error was detected.
  // Here we can perform some additional checks if we need to, or simply log
  // the issues found.

  // For now, just log an error if it occurred.
  if (!ok) {
    LOG(ERROR) << "Server certificate validation failed: "
               << X509_verify_cert_error_string(X509_STORE_CTX_get_error(ctx));
  } else {
    // TODO(avakulenko): Verify the server name in the certificate against
    // |host_| name expected by the caller: brbug.com/1050
  }
  return ok;
}

int TlsStream::TlsStreamImpl::OnCertVerifyResultsStatic(int ok,
                                                        X509_STORE_CTX* ctx) {
  // Obtain the pointer to the instance of TlsStream::TlsStreamImpl from the
  // SSL CTX object referenced by |ctx|.
  SSL* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(
      ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  SSL_CTX* ssl_ctx = ssl ? SSL_get_SSL_CTX(ssl) : nullptr;
  TlsStream::TlsStreamImpl* self = nullptr;
  if (ssl_ctx) {
    self = static_cast<TlsStream::TlsStreamImpl*>(SSL_CTX_get_ex_data(
        ssl_ctx, ssl_ctx_private_data_index));
  }
  return self ? self->OnCertVerifyResults(ok, ctx) : ok;
}

bool TlsStream::TlsStreamImpl::Init(StreamPtr socket,
                                    const std::string& host,
                                    const base::Closure& success_callback,
                                    const Stream::ErrorCallback& error_callback,
                                    ErrorPtr* error) {
  host_ = host;
  ctx_.reset(SSL_CTX_new(TLSv1_2_client_method()));
  if (!ctx_)
    return ReportError(error, FROM_HERE, "Cannot create SSL_CTX");

  // Top cipher suites supported by both Google GFEs and OpenSSL (in server
  // preferred order).
  int res = SSL_CTX_set_cipher_list(ctx_.get(),
                                    "ECDHE-ECDSA-AES128-GCM-SHA256:"
                                    "ECDHE-ECDSA-AES256-GCM-SHA384:"
                                    "ECDHE-RSA-AES128-GCM-SHA256:"
                                    "ECDHE-RSA-AES256-GCM-SHA384");
  if (res != 1)
    return ReportError(error, FROM_HERE, "Cannot set the cipher list");

  res = SSL_CTX_load_verify_locations(ctx_.get(), nullptr,
                                      kChromeOSCACertificatePath);
  if (res != 1) {
    return ReportError(error, FROM_HERE,
                       "Failed to specify trusted certificate location");
  }

  // Store a pointer to "this" into SSL_CTX instance.
  SSL_CTX_set_ex_data(ctx_.get(), ssl_ctx_private_data_index, this);
  SSL_CTX_set_verify(ctx_.get(), SSL_VERIFY_PEER,
                     &TlsStreamImpl::OnCertVerifyResultsStatic);

  socket_ = std::move(socket);
  ssl_.reset(SSL_new(ctx_.get()));

  // Enable TLS progress callback if VLOG level is >=3.
  if (VLOG_IS_ON(3))
    SSL_set_info_callback(ssl_.get(), TlsInfoCallback);

  stream_bio_ = BIO_new_stream(socket_.get());
  SSL_set_bio(ssl_.get(), stream_bio_, stream_bio_);
  SSL_set_connect_state(ssl_.get());

  // We might have no message loop (e.g. we are in unit tests).
  if (base::MessageLoop::current()) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&TlsStreamImpl::DoHandshake,
                   weak_ptr_factory_.GetWeakPtr(),
                   success_callback,
                   error_callback));
  } else {
    DoHandshake(success_callback, error_callback);
  }
  return true;
}

void TlsStream::TlsStreamImpl::RetryHandshake(
    const base::Closure& success_callback,
    const Stream::ErrorCallback& error_callback,
    Stream::AccessMode mode) {
  VLOG(1) << "Retrying TLS handshake";
  DoHandshake(success_callback, error_callback);
}

void TlsStream::TlsStreamImpl::DoHandshake(
    const base::Closure& success_callback,
    const Stream::ErrorCallback& error_callback) {
  VLOG(1) << "Begin TLS handshake";
  int res = SSL_do_handshake(ssl_.get());
  if (res == 1) {
    VLOG(1) << "Handshake successful";
    success_callback.Run();
    return;
  }
  ErrorPtr error;
  int err = SSL_get_error(ssl_.get(), res);
  if (err == SSL_ERROR_WANT_READ) {
    VLOG(1) << "Waiting for read data...";
    bool ok = socket_->WaitForData(
        Stream::AccessMode::READ,
        base::Bind(&TlsStreamImpl::RetryHandshake,
                   weak_ptr_factory_.GetWeakPtr(),
                   success_callback, error_callback),
        &error);
    if (ok)
      return;
  } else if (err == SSL_ERROR_WANT_WRITE) {
    VLOG(1) << "Waiting for write data...";
    bool ok = socket_->WaitForData(
        Stream::AccessMode::WRITE,
        base::Bind(&TlsStreamImpl::RetryHandshake,
                   weak_ptr_factory_.GetWeakPtr(),
                   success_callback, error_callback),
        &error);
    if (ok)
      return;
  } else {
    ReportError(&error, FROM_HERE, "TLS handshake failed.");
  }
  error_callback.Run(error.get());
}

/////////////////////////////////////////////////////////////////////////////
TlsStream::TlsStream(std::unique_ptr<TlsStreamImpl> impl)
    : impl_{std::move(impl)} {}

TlsStream::~TlsStream() {}

void TlsStream::Connect(StreamPtr socket,
                        const std::string& host,
                        const base::Callback<void(StreamPtr)>& success_callback,
                        const Stream::ErrorCallback& error_callback) {
  std::unique_ptr<TlsStreamImpl> impl{new TlsStreamImpl};
  std::unique_ptr<TlsStream> stream{new TlsStream{std::move(impl)}};

  TlsStreamImpl* pimpl = stream->impl_.get();
  ErrorPtr error;
  bool success = pimpl->Init(std::move(socket), host,
                             base::Bind(success_callback,
                                        base::Passed(std::move(stream))),
                             error_callback, &error);

  if (!success)
    error_callback.Run(error.get());
}

bool TlsStream::IsOpen() const {
  return impl_ ? true : false;
}

bool TlsStream::SetSizeBlocking(uint64_t size, ErrorPtr* error) {
  return stream_utils::ErrorOperationNotSupported(FROM_HERE, error);
}

bool TlsStream::Seek(int64_t offset,
                     Whence whence,
                     uint64_t* new_position,
                     ErrorPtr* error) {
  return stream_utils::ErrorOperationNotSupported(FROM_HERE, error);
}

bool TlsStream::ReadNonBlocking(void* buffer,
                                size_t size_to_read,
                                size_t* size_read,
                                bool* end_of_stream,
                                ErrorPtr* error) {
  if (!impl_)
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);
  return impl_->ReadNonBlocking(buffer, size_to_read, size_read, end_of_stream,
                                error);
}

bool TlsStream::WriteNonBlocking(const void* buffer,
                                 size_t size_to_write,
                                 size_t* size_written,
                                 ErrorPtr* error) {
  if (!impl_)
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);
  return impl_->WriteNonBlocking(buffer, size_to_write, size_written, error);
}

bool TlsStream::FlushBlocking(ErrorPtr* error) {
  if (!impl_)
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);
  return impl_->Flush(error);
}

bool TlsStream::CloseBlocking(ErrorPtr* error) {
  if (impl_ && !impl_->Close(error))
    return false;
  impl_.reset();
  return true;
}

bool TlsStream::WaitForData(AccessMode mode,
                            const base::Callback<void(AccessMode)>& callback,
                            ErrorPtr* error) {
  if (!impl_)
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);
  return impl_->WaitForData(mode, callback, error);
}

bool TlsStream::WaitForDataBlocking(AccessMode in_mode,
                                    AccessMode* out_mode,
                                    ErrorPtr* error) {
  if (!impl_)
    return stream_utils::ErrorStreamClosed(FROM_HERE, error);
  return impl_->WaitForDataBlocking(in_mode, out_mode, error);
}

void TlsStream::CancelPendingAsyncOperations() {
  if (impl_)
    impl_->CancelPendingAsyncOperations();
  Stream::CancelPendingAsyncOperations();
}

}  // namespace chromeos
