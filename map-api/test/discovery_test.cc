#include <dmap/discovery.h>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <sys/file.h>

#include "dmap/file-discovery.h"
#include "dmap/hub.h"
#include "dmap/server-discovery.h"
#include "dmap/test/testing-entrypoint.h"
#include "./dmap_fixture.h"

DECLARE_string(discovery_mode);

namespace dmap {

class DiscoveryTest : public MapApiFixture,
                      public ::testing::WithParamInterface<const char*> {
 protected:
  virtual void SetUp() {
    FLAGS_discovery_mode = GetParam();
    if (strcmp(GetParam(), "server") == 0) {
      launchDiscoveryServer();
    }
    MapApiFixture::SetUp();
  }

  virtual void TearDown() {
    MapApiFixture::TearDown();
    if (strcmp(GetParam(), "server") == 0) {
      killDiscoveryServer();
    }
  }

  void launchDiscoveryServer() {
    std::ostringstream command;
    std::string this_executable = getSelfpath();
    unsigned last_slash = this_executable.find_last_of("/");
    command << this_executable.substr(0, last_slash + 1) << "discovery-server";
    discovery_server_ = popen2(command.str().c_str());
  }

  void killDiscoveryServer() { kill(discovery_server_, SIGINT); }

  // http://stackoverflow.com/questions/548063/kill-a-process-started-with-popen
  pid_t popen2(const char* command) {
    pid_t pid = fork();
    CHECK_GE(pid, 0);
    if (pid == 0) {
      execl(command, command, NULL);
      perror("execl");
      exit(1);
    }
    return pid;
  }

  static void getPeersGrindThread() {
    std::set<PeerId> peers;
    for (size_t i = 0; i < kGetPeersGrindIterations; ++i) {
      Hub::instance().getPeers(&peers);
    }
  }

  static constexpr size_t kGetPeersGrindIterations = 1000;
  pid_t discovery_server_;
};

TEST_P(DiscoveryTest, ThreadSafety) {
  std::thread a(getPeersGrindThread), b(getPeersGrindThread);
  a.join();
  b.join();
}

INSTANTIATE_TEST_CASE_P(DiscoveryInstances, DiscoveryTest,
                        ::testing::Values("file", "server"));

class FileDiscoveryTest : public DiscoveryTest {
 public:
  void fakeZombieLockFile() {
    int lock_file =
        open(FileDiscovery::kLockFileName, O_WRONLY | O_EXCL | O_CREAT, 0);
    CHECK_NE(lock_file, -1);
    CHECK_NE(close(lock_file), -1) << errno;
  }

  void clearFakeZombieLockFile() {
    CHECK_NE(unlink(FileDiscovery::kLockFileName), -1);
  }
};

TEST_P(FileDiscoveryTest, DiscoveryLockTimeout) {
  fakeZombieLockFile();
  std::set<PeerId> peers;
  EXPECT_DEATH(Hub::instance().getPeers(&peers), "^");
  clearFakeZombieLockFile();
}

INSTANTIATE_TEST_CASE_P(FileDiscoveryInstance, FileDiscoveryTest,
                        ::testing::Values("file"));

}  // namespace dmap

MAP_API_UNITTEST_ENTRYPOINT
