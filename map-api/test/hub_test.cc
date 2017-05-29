#include <glog/logging.h>
#include <gtest/gtest.h>

#include "map-api/core.h"
#include "map-api/hub.h"
#include "map-api/ipc.h"
#include "map-api/test/testing-entrypoint.h"
#include "./map_api_fixture.h"

namespace map_api {

class HubTest : public MapApiFixture {};

TEST_F(HubTest, LaunchTest) {
  enum Processes {
    ROOT,
    SLAVE
  };
  enum Barriers {
    BEFORE_COUNT,
    AFTER_COUNT
  };
  if (getSubprocessId() == ROOT) {
    EXPECT_EQ(0, Hub::instance().peerSize());
    launchSubprocess(SLAVE);
    IPC::barrier(BEFORE_COUNT, 1);
    EXPECT_EQ(1, Hub::instance().peerSize());
    IPC::barrier(AFTER_COUNT, 1);
  } else {
    IPC::barrier(BEFORE_COUNT, 1);
    IPC::barrier(AFTER_COUNT, 1);
  }
}

}  // namespace map_api

DMAP_UNITTEST_ENTRYPOINT
