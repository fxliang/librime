#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rime/dict/dictionary.h>
#include <rime/service.h>

namespace fs = std::filesystem;
using namespace rime;

namespace {

struct ScopedDeployerPaths {
  explicit ScopedDeployerPaths(const path& staging, const path& prebuilt) {
    auto& deployer = Service::instance().deployer();
    staging_dir = deployer.staging_dir;
    prebuilt_data_dir = deployer.prebuilt_data_dir;
    deployer.staging_dir = staging;
    deployer.prebuilt_data_dir = prebuilt;
  }

  ~ScopedDeployerPaths() {
    auto& deployer = Service::instance().deployer();
    deployer.staging_dir = staging_dir;
    deployer.prebuilt_data_dir = prebuilt_data_dir;
  }

  path staging_dir;
  path prebuilt_data_dir;
};

void Touch(const path& file_path) {
  fs::create_directories(file_path.parent_path());
  std::ofstream(file_path.string()).close();
}

}  // namespace

TEST(RimeDictionaryComponentTest, FallbackToDefaultNamespaceWhenMissing) {
  auto test_root = fs::temp_directory_path() / "rime-dict-ns-fallback-test";
  fs::remove_all(test_root);
  auto staging = test_root / "staging";
  auto prebuilt = test_root / "prebuilt";
  fs::create_directories(staging);
  fs::create_directories(prebuilt);
  Touch(prebuilt / "luna_pinyin.table.bin");
  Touch(prebuilt / "luna_pinyin.prism.bin");
  Touch(prebuilt / "cangjie5.table.bin");

  ScopedDeployerPaths scoped_paths(staging, prebuilt);
  DictionaryComponent component;
  the<Dictionary> dict(
      component.Create("ns/luna_pinyin", "ns/luna_pinyin", {"ns/cangjie5"}));

  ASSERT_TRUE(dict);
  EXPECT_EQ(fs::absolute(prebuilt / "luna_pinyin.table.bin"),
            dict->primary_table()->file_path());
  EXPECT_EQ(fs::absolute(prebuilt / "luna_pinyin.prism.bin"),
            dict->prism()->file_path());
  ASSERT_EQ(2, dict->tables().size());
  EXPECT_EQ(fs::absolute(prebuilt / "cangjie5.table.bin"),
            dict->tables()[1]->file_path());

  fs::remove_all(test_root);
}
