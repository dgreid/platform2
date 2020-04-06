/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/******************************************************************************
 *
 ******************************************************************************/
#define LOG_TAG "MtkCam/Metadata"
//
#include <array>
#include <cutils/compiler.h>
#include <functional>
#include <inttypes.h>
//
#include <memory>
#include <mtkcam/utils/metadata/mtk_metadata_types.h>
#include <mtkcam/utils/std/Log.h>
#include <mtkcam/utils/std/common.h>
#include <mutex>
//
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/time.h>
//
#include <unordered_map>
#include <utility>
#include <vector>
//
namespace NSCam {
typedef int MERROR;
};
//

using NSCam::IMetadata;
using NSCam::MPoint;
using NSCam::MPointF;
using NSCam::MRational;
using NSCam::MRect;
using NSCam::MRectF;
using NSCam::MSize;
using NSCam::MSizeF;
using NSCam::Type2Type;
using NSCam::TYPE_MDOUBLE;
using NSCam::TYPE_MFLOAT;
using NSCam::TYPE_MINT32;
using NSCam::TYPE_MINT64;
using NSCam::TYPE_MPoint;
using NSCam::TYPE_MRational;
using NSCam::TYPE_MRect;
using NSCam::TYPE_MSize;
using NSCam::TYPE_MUINT8;

#define get_mtk_metadata_tag_type(x) mType

/******************************************************************************
 * IMetadata::Memory
 ******************************************************************************/
size_t IMetadata::Memory::size() const {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  return this->_data->size();
}

void IMetadata::Memory::resize(const size_t size,
                               uint8_t default_val /* = 0 */) {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  dup_data_locked();
  this->_data->resize(size, default_val);
}

size_t IMetadata::Memory::append(const Memory& other) {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  // acquires lockers
  std::lock(this->_data_lock, other._data_lock);
  std::lock_guard<std::mutex> lk1(this->_data_lock, std::adopt_lock);
  std::lock_guard<std::mutex> lk2(other._data_lock, std::adopt_lock);
#endif

  dup_data_locked();

  // append data
  this->_data->insert(this->_data->end(), other._data->begin(),
                      other._data->end());

  return this->_data->size();
}

size_t IMetadata::Memory::appendVector(const Memory& other) {
  return this->append(other);
}

size_t IMetadata::Memory::appendVector(const std::vector<uint8_t>& v) {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  dup_data_locked();
  // append data
  this->_data->insert(this->_data->end(), v.cbegin(), v.cend());

  return this->_data->size() + v.size();
}

const uint8_t* IMetadata::Memory::array() const {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  return this->_data->data();
}

uint8_t* IMetadata::Memory::editArray() {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  dup_data_locked();
  return this->_data->data();
}

uint8_t IMetadata::Memory::itemAt(size_t index) const {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  uint8_t val = this->_data->at(index);
  return val;
}

void IMetadata::Memory::clear() {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  this->_data->clear();
}

std::vector<uint8_t>::iterator IMetadata::Memory::begin() {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  return this->_data->begin();
}

std::vector<uint8_t>::iterator IMetadata::Memory::end() {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  return this->_data->end();
}

std::vector<uint8_t>::const_iterator IMetadata::Memory::cbegin() const {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  return this->_data->cbegin();
}

std::vector<uint8_t>::const_iterator IMetadata::Memory::cend() const {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(this->_data_lock);
#endif
  return this->_data->cend();
}

IMetadata::Memory::Memory() {
  this->_data = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>());
}

IMetadata::Memory::Memory(const Memory& other) {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(other._data_lock);
#endif
  this->_data = other._data;
}

IMetadata::Memory::Memory(Memory&& other) {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
  std::lock_guard<std::mutex> lk(other._data_lock);
#endif
  this->_data = std::move(other._data);
}

