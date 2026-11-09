/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A statically linked hook command for the user-namespace tests: it needs no libc inside the fixture root.
 *   marker <file> [fail]   writes "ran" into <file> and exits 7 when "fail" follows, otherwise 0. */
#include <fcntl.h>
#include <unistd.h>

// A freestanding hook command: raw argv and open(2) are the point, so these checks do not apply.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-vararg)
int main(int argc, char** argv) {  // NOLINT(modernize-use-trailing-return-type)
  const char* path = argc > 1 ? argv[1] : "/marker";
  const int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (descriptor < 0) {
    return 3;
  }
  const ssize_t written = write(descriptor, "ran\n", 4);
  close(descriptor);
  if (written != 4) {
    return 4;
  }
  return argc > 2 ? 7 : 0;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-vararg)
