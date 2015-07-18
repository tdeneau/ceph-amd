// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/RebuildObjectMapRequest.h"
#include "common/dout.h"
#include "common/errno.h"
#include "librbd/AsyncObjectThrottle.h"
#include "librbd/AsyncResizeRequest.h"
#include "librbd/AsyncTrimRequest.h"
#include "librbd/ImageCtx.h"
#include "librbd/ImageWatcher.h"
#include "librbd/internal.h"
#include "librbd/ObjectMap.h"
#include <boost/lambda/bind.hpp>
#include <boost/lambda/construct.hpp>

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::RebuildObjectMapRequest: "

namespace librbd {

namespace {

class C_VerifyObject : public C_AsyncObjectThrottle {
public:
  C_VerifyObject(AsyncObjectThrottle &throttle, ImageCtx *image_ctx,
                 uint64_t snap_id, uint64_t object_no)
    : C_AsyncObjectThrottle(throttle, *image_ctx), m_snap_id(snap_id),
      m_object_no(object_no), m_oid(m_image_ctx.get_object_name(m_object_no))
  {
    m_io_ctx.dup(m_image_ctx.md_ctx);
    m_io_ctx.snap_set_read(CEPH_SNAPDIR);
  }

  virtual void complete(int r) {
    if (should_complete(r)) {
      ldout(m_image_ctx.cct, 20) << m_oid << " C_VerifyObject completed "
                                 << dendl;
      finish(r);
      delete this;
    }
  }

  virtual int send() {
    send_list_snaps();
    return 0;
  }

private:
  librados::IoCtx m_io_ctx;
  uint64_t m_snap_id;
  uint64_t m_object_no;
  std::string m_oid;

  librados::snap_set_t m_snap_set;
  int m_snap_list_ret;

  bool should_complete(int r) {
    CephContext *cct = m_image_ctx.cct;
    if (r == 0) {
      r = m_snap_list_ret;
    }
    if (r < 0 && r != -ENOENT) {
      lderr(cct) << m_oid << " C_VerifyObject::should_complete: "
                 << "encountered an error: " << cpp_strerror(r) << dendl;
      return true;
    }

    ldout(cct, 20) << m_oid << " C_VerifyObject::should_complete: " << " r="
                   << r << dendl;
    return update_object_map(get_object_state());
  }

  void send_list_snaps() {
    assert(m_image_ctx.owner_lock.is_locked());
    ldout(m_image_ctx.cct, 5) << m_oid << " C_VerifyObject::send_list_snaps"
                              << dendl;

    librados::AioCompletion *comp = librados::Rados::aio_create_completion(
      this, NULL, rados_ctx_cb);

    librados::ObjectReadOperation op;
    op.list_snaps(&m_snap_set, &m_snap_list_ret);

    int r = m_io_ctx.aio_operate(m_oid, comp, &op, NULL);
    assert(r == 0);
    comp->release();
  }

  uint8_t get_object_state() {
    RWLock::RLocker snap_locker(m_image_ctx.snap_lock);
    for (std::vector<librados::clone_info_t>::const_iterator r =
           m_snap_set.clones.begin(); r != m_snap_set.clones.end(); ++r) {
      librados::snap_t from_snap_id;
      librados::snap_t to_snap_id;
      if (r->cloneid == librados::SNAP_HEAD) {
        from_snap_id = next_valid_snap_id(m_snap_set.seq + 1);
        to_snap_id = librados::SNAP_HEAD;
      } else {
        from_snap_id = next_valid_snap_id(r->snaps[0]);
        to_snap_id = r->snaps[r->snaps.size()-1];
      }

      if (to_snap_id < m_snap_id) {
        continue;
      } else if (m_snap_id < from_snap_id) {
        break;
      }

      if ((m_image_ctx.features & RBD_FEATURE_FAST_DIFF) != 0 &&
          from_snap_id != m_snap_id) {
        return OBJECT_EXISTS_CLEAN;
      }
      return OBJECT_EXISTS;
    }
    return OBJECT_NONEXISTENT;
  }

  uint64_t next_valid_snap_id(uint64_t snap_id) {
    assert(m_image_ctx.snap_lock.is_locked());

    std::map<librados::snap_t, SnapInfo>::iterator it =
      m_image_ctx.snap_info.lower_bound(snap_id);
    if (it == m_image_ctx.snap_info.end()) {
      return CEPH_NOSNAP;
    }
    return it->first;
  }

