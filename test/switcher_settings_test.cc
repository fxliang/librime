#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <gtest/gtest.h>
#include <rime/deployer.h>
#include <rime/lever/switcher_settings.h>

namespace fs = std::filesystem;
using namespace rime;

namespace {

void WriteSchema(const path& file_path,
                 const char* schema_id,
                 const char* name) {
  fs::create_directories(file_path.parent_path());
  std::ofstream ofs(file_path.string());
  ofs << "schema:\n";
  ofs << "  schema_id: " << schema_id << "\n";
  ofs << "  name: " << name << "\n";
}

bool ContainsSchemaId(const SwitcherSettings::SchemaList& schemas,
                      const string& schema_id) {
  return std::find_if(schemas.begin(), schemas.end(),
                      [&schema_id](const auto& item) {
                        return item.schema_id == schema_id;
                      }) != schemas.end();
}

}  // namespace

TEST(RimeSwitcherSettingsTest, ScanOnlyOneLevelAndSkipGeneratedDirs) {
  auto root = fs::temp_directory_path() / "rime-switcher-settings-scan-test";
  fs::remove_all(root);
  auto shared_data_dir = root / "shared";
  auto user_data_dir = root / "user";

  WriteSchema(shared_data_dir / "base.schema.yaml", "base", "base");
  WriteSchema(user_data_dir / "ns" / "one.schema.yaml", "one", "one");
  WriteSchema(user_data_dir / "ns" / "deep" / "two.schema.yaml", "two", "two");
  WriteSchema(user_data_dir / "build" / "skip.schema.yaml", "skip", "skip");
  WriteSchema(user_data_dir / "sync" / "skip.schema.yaml", "skip", "skip");
  WriteSchema(user_data_dir / "trash" / "skip.schema.yaml", "skip", "skip");
  WriteSchema(user_data_dir / "demo.userdb" / "skip.schema.yaml", "skip",
              "skip");

  Deployer deployer;
  deployer.shared_data_dir = shared_data_dir;
  deployer.user_data_dir = user_data_dir;
  deployer.staging_dir = user_data_dir / "build";
  deployer.sync_dir = user_data_dir / "sync";
  deployer.prebuilt_data_dir = shared_data_dir / "build";
  SwitcherSettings settings(&deployer);
  settings.Load();

  const auto& available = settings.available();
  EXPECT_TRUE(ContainsSchemaId(available, "base"));
  EXPECT_TRUE(ContainsSchemaId(available, "ns/one"));
  EXPECT_FALSE(ContainsSchemaId(available, "ns/deep/two"));
  EXPECT_FALSE(ContainsSchemaId(available, "build/skip"));
  EXPECT_FALSE(ContainsSchemaId(available, "sync/skip"));
  EXPECT_FALSE(ContainsSchemaId(available, "trash/skip"));
  EXPECT_FALSE(ContainsSchemaId(available, "demo.userdb/skip"));

  fs::remove_all(root);
}
