//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <sstream>
#include <fstream>
#include <filesystem>
#include <gtest/gtest.h>
#include <rime/component.h>
#include <rime/config.h>
#include <rime/service.h>

using namespace rime;
namespace fs = std::filesystem;

class RimeConfigCompilerTestBase : public ::testing::Test {
 protected:
  RimeConfigCompilerTestBase() = default;

  virtual string test_config_id() const = 0;

  virtual void SetUp() {
    component_.reset(new ConfigComponent<ConfigBuilder>);
    config_.reset(component_->Create(test_config_id()));
  }

  the<Config::Component> component_;
  the<Config> config_;
};

class RimeConfigCompilerTest : public RimeConfigCompilerTestBase {
 protected:
  string test_config_id() const override { return "config_compiler_test"; }
};

TEST_F(RimeConfigCompilerTest, IncludeLocalReference) {
  for (size_t i = 0; i < 4; ++i) {
    std::ostringstream oss;
    oss << "include_local_reference/@" << i << "/";
    const auto& prefix(oss.str());
    EXPECT_TRUE(config_->IsNull(prefix + "__include"));
    string actual;
    EXPECT_TRUE(config_->GetString(prefix + "terrans/player", &actual));
    EXPECT_EQ("slayers_boxer", actual);
    EXPECT_TRUE(config_->GetString(prefix + "protoss/player", &actual));
    EXPECT_EQ("grrrr", actual);
    EXPECT_TRUE(config_->GetString(prefix + "zerg/player", &actual));
    EXPECT_EQ("yellow", actual);
  }
}

TEST_F(RimeConfigCompilerTest, IncludeExternalReference) {
  const string& prefix = "include_external_reference/";
  EXPECT_TRUE(config_->IsNull(prefix + "terrans/__include"));
  int mineral = 0;
  EXPECT_TRUE(config_->GetInt(prefix + "terrans/tank/cost/mineral", &mineral));
  EXPECT_EQ(150, mineral);
  int gas = 0;
  EXPECT_TRUE(config_->GetInt(prefix + "terrans/tank/cost/gas", &gas));
  EXPECT_EQ(100, gas);
  EXPECT_TRUE(config_->IsNull(prefix + "protoss"));
  EXPECT_TRUE(config_->IsNull(prefix + "zerg"));
}

TEST_F(RimeConfigCompilerTest, IncludeExternalFile) {
  const string& prefix = "include_external_file/";
  EXPECT_TRUE(config_->IsNull(prefix + "__include"));
  EXPECT_TRUE(config_->IsMap(prefix + "terrans"));
  EXPECT_TRUE(config_->IsMap(prefix + "protoss"));
  EXPECT_TRUE(config_->IsMap(prefix + "zerg"));
  int number = 0;
  EXPECT_TRUE(config_->GetInt(prefix + "terrans/supply/produced", &number));
  EXPECT_EQ(28, number);
  EXPECT_TRUE(config_->IsList(prefix + "protoss/air_force"));
  string name;
  EXPECT_TRUE(config_->GetString(prefix + "zerg/queen", &name));
  EXPECT_EQ("Kerrigan", name);
}

TEST_F(RimeConfigCompilerTest, PatchReference) {
  const string& prefix = "patch_reference/";
  EXPECT_TRUE(config_->IsNull(prefix + "__patch"));
  EXPECT_EQ(4, config_->GetListSize(prefix + "battlefields"));
  string map;
  EXPECT_TRUE(config_->GetString(prefix + "battlefields/@3", &map));
  EXPECT_EQ("match point", map);
}

TEST_F(RimeConfigCompilerTest, PatchLiteral) {
  const string& prefix = "patch_literal/";
  EXPECT_TRUE(config_->IsNull(prefix + "__patch"));
  EXPECT_EQ(6, config_->GetListSize(prefix + "zerg/ground_units"));
  string unit;
  EXPECT_TRUE(config_->GetString(prefix + "zerg/ground_units/@5", &unit));
  EXPECT_EQ("lurker", unit);
  // assert that we patched on a copy of the included node
  EXPECT_EQ(5, config_->GetListSize("/starcraft/zerg/ground_units"));
}

