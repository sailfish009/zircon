// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <string.h>
#include <zx/vmar.h>

#include "video-buffer.h"

namespace video {
namespace usb {

VideoBuffer::~VideoBuffer() {
    if (virt_ != nullptr) {
        zx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(virt_), size_);
    }
}

zx_status_t VideoBuffer::Create(zx::vmo&& vmo,
                                fbl::unique_ptr<VideoBuffer>* out,
                                uint32_t max_frame_size) {
    if (!vmo.is_valid()) {
        zxlogf(ERROR, "invalid buffer handle\n");
        return ZX_ERR_BAD_HANDLE;
    }

    uint64_t size;
    zx_status_t status = vmo.get_size(&size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "could not get vmo size, err: %d\n", status);
        return status;
    }

    void* virt;
    uint32_t flags =  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    status = zx::vmar::root_self().map(0u, vmo,
                                       0u, size,
                                       flags, reinterpret_cast<uintptr_t*>(&virt));

    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to map VMO, got error: %d\n", status);
        return status;
    }

    // Zero out the buffer.
    memset(virt, 0, size);

    fbl::AllocChecker ac;
    fbl::unique_ptr<VideoBuffer> res(
        new (&ac) VideoBuffer(fbl::move(vmo), size, virt));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = res->Init(max_frame_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to init video buffer, err: %d\n", status);
        return status;
    }

    *out = fbl::move(res);
    return ZX_OK;
}

zx_status_t VideoBuffer::Init(uint32_t max_frame_size) {
    if (max_frame_size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    uint64_t num_frames = size() / max_frame_size;
    zxlogf(TRACE, "buffer size: %lu, num_frames: %lu\n", size(), num_frames);

    fbl::AllocChecker ac;
    free_frames_.reserve(num_frames, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    locked_frames_.reserve(num_frames, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    for (uint64_t i = 0; i < num_frames; ++i) {
        free_frames_.push_back(i * max_frame_size);
    }
    return ZX_OK;
}

zx_status_t VideoBuffer::GetNewFrame(FrameOffset* out_offset) {
    if (out_offset == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (has_in_progress_frame_) {
        zxlogf(ERROR, "GetNewFrame failed, already writing to frame at offset: %lu\n",
               in_progress_frame_);
        return ZX_ERR_BAD_STATE;
    }
    if (free_frames_.is_empty()) {
        return ZX_ERR_NOT_FOUND;
    }
    size_t last = free_frames_.size() - 1;
    in_progress_frame_ = free_frames_.erase(last);
    has_in_progress_frame_ = true;
    *out_offset = in_progress_frame_;
    return ZX_OK;
}

zx_status_t VideoBuffer::FrameCompleted() {
    if (!has_in_progress_frame_) {
        zxlogf(ERROR, "FrameCompleted failed, no frame is currently in progress\n");
        return ZX_ERR_BAD_STATE;
    }
    locked_frames_.push_back(in_progress_frame_);
    has_in_progress_frame_ = false;
    return ZX_OK;
}

zx_status_t VideoBuffer::FrameRelease(FrameOffset frame_offset) {
    // TODO(jocelyndang): implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace usb
} // namespace video