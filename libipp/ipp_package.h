// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_IPP_PACKAGE_H_
#define LIBIPP_IPP_PACKAGE_H_

#include "ipp_attribute.h"
#include "ipp_enums.h"
#include "ipp_export.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ipp {

class Group;

// This class represents IPP frame. It is container for Groups that represent
// IPP attributes groups (like operation-attributes). Groups in a single Package
// must have unique tags (names).
class IPP_EXPORT Package {
 public:
  // destructor
  virtual ~Package();

  // Returns vector of all groups in the package, all pointers are != nullptr.
  // Returned vector = GetKnownGroups() + vector with unknown groups.
  std::vector<Group*> GetAllGroups();
  std::vector<const Group*> GetAllGroups() const;

  // Returns pointer to the group with given tag.
  // Returns nullptr if there is no group with given tag in the package.
  Group* GetGroup(GroupTag);
  const Group* GetGroup(GroupTag) const;

  // Adds a new group to the package and returns pointer to it or nullptr if
  // a group with given tag already exists in the package.
  Group* AddUnknownGroup(GroupTag, bool is_a_set);

  // Returns reference to payload (e.g. document to print), empty vector means
  // no payload.
  std::vector<uint8_t>& Data() { return data_; }
  const std::vector<uint8_t>& Data() const { return data_; }

 protected:
  Package() = default;

 private:
  // Copy/move/assign constructors/operators are not available.
  Package(const Package&) = delete;
  Package(Package&&) = delete;
  Package& operator=(const Package&) = delete;
  Package& operator=(Package&&) = delete;

  // Returns vector with groups in the schema, all pointers are != nullptr.
  // This method must be overloaded in derived classes with schema.
  virtual std::vector<Group*> GetKnownGroups() { return {}; }
  virtual std::vector<const Group*> GetKnownGroups() const { return {}; }

  std::vector<Group*> unknown_groups_;
  std::vector<uint8_t> data_;
};

// Defined later in this file.
class Collection;

// Base class represents single IPP attribute group or a sequence of the
// same IPP attribute groups. Single instance of IPP attribute groups is
// represented by Collection object.
class IPP_EXPORT Group {
 public:
  virtual ~Group() { Resize(0); }

  // Returns tag of the group.
  GroupTag GetName() const { return name_; }

  // Returns true if it is a sequence of IPP groups (Collections) or false
  // if it is a single IPP group (one Collection).
  bool IsASet() const { return is_a_set_; }

  // Returns the current number of elements (IPP groups) in the Group.
  // (IsASet() == false) => always returns 1.
  size_t GetSize() const { return groups_.size(); }

  // Resizes a sequence of IPP groups.
  // Does nothing if (IsASet() == false) and (|new_size| > 0).
  void Resize(size_t new_size) {
    if (!is_a_set_ && new_size > 1)
      return;
    while (new_size < groups_.size()) {
      delete groups_.back();
      groups_.pop_back();
    }
    while (new_size > groups_.size()) {
      groups_.push_back(CreateCollection());
    }
  }

  // Returns a pointer to underlying collection, representing one of the IPP
  // groups. Returns nullptr <=> (index >= GetSize()).
  Collection* GetCollection(size_t index = 0) {
    if (index >= groups_.size())
      return nullptr;
    return groups_[index];
  }
  const Collection* GetCollection(size_t index = 0) const {
    if (index >= groups_.size())
      return nullptr;
    return groups_.at(index);
  }

 protected:
  Group(GroupTag name, bool is_a_set) : name_(name), is_a_set_(is_a_set) {}

 private:
  virtual Collection* CreateCollection() = 0;
  GroupTag name_;
  bool is_a_set_;
  std::vector<Collection*> groups_;
};

// Final class for Groups, represents Group with single IPP attribute group
// defined in the schema. Template parameter TCollection must be a class
// derived from Collection and defines the structure of the group.
template <class TCollection>
class SingleGroup : public Group {
 public:
  explicit SingleGroup(GroupTag name) : Group(name, false) {}
  // Allows to refer to fields in the underlying collection.
  // Creates the collection if it not exists.
  TCollection* operator->() {
    Resize(1);
    return dynamic_cast<TCollection*>(GetCollection());
  }
  // Returns reference to the underlying collection. If the group is empty
  // (GetSize() == 0) returns a reference to an empty static collection.
  const TCollection& Get() const {
    const Collection* coll = GetCollection();
    if (coll == nullptr)
      return TCollection::empty;
    return *(dynamic_cast<const TCollection*>(coll));
  }

 private:
  Collection* CreateCollection() override { return new TCollection; }
};

// Final class for Group, represents sequence of IPP attribute groups with
// the same tag and defined in the schema. Template parameter TCollection is
// a class derived from Collection and defines the structure of a single group.
template <class TCollection>
class SetOfGroups : public Group {
 public:
  explicit SetOfGroups(GroupTag name) : Group(name, true) {}
  // If |index| is out of range, the vector is resized to (index+1).
  TCollection& operator[](size_t index) {
    if (GetSize() <= index)
      Resize(index + 1);
    return *(dynamic_cast<TCollection*>(GetCollection(index)));
  }
  // Const version of the method above. If |index| is out of range,
  // a reference to an empty static collection is returned.
  const TCollection& At(size_t index) const {
    const Collection* coll = GetCollection(index);
    if (coll == nullptr)
      return TCollection::empty;
    return *(dynamic_cast<const TCollection*>(coll));
  }

 private:
  Collection* CreateCollection() override { return new TCollection; }
};

// Final class for Group, represents Group not defined in the schema.
class UnknownGroup : public Group {
 public:
  UnknownGroup(GroupTag name, bool is_a_set) : Group(name, is_a_set) {}

 private:
  Collection* CreateCollection() override { return new EmptyCollection; }
};

}  // namespace ipp

#endif  //  LIBIPP_IPP_PACKAGE_H_
