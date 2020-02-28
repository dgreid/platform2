// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/libvda/encode/gpu/gpu_vea_impl.h"

#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/macros.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "arc/vm/libvda/gbm_util.h"
#include "arc/vm/libvda/gpu/format_util.h"

namespace arc {
namespace {

inline arc::mojom::VideoPixelFormat ConvertInputFormatToMojoFormat(
    video_pixel_format_t format) {
  switch (format) {
    case YV12:
      return arc::mojom::VideoPixelFormat::PIXEL_FORMAT_YV12;
    case NV12:
      return arc::mojom::VideoPixelFormat::PIXEL_FORMAT_NV12;
    default:
      NOTREACHED();
  }
}

inline vea_error_t ConvertMojoError(
    arc::mojom::VideoEncodeAccelerator::Error error) {
  switch (error) {
    case arc::mojom::VideoEncodeAccelerator::Error::kIllegalStateError:
      return ILLEGAL_STATE_ERROR;
    case arc::mojom::VideoEncodeAccelerator::Error::kInvalidArgumentError:
      return INVALID_ARGUMENT_ERROR;
    case arc::mojom::VideoEncodeAccelerator::Error::kPlatformFailureError:
      return PLATFORM_FAILURE_ERROR;
    default:
      NOTREACHED();
  }
}

class GpuVeaContext : public VeaContext, arc::mojom::VideoEncodeClient {
 public:
  // Create a new GpuVeaContext. Must be called on |ipc_task_runner|.
  GpuVeaContext(
      const scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
      arc::mojom::VideoEncodeAcceleratorPtr vea_ptr);
  ~GpuVeaContext();

  using InitializeCallback = base::OnceCallback<void(bool)>;

  // Initializes the VDA context object. When complete, callback is called with
  // the boolean parameter set to true. Must be called on |ipc_task_runner_|.
  void Initialize(vea_config_t* config, InitializeCallback callback);

  // VeaContext overrides.
  int Encode(vea_input_buffer_id_t input_buffer_id,
             base::ScopedFD fd,
             size_t num_planes,
             video_frame_plane_t* planes,
             uint64_t timestamp,
             bool force_keyframe) override;

  int UseOutputBuffer(vea_output_buffer_id_t output_buffer_id,
                      base::ScopedFD fd,
                      uint32_t offset,
                      uint32_t size) override;

  int RequestEncodingParamsChange(uint32_t bitrate,
                                  uint32_t framerate) override;

  int Flush() override;

  // arc::mojom::VideoEncodeClient overrides.
  void RequireBitstreamBuffers(uint32_t input_count,
                               arc::mojom::SizePtr input_coded_size,
                               uint32_t output_buffer_size) override;

  void NotifyError(arc::mojom::VideoEncodeAccelerator::Error error) override;

 private:
  // Callback for VideoEncodeAcceleratorPtr connection errors.
  void OnVeaError(uint32_t custom_reason, const std::string& description);

  // Callback for VideoEncodeClientPtr connection errors.
  void OnVeaClientError(uint32_t custom_reason, const std::string& description);

  // Callback invoked when VideoEncodeAcceleratorPtr::Initialize completes.
  void OnInitialized(InitializeCallback, bool success);

  void OnInputBufferProcessed(vea_input_buffer_id_t input_buffer_id);
  void OnOutputBufferFilled(vea_output_buffer_id_t output_buffer_id,
                            uint32_t payload_size,
                            bool key_frame,
                            int64_t timestamp);
  void OnFlushDone(bool flush_done);

  void EncodeOnIpcThread(vea_input_buffer_id_t input_buffer_id,
                         base::ScopedFD fd,
                         std::vector<video_frame_plane_t> planes,
                         uint64_t timestamp,
                         bool force_keyframe);

  void UseOutputBufferOnIpcThread(vea_output_buffer_id_t output_buffer_id,
                                  base::ScopedFD fd,
                                  uint32_t offset,
                                  uint32_t size);
  void RequestEncodingParamsChangeOnIpcThread(uint32_t bitrate,
                                              uint32_t framerate);
  void FlushOnIpcThread();

  scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner_;
  // TODO(alexlau): Use THREAD_CHECKER macro after libchrome uprev
  // (crbug.com/909719).
  base::ThreadChecker ipc_thread_checker_;
  arc::mojom::VideoEncodeAcceleratorPtr vea_ptr_;
  mojo::Binding<arc::mojom::VideoEncodeClient> binding_;

