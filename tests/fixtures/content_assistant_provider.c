// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  char input[32768];
  const size_t size = fread(input, 1, sizeof(input) - 1, stdin);
  input[size] = '\0';
  const char* key = getenv("ZITTER_OPENAI_API_KEY");
  if (argc < 2 || (key && *key) || !strstr(input, "Connection test.") ||
      !strstr(input, "\\\"untrusted_browser_context\\\":null") ||
      strstr(input, "manual-fixture"))
    return 1;

  if (strcmp(argv[1], "exec") == 0) {
    puts("{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\","
         "\"text\":\"{\\\"answer\\\":\\\"Codex fixture ready\\\",\\\"actions\\\":[]}\"}}");
  } else if (strcmp(argv[1], "-p") == 0) {
    puts("{\"type\":\"result\",\"structured_output\":{"
         "\"answer\":\"Claude fixture ready\",\"actions\":[]}}");
  } else {
    return 1;
  }
  return 0;
}
