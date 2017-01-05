#ifndef DMAP_MESSAGE_H_
#define DMAP_MESSAGE_H_

#include <string>

#include <glog/logging.h>
#include <google/protobuf/io/gzip_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include "dmap/message.h"
#include "dmap/peer-id.h"
#include "./core.pb.h"

namespace dmap {

class Message : public proto::HubMessage {
 public:
  /**
   * Message impose shorthands for common messages
   */
  inline void ack();
  inline void decline();
  /**
   * Templated on type denomination because we might want to do different things
   * with the same payload type. Because this is templated on a const char*, the
   * template matches only if the pointer matches: This is great because it
   * forces the use of unique constants. On the other hand, this couples
   * the message type to the string that is used to identify it, e.g. in the
   * handler map
   */
  template <const char* message_type, typename PayloadType>
  void extract(PayloadType* payload) const;
  template <const char* message_type, typename PayloadType>
  void impose(const PayloadType& payload);
  /**
   * For payload-free messages, such as ACKs
   */
  template <const char* message_type>
  void impose();

  template <const char* message_type>
  bool isType() const;

  inline bool isOk() const { return isType<kAck>(); }

  inline PeerId sender() const { return PeerId(proto::HubMessage::sender()); }
  inline void setSender(const PeerId& peer_id) { set_sender(peer_id.ipPort()); }

  /**
   * General-purpose message types
   */
  static const char kAck[];
  static const char kDecline[];
  static const char kInvalid[];
  static const char kRedundant[];

  // Struct to deduce message name from message type at compile time.
  template <typename Type>
  struct UniqueType {
    static const char message_name[];
  };
};

/**
 * Anticipated common payload types: String and ProtoBuf
 */
#define DMAP_MESSAGE_IMPOSE_STRING_MESSAGE(type_denomination) \
  template <>                                                 \
  void Message::impose<type_denomination, std::string>(       \
      const std::string& payload) {                           \
    this->set_type(type_denomination);                        \
    this->set_serialized(payload);                            \
  }                                                           \
  extern void __FILE__##__LINE__(void)  // swallows the semicolon
#define DMAP_MESSAGE_EXTRACT_STRING_MESSAGE(type_denomination) \
  template <>                                                  \
  void Message::extract<type_denomination, std::string>(       \
      std::string* payload) const {                            \
    CHECK_NOTNULL(payload);                                    \
    CHECK(isType<type_denomination>());                        \
    *payload = serialized();                                   \
  }                                                            \
  extern void __FILE__##__LINE__##2(void)  // swallows the semicolon
#define DMAP_STRING_MESSAGE(type_denomination)           \
  DMAP_MESSAGE_IMPOSE_STRING_MESSAGE(type_denomination); \
  DMAP_MESSAGE_EXTRACT_STRING_MESSAGE(type_denomination)
#define DMAP_MESSAGE_IMPOSE_PROTO_MESSAGE(type_denomination, proto_type) \
  template <>                                                            \
  void Message::impose<type_denomination, proto_type>(                   \
      const proto_type& payload) {                                       \
    this->set_type(type_denomination);                                   \
    this->set_serialized(payload.SerializeAsString());                   \
  }                                                                      \
  extern void __FILE__##__LINE__(void)  // swallows the semicolon
#define DMAP_MESSAGE_EXTRACT_PROTO_MESSAGE(type_denomination, proto_type) \
  template <>                                                             \
  void Message::extract<type_denomination, proto_type>(                   \
      proto_type* payload) const {                                        \
    CHECK_NOTNULL(payload);                                               \
    CHECK(isType<type_denomination>());                                   \
    CHECK(payload->ParseFromString(this->serialized()));                  \
  }                                                                       \
  extern void __FILE__##__LINE__##2(void)  // swallows the semicolon
#define DMAP_PROTO_MESSAGE(type_denomination, proto_type)           \
  DMAP_MESSAGE_IMPOSE_PROTO_MESSAGE(type_denomination, proto_type); \
  DMAP_MESSAGE_EXTRACT_PROTO_MESSAGE(type_denomination, proto_type)
#define DMAP_UNIQUE_PROTO_MESSAGE(type_denomination_string, proto_type)   \
  template <> \
  const char Message::UniqueType<proto_type>::message_name[] = \
      type_denomination_string; \
  DMAP_MESSAGE_IMPOSE_PROTO_MESSAGE(Message::UniqueType<proto_type>::message_name, proto_type); \
  DMAP_MESSAGE_EXTRACT_PROTO_MESSAGE(Message::UniqueType<proto_type>::message_name, proto_type)


#define DMAP_MESSAGE_IMPOSE_COMPRESSED_PROTO_MESSAGE(type_denomination,     \
                                                     proto_type)            \
  template <>                                                               \
  void Message::impose<type_denomination, proto_type>(                      \
      const proto_type& payload) {                                          \
    this->set_type(type_denomination);                                      \
    google::protobuf::io::StringOutputStream serialized_stream(             \
        mutable_serialized());                                              \
    google::protobuf::io::GzipOutputStream gzip_stream(&serialized_stream); \
    payload.SerializeToZeroCopyStream(&gzip_stream);                        \
    gzip_stream.Close();                                                    \
  }                                                                         \
  extern void __FILE__##__LINE__(void)

#define DMAP_MESSAGE_EXTRACT_COMPRESSED_PROTO_MESSAGE(type_denomination,   \
                                                      proto_type)          \
  template <>                                                              \
  void Message::extract<type_denomination, proto_type>(                    \
      proto_type* payload) const {                                         \
    CHECK_NOTNULL(payload);                                                \
    CHECK(isType<type_denomination>());                                    \
    std::istringstream iss(serialized());                                  \
    google::protobuf::io::IstreamInputStream serialized_stream(&iss);      \
    google::protobuf::io::GzipInputStream gzip_stream(&serialized_stream); \
    payload->ParseFromZeroCopyStream(&gzip_stream);                        \
  }                                                                        \
  extern void __FILE__##__LINE__##2(void)

#define DMAP_COMPRESSED_PROTO_MESSAGE(type_denomination, proto_type)           \
  DMAP_MESSAGE_IMPOSE_COMPRESSED_PROTO_MESSAGE(type_denomination, proto_type); \
  DMAP_MESSAGE_EXTRACT_COMPRESSED_PROTO_MESSAGE(type_denomination, proto_type)

}  // namespace dmap

#include "./message-inl.h"

#endif  // DMAP_MESSAGE_H_