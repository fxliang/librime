//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2012-02-18 GONG Chen <chen.sst@gmail.com>
//
#include <utility>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <rime/config.h>
#include <rime/deployer.h>
#include <rime/lever/switcher_settings.h>

namespace fs = std::filesystem;

namespace rime {

namespace {

string QualifyWithNamespace(const string& name_space, const string& schema_id) {
  if (name_space.empty() || path(schema_id).has_parent_path()) {
    return schema_id;
  }
  return (path(name_space) / schema_id).generic_u8string();
}

string SchemaIdFromPath(const path& root, const path& schema_path) {
  std::error_code ec;
  path relative = fs::relative(schema_path, root, ec);
  if (ec || relative.empty()) {
    return string();
  }
  auto schema_id = relative.generic_u8string();
  if (boost::ends_with(schema_id, ".schema.yaml")) {
    boost::erase_tail(schema_id, string(".schema.yaml").length());
  }
  return schema_id;
}

bool ShouldSkipSchemaScanDirectory(const path& entry_path) {
  const auto name = entry_path.filename().generic_u8string();
  return boost::ends_with(name, ".userdb");
}

bool IsWithinDirectory(const path& candidate, const path& root) {
  std::error_code ec;
  auto relative = fs::relative(candidate, root, ec);
  if (ec || relative.empty()) {
    return false;
  }
  auto it = relative.begin();
  return it != relative.end() && *it != "..";
}

}  // namespace

SwitcherSettings::SwitcherSettings(Deployer* deployer)
    : CustomSettings(deployer, "default", "Rime::SwitcherSettings") {}

bool SwitcherSettings::Load() {
  auto ret = CustomSettings::Load();
  available_.clear();
  selection_.clear();
  hotkeys_.clear();
  GetAvailableSchemasFromDirectory(deployer_->shared_data_dir);
  GetAvailableSchemasFromDirectory(deployer_->user_data_dir);
  GetSelectedSchemasFromConfig();
  GetHotkeysFromConfig();
  return ret;
}

bool SwitcherSettings::Select(Selection selection) {
  selection_ = std::move(selection);
  auto schema_list = New<ConfigList>();
  for (const string& schema_id : selection_) {
    auto item = New<ConfigMap>();
    item->Set("schema", New<ConfigValue>(schema_id));
    schema_list->Append(item);
  }
  return Customize("schema_list", schema_list);
}

bool SwitcherSettings::SetHotkeys(const string& hotkeys) {
  // TODO: not implemented; validation required
  return false;
}

void SwitcherSettings::GetAvailableSchemasFromDirectory(const path& dir) {
  if (!fs::exists(dir) || !fs::is_directory(dir)) {
    LOG(INFO) << "directory '" << dir << "' does not exist.";
    return;
  }
  vector<path> skip_roots;
  if (deployer_) {
    skip_roots.push_back(deployer_->staging_dir);
    skip_roots.push_back(deployer_->sync_dir);
    skip_roots.push_back(deployer_->user_data_dir / "trash");
    skip_roots.push_back(deployer_->prebuilt_data_dir);
  }
  for (fs::recursive_directory_iterator it(dir), end; it != end; ++it) {
    path file_path(it->path());
    if (fs::is_directory(file_path)) {
      bool skip_this = ShouldSkipSchemaScanDirectory(file_path);
      if (!skip_this) {
        for (const auto& skip_root : skip_roots) {
          if (IsWithinDirectory(file_path, skip_root)) {
            skip_this = true;
            break;
          }
        }
      }
      if (it.depth() >= 1 || skip_this) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!fs::is_regular_file(file_path) ||
        !boost::ends_with(file_path.u8string(), ".schema.yaml")) {
      continue;
    }
    Config config;
    if (config.LoadFromFile(file_path)) {
      SchemaInfo info;
      info.schema_id = SchemaIdFromPath(dir, file_path);
      if (info.schema_id.empty() &&
          !config.GetString("schema/schema_id", &info.schema_id)) {
        continue;
      }
      if (!config.GetString("schema/name", &info.name))
        continue;
      // check for duplicates
      bool duplicated = false;
      for (const SchemaInfo& other : available_) {
        if (other.schema_id == info.schema_id) {
          duplicated = true;
          break;
        }
      }
      if (duplicated)
        continue;
      // details
      config.GetString("schema/version", &info.version);
      if (auto authors = config.GetList("schema/author")) {
        for (size_t i = 0; i < authors->size(); ++i) {
          auto author = authors->GetValueAt(i);
          if (author && !author->str().empty()) {
            if (!info.author.empty())
              info.author += "\n";
            info.author += author->str();
          }
        }
      }
      config.GetString("schema/description", &info.description);
      // output path in native encoding.
      info.file_path = file_path.string();
      available_.push_back(info);
    }
  }
}

void SwitcherSettings::GetSelectedSchemasFromConfig() {
  auto schema_list = config_.GetList("schema_list");
  if (!schema_list) {
    LOG(WARNING) << "schema list not defined.";
    return;
  }
  for (auto it = schema_list->begin(); it != schema_list->end(); ++it) {
    auto item = As<ConfigMap>(*it);
    if (!item)
      continue;
    auto schema_property = item->GetValue("schema");
    if (!schema_property)
      continue;
    string schema_id(schema_property->str());
    if (auto namespace_property = item->GetValue("namespace")) {
      schema_id = QualifyWithNamespace(namespace_property->str(), schema_id);
    }
    selection_.push_back(schema_id);
  }
}

void SwitcherSettings::GetHotkeysFromConfig() {
  auto hotkeys = config_.GetList("switcher/hotkeys");
  if (!hotkeys) {
    LOG(WARNING) << "hotkeys not defined.";
    return;
  }
  for (auto it = hotkeys->begin(); it != hotkeys->end(); ++it) {
    auto item = As<ConfigValue>(*it);
    if (!item)
      continue;
    const string& hotkey(item->str());
    if (hotkey.empty())
      continue;
    if (!hotkeys_.empty())
      hotkeys_ += ", ";
    hotkeys_ += hotkey;
  }
}

}  // namespace rime
