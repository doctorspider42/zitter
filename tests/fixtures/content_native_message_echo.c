// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(void) {
#ifdef _WIN32
  if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
      _setmode(_fileno(stdout), _O_BINARY) == -1)
    return 1;
#endif
  uint32_t size;
  char message[1024];
  while (fread(&size, sizeof(size), 1, stdin) == 1) {
    if (size == 0 || size > sizeof(message) || fread(message, 1, size, stdin) != size)
      return 1;
    if (fwrite(&size, sizeof(size), 1, stdout) != 1 ||
        fwrite(message, 1, size, stdout) != size || fflush(stdout) != 0)
      return 1;
  }
  return 0;
}