  arc::mojom::VideoPixelFormat default_mojo_input_format_;
};

GpuVeaContext::GpuVeaContext(
    const scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
    arc::mojom::VideoEncodeAcceleratorPtr vea_ptr)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      vea_ptr_(std::move(vea_ptr)),
      binding_(this) {
  // Since ipc_thread_checker_ binds to whichever thread it's created on, check
  // that we're on the correct thread first using BelongsToCurrentThread.
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  vea_ptr_.set_connection_error_with_reason_handler(
      base::BindRepeating(&GpuVeaContext::OnVeaError, base::Unretained(this)));

  DLOG(INFO) << "Created new GPU context";
}

GpuVeaContext::~GpuVeaContext() {
  DCHECK(ipc_thread_checker_.CalledOnValidThread());
}

void GpuVeaContext::Initialize(vea_config_t* config,
                               InitializeCallback callback) {
  DCHECK(ipc_thread_checker_.CalledOnValidThread());
  arc::mojom::VideoEncodeClientPtr client_ptr;
  binding_.Bind(mojo::MakeRequest(&client_ptr));
  binding_.set_connection_error_with_reason_handler(base::BindRepeating(
      &GpuVeaContext::OnVeaClientError, base::Unretained(this)));

  arc::mojom::VideoEncodeAcceleratorConfigPtr mojo_config =
      arc::mojom::VideoEncodeAcceleratorConfig::New();

  default_mojo_input_format_ =
      ConvertInputFormatToMojoFormat(config->input_format);

  mojo_config->input_format = default_mojo_input_format_;
  mojo_config->input_visible_size = arc::mojom::Size::New();
  mojo_config->input_visible_size->width = config->input_visible_width;
  mojo_config->input_visible_size->height = config->input_visible_height;

  mojo_config->output_profile =
      ConvertCodecProfileToMojoProfile(config->output_profile);
  mojo_config->initial_bitrate = config->initial_bitrate;
  mojo_config->initial_framerate = config->initial_framerate;
  mojo_config->has_initial_framerate = config->has_initial_framerate;
  mojo_config->h264_output_level = config->h264_output_level;
  mojo_config->has_h264_output_level = config->has_h264_output_level;
  mojo_config->storage_type = arc::mojom::VideoFrameStorageType::DMABUF;

  // TODO(alexlau): Make this use BindOnce.
  vea_ptr_->Initialize(
      std::move(mojo_config), std::move(client_ptr),
      base::Bind(&GpuVeaContext::OnInitialized, base::Unretained(this),
                 base::Passed(std::move(callback))));
}

void GpuVeaContext::OnInitialized(InitializeCallback callback, bool success) {
  DCHECK(ipc_thread_checker_.CalledOnValidThread());
  std::move(callback).Run(success);
}

void GpuVeaContext::OnVeaError(uint32_t custom_reason,
                               const std::string& description) {
  DCHECK(ipc_thread_checker_.CalledOnValidThread());
  DLOG(ERROR) << "VideoEncodeAccelerator mojo connection error. custom_reason="
              << custom_reason << " description=" << description;
}

void GpuVeaContext::OnVeaClientError(uint32_t custom_reason,
                                     const std::string& description) {
  DCHECK(ipc_thread_checker_.CalledOnValidThread());
  DLOG(ERROR) << "VideoEncodeClient mojo connection error. custom_reason="
              << custom_reason << " description=" << description;
}

int GpuVeaContext::Encode(vea_input_buffer_id_t input_buffer_id,
                          base::ScopedFD fd,
                          size_t num_planes,
                          video_frame_plane_t* planes,
                          uint64_t timestamp,
                          bool force_keyframe) {
  std::vector<video_frame_plane_t> planes_vector(planes, planes + num_planes);
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&GpuVeaContext::EncodeOnIpcThread, base::Unretained(this),
                     input_buffer_id, std::move(fd), std::move(planes_vector),
                     timestamp, force_keyframe));
  return 0;
}

void GpuVeaContext::EncodeOnIpcThread(vea_input_buffer_id_t input_buffer_id,
                                      base::ScopedFD fd,
                                      std::vector<video_frame_plane_t> planes,
                                      uint64_t timestamp,
                                      bool force_keyframe) {
  mojo::ScopedHandle handle_fd = mojo::WrapPlatformFile(fd.release());
  if (!handle_fd.is_valid()) {
    LOG(ERROR) << "Invalid input buffer handle.";
    return;
  }

  std::vector<arc::mojom::VideoFramePlanePtr> mojo_planes;
  for (const auto& plane : planes) {
    arc::mojom::VideoFramePlanePtr mojo_plane =
        arc::mojom::VideoFramePlane::New();
    mojo_plane->offset = plane.offset;
    mojo_plane->stride = plane.stride;
    mojo_planes.push_back(std::move(mojo_plane));
  }

  // TODO(alexlau): Make this use BindOnce.
  vea_ptr_->Encode(default_mojo_input_format_, std::move(handle_fd),
                   std::move(mojo_planes), timestamp, force_keyframe,
                   base::Bind(&GpuVeaContext::OnInputBufferProcessed,
                              base::Unretained(this), input_buffer_id));
}

