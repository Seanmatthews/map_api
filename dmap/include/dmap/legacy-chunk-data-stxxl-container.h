#ifndef DMAP_LEGACY_CHUNK_DATA_STXXL_CONTAINER_H_
#define DMAP_LEGACY_CHUNK_DATA_STXXL_CONTAINER_H_

#include <list>
#include <vector>

#include "dmap/legacy-chunk-data-container-base.h"
#include "dmap/stxxl-revision-store.h"

namespace dmap {

class LegacyChunkDataStxxlContainer : public LegacyChunkDataContainerBase {
 public:
  LegacyChunkDataStxxlContainer();
  virtual ~LegacyChunkDataStxxlContainer();

 private:
  virtual bool initImpl() final override;
  virtual bool insertImpl(const std::shared_ptr<const Revision>& query)
      final override;
  virtual bool bulkInsertImpl(const MutableRevisionMap& query) final override;
  virtual bool patchImpl(const std::shared_ptr<const Revision>& query)
      final override;
  virtual std::shared_ptr<const Revision> getByIdImpl(
      const dmap_common::Id& id, const LogicalTime& time) const final override;
  virtual void findByRevisionImpl(int key, const Revision& valueHolder,
                                  const LogicalTime& time,
                                  ConstRevisionMap* dest) const final override;
  virtual int countByRevisionImpl(int key, const Revision& valueHolder,
                                  const LogicalTime& time) const final override;
  virtual void getAvailableIdsImpl(const LogicalTime& time,
                                   std::vector<dmap_common::Id>* ids) const
      final override;
  virtual bool insertUpdatedImpl(const std::shared_ptr<Revision>& query)
      final override;
  virtual void findHistoryByRevisionImpl(int key, const Revision& valueHolder,
                                         const LogicalTime& time,
                                         HistoryMap* dest) const final override;
  virtual void chunkHistory(const dmap_common::Id& chunk_id, const LogicalTime& time,
                            HistoryMap* dest) const final override;
  virtual void itemHistoryImpl(const dmap_common::Id& id, const LogicalTime& time,
                               History* dest) const final override;
  virtual void clearImpl() final override;

  inline void forEachItemFoundAtTime(
      int key, const Revision& value_holder, const LogicalTime& time,
      const std::function<void(const dmap_common::Id& id,
                               const Revision::ConstPtr& item)>& action) const;
  inline void forChunkItemsAtTime(
      const dmap_common::Id& chunk_id, const LogicalTime& time,
      const std::function<void(const dmap_common::Id& id,
                               const Revision::ConstPtr& item)>& action) const;
  inline void trimToTime(const LogicalTime& time, HistoryMap* subject) const;

  class STXXLHistory : public std::list<CRURevisionInformation> {
   public:
    inline const_iterator latestAt(const LogicalTime& time) const {
      for (const_iterator it = cbegin(); it != cend(); ++it) {
        if (it->update_time_ <= time) {
          return it;
        }
      }
      return cend();
    }
  };
  typedef std::unordered_map<dmap_common::Id, STXXLHistory> STXXLHistoryMap;
  STXXLHistoryMap data_;

  static constexpr int kBlockSize = kSTXXLDefaultBlockSize;
  std::unique_ptr<STXXLRevisionStore<kBlockSize>> revision_store_;
};

}  // namespace dmap

#include "dmap/legacy-chunk-data-container-base-inl.h"

#endif  // DMAP_LEGACY_CHUNK_DATA_STXXL_CONTAINER_H_