  bool update_object_map(uint8_t new_state) {
    RWLock::RLocker owner_locker(m_image_ctx.owner_lock);
    CephContext *cct = m_image_ctx.cct;

    // should have been canceled prior to releasing lock
    assert(!m_image_ctx.image_watcher->is_lock_supported() ||
           m_image_ctx.image_watcher->is_lock_owner());

    RWLock::WLocker l(m_image_ctx.object_map_lock);
    uint8_t state = m_image_ctx.object_map[m_object_no];
    if (state == OBJECT_EXISTS && new_state == OBJECT_NONEXISTENT &&
        m_snap_id == CEPH_NOSNAP) {
      // might be writing object to OSD concurrently
      new_state = state;
    }

    if (new_state != state) {
      ldout(cct, 15) << m_oid << " C_VerifyObject::update_object_map "
                     << static_cast<uint32_t>(state) << "->"
                     << static_cast<uint32_t>(new_state) << dendl;
      m_image_ctx.object_map[m_object_no] = new_state;
    }
    return true;
  }
};

} // anonymous namespace


void RebuildObjectMapRequest::send() {
  send_resize_object_map();
}

bool RebuildObjectMapRequest::should_complete(int r) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 5) << this << " should_complete: " << " r=" << r << dendl;

  switch (m_state) {
  case STATE_RESIZE_OBJECT_MAP:
    ldout(cct, 5) << "RESIZE_OBJECT_MAP" << dendl;
    if (r == -ESTALE && !m_attempted_trim) {
      // objects are still flagged as in-use -- delete them
      m_attempted_trim = true;
      RWLock::RLocker owner_lock(m_image_ctx.owner_lock);
      send_trim_image();
      return false;
    } else if (r == 0) {
      RWLock::RLocker owner_lock(m_image_ctx.owner_lock);
      send_verify_objects();
    }
    break;

  case STATE_TRIM_IMAGE:
    ldout(cct, 5) << "TRIM_IMAGE" << dendl;
    if (r == 0) {
      RWLock::RLocker owner_lock(m_image_ctx.owner_lock);
      send_resize_object_map();
    }
    break;

  case STATE_VERIFY_OBJECTS:
    ldout(cct, 5) << "VERIFY_OBJECTS" << dendl;
    if (r == 0) {
      assert(m_image_ctx.owner_lock.is_locked());
      send_save_object_map();
    }
    break;

  case STATE_SAVE_OBJECT_MAP:
    ldout(cct, 5) << "SAVE_OBJECT_MAP" << dendl;
    if (r == 0) {
      RWLock::RLocker owner_lock(m_image_ctx.owner_lock);
      send_update_header();
    }
    break;
  case STATE_UPDATE_HEADER:
    ldout(cct, 5) << "UPDATE_HEADER" << dendl;
    if (r == 0) {
      return true;
    }
    break;

  default:
    assert(false);
    break;
  }

  if (r < 0) {
    lderr(cct) << "rebuild object map encountered an error: " << cpp_strerror(r)
               << dendl;
    return true;
  }
  return false;
}

void RebuildObjectMapRequest::send_resize_object_map() {
  assert(m_image_ctx.owner_lock.is_locked());
  CephContext *cct = m_image_ctx.cct;

  uint64_t num_objects;
  {
    RWLock::RLocker l(m_image_ctx.snap_lock);
    uint64_t size = get_image_size();
    num_objects = Striper::get_num_objects(m_image_ctx.layout, size);
  }

  if (m_image_ctx.object_map.size() == num_objects) {
    send_verify_objects();
    return;
  }

  ldout(cct, 5) << this << " send_resize_object_map" << dendl;
  m_state = STATE_RESIZE_OBJECT_MAP;

  // should have been canceled prior to releasing lock
  assert(!m_image_ctx.image_watcher->is_lock_supported() ||
         m_image_ctx.image_watcher->is_lock_owner());
  m_image_ctx.object_map.aio_resize(num_objects, OBJECT_NONEXISTENT,
                                    create_callback_context());
}