void GpuVeaContext::OnInputBufferProcessed(
    vea_input_buffer_id_t input_buffer_id) {
  DispatchProcessedInputBuffer(input_buffer_id);
}

int GpuVeaContext::UseOutputBuffer(vea_output_buffer_id_t output_buffer_id,
                                   base::ScopedFD fd,
                                   uint32_t offset,
                                   uint32_t size) {
  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&GpuVeaContext::UseOutputBufferOnIpcThread,
                                base::Unretained(this), output_buffer_id,
                                std::move(fd), offset, size));
  return 0;
}

void GpuVeaContext::UseOutputBufferOnIpcThread(
    vea_output_buffer_id_t output_buffer_id,
    base::ScopedFD fd,
    uint32_t offset,
    uint32_t size) {
  mojo::ScopedHandle handle_fd = mojo::WrapPlatformFile(fd.release());
  if (!handle_fd.is_valid()) {
    LOG(ERROR) << "Invalid output buffer handle.";
    return;
  }

  // TODO(alexlau): Make this use BindOnce.
  vea_ptr_->UseBitstreamBuffer(
      std::move(handle_fd), offset, size,
      base::Bind(&GpuVeaContext::OnOutputBufferFilled, base::Unretained(this),
                 output_buffer_id));
}

void GpuVeaContext::OnOutputBufferFilled(
    vea_output_buffer_id_t output_buffer_id,
    uint32_t payload_size,
    bool key_frame,
    int64_t timestamp) {
  DispatchProcessedOutputBuffer(output_buffer_id, payload_size, key_frame,
                                timestamp);
}

int GpuVeaContext::RequestEncodingParamsChange(uint32_t bitrate,
                                               uint32_t framerate) {
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&GpuVeaContext::RequestEncodingParamsChangeOnIpcThread,
                     base::Unretained(this), bitrate, framerate));
  return 0;
}

void GpuVeaContext::RequestEncodingParamsChangeOnIpcThread(uint32_t bitrate,
                                                           uint32_t framerate) {
  vea_ptr_->RequestEncodingParametersChange(bitrate, framerate);
}

int GpuVeaContext::Flush() {
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&GpuVeaContext::FlushOnIpcThread, base::Unretained(this)));
  return 0;
}

void GpuVeaContext::FlushOnIpcThread() {
  // TODO(alexlau): Make this use BindOnce.
  vea_ptr_->Flush(
      base::Bind(&GpuVeaContext::OnFlushDone, base::Unretained(this)));
}

void GpuVeaContext::OnFlushDone(bool flush_done) {
  DispatchFlushResponse(flush_done);
}

// VideoDecodeClient implementation function.
void GpuVeaContext::RequireBitstreamBuffers(
    uint32_t input_count,
    arc::mojom::SizePtr input_coded_size,
    uint32_t output_buffer_size) {
  DispatchRequireInputBuffers(input_count, input_coded_size->width,
                              input_coded_size->height, output_buffer_size);
}

// VideoDecodeClient implementation function.
void GpuVeaContext::NotifyError(
    arc::mojom::VideoEncodeAccelerator::Error error) {
  DispatchNotifyError(ConvertMojoError(error));
}

}  // namespace

// static
GpuVeaImpl* GpuVeaImpl::Create(VafConnection* conn) {
  auto impl = std::make_unique<GpuVeaImpl>(conn);
  if (!impl->Initialize()) {
    LOG(ERROR) << "Could not initialize GpuVeaImpl.";
    return nullptr;
  }

  return impl.release();
}

GpuVeaImpl::GpuVeaImpl(VafConnection* conn) : connection_(conn) {
  DLOG(INFO) << "Created GpuVeaImpl.";
}

GpuVeaImpl::~GpuVeaImpl() {
  DLOG(INFO) << "Destroyed GpuVeaImpl.";
}

