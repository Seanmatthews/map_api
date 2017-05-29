#ifndef DMAP_MESSAGE_INL_H_
#define DMAP_MESSAGE_INL_H_

namespace map_api {

inline void Message::ack() { impose<Message::kAck>(); }

inline void Message::decline() { impose<Message::kDecline>(); }

template <const char* message_type>
void Message::impose() {
  this->set_type(message_type);
  this->set_serialized("");
}

template <const char* message_type>
bool Message::isType() const {
  std::string expected_type(message_type);
  return this->type() == expected_type;
}

}  // namespace map_api

#endif /* DMAP_MESSAGE_INL_H_ */
