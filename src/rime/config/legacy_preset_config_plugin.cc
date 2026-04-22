//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <boost/algorithm/string.hpp>
#include <rime/config/config_compiler_impl.h>
#include <rime/config/config_cow_ref.h>
#include <rime/config/plugins.h>

namespace rime {

namespace {

bool NamespaceResourcesOnly(an<ConfigResource> resource) {
  if (!resource || !resource->data) {
    return false;
  }
  bool namespace_resources_only = false;
  auto value = As<ConfigValue>(
      resource->data->Traverse("schema/namespace_resources_only"));
  return value && value->GetBool(&namespace_resources_only) &&
         namespace_resources_only;
}

vector<string> PresetResourceCandidates(an<ConfigResource> resource,
                                        const string& preset_config_id) {
  vector<string> candidates;
  if (preset_config_id.empty()) {
    return candidates;
  }
  auto source_namespace = path(resource->resource_id).parent_path();
  if (source_namespace.empty() || path(preset_config_id).has_parent_path()) {
    candidates.push_back(preset_config_id);
    return candidates;
  }
  candidates.push_back(
      (source_namespace / preset_config_id).generic_u8string());
  if (!NamespaceResourcesOnly(resource)) {
    candidates.push_back(preset_config_id);
  }
  return candidates;
}

bool IncludePreset(ConfigCompiler* compiler,
                   an<ConfigResource> resource,
                   const string& preset_config_id,
                   const string& local_path,
                   an<ConfigItemRef> target) {
  for (const auto& resource_id :
       PresetResourceCandidates(resource, preset_config_id)) {
    Reference reference{resource_id, local_path, false};
    if (IncludeReference{reference}.TargetedAt(target).Resolve(compiler)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool LegacyPresetConfigPlugin::ReviewCompileOutput(
    ConfigCompiler* compiler,
    an<ConfigResource> resource) {
  return true;
}

bool LegacyPresetConfigPlugin::ReviewLinkOutput(ConfigCompiler* compiler,
                                                an<ConfigResource> resource) {
  if (!boost::ends_with(resource->resource_id, ".schema"))
    return true;
  if (auto preset = resource->data->Traverse("key_binder/import_preset")) {
    if (!Is<ConfigValue>(preset))
      return false;
    auto preset_config_id = As<ConfigValue>(preset)->str();
    LOG(INFO) << "interpreting key_binder/import_preset: " << preset_config_id;
    auto target = Cow(resource, "key_binder");
    auto map = As<ConfigMap>(**target);
    if (map && map->HasKey("bindings")) {
      // append to included list `key_binder/bindings/+` instead of overwriting
      auto appended = map->Get("bindings");
      *Cow(target, "bindings/+") = appended;
      // `*target` is already referencing a copied map, safe to edit directly
      (*target)["bindings"] = nullptr;
    }
    if (!IncludePreset(compiler, resource, preset_config_id, "key_binder",
                       target)) {
      LOG(ERROR) << "failed to include section " << preset_config_id
                 << ":/key_binder";
      return false;
    }
  }
  // NOTE: in the following cases, Cow() is not strictly necessary because
  // we know for sure that no other ConfigResource is going to reference the
  // root map node that will be modified. But other than the root node of the
  // resource being linked, it's possible a map or list has multiple references
  // in the node tree, therefore Cow() is recommended to make sure the
  // modifications only happen to one place.
  if (auto preset = resource->data->Traverse("punctuator/import_preset")) {
    if (!Is<ConfigValue>(preset))
      return false;
    auto preset_config_id = As<ConfigValue>(preset)->str();
    LOG(INFO) << "interpreting punctuator/import_preset: " << preset_config_id;
    if (!IncludePreset(compiler, resource, preset_config_id, "punctuator",
                       Cow(resource, "punctuator"))) {
      LOG(ERROR) << "failed to include section " << preset_config_id
                 << ":/punctuator";
      return false;
    }
  }
  if (auto preset = resource->data->Traverse("recognizer/import_preset")) {
    if (!Is<ConfigValue>(preset))
      return false;
    auto preset_config_id = As<ConfigValue>(preset)->str();
    LOG(INFO) << "interpreting recognizer/import_preset: " << preset_config_id;
    if (!IncludePreset(compiler, resource, preset_config_id, "recognizer",
                       Cow(resource, "recognizer"))) {
      LOG(ERROR) << "failed to include section " << preset_config_id
                 << ":/recognizer";
      return false;
    }
  }
  return true;
}

}  // namespace rime