TEST_F(RimeConfigCompilerTest, PatchList) {
  const string& prefix = "patch_list/";
  EXPECT_TRUE(config_->IsNull(prefix + "protoss/__patch"));
  EXPECT_EQ(8, config_->GetListSize(prefix + "protoss/ground_units"));
  string unit;
  EXPECT_TRUE(config_->GetString(prefix + "protoss/ground_units/@6", &unit));
  EXPECT_EQ("dark templar", unit);
  EXPECT_TRUE(config_->GetString(prefix + "protoss/ground_units/@7", &unit));
  EXPECT_EQ("dark archon", unit);
  // assert that we patched on a copy of the included node
  EXPECT_EQ(6, config_->GetListSize("/starcraft/protoss/ground_units"));
}

class RimeConfigDependencyTest : public RimeConfigCompilerTestBase {
 protected:
  string test_config_id() const override { return "config_dependency_test"; }
};

TEST_F(RimeConfigDependencyTest, DependencyChaining) {
  const string& prefix = "dependency_chaining/";
  EXPECT_TRUE(config_->IsNull(prefix + "alpha/__include"));
  EXPECT_TRUE(config_->IsNull(prefix + "beta/__include"));
  EXPECT_TRUE(config_->IsNull(prefix + "delta/__include"));
  string value;
  EXPECT_TRUE(config_->GetString(prefix + "alpha", &value));
  EXPECT_EQ("success", value);
  EXPECT_TRUE(config_->GetString(prefix + "beta", &value));
  EXPECT_EQ("success", value);
  EXPECT_TRUE(config_->GetString(prefix + "delta", &value));
  EXPECT_EQ("success", value);
}

// Unit test for https://github.com/rime/librime/issues/141
TEST_F(RimeConfigDependencyTest, DependencyPriorities) {
  const string& prefix = "dependency_priorities/";
  EXPECT_TRUE(config_->IsNull(prefix + "terrans/__include"));
  EXPECT_TRUE(config_->IsNull(prefix + "terrans/__patch"));
  EXPECT_TRUE(config_->IsNull(prefix + "protoss/__include"));
  EXPECT_TRUE(config_->IsNull(prefix + "protoss/__patch"));
  string player;
  EXPECT_TRUE(config_->GetString(prefix + "terrans/player", &player));
  EXPECT_EQ("nada", player);
  EXPECT_TRUE(config_->GetString(prefix + "protoss/player", &player));
  EXPECT_EQ("bisu", player);
}

class RimeConfigOptionalReferenceTest : public RimeConfigCompilerTestBase {
 protected:
  string test_config_id() const override {
    return "config_optional_reference_test";
  }
};

TEST_F(RimeConfigOptionalReferenceTest, OptionalReference) {
  const string& prefix = "/";
  EXPECT_TRUE(config_->IsNull(prefix + "__include"));
  EXPECT_TRUE(config_->IsNull(prefix + "__patch"));
  bool untouched;
  EXPECT_TRUE(config_->GetBool(prefix + "untouched", &untouched));
  EXPECT_TRUE(untouched);
}

class RimeConfigMergeTest : public RimeConfigCompilerTestBase {
 protected:
  string test_config_id() const override { return "config_merge_test"; }
};

TEST_F(RimeConfigMergeTest, AppendWithInclude) {
  const string& prefix = "append_with_include/";
  EXPECT_TRUE(config_->IsNull(prefix + "list/__include"));
  EXPECT_TRUE(config_->IsNull(prefix + "list/__append"));

  EXPECT_TRUE(config_->IsList(prefix + "list"));
  EXPECT_EQ(6 + 2, config_->GetListSize(prefix + "list"));
  string unit;
  EXPECT_TRUE(config_->GetString(prefix + "list/@6", &unit));
  EXPECT_EQ("dark templar", unit);
  EXPECT_TRUE(config_->GetString(prefix + "list/@7", &unit));
  EXPECT_EQ("dark archon", unit);

  // verify that we append to a copy and the original list is untouched
  EXPECT_EQ(6, config_->GetListSize("/starcraft/protoss/ground_units"));
}

