
#include "include/rados/librados.hpp"
#include "mds/mdstypes.h"

#include "cls_cephfs.h"

class AccumulateArgs;

class ClsCephFSClient
{
  public:
  static int accumulate_inode_metadata(
      librados::IoCtx &ctx,
      inodeno_t inode_no,
      const uint64_t obj_index,
      const uint64_t obj_size,
      const time_t mtime);

  static int fetch_inode_accumulate_result(
      librados::IoCtx &ctx,
      const std::string &oid,
      inode_backtrace_t *backtrace,
      ceph_file_layout *layout,
      AccumulateResult *result);
};

