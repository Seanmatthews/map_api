#ifndef MAP_API_CACHE_H_
#define MAP_API_CACHE_H_
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <multiagent-mapping-common/mapped-container-base.h>
#include <multiagent-mapping-common/traits.h>
#include <multiagent-mapping-common/unique-id.h>

#include "map-api/app-templates.h"
#include "map-api/cache-base.h"
#include "map-api/revision-map.h"
#include "map-api/transaction.h"

namespace map_api {
namespace traits {
template <bool IsSharedPointer, typename Type, typename DerivedType>
struct InstanceFactory {
  typedef DerivedType ElementType;
  static DerivedType* getPointerTo(Type& value) { return &value; }   // NOLINT
  static DerivedType& getReferenceTo(Type& value) { return value; }  // NOLINT
  static const DerivedType& getReferenceTo(const Type& value) {      // NOLINT
    return value;
  }
  static DerivedType* getPointerToDerived(Type& value) {  // NOLINT
    DerivedType* ptr = static_cast<DerivedType*>(&value);
    CHECK_NOTNULL(ptr);
    return ptr;
  }
  static DerivedType& getReferenceToDerived(Type& value) {  // NOLINT
    DerivedType* ptr = static_cast<DerivedType*>(&value);
    CHECK_NOTNULL(ptr);
    return *ptr;
  }
  static const DerivedType& getReferenceToDerived(
      const Type& value) {  // NOLINT
    const DerivedType* ptr = static_cast<const DerivedType*>(&value);
    CHECK_NOTNULL(ptr);
    return *ptr;
  }
  static void transferOwnership(std::shared_ptr<ElementType> object,
                                DerivedType* destination) {
    *destination = *object;
  }
};
template <typename Type, typename DerivedType>
struct InstanceFactory<true, Type, DerivedType> {
  typedef typename DerivedType::element_type ElementType;
  static typename Type::element_type* getPointerTo(Type& value) {  // NOLINT
    CHECK(value != nullptr);
    return value.get();
  }
  static typename Type::element_type& getReferenceTo(Type& value) {  // NOLINT
    CHECK(value != nullptr);
    return *value;
  }
  static const typename Type::element_type& getReferenceTo(
      const Type& value) {  // NOLINT
    CHECK(value != nullptr);
    return *value;
  }
  static typename DerivedType::element_type* getPointerToDerived(
      Type& value) {  // NOLINT
    CHECK(value != nullptr);
    typename DerivedType::element_type* ptr =
        static_cast<typename DerivedType::element_type*>(value.get());
    CHECK_NOTNULL(ptr);
    return ptr;
  }
  static typename DerivedType::element_type& getReferenceToDerived(
      Type& value) {  // NOLINT
    CHECK(value != nullptr);
    typename DerivedType::element_type* ptr =
        static_cast<typename DerivedType::element_type*>(value.get());
    CHECK_NOTNULL(ptr);
    return *ptr;
  }
  static const typename DerivedType::element_type& getReferenceToDerived(
      const Type& value) {  // NOLINT
    CHECK(value != nullptr);
    const typename DerivedType::element_type* ptr =
        static_cast<const typename DerivedType::element_type*>(value.get());
    CHECK_NOTNULL(ptr);
    return *ptr;
  }
  static void transferOwnership(std::shared_ptr<ElementType> object,
                                Type* destination) {
    *destination = object;
  }
};
}  // namespace traits

class ChunkManagerBase;
class NetTable;

template <typename IdType, typename ObjectType>
void objectToRevision(const IdType id, const ObjectType& object,
                      map_api::Revision* revision) {
  CHECK_NOTNULL(revision);
  objectToRevision(object, revision);
  IdType present_id = revision->getId<IdType>();
  if (present_id.isValid()) {
    CHECK_EQ(id, present_id);
  } else {
    revision->setId(id);
  }
}

/**
 * IdType needs to be a UniqueId.
 * The type Value is the type of the actual container objects.
 * The type DerivedValue is the type of the objects to be newly constructed.
 */
template <typename IdType, typename Value, typename DerivedValue = Value>
class Cache : public CacheBase,
              public common::MappedContainerBase<IdType, Value> {
 public:
  typedef typename common::IsPointerType<Value>::const_ref_type
      ConstRefReturnType;
  typedef std::shared_ptr<Cache<IdType, Value, DerivedValue> > Ptr;
  typedef std::shared_ptr<const Cache<IdType, Value, DerivedValue> > ConstPtr;

  Cache(const std::shared_ptr<Transaction>& transaction, NetTable* const table,
        const std::shared_ptr<ChunkManagerBase>& chunk_manager);
  virtual ~Cache();
  Value& getMutable(const IdType& id);
  ConstRefReturnType get(const IdType& id) const;
  std::shared_ptr<const Revision> getRevision(const IdType& id) const;
  /**
   * Inserted objects will live in cache_, but not in revisions_.
   * @return false if some item with same id already exists (in current chunks)
   */
  bool insert(const IdType& id, const Value& value);

  /**
   * Erase object from cache and database.
   */
  void erase(const IdType& id);

  /**
   * Will cache revision of object.
   */
  bool has(const IdType& id) const;

  /**
   * Available with the currently active set of chunks.
   */
  void getAllAvailableIds(std::vector<IdType>* available_ids) const;

  virtual size_t size() const;
  bool empty() const;
  virtual size_t numCachedItems() const;
  virtual std::string underlyingTableName() const;

 private:
  static constexpr bool kIsPointer = common::IsPointerType<Value>::value;
  typedef traits::InstanceFactory<kIsPointer, Value, DerivedValue> Factory;

  /**
   * Mutex MUST be locked prior to calling the getRevisionLocked functions.
   */
  std::shared_ptr<const Revision> getRevisionLocked(const IdType& id) const;
  void prefetchAllRevisionsLocked() const;
  virtual void prepareForCommit() override;

  struct ValueHolder {
    enum class DirtyState : bool {
      kDirty = true,
      kClean = false
    };
    ValueHolder(const Value& _value, DirtyState _dirty) :
      value(_value), dirty(_dirty) { }
    Value value;
    DirtyState dirty;
  };

  typedef std::unordered_map<IdType, ValueHolder> CacheMap;
  typedef std::unordered_set<IdType> IdSet;
  typedef std::vector<IdType> IdVector;

  mutable CacheMap cache_;
  mutable ConstRevisionMap revisions_;
  IdSet removals_;
  NetTable* underlying_table_;
  std::shared_ptr<ChunkManagerBase> chunk_manager_;
  bool staged_;

  class TransactionAccessFactory {
   public:
    class TransactionAccess {
      friend class TransactionAccessFactory;

     public:
      inline Transaction* operator->() const { return transaction_; }
      inline ~TransactionAccess() { transaction_->disableDirectAccess(); }

     private:
      explicit inline TransactionAccess(Transaction* transaction)
          : transaction_(transaction) {
        transaction_->enableDirectAccess();
      }
      Transaction* transaction_;
    };
    explicit inline TransactionAccessFactory(
        std::shared_ptr<Transaction> transaction)
        : transaction_(transaction) {}
    inline TransactionAccess get() const {
      return TransactionAccess(transaction_.get());
    }

   private:
    std::shared_ptr<Transaction> transaction_;
  };
  TransactionAccessFactory transaction_;

  class AvailableIds {
   public:
    AvailableIds(NetTable* underlying_table,
                 TransactionAccessFactory* transaction);
    const IdVector& getAllIds() const;
    bool hasId(const IdType& id) const;
    void addId(const IdType& id);
    void removeId(const IdType& id);

   private:
    void getAvailableIdsLocked() const;
    mutable IdVector ordered_available_ids_;
    mutable IdSet available_ids_;
    mutable bool ids_fetched_;
    NetTable* underlying_table_;
    TransactionAccessFactory* transaction_;
  };
  AvailableIds available_ids_;

  mutable std::mutex mutex_;
  typedef std::lock_guard<std::mutex> LockGuard;
};

}  // namespace map_api

#include "./map-api/cache-inl.h"

#endif  // MAP_API_CACHE_H_
