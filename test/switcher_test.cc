#include <vector>
#include <gtest/gtest.h>
#include <rime/config.h>
#include <rime/switcher.h>

using namespace rime;

TEST(RimeSwitcherTest, ResolveNamespacedSchemaInSchemaList) {
  Config config;
  EXPECT_TRUE(config.SetString("schema_list/@next/schema", "luna_pinyin"));
  EXPECT_TRUE(config.SetString("schema_list/@last/namespace", "ns"));
  EXPECT_TRUE(config.SetString("schema_list/@next/schema", "stroke"));

  std::vector<string> schema_ids;
  EXPECT_EQ(2, Switcher::ForEachSchemaListEntry(
                   &config, [&schema_ids](const string& schema_id) {
                     schema_ids.push_back(schema_id);
                     return true;
                   }));
  ASSERT_EQ(2, schema_ids.size());
  EXPECT_EQ("ns/luna_pinyin", schema_ids[0]);
  EXPECT_EQ("stroke", schema_ids[1]);
}

TEST(RimeSwitcherTest, KeepQualifiedSchemaIdUnchanged) {
  Config config;
  EXPECT_TRUE(config.SetString("schema_list/@next/schema", "shared/stroke"));
  EXPECT_TRUE(config.SetString("schema_list/@last/namespace", "ns"));

  std::vector<string> schema_ids;
  EXPECT_EQ(1, Switcher::ForEachSchemaListEntry(
                   &config, [&schema_ids](const string& schema_id) {
                     schema_ids.push_back(schema_id);
                     return true;
                   }));
  ASSERT_EQ(1, schema_ids.size());
  EXPECT_EQ("shared/stroke", schema_ids[0]);
}
