#ifndef METATABLE_H_
#define METATABLE_H_

#include "map-api/cr-table.h"

namespace map_api {

/**
 * The Metatable is a CR (create and read) table that holds the definitions of
 * all application-defined tables. It is used to synchronize table definitions
 * across the peers
 */
class Metatable final : public CRTable {
 public:
  static const std::string kNameField;
  static const std::string kDescriptorField;

  virtual const std::string name() const final override;
  virtual void defineFieldsCRDerived() final override;
  static Metatable& instance();
 protected:
  MAP_API_TABLE_SINGLETON_PATTERN_PROTECTED_METHODS(Metatable);
 private:
  /**
   * Overriding sync to do nothing - we don't want an infinite recursion
   */
  inline virtual bool sync() {
    return true;
  }
};

} /* namespace map_api */

#endif /* METATABLE_H_ */