TEST_F(RimeConfigMergeTest, AppendWithPatch) {
  const string& prefix = "append_with_patch/";
  EXPECT_TRUE(config_->IsNull(prefix + "__include"));
  EXPECT_TRUE(config_->IsNull(prefix + "__patch"));

  string player;
  EXPECT_TRUE(config_->GetString(prefix + "terrans/player", &player));
  EXPECT_EQ("slayers_boxer, nada", player);

  EXPECT_EQ(2, config_->GetListSize(prefix + "terrans/air_units"));
  string unit;
  EXPECT_TRUE(config_->GetString(prefix + "terrans/air_units/@0", &unit));
  EXPECT_EQ("wraith", unit);
  EXPECT_TRUE(config_->GetString(prefix + "terrans/air_units/@1", &unit));
  EXPECT_EQ("battlecruiser", unit);

  EXPECT_EQ(6 + 2, config_->GetListSize(prefix + "protoss/ground_units"));

  // verify that we append to a copy and the original list is untouched
  EXPECT_TRUE(config_->GetString("/starcraft/terrans/player", &player));
  EXPECT_EQ("slayers_boxer", player);
  EXPECT_TRUE(config_->IsNull("/starcraft/terrans/air_units"));
  EXPECT_EQ(6, config_->GetListSize("/starcraft/protoss/ground_units"));
}

TEST_F(RimeConfigMergeTest, MergeTree) {
  const string& prefix = "merge_tree/";
  EXPECT_TRUE(config_->IsNull(prefix + "__include"));

  string player;
  EXPECT_TRUE(config_->GetString(prefix + "terrans/player", &player));
  EXPECT_EQ("slayers_boxer", player);
  EXPECT_TRUE(config_->IsNull(prefix + "terrans/air_units"));
  EXPECT_TRUE(config_->IsList(prefix + "terrans/ground_units"));
  EXPECT_EQ(5 + 2, config_->GetListSize(prefix + "terrans/ground_units"));
  string unit;
  EXPECT_TRUE(config_->GetString(prefix + "terrans/ground_units/@5", &unit));
  EXPECT_EQ("medic", unit);
  EXPECT_TRUE(config_->GetString(prefix + "terrans/ground_units/@6", &unit));
  EXPECT_EQ("goliath", unit);

  EXPECT_TRUE(config_->GetString(prefix + "protoss/player", &player));
  EXPECT_EQ("grrrr", player);
  EXPECT_TRUE(config_->IsNull(prefix + "protoss/ground_units/__append"));
  EXPECT_TRUE(config_->IsList(prefix + "protoss/ground_units"));
  EXPECT_EQ(6 + 2, config_->GetListSize(prefix + "protoss/ground_units"));
  EXPECT_TRUE(config_->GetString(prefix + "protoss/ground_units/@6", &unit));
  EXPECT_EQ("dark templar", unit);
  EXPECT_TRUE(config_->GetString(prefix + "protoss/ground_units/@7", &unit));
  EXPECT_EQ("dark archon", unit);

  EXPECT_TRUE(config_->GetString(prefix + "zerg/player", &player));
  EXPECT_EQ("yellow", player);
  EXPECT_TRUE(config_->IsList(prefix + "zerg/ground_units"));
  EXPECT_EQ(0, config_->GetListSize(prefix + "zerg/ground_units"));

  // verify that we merge to a copy and the original list is untouched
  EXPECT_TRUE(config_->IsNull("/starcraft/terrans/ground_units"));
  EXPECT_EQ(6, config_->GetListSize("/starcraft/protoss/ground_units"));
  EXPECT_EQ(5, config_->GetListSize("/starcraft/zerg/ground_units"));
}

class RimeConfigCircularDependencyTest : public RimeConfigCompilerTestBase {
 protected:
  string test_config_id() const override {
    return "config_circular_dependency_test";
  }
};

TEST_F(RimeConfigMergeTest, CreateListWithInplacePatch) {
  const string& prefix = "create_list_with_inplace_patch/";
  EXPECT_TRUE(config_->IsList(prefix + "all_ground_units"));
  EXPECT_EQ(16, config_->GetListSize(prefix + "all_ground_units"));
}

TEST_F(RimeConfigMergeTest, AppendToPlusKeyInMap) {
  const string& prefix = "append_to_plus_key_in_map/";
  EXPECT_TRUE(config_->IsNull(prefix + "__include"));
  EXPECT_TRUE(config_->IsNull(prefix + "__patch"));
  EXPECT_TRUE(config_->IsList(prefix + "translator/comment_format"));
  EXPECT_EQ(2, config_->GetListSize(prefix + "translator/comment_format"));
  string item;
  EXPECT_TRUE(
      config_->GetString(prefix + "translator/comment_format/@0", &item));
  EXPECT_EQ("xform/^~//", item);
  EXPECT_TRUE(
      config_->GetString(prefix + "translator/comment_format/@1", &item));
  EXPECT_EQ("xlit|abc|abc|", item);
}

