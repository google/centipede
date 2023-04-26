// Copyright 2023 The Centipede Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Centipede puzzle: the fuzz function starts a subprocess which is
// the actual fuzz function. The input is passed to the subprocess
// and coverage is collected from the subprocess.
//
// RUN: Run --timeout_per_input=5 --batch_size=10 && SolutionIs FUZZ

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  int pid = fork();
  if (pid < 0) {
    perror("fork");
    abort();
  }
  if (pid == 0) {
    int fd = memfd_create("centipede-test", 0);
    if (fd < 0) {
      perror("memfd_create");
      abort();
    }
    if (write(fd, data, size) != size) {
      perror("write");
      abort();
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
      perror("fseek");
      abort();
    }
    if (dup2(fd, STDIN_FILENO) < 0) {
      perror("dup2");
      abort();
    }
    const char* arg[] = {"exe", nullptr};
    const char* env[] = {"CENTIPEDE_TEST_CHILD=1", nullptr, nullptr};
    for (char** e = environ; *e; e++) {
      if (strstr(*e, "CENTIPEDE_RUNNER_FLAGS=")) env[1] = *e;
    }
    execve("/proc/self/exe", const_cast<char**>(arg), const_cast<char**>(env));
    abort();
  }
  int status;
  while (waitpid(pid, &status, __WALL) != pid) {
  }
  if ((WIFEXITED(status) && WEXITSTATUS(status)) || WIFSIGNALED(status))
    abort();
  return 0;
}

extern "C" {
int CentipedeManualCoverage() { return 1; }
void CentipedeCollectCoverage(int exit_status);
__attribute__((weak)) void CentipedeIsPresent();
__attribute__((weak)) void __libfuzzer_is_present();
}

static __attribute__((constructor)) void ctor() {
  if (!getenv("CENTIPEDE_TEST_CHILD")) return;
  if (!CentipedeIsPresent || !__libfuzzer_is_present) {
    fprintf(stderr, "Centipede is not present\n");
    abort();
  }
  char data[16];
  ssize_t size = read(STDIN_FILENO, data, sizeof(data));
  if (size < 0) {
    perror("read");
    abort();
  }
  if (size == 4 && data[0] == 'F' && data[1] == 'U' && data[2] == 'Z' &&
      data[3] == 'Z') {
    abort();
  }
  CentipedeCollectCoverage(0);
  _exit(0);
}
