/*
 * Copyright RIME Developers
 * Distributed under the BSD License
 *
 * 2011-08-29 GONG Chen <chen.sst@gmail.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rime_api.h>
#include <map>
#include <vector>
#include "codepage.h"

using SessionsMap = std::map<int, RimeSessionId>;

void print_status(RimeStatus* status) {
  printf("schema: %s / %s\n", status->schema_id, status->schema_name);
  printf("status: ");
  if (status->is_disabled)
    printf("disabled ");
  if (status->is_composing)
    printf("composing ");
  if (status->is_ascii_mode)
    printf("ascii ");
  if (status->is_full_shape)
    printf("full_shape ");
  if (status->is_simplified)
    printf("simplified ");
  printf("\n");
}

void print_composition(RimeComposition* composition) {
  const char* preedit = composition->preedit;
  if (!preedit)
    return;
  size_t len = strlen(preedit);
  size_t start = composition->sel_start;
  size_t end = composition->sel_end;
  size_t cursor = composition->cursor_pos;
  for (size_t i = 0; i <= len; ++i) {
    if (start < end) {
      if (i == start) {
        putchar('[');
      } else if (i == end) {
        putchar(']');
      }
    }
    if (i == cursor)
      putchar('|');
    if (i < len)
      putchar(preedit[i]);
  }
  printf("\n");
}

void print_menu(RimeMenu* menu) {
  if (menu->num_candidates == 0)
    return;
  printf("page: %d%c (of size %d)\n", menu->page_no + 1,
         menu->is_last_page ? '$' : ' ', menu->page_size);
  for (int i = 0; i < menu->num_candidates; ++i) {
    bool highlighted = i == menu->highlighted_candidate_index;
    printf("%d. %c%s%c%s\n", i + 1, highlighted ? '[' : ' ',
           menu->candidates[i].text, highlighted ? ']' : ' ',
           menu->candidates[i].comment ? menu->candidates[i].comment : "");
  }
}

void print_context(RimeContext* context) {
  if (context->composition.length > 0 || context->menu.num_candidates > 0) {
    print_composition(&context->composition);
  } else {
    printf("(not composing)\n");
  }
  print_menu(&context->menu);
}

void print(RimeSessionId session_id) {
  RimeApi* rime = rime_get_api();

  RIME_STRUCT(RimeCommit, commit);
  RIME_STRUCT(RimeStatus, status);
  RIME_STRUCT(RimeContext, context);

  if (rime->get_commit(session_id, &commit)) {
    printf("commit: %s\n", commit.text);
    rime->free_commit(&commit);
  }

  if (rime->get_status(session_id, &status)) {
    print_status(&status);
    rime->free_status(&status);
  }

  if (rime->get_context(session_id, &context)) {
    print_context(&context);
    rime->free_context(&context);
  }
}

bool execute_special_command(const char* line,
                             RimeSessionId& session_id,
                             SessionsMap& session_maps) {
  RimeApi* rime = rime_get_api();
  auto print_sessions = [rime, &session_id, &session_maps]() {
    printf("current sessions list:\n");
    for (const auto& p : session_maps) {
      char schema_id[256] = {0};
      rime->get_current_schema(p.second, schema_id, sizeof(schema_id));
      char mk = (p.second == session_id) ? '>' : ' ';
      printf("%c %d. session_id: %lx, schema_id: %s\n", mk, p.first, p.second,
             schema_id);
    }
  };
  auto print_current_schema = [rime, &session_id]() {
    char current[100] = {0};
    if (rime->get_current_schema(session_id, current, sizeof(current))) {
      printf("current schema: [%s]\n", current);
    }
  };

  if (!strcmp(line, "ls sessions")) {
    print_sessions();
    return true;
  }
  if (!strcmp(line, "add session")) {
    RimeSessionId id = rime->create_session();
    if (!id) {
      fprintf(stderr, "Error creating new rime session.\n");
      return true;
    }
    session_maps[session_maps.rbegin()->first + 1] = id;
    session_id = id;
    print_sessions();
    print_current_schema();
    return true;
  }

  if (!strcmp(line, "print schema list") || !strcmp(line, "ls schemas")) {
    RimeSchemaList list;
    if (rime->get_schema_list(&list)) {
      printf("schema list:\n");
      for (size_t i = 0; i < list.size; ++i) {
        printf("%lu. %s [%s]\n", (i + 1), list.list[i].name,
               list.list[i].schema_id);
      }
      rime->free_schema_list(&list);
    }
    print_current_schema();
    return true;
  }
  const char* kSelectSchemaCommand = "select schema ";
  size_t command_length = strlen(kSelectSchemaCommand);
  if (!strncmp(line, kSelectSchemaCommand, command_length)) {
    const char* schema_id = line + command_length;
    if (rime->select_schema(session_id, schema_id)) {
      printf("selected schema: [%s]\n", schema_id);
    }
    return true;
  }
  const char* kSelectCandidateCommand = "select candidate ";
  command_length = strlen(kSelectCandidateCommand);
  if (!strncmp(line, kSelectCandidateCommand, command_length)) {
    int index = atoi(line + command_length);
    if (index > 0 &&
        rime->select_candidate_on_current_page(session_id, index - 1)) {
      print(session_id);
    } else {
      fprintf(stderr, "cannot select candidate at index %d.\n", index);
    }
    return true;
  }
  if (!strcmp(line, "print candidate list")) {
    RimeCandidateListIterator iterator = {0};
    if (rime->candidate_list_begin(session_id, &iterator)) {
      while (rime->candidate_list_next(&iterator)) {
        printf("%d. %s", iterator.index + 1, iterator.candidate.text);
        if (iterator.candidate.comment)
          printf(" (%s)", iterator.candidate.comment);
        putchar('\n');
      }
      rime->candidate_list_end(&iterator);
    } else {
      printf("no candidates.\n");
    }
    return true;
  }
  const char* kSetOptionCommand = "set option ";
  command_length = strlen(kSetOptionCommand);
  if (!strncmp(line, kSetOptionCommand, command_length)) {
    Bool is_on = True;
    const char* option = line + command_length;
    if (*option == '!') {
      is_on = False;
      ++option;
    }
    rime->set_option(session_id, option, is_on);
    printf("%s set %s.\n", option, is_on ? "on" : "off");
    return true;
  }
  if (!strcmp(line, "synchronize")) {
    return rime->sync_user_data();
  }

  const char* kSelectSession = "select session";
  command_length = strlen(kSelectSession);
  if (!strncmp(line, kSelectSession, command_length)) {
    int index = atoi(line + command_length);
    if (session_maps.find(index) != session_maps.end()) {
      session_id = session_maps[index];
      print_sessions();
      print_current_schema();
      return true;
    }
  }
  const char* kKillSession = "kill session";
  command_length = strlen(kKillSession);
  if (!strncmp(line, kKillSession, command_length)) {
    int index = atoi(line + command_length);
    if (index > 0) {
      auto p = session_maps.find(index);
      if (p != session_maps.end() && session_maps.size() > 1) {
        rime->destroy_session(session_maps[index]);
        session_maps.erase(index);
        ++p;
        session_id = p->second;
        print_sessions();
        print_current_schema();
        return true;
      } else {
        printf("don't kill the last session\n");
        print_sessions();
        return true;
      }
    } else {
      printf("invalid session index, please recheck!\n");
      return true;
    }
  }
  return false;
}

void on_message(void* context_object,
                RimeSessionId session_id,
                const char* message_type,
                const char* message_value) {
  printf("message: [%lu] [%s] %s\n", session_id, message_type, message_value);
  RimeApi* rime = rime_get_api();
  if (RIME_API_AVAILABLE(rime, get_state_label) &&
      !strcmp(message_type, "option")) {
    Bool state = message_value[0] != '!';
    const char* option_name = message_value + !state;
    const char* state_label =
        rime->get_state_label(session_id, option_name, state);
    if (state_label) {
      printf("updated option: %s = %d // %s\n", option_name, state,
             state_label);
    }
  }
}

int main(int argc, char* argv[]) {
  unsigned int codepage = SetConsoleOutputCodePage();
  RimeApi* rime = rime_get_api();

  RIME_STRUCT(RimeTraits, traits);
  traits.app_name = "rime.console";
  rime->setup(&traits);

  rime->set_notification_handler(&on_message, NULL);

  fprintf(stderr, "initializing...\n");
reload:
  rime->initialize(NULL);
  Bool full_check = True;
  if (rime->start_maintenance(full_check))
    rime->join_maintenance_thread();
  fprintf(stderr, "ready.\n");

  RimeSessionId session_id = rime->create_session();
  if (!session_id) {
    fprintf(stderr, "Error creating rime session.\n");
    SetConsoleOutputCodePage(codepage);
    return 1;
  }

  SessionsMap session_maps;
  session_maps[1] = session_id;

  const int kMaxLength = 99;
  char line[kMaxLength + 1] = {0};
  while (fgets(line, kMaxLength, stdin) != NULL) {
    for (char* p = line; *p; ++p) {
      if (*p == '\r' || *p == '\n') {
        *p = '\0';
        break;
      }
    }
    if (!strcmp(line, "exit"))
      break;
    else if (!strcmp(line, "reload")) {
      for (const auto& p : session_maps) {
        rime->destroy_session(p.second);
      }
      rime->finalize();
      goto reload;
    }
    if (execute_special_command(line, session_id, session_maps))
      continue;
    if (rime->simulate_key_sequence(session_id, line)) {
      print(session_id);
    } else {
      fprintf(stderr, "Error processing key sequence: %s\n", line);
    }
  }

  for (const auto& p : session_maps) {
    rime->destroy_session(p.second);
  }

  rime->finalize();

  SetConsoleOutputCodePage(codepage);
  return 0;
}