IMetadata::Memory& IMetadata::Memory::operator=(const Memory& other) {
  if (CC_UNLIKELY(this == &other)) {
    return *this;
  } else {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
    // acquires lockers
    std::lock(this->_data_lock, other._data_lock);
    std::lock_guard<std::mutex> lk1(this->_data_lock, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(other._data_lock, std::adopt_lock);
#endif
    this->_data = other._data;
  }

  return *this;
}

IMetadata::Memory& IMetadata::Memory::operator=(Memory&& other) {
  if (CC_UNLIKELY(this == &other)) {
    return *this;
  } else {
#if IMETADATA_MEMORY_THREAD_SAFE_SUPPORT
    // acquires lockers
    std::lock(this->_data_lock, other._data_lock);
    std::lock_guard<std::mutex> lk1(this->_data_lock, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(other._data_lock, std::adopt_lock);
#endif
    this->_data = std::move(other._data);
  }

  return *this;
}

void IMetadata::Memory::dup_data_locked() {
  // check if unique, if yes, no need to dup
  if (this->_data.use_count() == 1) {
    return;
  }

  // query size, it might be 0, but it's okay.
  const size_t dataSize = this->_data->size();

  // create a shared_ptr contains std::vector<uint8_t>
  auto spData =
      std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>(dataSize));

  // copy data if necessary (dataSize may be 0, but it's okay)
  std::memcpy(spData->data(), this->_data->data(), dataSize);

  // owns the unique data
  this->_data = spData;
}

/******************************************************************************
 *
 ******************************************************************************/
class IMetadata::IEntry::Implementor {
 private:
  Tag_t mTag;
  int mType;
  mutable std::mutex mResourceMtx;

 public:  ////                    Instantiation.
  virtual ~Implementor();
  explicit Implementor(Tag_t tag);
  Implementor& operator=(Implementor const& other);
  Implementor(Implementor const& other);

 public:  ////                    Accessors.
  /**
   * Return the tag.
   */
  virtual MUINT32 tag() const;

  /**
   * Return the type.
   */
  virtual MINT32 type() const;

  /**
   * Return the start address of IEntry's container.
   *  @note The memory chunk of container is always continuous.
   */
  virtual const void* data() const;

  /**
   * Check to see whether it is empty (no items) or not.
   */
  virtual MBOOL isEmpty() const;

  /**
   * Return the number of items.
   */
  virtual MUINT count() const;

  /**
   * Return how many items can be stored without reallocating the backing store.
   */
  virtual MUINT capacity() const;

  /**
   * Set the capacity.
   */
  virtual MBOOL setCapacity(MUINT size);

 public:  ////                    Operations.
  /**
   * Clear all items.
   * Note: Copy-on write.
   */
  virtual MVOID clear();

  /**
   * Delete an item at a given index.
   * Note: Copy-on write.
   */
  virtual MERROR removeAt(MUINT index);

#define IMETADATA_IENTRY_OPS_DECLARATION(_T)                                \
  class my_vector_##_T : public std::vector<_T> {                           \
   public:                                                                  \
    MERROR removeAt(MUINT index) {                                          \
      if (CC_UNLIKELY(index >= this->size())) {                             \
        return UNKNOWN_ERROR;                                               \
      }                                                                     \
      this->erase(this->begin() + index);                                   \
      return OK;                                                            \
    }                                                                       \
  };                                                                        \
  virtual MVOID push_back(_T const& item, Type2Type<_T>) {                  \
    std::lock_guard<std::mutex> lk(mResourceMtx);                           \
    mType = TYPE_##_T;                                                      \
    mStorage_##_T.push_back(item);                                          \
  }                                                                         \
  virtual MVOID replaceItemAt(MUINT index, _T const& item, Type2Type<_T>) { \
    std::lock_guard<std::mutex> lk(mResourceMtx);                           \
    mType = TYPE_##_T;                                                      \
    mStorage_##_T.at(index) = item;                                         \
  }                                                                         \
  virtual _T itemAt(MUINT index, Type2Type<_T>) const {                     \
    std::lock_guard<std::mutex> lk(mResourceMtx);                           \
    return reinterpret_cast<const _T&>(mStorage_##_T.at(index));            \
  }                                                                         \
  my_vector_##_T mStorage_##_T;

  IMETADATA_IENTRY_OPS_DECLARATION(MUINT8)
  IMETADATA_IENTRY_OPS_DECLARATION(MINT32)
  IMETADATA_IENTRY_OPS_DECLARATION(MFLOAT)
  IMETADATA_IENTRY_OPS_DECLARATION(MINT64)
  IMETADATA_IENTRY_OPS_DECLARATION(MDOUBLE)
  IMETADATA_IENTRY_OPS_DECLARATION(MRational)
  IMETADATA_IENTRY_OPS_DECLARATION(MPoint)
  IMETADATA_IENTRY_OPS_DECLARATION(MSize)
  IMETADATA_IENTRY_OPS_DECLARATION(MRect)
  IMETADATA_IENTRY_OPS_DECLARATION(IMetadata)
  IMETADATA_IENTRY_OPS_DECLARATION(Memory)

#undef IMETADATA_IENTRY_OPS_DECLARATION
};

/******************************************************************************
 *
 ******************************************************************************/
#define RETURN_TYPE_OPS(_Type, _Ops)                                             \
  _Type == TYPE_MUINT8                                                           \
      ? mStorage_MUINT8._Ops                                                     \
      : _Type == TYPE_MINT32                                                     \
            ? mStorage_MINT32._Ops                                               \
            : _Type == TYPE_MFLOAT                                               \
                  ? mStorage_MFLOAT._Ops                                         \
                  : _Type == TYPE_MINT64                                         \
                        ? mStorage_MINT64._Ops                                   \
                        : _Type == TYPE_MDOUBLE                                  \
                              ? mStorage_MDOUBLE._Ops                            \
                              : _Type == TYPE_MRational                          \
                                    ? mStorage_MRational._Ops                    \
                                    : _Type == TYPE_MPoint                       \
                                          ? mStorage_MPoint._Ops                 \
                                          : _Type == TYPE_MSize                  \
                                                ? mStorage_MSize._Ops            \
                                                : _Type == TYPE_Memory           \
                                                      ? mStorage_Memory._Ops     \
                                                      : _Type == TYPE_MRect      \
                                                            ? mStorage_MRect     \
                                                                  ._Ops          \
                                                            : mStorage_IMetadata \
                                                                  ._Ops;

#define RETURN_TYPE_SRC_DST_OPS(_Type, _Ops, _Dst)              \
  [&, this]() {                                                 \
    if (_Type == TYPE_MUINT8)                                   \
      return (mStorage_MUINT8 _Ops _Dst.mStorage_MUINT8);       \
    else if (_Type == TYPE_MINT32)                              \
      return (mStorage_MINT32 _Ops _Dst.mStorage_MINT32);       \
    else if (_Type == TYPE_MFLOAT)                              \
      return (mStorage_MFLOAT _Ops _Dst.mStorage_MFLOAT);       \
    else if (_Type == TYPE_MINT64)                              \
      return (mStorage_MINT64 _Ops _Dst.mStorage_MINT64);       \
    else if (_Type == TYPE_MDOUBLE)                             \
      return (mStorage_MDOUBLE _Ops _Dst.mStorage_MDOUBLE);     \
    else if (_Type == TYPE_MRational)                           \
      return (mStorage_MRational _Ops _Dst.mStorage_MRational); \
    else if (_Type == TYPE_MPoint)                              \
      return (mStorage_MPoint _Ops _Dst.mStorage_MPoint);       \
    else if (_Type == TYPE_MSize)                               \
      return (mStorage_MSize _Ops _Dst.mStorage_MSize);         \
    else if (_Type == TYPE_MRect)                               \
      return (mStorage_MRect _Ops _Dst.mStorage_MRect);         \
    else if (_Type == TYPE_IMetadata)                           \
      return (mStorage_IMetadata _Ops _Dst.mStorage_IMetadata); \
    else if (_Type == TYPE_Memory)                              \
      return (mStorage_Memory _Ops _Dst.mStorage_Memory);       \
    return (mStorage_MUINT8 _Ops _Dst.mStorage_MUINT8);         \
  }()

#define SET_TYPE_OPS(_Type, _Ops, _Val)                                          \
  _Type == TYPE_MUINT8                                                           \
      ? mStorage_MUINT8._Ops(_Val)                                               \
      : _Type == TYPE_MINT32                                                     \
            ? mStorage_MINT32._Ops(_Val)                                         \
            : _Type == TYPE_MFLOAT                                               \
                  ? mStorage_MFLOAT._Ops(_Val)                                   \
                  : _Type == TYPE_MINT64                                         \
                        ? mStorage_MINT64._Ops(_Val)                             \
                        : _Type == TYPE_MDOUBLE                                  \
                              ? mStorage_MDOUBLE._Ops(_Val)                      \
                              : _Type == TYPE_MRational                          \
                                    ? mStorage_MRational._Ops(_Val)              \
                                    : _Type == TYPE_MPoint                       \
                                          ? mStorage_MPoint._Ops(_Val)           \
                                          : _Type == TYPE_MSize                  \
                                                ? mStorage_MSize._Ops(_Val)      \
                                                : _Type == TYPE_Memory           \
                                                      ? mStorage_Memory._Ops(    \
                                                            _Val)                \
                                                      : _Type == TYPE_MRect      \
                                                            ? mStorage_MRect     \
                                                                  ._Ops(_Val)    \
                                                            : mStorage_IMetadata \
                                                                  ._Ops(_Val);

#define SRC_DST_OPERATOR(_Type, _Ops, _Src)            \
  if (_Type == TYPE_MUINT8)                            \
    (mStorage_MUINT8 _Ops _Src.mStorage_MUINT8);       \
  else if (_Type == TYPE_MINT32)                       \
    (mStorage_MINT32 _Ops _Src.mStorage_MINT32);       \
  else if (_Type == TYPE_MFLOAT)                       \
    (mStorage_MFLOAT _Ops _Src.mStorage_MFLOAT);       \
  else if (_Type == TYPE_MINT64)                       \
    (mStorage_MINT64 _Ops _Src.mStorage_MINT64);       \
  else if (_Type == TYPE_MDOUBLE)                      \
    (mStorage_MDOUBLE _Ops _Src.mStorage_MDOUBLE);     \
  else if (_Type == TYPE_MRational)                    \
    (mStorage_MRational _Ops _Src.mStorage_MRational); \
  else if (_Type == TYPE_MPoint)                       \
    (mStorage_MPoint _Ops _Src.mStorage_MPoint);       \
  else if (_Type == TYPE_MSize)                        \
    (mStorage_MSize _Ops _Src.mStorage_MSize);         \
  else if (_Type == TYPE_MRect)                        \
    (mStorage_MRect _Ops _Src.mStorage_MRect);         \
  else if (_Type == TYPE_Memory)                       \
    (mStorage_Memory _Ops _Src.mStorage_Memory);       \
  else if (_Type == TYPE_IMetadata)                    \
    (mStorage_IMetadata _Ops _Src.mStorage_IMetadata);
/******************************************************************************
 *
 ******************************************************************************/
IMetadata::IEntry::Implementor::Implementor(Tag_t tag)
    : mTag(tag),
      mType(-1)
#define STORAGE_DECLARATION(_T) , mStorage_##_T()

          STORAGE_DECLARATION(MUINT8) STORAGE_DECLARATION(MINT32)
              STORAGE_DECLARATION(MFLOAT) STORAGE_DECLARATION(MINT64)
                  STORAGE_DECLARATION(MDOUBLE) STORAGE_DECLARATION(MRational)
                      STORAGE_DECLARATION(MPoint) STORAGE_DECLARATION(MSize)
                          STORAGE_DECLARATION(MRect)
                              STORAGE_DECLARATION(IMetadata)
                                  STORAGE_DECLARATION(Memory)

#undef STORAGE_DECLARATION
{
}

IMetadata::IEntry::Implementor::Implementor(
    IMetadata::IEntry::Implementor const& other)
    : mTag(other.mTag), mType(other.mType) {
  std::lock_guard<std::mutex> lk1(other.mResourceMtx);
#define STORAGE_DECLARATION(_T) mStorage_##_T = other.mStorage_##_T;

  STORAGE_DECLARATION(MUINT8)
  STORAGE_DECLARATION(MINT32)
  STORAGE_DECLARATION(MFLOAT)
  STORAGE_DECLARATION(MINT64)
  STORAGE_DECLARATION(MDOUBLE)
  STORAGE_DECLARATION(MRational)
  STORAGE_DECLARATION(MPoint)
  STORAGE_DECLARATION(MSize)
  STORAGE_DECLARATION(MRect)
  STORAGE_DECLARATION(IMetadata)
  STORAGE_DECLARATION(Memory)

#undef STORAGE_DECLARATION
}

IMetadata::IEntry::Implementor& IMetadata::IEntry::Implementor::operator=(
    IMetadata::IEntry::Implementor const& other) {
  if (this != &other) {
    std::lock(mResourceMtx, other.mResourceMtx);
    std::lock_guard<std::mutex>(mResourceMtx, std::adopt_lock);
    std::lock_guard<std::mutex>(other.mResourceMtx, std::adopt_lock);
    mTag = other.mTag;
    mType = other.mType;
    SRC_DST_OPERATOR(get_mtk_metadata_tag_type(mTag), =, other);
  } else {
    CAM_LOGW("this(%p) == other(%p)", this, &other);
  }

  return *this;
}

IMetadata::IEntry::Implementor::~Implementor() {
  std::lock_guard<std::mutex> lk1(mResourceMtx);
#define STORAGE_DELECTION(_T) mStorage_##_T.clear();

  STORAGE_DELECTION(MUINT8)
  STORAGE_DELECTION(MINT32)
  STORAGE_DELECTION(MFLOAT)
  STORAGE_DELECTION(MINT64)
  STORAGE_DELECTION(MDOUBLE)
  STORAGE_DELECTION(MRational)
  STORAGE_DELECTION(MPoint)
  STORAGE_DELECTION(MSize)
  STORAGE_DELECTION(MRect)
  STORAGE_DELECTION(IMetadata)
  STORAGE_DELECTION(Memory)

#undef STORAGE_DELECTION
}

MUINT32
IMetadata::IEntry::Implementor::tag() const {
  return mTag;
}

MINT32
IMetadata::IEntry::Implementor::type() const {
  return mType;
}

const void* IMetadata::IEntry::Implementor::data() const {
  std::lock_guard<std::mutex> lk1(mResourceMtx);
  if (get_mtk_metadata_tag_type(mTag) == -1) {
    return nullptr;
  }

  switch (get_mtk_metadata_tag_type(mTag)) {
#define __DATA_CASE__(_T) \
  case TYPE_##_T:         \
    return mStorage_##_T.data();
    __DATA_CASE__(MUINT8)
    __DATA_CASE__(MINT32)
    __DATA_CASE__(MFLOAT)
    __DATA_CASE__(MINT64)
    __DATA_CASE__(MDOUBLE)
    __DATA_CASE__(MRational)
    __DATA_CASE__(MPoint)
    __DATA_CASE__(MSize)
    __DATA_CASE__(MRect)
    __DATA_CASE__(IMetadata)
    __DATA_CASE__(Memory)
    default:
      return nullptr;
#undef __DATA_CASE__
  }

  return nullptr;
}

MBOOL
IMetadata::IEntry::Implementor::isEmpty() const {
  std::lock_guard<std::mutex> lk1(mResourceMtx);
  return get_mtk_metadata_tag_type(mTag) == -1
             ? MTRUE
             : RETURN_TYPE_OPS(get_mtk_metadata_tag_type(mTag), empty());
}

MUINT
IMetadata::IEntry::Implementor::count() const {
  std::lock_guard<std::mutex> lk1(mResourceMtx);
  return get_mtk_metadata_tag_type(mTag) == -1
             ? 0
             : RETURN_TYPE_OPS(get_mtk_metadata_tag_type(mTag), size());
}

MUINT
IMetadata::IEntry::Implementor::capacity() const {
  std::lock_guard<std::mutex> lk1(mResourceMtx);
  return get_mtk_metadata_tag_type(mTag) == -1
             ? 0
             : RETURN_TYPE_OPS(get_mtk_metadata_tag_type(mTag), capacity());
}

MBOOL
IMetadata::IEntry::Implementor::setCapacity(MUINT size) {
  std::lock_guard<std::mutex> lk1(mResourceMtx);
  if (get_mtk_metadata_tag_type(mTag) == -1) {
    return MFALSE;
  }

  SET_TYPE_OPS(get_mtk_metadata_tag_type(mTag), reserve, size)

  return MTRUE;
}

MVOID
IMetadata::IEntry::Implementor::clear() {
  std::lock_guard<std::mutex> lk1(mResourceMtx);
  if (get_mtk_metadata_tag_type(mTag) != -1) {
    RETURN_TYPE_OPS(get_mtk_metadata_tag_type(mTag), clear());
  }
}

MERROR
IMetadata::IEntry::Implementor::removeAt(MUINT index) {
  std::lock_guard<std::mutex> lk1(mResourceMtx);
  MERROR ret =
      get_mtk_metadata_tag_type(mTag) == -1
          ? BAD_VALUE
          : SET_TYPE_OPS(get_mtk_metadata_tag_type(mTag), removeAt, index);
  return ret == BAD_VALUE ? BAD_VALUE : OK;
}

#undef RETURN_TYPE_OPS
#undef SET_TYPE_OPS
#undef SRC_DST_OPERATOR
/******************************************************************************
 *
 ******************************************************************************/
#define AEE_IF_TAG_ERROR(_TAG_)       \
  if (_TAG_ == (uint32_t)-1) {        \
    CAM_LOGE("tag(%d) error", _TAG_); \
  }

IMetadata::IEntry::IEntry(Tag_t tag) : mpImp(new Implementor(tag)) {}

IMetadata::IEntry::IEntry(IMetadata::IEntry const& other) {
  std::lock_guard<std::mutex> lk1(other.mEntryLock);
  mpImp = new Implementor(*(other.mpImp));
}

IMetadata::IEntry::~IEntry() {
  std::lock_guard<std::mutex> lk1(mEntryLock);
  if (mpImp) {
    delete mpImp;
    mpImp = nullptr;
  }
}

IMetadata::IEntry& IMetadata::IEntry::operator=(
    IMetadata::IEntry const& other) {
  if (this != &other) {
    std::lock(mEntryLock, other.mEntryLock);
    std::lock_guard<std::mutex> lk1(mEntryLock, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(other.mEntryLock, std::adopt_lock);
    delete mpImp;
    mpImp = new Implementor(*(other.mpImp));
  } else {
    CAM_LOGW("this(%p) == other(%p)", this, &other);
  }

  return *this;
}

MUINT32
IMetadata::IEntry::tag() const {
  std::lock_guard<std::mutex> lk1(mEntryLock);
  return mpImp->tag();
}

MINT32
IMetadata::IEntry::type() const {
  std::lock_guard<std::mutex> lk1(mEntryLock);
  return mpImp->type();
}

const void* IMetadata::IEntry::data() const {
  std::lock_guard<std::mutex> lk1(mEntryLock);
  return mpImp->data();
}

MBOOL
IMetadata::IEntry::isEmpty() const {
  std::lock_guard<std::mutex> lk1(mEntryLock);
  return mpImp->isEmpty();
}

MUINT
IMetadata::IEntry::count() const {
  std::lock_guard<std::mutex> lk1(mEntryLock);
  return mpImp->count();
}

MUINT
IMetadata::IEntry::capacity() const {
  AEE_IF_TAG_ERROR(tag())
  std::lock_guard<std::mutex> lk1(mEntryLock);
  return mpImp->capacity();
}

MBOOL
IMetadata::IEntry::setCapacity(MUINT size) {
  AEE_IF_TAG_ERROR(tag())
  std::lock_guard<std::mutex> lk1(mEntryLock);
  return mpImp->setCapacity(size);
}

MVOID
IMetadata::IEntry::clear() {
  AEE_IF_TAG_ERROR(tag())
  std::lock_guard<std::mutex> lk1(mEntryLock);
  mpImp->clear();
}

MERROR
IMetadata::IEntry::removeAt(MUINT index) {
  AEE_IF_TAG_ERROR(tag())
  std::lock_guard<std::mutex> lk1(mEntryLock);
  return mpImp->removeAt(index);
}

#define ASSERT_CHECK(_defaultT, _T)                                       \
  CAM_LOGE_IF(TYPE_##_T != _defaultT, "tag(%x), type(%d) should be (%d)", \
              tag(), TYPE_##_T, _defaultT);                               \
  if (TYPE_##_T != _defaultT) {                                           \
  }
#undef ASSERT_CHECK

#define IMETADATA_IENTRY_OPS_DECLARATION(_T)                            \
  MVOID                                                                 \
  IMetadata::IEntry::push_back(_T const& item, Type2Type<_T> type) {    \
    AEE_IF_TAG_ERROR(tag())                                             \
    std::lock_guard<std::mutex> lk1(mEntryLock);                        \
    mpImp->push_back(item, type);                                       \
  }                                                                     \
  MVOID                                                                 \
  IMetadata::IEntry::replaceItemAt(MUINT index, _T const& item,         \
                                   Type2Type<_T> type) {                \
    AEE_IF_TAG_ERROR(tag())                                             \
    std::lock_guard<std::mutex> lk1(mEntryLock);                        \
    mpImp->replaceItemAt(index, item, type);                            \
  }                                                                     \
  _T IMetadata::IEntry::itemAt(MUINT index, Type2Type<_T> type) const { \
    AEE_IF_TAG_ERROR(tag())                                             \
    std::lock_guard<std::mutex> lk1(mEntryLock);                        \
    return mpImp->itemAt(index, type);                                  \
  }

IMETADATA_IENTRY_OPS_DECLARATION(MUINT8)
IMETADATA_IENTRY_OPS_DECLARATION(MINT32)
IMETADATA_IENTRY_OPS_DECLARATION(MFLOAT)
IMETADATA_IENTRY_OPS_DECLARATION(MINT64)
IMETADATA_IENTRY_OPS_DECLARATION(MDOUBLE)
IMETADATA_IENTRY_OPS_DECLARATION(MRational)
IMETADATA_IENTRY_OPS_DECLARATION(MPoint)
IMETADATA_IENTRY_OPS_DECLARATION(MSize)
IMETADATA_IENTRY_OPS_DECLARATION(MRect)
IMETADATA_IENTRY_OPS_DECLARATION(IMetadata)
IMETADATA_IENTRY_OPS_DECLARATION(IMetadata::Memory)
#undef IMETADATA_IENTRY_OPS_DECLARATION

#undef AEE_IF_TAG_ERROR
/******************************************************************************
 *
 ******************************************************************************/
class IMetadata::Implementor {
 public:  ////                        Instantiation.
  virtual ~Implementor();
  Implementor();
  Implementor& operator=(Implementor const& other);
  Implementor(Implementor const& other);

 public:  ////                        operators
  Implementor& operator+=(Implementor const& other);
  Implementor const operator+(Implementor const& other) const;

 public:  ////                        Accessors.
  /**
   * Check to see whether it is empty (no entries) or not.
   */
  virtual MBOOL isEmpty() const;

  /**
   * Return the number of entries.
   */
  virtual MUINT count() const;

 public:  ////                        Operations.
  /**
   * Add the given IMetadata, if the key exists both in the current and the
   * given one, the value will be replaced by the given one. E.g.: C = A.add(B);
   * --> C = A + B; Return the merged IMetadata. Note: Copy-on write.
   */
  virtual Implementor add(Implementor const& other) const;

  /**
   * Append the given IMetadata, if the key exists both in the current
   * and given one, the value will be replaced by the given one.
   * E.g.: C = A.append(B); --> A = A + B;
   * Note: Copy-on write.
   */
  virtual MVOID append(Implementor const& other);

  /**
   * Clear all entries.
   * Note: Copy-on write.
   */
  virtual MVOID clear();

  /**
   * Delete an entry by tag.
   * Note: Copy-on write.
   */
  virtual MERROR remove(Tag_t tag);

  /**
   * Sort all entries for faster find.
   * Note: Copy-on write.
   */
  virtual MERROR sort();

  /**
   * Update metadata entry. An entry will be created if it doesn't exist
   * already. Note: Copy-on write.
   */
  virtual MERROR update(Tag_t tag, IEntry const& entry);

  /**
   * Get metadata entry by tag, with no editing.
   */
  virtual IEntry entryFor(Tag_t tag) const;

  /**
   * Get metadata entry by index, with no editing.
   */
  virtual IEntry entryAt(MUINT index) const;

  /**
   * Take metadata entry by tag.
   * Complexity: O(log N)
   */
  virtual IEntry takeEntryFor(Tag_t tag);

  /**
   * Take metadata entry by index.
   * Complexity: O(1)
   */
  virtual IEntry takeEntryAt(MUINT index);

  /**
   * Flatten IMetadata.
   */
  virtual ssize_t flatten(char* buf, size_t buf_size) const;

  /**
   * Unflatten IMetadata.
   */
  virtual ssize_t unflatten(char* buf, size_t buf_size);

  virtual void dump(int layer = 0);

 public:
  std::unordered_map<Tag_t, IEntry> mMap;
  mutable std::mutex mResourceMtx;
};

/******************************************************************************
 *
 ******************************************************************************/
IMetadata::Implementor::Implementor() {}

IMetadata::Implementor::Implementor(IMetadata::Implementor const& other) {
  std::lock_guard<std::mutex> lk(other.mResourceMtx);
  mMap = other.mMap;
}

IMetadata::Implementor::~Implementor() {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  mMap.clear();
}

IMetadata::Implementor& IMetadata::Implementor::operator+=(
    IMetadata::Implementor const& other) {
  if (this != &other) {
    std::lock(mResourceMtx, other.mResourceMtx);
    std::lock_guard<std::mutex>(mResourceMtx, std::adopt_lock);
    std::lock_guard<std::mutex>(other.mResourceMtx, std::adopt_lock);
    //
    for (const auto& item : other.mMap) {
      mMap[item.first] = item.second;
    }
  } else {
    CAM_LOGW("this(%p) == other(%p)", this, &other);
  }

  return *this;
}

IMetadata::Implementor const IMetadata::Implementor::operator+(
    IMetadata::Implementor const& other) const {
  return Implementor(*this) += other;
}

IMetadata::Implementor& IMetadata::Implementor::operator=(
    IMetadata::Implementor const& other) {
  if (this != &other) {
    std::lock(mResourceMtx, other.mResourceMtx);
    std::lock_guard<std::mutex>(mResourceMtx, std::adopt_lock);
    std::lock_guard<std::mutex>(other.mResourceMtx, std::adopt_lock);
    // release mMap'storage
    // assign other.mMap's storage pointer to mMap
    // add 1 to storage's sharebuffer
    mMap = other.mMap;
  } else {
    CAM_LOGW("this(%p) == other(%p)", this, &other);
  }

  return *this;
}

MBOOL
IMetadata::Implementor::isEmpty() const {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  return mMap.empty();
}

MUINT
IMetadata::Implementor::count() const {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  return mMap.size();
}

MVOID
IMetadata::Implementor::clear() {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  mMap.clear();
}

IMetadata::Implementor IMetadata::Implementor::add(
    Implementor const& other) const {
  return *this + other;
}

MVOID
IMetadata::Implementor::append(Implementor const& other) {
  *this += other;
}

MERROR
IMetadata::Implementor::remove(Tag_t tag) {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  auto itr = mMap.find(tag);
  if (CC_LIKELY(itr != mMap.end())) {
    mMap.erase(itr);
    return OK;
  }
  return NO_MEMORY;
}

MERROR
IMetadata::Implementor::sort() {
  return OK;
}

MERROR
IMetadata::Implementor::update(Tag_t tag, IEntry const& entry) {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  mMap[tag] = entry;
  return OK;
}

IMetadata::IEntry IMetadata::Implementor::entryFor(Tag_t tag) const {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  const auto itr = mMap.find(tag);
  if (CC_UNLIKELY(itr == mMap.end())) {
    return IMetadata::IEntry();
  }
  return itr->second;
}

IMetadata::IEntry IMetadata::Implementor::entryAt(MUINT index) const {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  if (CC_UNLIKELY(index >= mMap.size())) {
    return IMetadata::IEntry();
  }

  auto itr = mMap.begin();
  for (size_t i = 0; i < index; ++i, ++itr) {
  }
  return itr->second;
}

IMetadata::IEntry IMetadata::Implementor::takeEntryFor(Tag_t tag) {
  IMetadata::IEntry entry;
  //
  std::lock_guard<std::mutex> lk(mResourceMtx);
  //
  auto itr = mMap.find(tag);
  if (CC_LIKELY(itr != mMap.end())) {
    entry = itr->second;
    mMap.erase(itr);
  }
  return entry;
}

IMetadata::IEntry IMetadata::Implementor::takeEntryAt(MUINT index) {
  std::lock_guard<std::mutex> lk(mResourceMtx);
  if (CC_UNLIKELY(index >= mMap.size())) {
    return IMetadata::IEntry();
  }

  auto itr = mMap.begin();
  for (size_t i = 0; i < index; ++i, ++itr) {
    // move itr
  }
  IMetadata::IEntry r = itr->second;
  mMap.erase(itr);

  return r;
}

#define DUMP_METADATA_STRING(_layer_, _msg_) \
  CAM_LOGD("(L%d) %s", _layer_, _msg_.c_str());

void IMetadata::Implementor::dump(int layer) {
  auto itr = mMap.begin();
  for (size_t i = 0; i < mMap.size(); ++i, ++itr) {
    IMetadata::IEntry entry = itr->second;
    std::string msg = base::StringPrintf(
        "[%s] Map(%zu/%zu) tag(0x%x) type(%d) count(%d) ", __FUNCTION__, i,
        mMap.size(), entry.tag(), entry.type(), entry.count());
    //
    if (TYPE_IMetadata == entry.type()) {
      for (size_t j = 0; j < entry.count(); ++j) {
        IMetadata meta = entry.itemAt(j, Type2Type<IMetadata>());
        msg += base::StringPrintf("metadata.. ");
        DUMP_METADATA_STRING(layer, msg);
        meta.dump(layer + 1);
      }
    } else {
      switch (entry.type()) {
        case TYPE_MUINT8:
          for (size_t j = 0; j < entry.count(); j++) {
            msg +=
                base::StringPrintf("%d ", entry.itemAt(j, Type2Type<MUINT8>()));
          }
          break;
        case TYPE_MINT32:
          for (size_t j = 0; j < entry.count(); j++) {
            msg +=
                base::StringPrintf("%d ", entry.itemAt(j, Type2Type<MINT32>()));
          }
          break;
        case TYPE_MINT64:
          for (size_t j = 0; j < entry.count(); j++) {
            msg += base::StringPrintf("%" PRId64 " ",
                                      entry.itemAt(j, Type2Type<MINT64>()));
          }
          break;
        case TYPE_MFLOAT:
          for (size_t j = 0; j < entry.count(); j++) {
            msg +=
                base::StringPrintf("%f ", entry.itemAt(j, Type2Type<MFLOAT>()));
          }
          break;
        case TYPE_MDOUBLE:
          for (size_t j = 0; j < entry.count(); j++) {
            msg += base::StringPrintf("%lf ",
                                      entry.itemAt(j, Type2Type<MDOUBLE>()));
          }
          break;
        case TYPE_MSize:
          for (size_t j = 0; j < entry.count(); j++) {
            MSize src_size = entry.itemAt(j, Type2Type<MSize>());
            msg += base::StringPrintf("size(%d,%d) ", src_size.w, src_size.h);
          }
          break;
        case TYPE_MRect:
          for (size_t j = 0; j < entry.count(); j++) {
            MRect src_rect = entry.itemAt(j, Type2Type<MRect>());
            msg += base::StringPrintf("rect(%d,%d,%d,%d) ", src_rect.p.x,
                                      src_rect.p.y, src_rect.s.w, src_rect.s.h);
          }
          break;
        case TYPE_MPoint:
          for (size_t j = 0; j < entry.count(); j++) {
            MPoint src_point = entry.itemAt(j, Type2Type<MPoint>());
            msg +=
                base::StringPrintf("point(%d,%d) ", src_point.x, src_point.y);
          }
          break;
        case TYPE_MRational:
          for (size_t j = 0; j < entry.count(); j++) {
            MRational src_rational = entry.itemAt(j, Type2Type<MRational>());
            msg +=
                base::StringPrintf("rational(%d,%d) ", src_rational.numerator,
                                   src_rational.denominator);
          }
          break;
        case TYPE_Memory:
          msg += base::StringPrintf("Memory type: not dump!");
          break;
        default:
          msg += base::StringPrintf("unsupported type(%d)", entry.type());
      }
    }
    DUMP_METADATA_STRING(layer, msg);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
struct metadata_buffer_entry_info {
  MUINT32 tag;
  MUINT8 type;
  MUINT32 count;
};

struct metadata_buffer_handle {
  metadata_buffer_handle(char* b, size_t s) : buffer(b), size(s), offset(0) {}

  MERROR get_entry_count(MUINT32& entry_count) {
    align(sizeof(entry_count));

    entry_count = *(reinterpret_cast<MUINT32*>(current()));
    move(sizeof(entry_count));

    return OK;
  }

  MERROR set_entry_count(MUINT32 entry_count) {
    align(sizeof(entry_count));

    *(reinterpret_cast<MUINT32*>(current())) = entry_count;
    move(sizeof(entry_count));

    return OK;
  }

  inline MERROR get_entry_info(metadata_buffer_entry_info* info) {
    align(alignof(metadata_buffer_entry_info));

    if (!check_size(sizeof(metadata_buffer_entry_info))) {
      CAM_LOGE("[get_entry_info] out of boundary");
      return NO_MEMORY;
    }

    memcpy(info, current(), sizeof(metadata_buffer_entry_info));
    move(sizeof(metadata_buffer_entry_info));

    return OK;
  }

  inline MERROR set_entry_info(metadata_buffer_entry_info* info) {
    align(alignof(metadata_buffer_entry_info));

    if (!check_size(sizeof(metadata_buffer_entry_info))) {
      CAM_LOGE("[set_entry_info] out of boundary");
      return NO_MEMORY;
    }

    memcpy(current(), info, sizeof(metadata_buffer_entry_info));
    move(sizeof(metadata_buffer_entry_info));
    return OK;
  }

  inline MBOOL check_size(size_t data_size) {
    if (size < offset + data_size) {
      CAM_LOGE("memory not enough, size=%zu, offset=%zu, data=%zu", size,
               offset, data_size);
      return MFALSE;
    }
    return MTRUE;
  }

  inline MVOID align(MUINT8 alignment) {
    offset += alignment - ((uintptr_t)current() % alignment);
  }

  inline char* current() { return buffer + offset; }

  inline size_t move(size_t m) {
    offset += m;
    return offset;
  }

  char* buffer;
  size_t size;
  size_t offset;
};

template <class T>
static MERROR write_to_buffer(metadata_buffer_handle* handle,
                              const IMetadata::IEntry& entry) {
  if (!handle->check_size(sizeof(T) * entry.count())) {
    return NO_MEMORY;
  }

  for (MUINT32 i = 0; i < entry.count(); i++) {
    const T t = entry.itemAt(i, Type2Type<T>());
    *(reinterpret_cast<T*>(handle->current())) = t;
    handle->move(sizeof(T));
  }

  return OK;
}

template <class T>
static MERROR read_from_buffer(metadata_buffer_handle* handle,
                               IMetadata::IEntry* entry,
                               MINT32 count) {
  if (!handle->check_size(sizeof(T) * count)) {
    return NO_MEMORY;
  }

  entry->setCapacity(count);

  T* p;
  for (MUINT32 i = 0; i < count; i++) {
    p = reinterpret_cast<T*>(handle->current());
    entry->push_back(*p, Type2Type<T>());
    handle->move(sizeof(T));
  }

  return OK;
}

template <>
MERROR write_to_buffer<IMetadata::Memory>(metadata_buffer_handle* handle,
                                          const IMetadata::IEntry& entry) {
  for (uint32_t i = 0; i < entry.count(); i++) {
    const IMetadata::Memory m = entry.itemAt(i, Type2Type<IMetadata::Memory>());

    if (i) {
      handle->align(sizeof(MUINT32));
    }

    if (!handle->check_size(sizeof(MUINT32) + sizeof(MUINT8) * m.size())) {
      return NO_MEMORY;
    }

    size_t s = m.size();
    *(reinterpret_cast<size_t*>(handle->current())) = s;
    handle->move(sizeof(s));
    memcpy(handle->current(), m.array(), m.size());
    handle->move(sizeof(MUINT8) * m.size());
  }

  return OK;
}

template <>
MERROR read_from_buffer<IMetadata::Memory>(metadata_buffer_handle* handle,
                                           IMetadata::IEntry* entry,
                                           MINT32 count) {
  MUINT32 s;
  for (MUINT32 i = 0; i < count; i++) {
    if (i) {
      handle->align(sizeof(MUINT32));
    }

    if (!handle->check_size(sizeof(MUINT32))) {
      return NO_MEMORY;
    }

    s = *(reinterpret_cast<size_t*>(handle->current()));
    handle->move(sizeof(MUINT32));

    if (!handle->check_size(sizeof(MUINT8) * s)) {
      return NO_MEMORY;
    }

    IMetadata::Memory mMemory;
    mMemory.resize(s);
    memcpy(mMemory.editArray(), handle->current(), sizeof(MUINT8) * s);
    handle->move(s);

    entry->push_back(mMemory, Type2Type<IMetadata::Memory>());
  }

  return OK;
}

template <>
MERROR write_to_buffer<IMetadata>(metadata_buffer_handle* handle,
                                  const IMetadata::IEntry& entry) {
  for (uint32_t i = 0; i < entry.count(); i++) {
    const IMetadata m = entry.itemAt(i, Type2Type<IMetadata>());
    handle->offset += m.flatten(handle->buffer + handle->offset,
                                handle->size - handle->offset);
  }

  return OK;
}

template <>
MERROR read_from_buffer<IMetadata>(metadata_buffer_handle* handle,
                                   IMetadata::IEntry* entry,
                                   MINT32 count) {
  for (MUINT32 i = 0; i < count; i++) {
    IMetadata meta;
    handle->offset += meta.unflatten(handle->buffer + handle->offset,
                                     handle->size - handle->offset);
    entry->push_back(meta, Type2Type<IMetadata>());
  }
  return OK;
}

#if 0  // to do
static int sFlattenLayer = 0;
static int sUnflattenLayer = 0;
#define CAM_LOGD_TIME(fmt, arg...) CAM_LOGD("[%s] " fmt, __FUNCTION__, ##arg)
#else
#define CAM_LOGD_TIME(fmt, arg...)
#endif

ssize_t IMetadata::Implementor::flatten(char* buf, size_t buf_size) const {
  struct timeval t;
  gettimeofday(&t, NULL);
  MINT64 start_time = (t.tv_sec) * 1000000000LL + (t.tv_usec) * 1000LL;
  //
  std::lock_guard<std::mutex> lk(mResourceMtx);
  //
  int ret = 0;
  metadata_buffer_handle handle(buf, buf_size);
  metadata_buffer_entry_info info;
  handle.set_entry_count(mMap.size());

  auto itr = mMap.begin();
  for (MUINT32 i = 0; i < mMap.size(); ++i, ++itr) {
    const IMetadata::IEntry& entry = itr->second;

    info.tag = entry.tag();
    info.type = entry.type();
    info.count = entry.count();
    handle.set_entry_info(&info);

    typedef NSCam::IMetadata IMetadata;
    switch (info.type) {
#define CASE_WRITER(_type_)                        \
  case TYPE_##_type_:                              \
    ret = write_to_buffer<_type_>(&handle, entry); \
    break;

      CASE_WRITER(MUINT8);
      CASE_WRITER(MINT32);
      CASE_WRITER(MFLOAT);
      CASE_WRITER(MINT64);
      CASE_WRITER(MDOUBLE);
      CASE_WRITER(MRational);
      CASE_WRITER(MPoint);
      CASE_WRITER(MSize);
      CASE_WRITER(MRect);
      CASE_WRITER(IMetadata);
      CASE_WRITER(Memory);
#undef CASE_WRITER
      default:
        CAM_LOGW("[flatten] data type %u is unsupported. tag:0x%x", info.type,
                 info.tag);
        ret = BAD_VALUE;
        continue;
    }

    if (ret < 0) {
      return ret;
    }
  }
  gettimeofday(&t, NULL);
  MINT64 end_time = (t.tv_sec) * 1000000000LL + (t.tv_usec) * 1000LL;

  CAM_LOGD("offset:%zu time:%" PRIu64 "us", handle.offset,
           (end_time - start_time) / 1000);

  return handle.offset;
}

ssize_t IMetadata::Implementor::unflatten(char* buf, size_t buf_size) {
  struct timeval t;
  gettimeofday(&t, NULL);
  MINT64 start_time = (t.tv_sec) * 1000000000LL + (t.tv_usec) * 1000LL;
  //
  std::lock_guard<std::mutex> lk(mResourceMtx);
  //
  int ret = 0;
  metadata_buffer_handle handle(buf, buf_size);
  metadata_buffer_entry_info info;
  MUINT32 entry_count;
  handle.get_entry_count(entry_count);
  mMap.reserve(entry_count);

  for (MUINT32 i = 0; i < entry_count; i++) {
    handle.get_entry_info(&info);
    IMetadata::IEntry entry(info.tag);

    typedef NSCam::IMetadata IMetadata;
    switch (info.type) {
#define CASE_READER(_type_)                                      \
  case TYPE_##_type_:                                            \
    ret = read_from_buffer<_type_>(&handle, &entry, info.count); \
    break;

      CASE_READER(MUINT8);
      CASE_READER(MINT32);
      CASE_READER(MFLOAT);
      CASE_READER(MINT64);
      CASE_READER(MDOUBLE);
      CASE_READER(MRational);
      CASE_READER(MPoint);
      CASE_READER(MSize);
      CASE_READER(MRect);
      CASE_READER(IMetadata);
      CASE_READER(Memory);
#undef CASE_READER
      default:
        CAM_LOGW("[unflatten] data type %u is unsupported. tag:0x%x", info.type,
                 info.tag);
        ret = BAD_VALUE;
        continue;
    }

    if (ret < 0) {
      return ret;
    }

    mMap[info.tag] = entry;
  }

  gettimeofday(&t, NULL);
  MINT64 end_time = (t.tv_sec) * 1000000000LL + (t.tv_usec) * 1000LL;
  CAM_LOGD("offset:%zu time:%" PRIu64 "us", handle.offset,
           (end_time - start_time) / 1000);

  return handle.offset;
}

/******************************************************************************
 *
 ******************************************************************************/
IMetadata::IMetadata() : mpImp(new Implementor()) {}

IMetadata::IMetadata(IMetadata const& other) {
  std::lock_guard<std::mutex> lk1(other.mMetadataLock);
  mpImp = new Implementor(*(other.mpImp));
}

IMetadata::~IMetadata() {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  if (mpImp) {
    delete mpImp;
    mpImp = nullptr;
  }
}

IMetadata& IMetadata::operator=(IMetadata const& other) {
  if (this != &other) {
    std::lock(mMetadataLock, other.mMetadataLock);
    std::lock_guard<std::mutex> lk1(mMetadataLock, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(other.mMetadataLock, std::adopt_lock);
    //
    delete mpImp;
    mpImp = new Implementor(*(other.mpImp));
  } else {
    CAM_LOGW("this(%p) == other(%p)", this, &other);
  }

  return *this;
}

IMetadata& IMetadata::operator+=(IMetadata const& other) {
  std::lock(mMetadataLock, other.mMetadataLock);
  std::lock_guard<std::mutex> lk1(mMetadataLock, std::adopt_lock);
  std::lock_guard<std::mutex> lk2(other.mMetadataLock, std::adopt_lock);
  //
  *mpImp += *other.mpImp;
  return *this;
}

IMetadata const IMetadata::operator+(IMetadata const& other) {
  return IMetadata(*this) += other;
}

MBOOL
IMetadata::isEmpty() const {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->isEmpty();
}

MUINT
IMetadata::count() const {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->count();
}

IMetadata IMetadata::add(IMetadata const& other) const {
  class IMetadata_Wrapper : public IMetadata {
   public:
    inline Implementor* getImplementor() { return mpImp; }
  };

  if (this != &other) {
    IMetadata_Wrapper curr;
    std::lock(mMetadataLock, other.mMetadataLock);
    std::lock_guard<std::mutex> lk1(mMetadataLock, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(other.mMetadataLock, std::adopt_lock);
    *(curr.getImplementor()) = mpImp->add(*other.mpImp);
    return *dynamic_cast<IMetadata*>(&curr);
  }

  CAM_LOGW("this(%p) == other(%p)", this, &other);
  return *this;
}

MVOID
IMetadata::append(IMetadata const& other) {
  if (this != &other) {
    std::lock(mMetadataLock, other.mMetadataLock);
    std::lock_guard<std::mutex> lk1(mMetadataLock, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(other.mMetadataLock, std::adopt_lock);
    mpImp->append(*(other.mpImp));
  } else {
    CAM_LOGW("this(%p) == other(%p)", this, &other);
  }
}

MVOID
IMetadata::clear() {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  mpImp->clear();
}

MERROR
IMetadata::remove(Tag_t tag) {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->remove(tag) >= 0 ? OK : BAD_VALUE;
}

MERROR
IMetadata::sort() {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->sort();
}

MERROR
IMetadata::update(Tag_t tag, IMetadata::IEntry const& entry) {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  MERROR ret = mpImp->update(
      tag, entry);  // keyedVector has two possibilities: BAD_VALUE/NO_MEMORY
  return ret >= 0 ? (MERROR)OK : (MERROR)ret;
}

IMetadata::IEntry IMetadata::entryFor(Tag_t tag) const {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->entryFor(tag);
}

IMetadata::IEntry IMetadata::entryAt(MUINT index) const {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->entryAt(index);
}

IMetadata::IEntry IMetadata::takeEntryFor(Tag_t tag) {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->takeEntryFor(tag);
}

IMetadata::IEntry IMetadata::takeEntryAt(MUINT index) {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->takeEntryAt(index);
}

ssize_t IMetadata::flatten(void* buf, size_t buf_size) const {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->flatten(static_cast<char*>(buf), buf_size);
}

ssize_t IMetadata::unflatten(void* buf, size_t buf_size) {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  return mpImp->unflatten(static_cast<char*>(buf), buf_size);
}

void IMetadata::dump(int layer) {
  std::lock_guard<std::mutex> lk1(mMetadataLock);
  mpImp->dump(layer);
}
