//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2012-01-05 GONG Chen <chen.sst@gmail.com>
// 2014-07-06 GONG Chen <chen.sst@gmail.com> redesigned binary file format.
//
#include <cfloat>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <rime/config.h>
#include <rime/resource.h>
#include <rime/schema.h>
#include <rime/service.h>
#include <rime/ticket.h>
#include <rime/dict/db_pool_impl.h>
#include <rime/dict/dict_settings.h>
#include <rime/dict/reverse_lookup_dictionary.h>

namespace rime {

const char kReverseFormat[] = "Rime::Reverse/3.1";
const double kReverseFormatCompatible = 3.0;

const char kReverseFormatPrefix[] = "Rime::Reverse/";
const size_t kReverseFormatPrefixLen = sizeof(kReverseFormatPrefix) - 1;

static const char* kStemKeySuffix = "\x1fstem";

ReverseDb::ReverseDb(const path& file_path) : MappedFile(file_path) {}

bool ReverseDb::Load() {
  LOG(INFO) << "loading reversedb: " << file_path();

  if (IsOpen())
    Close();

  if (!OpenReadOnly()) {
    LOG(ERROR) << "Error opening reversedb '" << file_path() << "'.";
    return false;
  }

  metadata_ = Find<reverse::Metadata>(0);
  if (!metadata_) {
    LOG(ERROR) << "metadata not found.";
    Close();
    return false;
  }
  if (strncmp(metadata_->format, kReverseFormatPrefix,
              kReverseFormatPrefixLen)) {
    LOG(ERROR) << "invalid metadata.";
    Close();
    return false;
  }
  double format = std::atof(&metadata_->format[kReverseFormatPrefixLen]);
  if (format - kReverseFormatCompatible < 0.0 - DBL_EPSILON ||
      format - kReverseFormatCompatible > 1.0 + DBL_EPSILON) {
    LOG(ERROR) << "incompatible reversedb format.";
    Close();
    return false;
  }

  key_trie_.reset(
      new StringTable(metadata_->key_trie.get(), metadata_->key_trie_size));
  value_trie_.reset(
      new StringTable(metadata_->value_trie.get(), metadata_->value_trie_size));

  return true;
}

bool ReverseDb::Lookup(const string& text, string* result) {
  if (!key_trie_ || !value_trie_ || !metadata_->index.size) {
    return false;
  }
  StringId key_id = key_trie_->Lookup(text);
  if (key_id == kInvalidStringId) {
    return false;
  }
  StringId value_id = metadata_->index.at[key_id];
  *result = value_trie_->GetString(value_id);
  return !result->empty();
}

bool ReverseDb::Build(DictSettings* settings,
                      const Syllabary& syllabary,
                      const Vocabulary& vocabulary,
                      const ReverseLookupTable& stems,
                      uint32_t dict_file_checksum) {
  LOG(INFO) << "building reversedb...";
  ReverseLookupTable rev_table;
  int syllable_id = 0;
  for (const string& syllable : syllabary) {
    auto it = vocabulary.find(syllable_id++);
    if (it == vocabulary.end())
      continue;
    const auto& entries(it->second.entries);
    for (const auto& e : entries) {
      rev_table[e->text].insert(syllable);
    }
  }
  StringTableBuilder key_trie_builder;
  StringTableBuilder value_trie_builder;
  size_t entry_count = rev_table.size() + stems.size();
  vector<StringId> key_ids(entry_count);
  vector<StringId> value_ids(entry_count);
  int i = 0;
  // save reverse lookup entries
  for (const auto& v : rev_table) {
    const string& key(v.first);
    string value(boost::algorithm::join(v.second, " "));
    key_trie_builder.Add(key, 0.0, &key_ids[i]);
    value_trie_builder.Add(value, 0.0, &value_ids[i]);
    ++i;
  }
  // save stems
  for (const auto& v : stems) {
    string key(v.first + kStemKeySuffix);
    string value(boost::algorithm::join(v.second, " "));
    key_trie_builder.Add(key, 0.0, &key_ids[i]);
    value_trie_builder.Add(value, 0.0, &value_ids[i]);
    ++i;
  }
  key_trie_builder.Build();
  value_trie_builder.Build();

  // dict settings required by UniTE
  string dict_settings;
  if (settings && settings->use_rule_based_encoder()) {
    std::ostringstream yaml;
    settings->SaveToStream(yaml);
    dict_settings = yaml.str();
  }

  // creating reversedb file
  const size_t kReservedSize = 1024;
  size_t key_trie_image_size = key_trie_builder.BinarySize();
  size_t value_trie_image_size = value_trie_builder.BinarySize();
  size_t estimated_data_size = kReservedSize + dict_settings.length() +
                               entry_count * sizeof(StringId) +
                               key_trie_image_size + value_trie_image_size;
  if (!Create(estimated_data_size)) {
    LOG(ERROR) << "Error creating prism file '" << file_path() << "'.";
    return false;
  }

  // create metadata
  metadata_ = Allocate<reverse::Metadata>();
  if (!metadata_) {
    LOG(ERROR) << "Error creating metadata in file '" << file_path() << "'.";
    return false;
  }
  metadata_->dict_file_checksum = dict_file_checksum;
  if (!dict_settings.empty()) {
    if (!CopyString(dict_settings, &metadata_->dict_settings)) {
      LOG(ERROR) << "Error saving dict settings.";
      return false;
    }
  }

  auto entries = Allocate<StringId>(entry_count);
  if (!entries) {
    return false;
  }
  for (size_t i = 0; i < entry_count; ++i) {
    entries[key_ids[i]] = value_ids[i];
  }
  metadata_->index.size = entry_count;
  metadata_->index.at = entries;

  // save key trie image
  char* key_trie_image = Allocate<char>(key_trie_image_size);
  if (!key_trie_image) {
    LOG(ERROR) << "Error creating key trie image.";
    return false;
  }
  key_trie_builder.Dump(key_trie_image, key_trie_image_size);
  metadata_->key_trie = key_trie_image;
  metadata_->key_trie_size = key_trie_image_size;

  // save value trie image
  char* value_trie_image = Allocate<char>(value_trie_image_size);
  if (!value_trie_image) {
    LOG(ERROR) << "Error creating value trie image.";
    return false;
  }
  value_trie_builder.Dump(value_trie_image, value_trie_image_size);
  metadata_->value_trie = value_trie_image;
  metadata_->value_trie_size = value_trie_image_size;

  // at last, complete the metadata
  std::strncpy(metadata_->format, kReverseFormat,
               reverse::Metadata::kFormatMaxLength);
  return true;
}

bool ReverseDb::Save() {
  LOG(INFO) << "saving reverse file: " << file_path();
  return ShrinkToFit();
}

uint32_t ReverseDb::dict_file_checksum() const {
  return metadata_ ? metadata_->dict_file_checksum : 0;
}

ReverseLookupDictionary::ReverseLookupDictionary(an<ReverseDb> db) : db_(db) {}

bool ReverseLookupDictionary::Load() {
  return db_ && (db_->IsOpen() || db_->Load());
}

bool ReverseLookupDictionary::ReverseLookup(const string& text,
                                            string* result) {
  return db_->Lookup(text, result);
}

bool ReverseLookupDictionary::LookupStems(const string& text, string* result) {
  return db_->Lookup(text + kStemKeySuffix, result);
}

an<DictSettings> ReverseLookupDictionary::GetDictSettings() {
  an<DictSettings> settings;
  reverse::Metadata* metadata = db_->metadata();
  if (metadata && !metadata->dict_settings.empty()) {
    string yaml(metadata->dict_settings.c_str());
    std::istringstream iss(yaml);
    settings = New<DictSettings>();
    if (!settings->LoadFromStream(iss)) {
      settings.reset();
    }
  }
  return settings;
}

static const ResourceType kReverseDbResourceType = {"reverse_db", "",
                                                    ".reverse.bin"};

namespace {

bool IsRelativePathUnderRoot(const path& full_path, const path& root_path) {
  if (full_path.empty() || root_path.empty()) {
    return false;
  }
  const auto rel = std::filesystem::absolute(full_path).lexically_relative(
      std::filesystem::absolute(root_path));
  if (rel.empty()) {
    return false;
  }
  const auto rel_text = rel.generic_u8string();
  return rel_text != ".." && rel_text.rfind("../", 0) != 0;
}

path SchemaNamespacePath(const Ticket& ticket, Config* config) {
  auto schema_namespace = path(ticket.schema->schema_id()).parent_path();
  if (!schema_namespace.empty()) {
    return schema_namespace;
  }
  if (!config) {
    return path();
  }
  const auto& config_path = config->file_path();
  auto parent_path = config_path.parent_path();
  if (parent_path.empty()) {
    return path();
  }
  auto& deployer = Service::instance().deployer();
  const vector<path> roots = {deployer.staging_dir, deployer.prebuilt_data_dir,
                              deployer.user_data_dir, deployer.shared_data_dir};
  for (const auto& root : roots) {
    if (!IsRelativePathUnderRoot(config_path, root)) {
      continue;
    }
    auto relative_parent =
        std::filesystem::absolute(parent_path)
            .lexically_relative(std::filesystem::absolute(root));
    if (!relative_parent.empty()) {
      auto relative_parent_text = relative_parent.generic_u8string();
      if (relative_parent_text != ".." &&
          relative_parent_text.rfind("../", 0) != 0) {
        return relative_parent;
      }
    }
  }
  return path();
}

string ResolveDictNameWithNamespaceFallback(ResourceResolver* resolver,
                                            const string& dict_name) {
  if (!resolver || dict_name.empty()) {
    return dict_name;
  }
  if (std::filesystem::exists(resolver->ResolvePath(dict_name))) {
    return dict_name;
  }
  auto dict_path = path(dict_name);
  if (dict_path.is_absolute()) {
    return dict_name;
  }

  vector<path> namespace_dirs;
  auto root_path = resolver->root_path();
  if (!root_path.empty()) {
    for (const auto& entry : std::filesystem::directory_iterator(root_path)) {
      if (entry.is_directory()) {
        namespace_dirs.push_back(entry.path().filename());
      }
    }
    std::sort(namespace_dirs.begin(), namespace_dirs.end());
  }

  for (const auto& ns : namespace_dirs) {
    auto prefixed = (ns / dict_path).generic_u8string();
    if (std::filesystem::exists(resolver->ResolvePath(prefixed))) {
      return prefixed;
    }
    if (dict_path.has_parent_path()) {
      auto inserted = (dict_path.parent_path() / ns / dict_path.filename())
                          .generic_u8string();
      if (std::filesystem::exists(resolver->ResolvePath(inserted))) {
        return inserted;
      }
    }
  }
  return dict_name;
}

}  // namespace

ReverseLookupDictionaryComponent::ReverseLookupDictionaryComponent()
    : DbPool(the<ResourceResolver>(
          Service::instance().CreateDeployedResourceResolver(
              kReverseDbResourceType))) {}

ReverseLookupDictionary* ReverseLookupDictionaryComponent::Create(
    const string& dict_name) {
  auto db = GetDb(ResolveDictNameWithNamespaceFallback(resource_resolver_.get(),
                                                       dict_name));
  return new ReverseLookupDictionary(db);
};

ReverseLookupDictionary* ReverseLookupDictionaryComponent::Create(
    const Ticket& ticket) {
  if (!ticket.schema)
    return NULL;
  Config* config = ticket.schema->config();
  string dict_name;
  if (!config->GetString(ticket.name_space + "/dictionary", &dict_name)) {
    // missing!
    return NULL;
  }
  auto schema_namespace = SchemaNamespacePath(ticket, config);
  if (!schema_namespace.empty() && !path(dict_name).has_parent_path()) {
    auto namespaced_dict_name =
        (schema_namespace / dict_name).generic_u8string();
    bool namespace_resources_only = false;
    config->GetBool("schema/namespace_resources_only",
                    &namespace_resources_only);
    if (namespace_resources_only ||
        std::filesystem::exists(
            resource_resolver_->ResolvePath(namespaced_dict_name))) {
      dict_name = std::move(namespaced_dict_name);
    }
  }
  return Create(dict_name);
}

}  // namespace rime
