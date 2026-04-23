#include <algorithm>
#include <filesystem>
#include <rime/namespace_resource_utils.h>
#include <rime/config.h>
#include <rime/service.h>

namespace rime {

namespace {

bool IsRelativeResourcePath(const path& p) {
  return p.is_relative() && !p.empty() && *p.begin() != "..";
}

void AppendUnique(vector<string>* candidates, const string& candidate) {
  if (candidate.empty()) {
    return;
  }
  if (find(candidates->begin(), candidates->end(), candidate) ==
      candidates->end()) {
    candidates->push_back(candidate);
  }
}

}  // namespace

string SchemaNamespaceFromSchemaId(const string& schema_id) {
  auto ns = path(schema_id).parent_path();
  return ns.empty() ? string() : ns.generic_u8string();
}

string InferSchemaNamespace(const string& schema_id, Config* config) {
  string schema_namespace = SchemaNamespaceFromSchemaId(schema_id);
  if (!schema_namespace.empty()) {
    return schema_namespace;
  }
  if (!config) {
    return {};
  }
  const auto& config_path = config->file_path();
  if (config_path.empty()) {
    return {};
  }
  const auto parent = config_path.parent_path();
  if (parent.empty()) {
    return {};
  }
  return parent.filename().generic_u8string();
}

bool NamespaceResourcesOnlyFromConfig(Config* config) {
  if (!config) {
    return false;
  }
  bool namespace_resources_only = false;
  config->GetBool("schema/namespace_resources_only", &namespace_resources_only);
  return namespace_resources_only;
}

vector<string> BuildSchemaScopedResourceCandidates(
    const string& resource_id,
    const string& schema_id,
    Config* config,
    bool namespace_resources_only,
    bool allow_default_namespace_fallback,
    bool allow_parent_path_namespace_candidates) {
  vector<string> candidates;
  if (resource_id.empty()) {
    return candidates;
  }

  const path resource_path(resource_id);
  const string schema_namespace = InferSchemaNamespace(schema_id, config);
  const bool has_schema_namespace = !schema_namespace.empty();

  if (!IsRelativeResourcePath(resource_path)) {
    AppendUnique(&candidates, resource_id);
    return candidates;
  }

  const bool has_parent = resource_path.has_parent_path();
  if (has_parent) {
    AppendUnique(&candidates, resource_path.generic_u8string());
  }

  if (has_schema_namespace) {
    AppendUnique(&candidates,
                 (path(schema_namespace) / resource_path).generic_u8string());
    if (allow_parent_path_namespace_candidates && has_parent) {
      const auto inserted = resource_path.parent_path() / schema_namespace /
                            resource_path.filename();
      AppendUnique(&candidates, inserted.generic_u8string());
    }
  }

  if (!has_parent &&
      (!has_schema_namespace ||
       (!namespace_resources_only && allow_default_namespace_fallback))) {
    AppendUnique(&candidates, resource_path.generic_u8string());
  }

  if (candidates.empty()) {
    AppendUnique(&candidates, resource_id);
  }
  return candidates;
}

std::optional<path> ResolveFirstExistingResourcePath(
    const vector<string>& candidate_resource_ids,
    ResourceResolver* primary_resolver,
    ResourceResolver* secondary_resolver,
    string* resolved_resource_id) {
  auto resolve_from = [&](ResourceResolver* resolver,
                          const string& candidate) -> std::optional<path> {
    if (!resolver) {
      return {};
    }
    const auto resolved = resolver->ResolvePath(candidate);
    if (std::filesystem::exists(resolved)) {
      return resolved;
    }
    return {};
  };

  for (const auto& candidate : candidate_resource_ids) {
    if (auto resolved = resolve_from(primary_resolver, candidate)) {
      if (resolved_resource_id) {
        *resolved_resource_id = candidate;
      }
      return resolved;
    }
    if (auto resolved = resolve_from(secondary_resolver, candidate)) {
      if (resolved_resource_id) {
        *resolved_resource_id = candidate;
      }
      return resolved;
    }
  }

  return {};
}

string ResolveSchemaScopedResourceId(const string& schema_id,
                                     bool namespace_resources_only,
                                     const string& resource_id,
                                     const ResourceType& resource_type) {
  the<ResourceResolver> resolver(
      Service::instance().CreateResourceResolver(resource_type));
  const auto candidates = BuildSchemaScopedResourceCandidates(
      resource_id, schema_id, nullptr, namespace_resources_only,
      /*allow_default_namespace_fallback=*/true,
      /*allow_parent_path_namespace_candidates=*/false);
  string resolved_resource_id;
  if (ResolveFirstExistingResourcePath(candidates, resolver.get(), nullptr,
                                       &resolved_resource_id)) {
    return resolved_resource_id;
  }
  return candidates.empty() ? resource_id : candidates.front();
}

}  // namespace rime