void RebuildObjectMapRequest::send_trim_image() {
  CephContext *cct = m_image_ctx.cct;

  RWLock::RLocker l(m_image_ctx.owner_lock);

  // should have been canceled prior to releasing lock
  assert(!m_image_ctx.image_watcher->is_lock_supported() ||
         m_image_ctx.image_watcher->is_lock_owner());
  ldout(cct, 5) << this << " send_trim_image" << dendl;
  m_state = STATE_TRIM_IMAGE;

  uint64_t new_size;
  uint64_t orig_size;
  {
    RWLock::RLocker l(m_image_ctx.snap_lock);
    new_size = get_image_size();
    orig_size = m_image_ctx.get_object_size() *
                m_image_ctx.object_map.size();
  }
  AsyncTrimRequest *req = new AsyncTrimRequest(m_image_ctx,
                                               create_callback_context(),
                                               orig_size, new_size,
                                               m_prog_ctx);
  req->send();
}

void RebuildObjectMapRequest::send_verify_objects() {
  assert(m_image_ctx.owner_lock.is_locked());
  CephContext *cct = m_image_ctx.cct;

  uint64_t snap_id;
  uint64_t num_objects;
  {
    RWLock::RLocker l(m_image_ctx.snap_lock);
    snap_id = m_image_ctx.snap_id;
    num_objects = Striper::get_num_objects(m_image_ctx.layout,
                                           m_image_ctx.get_image_size(snap_id));
  }

  if (num_objects == 0) {
    send_save_object_map();
    return;
  }

  m_state = STATE_VERIFY_OBJECTS;
  ldout(cct, 5) << this << " send_verify_objects" << dendl;

  AsyncObjectThrottle::ContextFactory context_factory(
    boost::lambda::bind(boost::lambda::new_ptr<C_VerifyObject>(),
      boost::lambda::_1, &m_image_ctx, snap_id, boost::lambda::_2));
  AsyncObjectThrottle *throttle = new AsyncObjectThrottle(
    this, m_image_ctx, context_factory, create_callback_context(), &m_prog_ctx,
    0, num_objects);
  throttle->start_ops(cct->_conf->rbd_concurrent_management_ops);
}

void RebuildObjectMapRequest::send_save_object_map() {
  assert(m_image_ctx.owner_lock.is_locked());
  CephContext *cct = m_image_ctx.cct;

  ldout(cct, 5) << this << " send_save_object_map" << dendl;
  m_state = STATE_SAVE_OBJECT_MAP;

  // should have been canceled prior to releasing lock
  assert(!m_image_ctx.image_watcher->is_lock_supported() ||
         m_image_ctx.image_watcher->is_lock_owner());
  m_image_ctx.object_map.aio_save(create_callback_context());
}

void RebuildObjectMapRequest::send_update_header() {
  assert(m_image_ctx.owner_lock.is_locked());

  // should have been canceled prior to releasing lock
  assert(!m_image_ctx.image_watcher->is_lock_supported() ||
         m_image_ctx.image_watcher->is_lock_owner());

  ldout(m_image_ctx.cct, 5) << this << " send_update_header" << dendl;
  m_state = STATE_UPDATE_HEADER;

  librados::ObjectWriteOperation op;
  if (m_image_ctx.image_watcher->is_lock_supported()) {
    m_image_ctx.image_watcher->assert_header_locked(&op);
  }

  uint64_t flags = RBD_FLAG_OBJECT_MAP_INVALID | RBD_FLAG_FAST_DIFF_INVALID;
  cls_client::set_flags(&op, m_image_ctx.snap_id, 0, flags);

  librados::AioCompletion *comp = create_callback_completion();
  int r = m_image_ctx.md_ctx.aio_operate(m_image_ctx.header_oid, comp, &op);
  assert(r == 0);
  comp->release();

  RWLock::WLocker snap_locker(m_image_ctx.snap_lock);
  m_image_ctx.update_flags(m_image_ctx.snap_id, flags, false);
}

uint64_t RebuildObjectMapRequest::get_image_size() const {
  assert(m_image_ctx.snap_lock.is_locked());
  if (m_image_ctx.snap_id == CEPH_NOSNAP) {
    if (!m_image_ctx.async_resize_reqs.empty()) {
      return m_image_ctx.async_resize_reqs.front()->get_image_size();
    } else {
      return m_image_ctx.size;
    }
  }
  return  m_image_ctx.get_image_size(m_image_ctx.snap_id);
}

} // namespace librbd
