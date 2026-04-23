#ifndef RIME_NAMESPACE_RESOURCE_UTILS_H_
#define RIME_NAMESPACE_RESOURCE_UTILS_H_

#include <optional>
#include <rime/common.h>
#include <rime/resource.h>

namespace rime {

class Config;

RIME_DLL string SchemaNamespaceFromSchemaId(const string& schema_id);

RIME_DLL string InferSchemaNamespace(const string& schema_id, Config* config);

RIME_DLL bool NamespaceResourcesOnlyFromConfig(Config* config);

RIME_DLL vector<string> BuildSchemaScopedResourceCandidates(
    const string& resource_id,
    const string& schema_id,
    Config* config,
    bool namespace_resources_only,
    bool allow_default_namespace_fallback = true,
    bool allow_parent_path_namespace_candidates = false);

RIME_DLL std::optional<path> ResolveFirstExistingResourcePath(
    const vector<string>& candidate_resource_ids,
    ResourceResolver* primary_resolver,
    ResourceResolver* secondary_resolver = nullptr,
    string* resolved_resource_id = nullptr);

RIME_DLL string
ResolveSchemaScopedResourceId(const string& schema_id,
                              bool namespace_resources_only,
                              const string& resource_id,
                              const ResourceType& resource_type);

}  // namespace rime

#endif  // RIME_NAMESPACE_RESOURCE_UTILS_H_