TEST_F(RimeConfigCircularDependencyTest, BestEffortResolution) {
  const string& prefix = "test/";
  EXPECT_TRUE(config_->IsNull(prefix + "__patch"));
  EXPECT_TRUE(config_->IsNull(prefix + "work/__include"));
  string home;
  EXPECT_TRUE(config_->GetString(prefix + "home", &home));
  EXPECT_EQ("naive", home);
  string work;
  EXPECT_TRUE(config_->GetString(prefix + "work", &work));
  EXPECT_EQ("excited", work);
}

namespace {

struct ScopedDataDirs {
  ScopedDataDirs(const path& user_data_dir, const path& shared_data_dir) {
    auto& deployer = Service::instance().deployer();
    old_user_data_dir = deployer.user_data_dir;
    old_shared_data_dir = deployer.shared_data_dir;
    deployer.user_data_dir = user_data_dir;
    deployer.shared_data_dir = shared_data_dir;
  }

  ~ScopedDataDirs() {
    auto& deployer = Service::instance().deployer();
    deployer.user_data_dir = old_user_data_dir;
    deployer.shared_data_dir = old_shared_data_dir;
  }

  path old_user_data_dir;
  path old_shared_data_dir;
};

void WriteConfigFile(const path& file_path, const string& content) {
  fs::create_directories(file_path.parent_path());
  std::ofstream ofs(file_path.string());
  ofs << content;
}

path CreateTempTestRoot() {
  auto root = fs::temp_directory_path() / "rime-config-include-ns-test";
  fs::remove_all(root);
  fs::create_directories(root);
  return root;
}

}  // namespace

TEST(RimeConfigNamespaceIncludeTest, PreferNamespacedResource) {
  auto root = CreateTempTestRoot();
  auto user_data_dir = root / "user";
  auto shared_data_dir = root / "shared";
  fs::create_directories(user_data_dir);
  fs::create_directories(shared_data_dir);
  WriteConfigFile(user_data_dir / "ns/main.yaml",
                  "test:\n  __include: base:/\n");
  WriteConfigFile(user_data_dir / "ns/base.yaml", "value: 2\n");
  WriteConfigFile(user_data_dir / "base.yaml", "value: 1\n");

  ScopedDataDirs scoped(user_data_dir, shared_data_dir);
  the<Config::Component> component(new ConfigComponent<ConfigBuilder>);
  the<Config> config(component->Create("ns/main"));
  ASSERT_TRUE(config);

  int value = 0;
  EXPECT_TRUE(config->GetInt("test/value", &value));
  EXPECT_EQ(2, value);
  fs::remove_all(root);
}

TEST(RimeConfigNamespaceIncludeTest, FallbackToDefaultNamespaceResource) {
  auto root = CreateTempTestRoot();
  auto user_data_dir = root / "user";
  auto shared_data_dir = root / "shared";
  fs::create_directories(user_data_dir);
  fs::create_directories(shared_data_dir);
  WriteConfigFile(user_data_dir / "ns/main.yaml",
                  "test:\n  __include: base:/\n");
  WriteConfigFile(user_data_dir / "base.yaml", "value: 1\n");

  ScopedDataDirs scoped(user_data_dir, shared_data_dir);
  the<Config::Component> component(new ConfigComponent<ConfigBuilder>);
  the<Config> config(component->Create("ns/main"));
  ASSERT_TRUE(config);

  int value = 0;
  EXPECT_TRUE(config->GetInt("test/value", &value));
  EXPECT_EQ(1, value);
  fs::remove_all(root);
}

TEST(RimeConfigNamespaceIncludeTest, DisableFallbackWithUseNamespaceResources) {
  auto root = CreateTempTestRoot();
  auto user_data_dir = root / "user";
  auto shared_data_dir = root / "shared";
  fs::create_directories(user_data_dir);
  fs::create_directories(shared_data_dir);
  WriteConfigFile(user_data_dir / "ns/main.yaml",
                  "schema:\n"
                  "  namespace_resources_only: true\n"
                  "test:\n"
                  "  __include: base:/?\n");
  WriteConfigFile(user_data_dir / "base.yaml", "value: 1\n");

  ScopedDataDirs scoped(user_data_dir, shared_data_dir);
  the<Config::Component> component(new ConfigComponent<ConfigBuilder>);
  the<Config> config(component->Create("ns/main"));
  ASSERT_TRUE(config);
  EXPECT_TRUE(config->IsNull("test/value"));

  fs::remove_all(root);
}
