#ifndef DMAP_REVISION_MAP_H_
#define DMAP_REVISION_MAP_H_

#include <memory>
#include <unordered_map>
#include <utility>

#include <map-api-common/unique-id.h>

#include "map-api/revision.h"

namespace map_api {

template <typename ConstNonConstRevision>
class RevisionMapBase
    : public std::unordered_map<map_api_common::Id,
                                std::shared_ptr<ConstNonConstRevision>> {
 public:
  typedef std::unordered_map<
      map_api_common::Id, std::shared_ptr<ConstNonConstRevision>>
      Base;
  typedef typename Base::iterator iterator;
  typedef typename Base::const_iterator const_iterator;

  using Base::find;
  template <typename IdType>
  iterator find(const map_api_common::UniqueId<IdType>& key);
  template <typename IdType>
  const_iterator find(const map_api_common::UniqueId<IdType>& key) const;

  using Base::insert;
  std::pair<iterator, bool> insert(
      const std::shared_ptr<ConstNonConstRevision>& revision);
  template <typename IdType>
  std::pair<typename Base::iterator, bool> insert(
      const map_api_common::UniqueId<IdType>& key,
      const std::shared_ptr<ConstNonConstRevision>& revision);
};

// Using derived classes here allows forward declaration.
class ConstRevisionMap : public RevisionMapBase<const Revision> {};
class MutableRevisionMap : public RevisionMapBase<Revision> {};

}  // namespace map_api

#include "map-api/revision-map-inl.h"

#endif  // DMAP_REVISION_MAP_H_
