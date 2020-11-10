// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/key_file_store.h"

#include <list>
#include <map>
#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/optional.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/scoped_umask.h>
#include <fcntl.h>
#include <re2/re2.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "shill/key_value_store.h"
#include "shill/logging.h"

using std::set;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kStorage;
static string ObjectID(const KeyFileStore* k) {
  return "(key_file_store)";
}
}  // namespace Logging

namespace {

// GLib uses the semicolon for separating lists, but it is configurable,
// so we don't want to hardcode it around this file.
constexpr char kListSeparator = ';';

string Escape(const string& str, base::Optional<char> separator) {
  string out;
  bool leading_space = true;
  for (const char c : str) {
    switch (c) {
      case ' ':
        if (leading_space) {
          out += "\\s";
        } else {
          out += ' ';
        }
        break;
      case '\t':
        if (leading_space) {
          out += "\\t";
        } else {
          out += '\t';
        }
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\\':
        out += "\\\\";
        leading_space = false;
        break;
      default:
        if (separator.has_value() && c == separator.value()) {
          out += "\\";
          out += c;
          leading_space = true;
        } else {
          out += c;
          leading_space = false;
        }
        break;
    }
  }
  return out;
}

bool Unescape(const string& str,
              base::Optional<char> separator,
              vector<string>* out) {
  DCHECK(out);
  out->clear();
  string current;
  bool escaping = false;
  for (const char c : str) {
    if (escaping) {
      switch (c) {
        case 's':
          current += ' ';
          break;
        case 't':
          current += '\t';
          break;
        case 'n':
          current += '\n';
          break;
        case 'r':
          current += '\r';
          break;
        default:
          current += c;
          break;
      }
      escaping = false;
      continue;
    }

    if (c == '\\') {
      escaping = true;
      continue;
    }

    if (separator.has_value() && c == separator.value()) {
      out->push_back(current);
      current.clear();
      continue;
    }

    current += c;
  }

  if (escaping) {
    LOG(ERROR) << "Unterminated escape sequence in \"" << str << "\"";
    return false;
  }
  // If we are parsing a list and the current string is empty, then the last
  // character was either a separator (closing off a list item) or the entire
  // list is empty. In this case, we don't add an element.
  // Otherwise, we are parsing not as as list, in which case |current| holds
  // the whole value, or we've started to parse a value but it is technically
  // unterminated, which glib still accepts. In those cases, we add to the
  // output.
  if (!separator.has_value() || !current.empty()) {
    out->push_back(current);
  }
  return true;
}

using KeyValuePair = std::pair<string, string>;
bool IsBlankComment(const KeyValuePair& kv) {
  return kv.first.empty() && kv.second.empty();
}

class Group {
 public:
  explicit Group(const string& name) : name_(name) {}
  Group(const Group&) = delete;
  Group& operator=(const Group&) = delete;

  void Set(const string& key, const string& value) {
    if (index_.count(key) > 0) {
      index_[key]->second = value;
      return;
    }

    entries_.push_back({key, value});
    index_[key] = &entries_.back();
  }

  base::Optional<string> Get(const string& key) const {
    const auto it = index_.find(key);
    if (it == index_.end()) {
      return base::nullopt;
    }

    return it->second->second;
  }

  bool Delete(const string& key) {
    const auto it = index_.find(key);
    if (it == index_.end()) {
      return false;
    }

    KeyValuePair* pair = it->second;
    index_.erase(it);
    base::Erase(entries_, *pair);
    return true;
  }

  // Comment lines are ignored, but they have to be preserved when the file is
  // written back out. Hence, we add them to the entries list but not to the
  // index.
  void AddComment(const string& comment) { entries_.push_back({"", comment}); }

  // Serializes this group to a string, preserving comments.
  string Serialize(bool is_last_group) const {
    string data = base::StringPrintf("[%s]\n", name_.c_str());
    for (const auto& entry : entries_) {
      if (!entry.first.empty()) {
        data += entry.first + "=";
      }
      data += entry.second + "\n";
    }
    // If this is not the last group and there isn't already a blank
    // comment line, glib adds a blank line for readability. Replicate
    // that behavior here.
    if (!is_last_group &&
        (entries_.empty() || !IsBlankComment(entries_.back()))) {
      data += "\n";
    }
    return data;
  }

 private:
  string name_;
  std::list<KeyValuePair> entries_;
  std::map<string, KeyValuePair*> index_;
};

}  // namespace

constexpr LazyRE2 group_header_matcher = {
    "\\[([^[:cntrl:]\\]]*)\\][[:space:]]*"};
