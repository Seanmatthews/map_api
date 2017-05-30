// Copyright (C) 2014-2017 Titus Cieslewski, ASL, ETH Zurich, Switzerland
// You can contact the author at <titus at ifi dot uzh dot ch>
// Copyright (C) 2014-2015 Simon Lynen, ASL, ETH Zurich, Switzerland
// Copyright (c) 2014-2015, Marcin Dymczyk, ASL, ETH Zurich, Switzerland
// Copyright (c) 2014, Stéphane Magnenat, ASL, ETH Zurich, Switzerland
//
// This file is part of Map API.
//
// Map API is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Map API is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Map API.  If not, see <http://www.gnu.org/licenses/>.

#include <map-api/discovery.h>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <sys/file.h>

#include "map-api/file-discovery.h"
#include "map-api/hub.h"
#include "map-api/server-discovery.h"
#include "map-api/test/testing-entrypoint.h"
#include "./map_api_fixture.h"

DECLARE_string(discovery_mode);

namespace map_api {

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

}  // namespace map_api

MAP_API_UNITTEST_ENTRYPOINT
