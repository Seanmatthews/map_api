#ifndef MAP_API_BENCHMARKS_MULTI_KMEANS_HOARDER_H_
#define MAP_API_BENCHMARKS_MULTI_KMEANS_HOARDER_H_

#include <vector>

#include <map-api/id.h>

#include "map_api_benchmarks/common.h"

namespace map_api {
namespace benchmarks {

class MultiKmeansHoarder {
 public:
  /**
   * Commits the supplied data as initial state to the Map API, and displays
   * it with gnuplot
   */
  void init(
      const DescriptorVector& descriptors, const DescriptorVector& centers,
      const std::vector<unsigned int>& membership, const Scalar area_width,
      map_api::Id* data_chunk_id, map_api::Id* center_chunk_id,
      map_api::Id* membership_chunk_id);
  /**
   * Refreshes the latest Map API data to gnuplot
   */
  void refresh();
 private:
  FILE* gnuplot_;
};

} /* namespace benchmarks */
} /* namespace map_api */

#endif /* MAP_API_BENCHMARKS_MULTI_KMEANS_HOARDER_H_ */