constexpr LazyRE2 key_value_matcher = {"([^ ]+?) *= *(.*)"};

class KeyFileStore::KeyFile {
 public:
  static std::unique_ptr<KeyFile> Create(const base::FilePath& path) {
    string contents;
    if (!base::ReadFileToString(path, &contents)) {
      return nullptr;
    }

    vector<string> lines = base::SplitString(
        contents, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    // Trim final empty line if present, since ending a file on a newline
    // will cause us to have an extra with base::SPLIT_WANT_ALL.
    if (!lines.empty() && lines.back().empty()) {
      lines.pop_back();
    }

    std::list<string> pre_group_comments;
    std::list<Group> groups;
    std::map<std::string, Group*> index;
    for (const auto& line : lines) {
      // Trim leading spaces.
      auto pos = line.find_first_not_of(' ');
      string trimmed_line;
      if (pos != std::string::npos) {
        trimmed_line = line.substr(pos);
      }

      if (trimmed_line.empty() || trimmed_line[0] == '#') {
        // Comment line.
        if (groups.empty()) {
          pre_group_comments.push_back(line);
        } else {
          groups.back().AddComment(line);
        }
        continue;
      }

      string group_name;
      if (RE2::FullMatch(trimmed_line, *group_header_matcher, &group_name)) {
        // Group header.
        groups.emplace_back(group_name);
        index[group_name] = &groups.back();
        continue;
      }

      string key;
      string value;
      if (RE2::FullMatch(trimmed_line, *key_value_matcher, &key, &value)) {
        // Key-value pair.
        if (groups.empty()) {
          LOG(ERROR) << "Key-value pair found without a group";
          return nullptr;
        }

        groups.back().Set(key, value);
        continue;
      }

      LOG(ERROR) << "Could not parse line: \"" << line << "\"";
      return nullptr;
    }

    return std::unique_ptr<KeyFile>(
        new KeyFile(path, std::move(pre_group_comments), std::move(groups),
                    std::move(index)));
  }

  void Set(const string& group, const string& key, const string& value) {
    if (index_.count(group) == 0) {
      groups_.emplace_back(group);
      index_[group] = &groups_.back();
    }

    index_[group]->Set(key, value);
  }

  base::Optional<string> Get(const string& group, const string& key) const {
    const auto it = index_.find(group);
    if (it == index_.end()) {
      return base::nullopt;
    }

    return it->second->Get(key);
  }

  bool Delete(const string& group, const string& key) {
    const auto it = index_.find(group);
    if (it == index_.end()) {
      return false;
    }

    return it->second->Delete(key);
  }

  bool HasGroup(const string& group) const { return index_.count(group) > 0; }

  bool DeleteGroup(const string& group) {
    const auto it = index_.find(group);
    if (it == index_.end()) {
      return false;
    }

    Group* grp = it->second;
    index_.erase(it);
    base::EraseIf(groups_, [grp](const Group& g) { return &g == grp; });
    return true;
  }

  set<string> GetGroups() const {
    set<string> group_names;
    for (const auto& group : index_) {
      group_names.insert(group.first);
    }
    return group_names;
  }

  void SetHeader(const string& header) {
    vector<string> lines = base::SplitString(
        header, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

    pre_group_comments_.clear();
    for (const string& line : lines) {
      pre_group_comments_.push_back("#" + line);
    }
  }

  bool Flush() const {
    string to_write;
    for (const string& line : pre_group_comments_) {
      to_write += line + '\n';
    }
    for (const Group& group : groups_) {
      to_write += group.Serialize(&group == &groups_.back());
    }

    brillo::ScopedUmask owner_only_umask(~(S_IRUSR | S_IWUSR) & 0777);
    if (!base::ImportantFileWriter::WriteFileAtomically(path_, to_write)) {
      LOG(ERROR) << "Failed to store key file: " << path_.value();
      return false;
    }
    return true;
  }

 private:
  KeyFile(const base::FilePath& path,
          std::list<string> pre_group_comments,
          std::list<Group> groups,
          std::map<std::string, Group*> index)
      : path_(path),
        pre_group_comments_(pre_group_comments),
        groups_(std::move(groups)),
        index_(std::move(index)) {}
  KeyFile(const KeyFile&) = delete;
  KeyFile& operator=(const KeyFile&) = delete;

  base::FilePath path_;
  std::list<string> pre_group_comments_;
  std::list<Group> groups_;
  std::map<string, Group*> index_;
};

const char KeyFileStore::kCorruptSuffix[] = ".corrupted";

KeyFileStore::KeyFileStore(const base::FilePath& path)
    : key_file_(nullptr), path_(path) {
  CHECK(!path_.empty());
}

KeyFileStore::~KeyFileStore() = default;

bool KeyFileStore::IsEmpty() const {
  int64_t file_size = 0;
  return !base::GetFileSize(path_, &file_size) || file_size <= 0;
}

bool KeyFileStore::Open() {
  CHECK(!key_file_);
  if (IsEmpty()) {
    LOG(INFO) << "Creating a new key file at " << path_.value();
    base::File f(path_, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                            base::File::FLAG_WRITE);
  }

  key_file_ = KeyFile::Create(path_);
  if (!key_file_) {
    LOG(ERROR) << "Failed to load key file from " << path_.value();
    return false;
  }

  return true;
}

bool KeyFileStore::Close() {
  bool success = Flush();
  key_file_.reset();
  return success;
}

bool KeyFileStore::Flush() {
  return key_file_->Flush();
}

bool KeyFileStore::MarkAsCorrupted() {
  LOG(INFO) << "In " << __func__ << " for " << path_.value();
  string corrupted_path = path_.value() + kCorruptSuffix;
  int ret = rename(path_.value().c_str(), corrupted_path.c_str());
  if (ret != 0) {
    PLOG(ERROR) << "File rename failed";
    return false;
  }
  return true;
}

set<string> KeyFileStore::GetGroups() const {
  CHECK(key_file_);
  return key_file_->GetGroups();
}

// Returns a set so that caller can easily test whether a particular group
// is contained within this collection.
set<string> KeyFileStore::GetGroupsWithKey(const string& key) const {
  set<string> groups = GetGroups();
  set<string> groups_with_key;
  for (const auto& group : groups) {
    if (key_file_->Get(group, key).has_value()) {
      groups_with_key.insert(group);
    }
  }
  return groups_with_key;
}

set<string> KeyFileStore::GetGroupsWithProperties(
    const KeyValueStore& properties) const {
  set<string> groups = GetGroups();
  set<string> groups_with_properties;
  for (const auto& group : groups) {
    if (DoesGroupMatchProperties(group, properties)) {
      groups_with_properties.insert(group);
    }
  }
  return groups_with_properties;
}

bool KeyFileStore::ContainsGroup(const string& group) const {
  CHECK(key_file_);
  return key_file_->HasGroup(group);
}

bool KeyFileStore::DeleteKey(const string& group, const string& key) {
  CHECK(key_file_);
  return key_file_->Delete(group, key);
}

bool KeyFileStore::DeleteGroup(const string& group) {
  CHECK(key_file_);
  return key_file_->DeleteGroup(group);
}

bool KeyFileStore::SetHeader(const string& header) {
  CHECK(key_file_);
  key_file_->SetHeader(header);
  return true;
}

bool KeyFileStore::GetString(const string& group,
                             const string& key,
                             string* value) const {
  CHECK(key_file_);
  base::Optional<string> data = key_file_->Get(group, key);
  if (!data.has_value()) {
    SLOG(this, 10) << "Failed to lookup (" << group << ":" << key << ")";
    return false;
  }

  vector<string> temp;
  if (!Unescape(data.value(), base::nullopt, &temp)) {
    SLOG(this, 10) << "Failed to parse (" << group << ":" << key << ") as"
                   << " string";
    return false;
  }

  CHECK_EQ(1U, temp.size());
  if (value) {
    *value = temp[0];
  }
  return true;
}

bool KeyFileStore::SetString(const string& group,
                             const string& key,
                             const string& value) {
  CHECK(key_file_);
  key_file_->Set(group, key, Escape(value, base::nullopt));
  return true;
}

bool KeyFileStore::GetBool(const string& group,
                           const string& key,
                           bool* value) const {
  CHECK(key_file_);
  base::Optional<string> data = key_file_->Get(group, key);
  if (!data.has_value()) {
    SLOG(this, 10) << "Failed to lookup (" << group << ":" << key << ")";
    return false;
  }

  bool b;
  if (data.value() == "true") {
    b = true;
  } else if (data.value() == "false") {
    b = false;
  } else {
    SLOG(this, 10) << "Failed to parse (" << group << ":" << key << ") as"
                   << " bool";
    return false;
  }

  if (value) {
    *value = b;
  }
  return true;
}

bool KeyFileStore::SetBool(const string& group, const string& key, bool value) {
  CHECK(key_file_);
  key_file_->Set(group, key, value ? "true" : "false");
  return true;
}

bool KeyFileStore::GetInt(const string& group,
                          const string& key,
                          int* value) const {
  CHECK(key_file_);
  base::Optional<string> data = key_file_->Get(group, key);
  if (!data.has_value()) {
    SLOG(this, 10) << "Failed to lookup (" << group << ":" << key << ")";
    return false;
  }

  int i;
  if (!base::StringToInt(data.value(), &i)) {
    SLOG(this, 10) << "Failed to parse (" << group << ":" << key << ") as"
                   << " int";
    return false;
  }

  if (value) {
    *value = i;
  }
  return true;
}

bool KeyFileStore::SetInt(const string& group, const string& key, int value) {
  CHECK(key_file_);
  key_file_->Set(group, key, base::NumberToString(value));
  return true;
}

bool KeyFileStore::GetUint64(const string& group,
                             const string& key,
                             uint64_t* value) const {
  CHECK(key_file_);
  base::Optional<string> data = key_file_->Get(group, key);
  if (!data.has_value()) {
    SLOG(this, 10) << "Failed to lookup (" << group << ":" << key << ")";
    return false;
  }

  uint64_t i;
  if (!base::StringToUint64(data.value(), &i)) {
    SLOG(this, 10) << "Failed to parse (" << group << ":" << key << "): "
                   << " as uint64";
    return false;
  }

  if (value) {
    *value = i;
  }
  return true;
}

bool KeyFileStore::SetUint64(const string& group,
                             const string& key,
                             uint64_t value) {
  CHECK(key_file_);
  key_file_->Set(group, key, base::NumberToString(value));
  return true;
}

bool KeyFileStore::GetStringList(const string& group,
                                 const string& key,
                                 vector<string>* value) const {
  CHECK(key_file_);
  base::Optional<string> data = key_file_->Get(group, key);
  if (!data.has_value()) {
    SLOG(this, 10) << "Failed to lookup (" << group << ":" << key << ")";
    return false;
  }

  vector<string> list;
  if (!Unescape(data.value(), kListSeparator, &list)) {
    SLOG(this, 10) << "Failed to parse (" << group << ":" << key << "): "
                   << " as string list";
    return false;
  }

  if (value) {
    *value = list;
  }
  return true;
}

bool KeyFileStore::SetStringList(const string& group,
                                 const string& key,
                                 const vector<string>& value) {
  CHECK(key_file_);
  vector<string> escaped_strings;
  // glib appends a separator to every element of the list.
  for (const auto& string_entry : value) {
    escaped_strings.push_back(Escape(string_entry, kListSeparator) +
                              kListSeparator);
  }
  key_file_->Set(group, key, base::JoinString(escaped_strings, string()));
  return true;
}

bool KeyFileStore::GetCryptedString(const string& group,
                                    const string& deprecated_key,
                                    const string& plaintext_key,
                                    string* value) const {
  if (GetString(group, plaintext_key, value)) {
    return true;
  }

  if (!GetString(group, deprecated_key, value)) {
    return false;
  }
  if (value) {
    auto plaintext = Crypto::Decrypt(*value);
    if (!plaintext.has_value()) {
      return false;
    }
    *value = std::move(plaintext).value();
  }
  return true;
}

bool KeyFileStore::SetCryptedString(const string& group,
                                    const string& deprecated_key,
                                    const string& plaintext_key,
                                    const string& value) {
  SetString(group, deprecated_key, Crypto::Encrypt(value));
  return SetString(group, plaintext_key, value);
}

bool KeyFileStore::DoesGroupMatchProperties(
    const string& group, const KeyValueStore& properties) const {
  for (const auto& property : properties.properties()) {
    if (property.second.IsTypeCompatible<bool>()) {
      bool value;
      if (!GetBool(group, property.first, &value) ||
          value != property.second.Get<bool>()) {
        return false;
      }
    } else if (property.second.IsTypeCompatible<int32_t>()) {
      int value;
      if (!GetInt(group, property.first, &value) ||
          value != property.second.Get<int32_t>()) {
        return false;
      }
    } else if (property.second.IsTypeCompatible<string>()) {
      string value;
      if (!GetString(group, property.first, &value) ||
          value != property.second.Get<string>()) {
        return false;
      }
    }
  }
  return true;
}

std::unique_ptr<StoreInterface> CreateStore(const base::FilePath& path) {
  return std::make_unique<KeyFileStore>(path);
}

}  // namespace shill
