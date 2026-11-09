// Runs the tests inside a user namespace in which the unprivileged user is uid 0, so libalpm's chroot for hooks and
// scriptlets works without real root (spike S-3). The namespace is entered before any thread exists and never when only
// listing tests, so test discovery stays cheap and safe.
//
// Environment:
//   HOLONIGHT_TEST_NO_USERNS=1   behave as if namespaces were unavailable
//   HOLONIGHT_REQUIRE_USERNS=1   fail instead of skipping when they are unavailable (CI)
// A skip prints "HOLONIGHT_SKIP userns: <reason>" and exits 77, which CTest maps to Skipped.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iostream>
#include <optional>
#include <sched.h>
#include <span>
#include <string>
#include <unistd.h>

namespace {

constexpr int kExitSkipped = 77;

bool flagSet(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && std::strcmp(value, "1") == 0;
}

bool writeFile(const char* path, const std::string& text) {
  std::ofstream out(path);
  out << text;
  return static_cast<bool>(out);
}

// The host user id behind uid 0 of this namespace, from /proc/self/uid_map.
std::optional<unsigned long> hostUidOfNamespaceRoot() {
  std::ifstream map("/proc/self/uid_map");
  unsigned long inside = 0;
  unsigned long outside = 0;
  unsigned long count = 0;
  if (map >> inside >> outside >> count && inside == 0) {
    return outside;
  }
  return std::nullopt;
}

// Returns a reason when the namespace could not be entered, nullopt when it was.
std::optional<std::string> enterUserNamespace() {
  if (flagSet("HOLONIGHT_TEST_NO_USERNS")) {
    return std::string("disabled by HOLONIGHT_TEST_NO_USERNS");
  }
  const uid_t uid = ::geteuid();
  const gid_t gid = ::getegid();
  if (uid == 0) {
    std::cerr << "refusing to run as root (REQ-C-010)\n";
    std::exit(1);
  }
  if (::unshare(CLONE_NEWUSER) != 0) {
    return std::string("unshare(CLONE_NEWUSER) failed: ") + std::strerror(errno);
  }
  if (!writeFile("/proc/self/setgroups", "deny") ||
      !writeFile("/proc/self/uid_map", "0 " + std::to_string(uid) + " 1") ||
      !writeFile("/proc/self/gid_map", "0 " + std::to_string(gid) + " 1")) {
    return std::string("cannot write the uid and gid maps");
  }
  // Inside the namespace we are uid 0, and the host user behind it must be an ordinary one.
  const auto host = hostUidOfNamespaceRoot();
  if (!host || *host == 0) {
    return std::string("uid_map does not map an unprivileged host user");
  }
  return std::nullopt;
}

}  // namespace

int main(int argc, char* argv[]) {
  const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
  const bool listing = std::ranges::any_of(
      arguments.subspan(1), [](const char* argument) { return std::strncmp(argument, "--gtest_list_tests", 18) == 0; });
  if (!listing) {
    if (const auto reason = enterUserNamespace()) {
      if (flagSet("HOLONIGHT_REQUIRE_USERNS")) {
        std::cerr << "HOLONIGHT_FAIL userns required but unavailable: " << *reason << '\n';
        return 1;
      }
      std::cerr << "HOLONIGHT_SKIP userns: " << *reason << '\n';
      return kExitSkipped;
    }
  }
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
