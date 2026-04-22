//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2013-05-26 GONG Chen <chen.sst@gmail.com>
//

#include <ctime>
#include <map>
#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/config.h>
#include <rime/schema.h>
#include <rime/switcher.h>
#include <rime/translation.h>
#include <rime/gear/schema_list_translator.h>

namespace rime {

namespace {

string DisplayTagForSchemaId(const string& schema_id) {
  auto schema_path = path(schema_id);
  if (!schema_path.has_parent_path()) {
    return string();
  }
  return schema_path.parent_path().generic_u8string();
}

}  // namespace

class SchemaSelection : public SimpleCandidate, public SwitcherCommand {
 public:
  SchemaSelection(Schema* schema)
      : SimpleCandidate("schema", 0, 0, schema->schema_name()),
        SwitcherCommand(schema->schema_id()) {}
  virtual void Apply(Switcher* switcher);
};

void SchemaSelection::Apply(Switcher* switcher) {
  switcher->DeactivateAndApply([this, switcher] {
    if (Engine* engine = switcher->attached_engine()) {
      if (keyword_ != engine->schema()->schema_id()) {
        engine->ApplySchema(new Schema(keyword_));
      }
    }
  });
}

class SchemaAction : public ShadowCandidate, public SwitcherCommand {
 public:
  SchemaAction(an<Candidate> schema, an<Candidate> command)
      : ShadowCandidate(schema, command->type()),
        SwitcherCommand(As<SwitcherCommand>(schema)->keyword()),
        command_(As<SwitcherCommand>(command)) {}
  virtual void Apply(Switcher* switcher);

 private:
  an<SwitcherCommand> command_;
};

void SchemaAction::Apply(Switcher* switcher) {
  if (command_) {
    command_->Apply(switcher);
  }
}

class SchemaListTranslation : public FifoTranslation {
 public:
  SchemaListTranslation(Switcher* switcher) { LoadSchemaList(switcher); }
  virtual int Compare(an<Translation> other, const CandidateList& candidates);

 protected:
  void LoadSchemaList(Switcher* switcher);
};

int SchemaListTranslation::Compare(an<Translation> other,
                                   const CandidateList& candidates) {
  if (!other || other->exhausted())
    return -1;
  if (exhausted())
    return 1;
  // switches should immediately follow current schema (#0)
  auto theirs = other->Peek();
  if (theirs && theirs->type() == "unfold") {
    if (cursor_ == 0) {
      // unfold its options when the current schema is selected
      candies_[0] = New<SchemaAction>(candies_[0], theirs);
    }
    return cursor_ == 0 ? -1 : 1;
  }
  if (theirs && theirs->type() == "switch") {
    return cursor_ == 0 ? -1 : 1;
  }
  return Translation::Compare(other, candidates);
}

void SchemaListTranslation::LoadSchemaList(Switcher* switcher) {
  Engine* engine = switcher->attached_engine();
  if (!engine)
    return;
  Config* config = switcher->schema()->config();
  if (!config)
    return;
  // current schema comes first
  Schema* current_schema = engine->schema();
  vector<an<SchemaSelection>> schema_candidates;
  if (current_schema) {
    schema_candidates.push_back(New<SchemaSelection>(current_schema));
  }
  Config* user_config = switcher->user_config();
  size_t fixed = schema_candidates.size();
  time_t now = time(NULL);
  // load the rest schema list
  Switcher::ForEachSchemaListEntry(config, [current_schema, user_config, now,
                                            &schema_candidates](
                                               const string& schema_id) {
    if (current_schema && schema_id == current_schema->schema_id())
      return /* continue = */ true;
    Schema schema(schema_id);
    auto cand = New<SchemaSelection>(&schema);
    int timestamp = 0;
    if (user_config && user_config->GetInt(
                           "var/schema_access_time/" + schema_id, &timestamp)) {
      if (timestamp <= now)
        cand->set_quality(timestamp);
    }
    schema_candidates.push_back(cand);
    return /* continue = */ true;
  });
  map<string, int> schema_name_count;
  for (const auto& cand : schema_candidates) {
    ++schema_name_count[cand->text()];
  }
  for (const auto& cand : schema_candidates) {
    auto schema_tag = DisplayTagForSchemaId(cand->keyword());
    auto schema_path = path(cand->keyword());
    if (schema_path.has_parent_path() && !schema_tag.empty()) {
      cand->set_comment(schema_tag);
    }
    Append(cand);
  }
  DLOG(INFO) << "num schemata: " << candies_.size();
  bool fix_order = false;
  config->GetBool("switcher/fix_schema_list_order", &fix_order);
  if (fix_order)
    return;
  // reorder schema list by recency
  std::stable_sort(candies_.begin() + fixed, candies_.end(),
                   [](const an<Candidate>& x, const an<Candidate>& y) {
                     return x->quality() > y->quality();
                   });
}

SchemaListTranslator::SchemaListTranslator(const Ticket& ticket)
    : Translator(ticket) {}

an<Translation> SchemaListTranslator::Query(const string& input,
                                            const Segment& segment) {
  auto switcher = dynamic_cast<Switcher*>(engine_);
  if (!switcher) {
    return nullptr;
  }
  return New<SchemaListTranslation>(switcher);
}

}  // namespace rime
