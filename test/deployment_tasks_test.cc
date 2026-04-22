#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rime/deployer.h>
#include <rime/lever/deployment_tasks.h>

namespace fs = std::filesystem;
using namespace rime;

namespace {

void WriteTextFile(const path& file_path, const char* content) {
  fs::create_directories(file_path.parent_path());
  std::ofstream ofs(file_path.string());
  ofs << content;
}

}  // namespace

TEST(RimeDeploymentTasksTest, BackupConfigFilesIncludeNamespaceSubdirs) {
  auto root = fs::temp_directory_path() / "rime-backup-config-ns-test";
  fs::remove_all(root);
  auto user_data_dir = root / "user";
  auto sync_dir = root / "sync";
  fs::create_directories(user_data_dir);
  fs::create_directories(sync_dir);

  WriteTextFile(user_data_dir / "default.yaml", "schema_list: []\n");
  WriteTextFile(user_data_dir / "ns" / "wubi091.custom.yaml", "patch: {}\n");
  WriteTextFile(user_data_dir / "ns" / "essay.txt", "test\n");
  WriteTextFile(user_data_dir / "sync" / "peer" / "default.yaml", "skip\n");
  WriteTextFile(user_data_dir / "build" / "foo.txt", "skip\n");
  WriteTextFile(user_data_dir / "trash" / "bar.yaml", "skip\n");

  Deployer deployer;
  deployer.user_data_dir = user_data_dir;
  deployer.sync_dir = user_data_dir / "sync";
  deployer.staging_dir = user_data_dir / "build";
  deployer.user_id = "u1";
  deployer.backup_config_files = true;

  BackupConfigFiles task;
  ASSERT_TRUE(task.Run(&deployer));

  auto backup_dir = deployer.user_data_sync_dir();
  EXPECT_TRUE(fs::exists(backup_dir / "default.yaml"));
  EXPECT_TRUE(fs::exists(backup_dir / "ns" / "wubi091.custom.yaml"));
  EXPECT_TRUE(fs::exists(backup_dir / "ns" / "essay.txt"));
  EXPECT_FALSE(fs::exists(backup_dir / "sync" / "peer" / "default.yaml"));
  EXPECT_FALSE(fs::exists(backup_dir / "build" / "foo.txt"));
  EXPECT_FALSE(fs::exists(backup_dir / "trash" / "bar.yaml"));

  fs::remove_all(root);
}