bool GpuVeaImpl::Initialize() {
  input_formats_ = GetSupportedRawFormats(GbmUsageType::ENCODE);
  if (input_formats_.empty())
    return false;

  ipc_task_runner_ = connection_->GetIpcTaskRunner();
  CHECK(!ipc_task_runner_->BelongsToCurrentThread());

  base::WaitableEvent init_complete_event(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);

  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&GpuVeaImpl::InitializeOnIpcThread,
                                base::Unretained(this), &init_complete_event));
  init_complete_event.Wait();

  if (output_formats_.empty())
    return false;

  capabilities_.num_input_formats = input_formats_.size();
  capabilities_.input_formats = input_formats_.data();
  capabilities_.num_output_formats = output_formats_.size();
  capabilities_.output_formats = output_formats_.data();

  return true;
}

void GpuVeaImpl::InitializeOnIpcThread(
    base::WaitableEvent* init_complete_event) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  arc::mojom::VideoEncodeAcceleratorPtr vea_ptr;
  connection_->CreateEncodeAccelerator(&vea_ptr);
  auto* proxy = vea_ptr.get();

  // TODO(alexlau): Make this use BindOnce.
  proxy->GetSupportedProfiles(
      base::Bind(&GpuVeaImpl::OnGetSupportedProfiles, base::Unretained(this),
                 base::Passed(std::move(vea_ptr)), init_complete_event));
}

void GpuVeaImpl::OnGetSupportedProfiles(
    arc::mojom::VideoEncodeAcceleratorPtr vea_ptr,
    base::WaitableEvent* init_complete_event,
    std::vector<arc::mojom::VideoEncodeProfilePtr> profiles) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  output_formats_.clear();
  for (const auto& profile : profiles) {
    vea_profile_t p;
    p.profile = ConvertMojoProfileToCodecProfile(profile->profile);
    const auto& max_resolution = profile->max_resolution;
    p.max_width = max_resolution->width;
    p.max_height = max_resolution->height;
    p.max_framerate_numerator = profile->max_framerate_numerator;
    p.max_framerate_denominator = profile->max_framerate_denominator;
    output_formats_.push_back(std::move(p));
  }
  init_complete_event->Signal();
}

VeaContext* GpuVeaImpl::InitEncodeSession(vea_config_t* config) {
  DCHECK(!ipc_task_runner_->BelongsToCurrentThread());

  if (!connection_) {
    DLOG(FATAL) << "InitEncodeSession called before successful Initialize().";
    return nullptr;
  }

  DLOG(INFO) << "Initializing encode session";

  base::WaitableEvent init_complete_event(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  VeaContext* context = nullptr;
  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&GpuVeaImpl::InitEncodeSessionOnIpcThread,
                                base::Unretained(this), config,
                                &init_complete_event, &context));
  init_complete_event.Wait();
  return context;
}

void GpuVeaImpl::InitEncodeSessionOnIpcThread(
    vea_config_t* config,
    base::WaitableEvent* init_complete_event,
    VeaContext** out_context) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  arc::mojom::VideoEncodeAcceleratorPtr vea_ptr;
  connection_->CreateEncodeAccelerator(&vea_ptr);
  std::unique_ptr<GpuVeaContext> context =
      std::make_unique<GpuVeaContext>(ipc_task_runner_, std::move(vea_ptr));
  GpuVeaContext* context_ptr = context.get();
  // TODO(alexlau): Make this use BindOnce.
  context_ptr->Initialize(
      config,
      base::Bind(
          &GpuVeaImpl::InitEncodeSessionAfterContextInitializedOnIpcThread,
          base::Unretained(this), init_complete_event, out_context,
          base::Passed(std::move(context))));
}

void GpuVeaImpl::InitEncodeSessionAfterContextInitializedOnIpcThread(
    base::WaitableEvent* init_complete_event,
    VeaContext** out_context,
    std::unique_ptr<VeaContext> context,
    bool success) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  if (success) {
    *out_context = context.release();
  } else {
    DLOG(ERROR) << "Failed to initialize encode session.";
  }
  init_complete_event->Signal();
}

void GpuVeaImpl::CloseEncodeSession(VeaContext* context) {
  if (!connection_) {
    DLOG(FATAL) << "CloseEncodeSession called before successful Initialize().";
    return;
  }
  DLOG(INFO) << "Closing encode session";
  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&GpuVeaImpl::CloseEncodeSessionOnIpcThread,
                                base::Unretained(this), context));
}

void GpuVeaImpl::CloseEncodeSessionOnIpcThread(VeaContext* context) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  delete context;
}

}  // namespace arc
