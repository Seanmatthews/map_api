#ifndef MAP_API_LOGICAL_TIME_H_
#define MAP_API_LOGICAL_TIME_H_

#include <cstdint>
#include <iostream>  // NOLINT
#include <mutex>

namespace map_api {
class LogicalTime;
}  // namespace map_api

namespace std {
template <>
struct hash<map_api::LogicalTime> {
  inline size_t operator()(const map_api::LogicalTime& time) const;
};
}  // namespace std

namespace map_api {
class LogicalTime {
 public:
  friend size_t std::hash<LogicalTime>::operator()(const LogicalTime&) const;
  /**
   * Invalid time
   */
  LogicalTime();
  /**
   * To deserialize from database.
   */
  explicit LogicalTime(uint64_t serialized);

  bool isValid() const;
  /**
   * Returns a current logical time and advances the value of the clock by one
   */
  static LogicalTime sample();

  uint64_t serialize() const;
  /**
   * If other_time exceeds or equals current_, current_ is advanced to
   * other_time + 1
   */
  static void synchronize(const LogicalTime& other_time);

  inline bool operator <(const LogicalTime& other) const;
  inline bool operator <=(const LogicalTime& other) const;
  inline bool operator >(const LogicalTime& other) const;
  inline bool operator >=(const LogicalTime& other) const;
  inline bool operator ==(const LogicalTime& other) const;

 private:
  uint64_t value_;
  static uint64_t current_;
  static std::mutex current_mutex_;
};

}  // namespace map_api

namespace std {
inline ostream& operator<<(ostream& out, const map_api::LogicalTime& time) {
  out << "Logical time(" << time.serialize() << ")";
  return out;
}

inline size_t std::hash<map_api::LogicalTime>::operator()(
    const map_api::LogicalTime& time) const {
  return std::hash<uint64_t>()(time.value_);
}
}  // namespace std

#include "map-api/logical-time-inl.h"

#endif  // MAP_API_LOGICAL_TIME_H_
