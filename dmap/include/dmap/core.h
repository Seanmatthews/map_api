#ifndef DMAP_CORE_H_
#define DMAP_CORE_H_

#include <mutex>

namespace dmap {
class Hub;
class NetTableManager;

/**
 * The map api core class is the first interface between robot application and
 * the map api system. It is a singleton in order to:
 * - Ensure that only one instance of the database is created and used
 * - Ensure that only one thread is present to communicate with other nodes
 */
class Core final {
 public:
  // Returns null iff core is not initialized yet. Waits on initialized_mutex_.
  static Core* instance();
  // Returns null if core is not initialized, or if initialized_mutex_ is
  // locked.
  static Core* instanceNoWait();

  static void initializeInstance();
  /**
   * Initializer
   */
  void init();
  /**
   * Check if initialized
   */
  bool isInitialized() const;
  /**
   * Makes the server thread re-enter, disconnects from database and removes
   * own address from discovery file.
   */
  void kill();
  /**
   * Same as kill, but makes sure each chunk has at least one other peer. Use
   * this only if you are sure that your data will be picked up by other peers.
   */
  void killOnceShared();
  // The following can malfunction if the only other peer leaves in the middle
  // of the execution of this function.
  void killOnceSharedUnlessAlone();

  // NetTableManager& tableManager();
  // const NetTableManager& tableManager() const;

 private:
  Core();
  ~Core();

  /**
   * Hub instance
   */
  Hub& hub_;
  NetTableManager& table_manager_;

  static Core instance_;
  bool initialized_ = false;
  std::mutex initialized_mutex_;
};
}  // namespace dmap

#endif  // DMAP_CORE_H_
