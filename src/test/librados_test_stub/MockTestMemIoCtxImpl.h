// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef LIBRADOS_TEST_STUB_MOCK_TEST_MEM_IO_CTX_IMPL_H
#define LIBRADOS_TEST_STUB_MOCK_TEST_MEM_IO_CTX_IMPL_H

#include "test/librados_test_stub/TestMemIoCtxImpl.h"
#include "gmock/gmock.h"

namespace librados {

class MockTestMemRadosClient;

class MockTestMemIoCtxImpl : public TestMemIoCtxImpl {
public:
  MockTestMemIoCtxImpl(MockTestMemRadosClient *mock_client,
                       TestMemRadosClient *client, int64_t pool_id,
                       const std::string& pool_name,
                       TestMemRadosClient::Pool *pool)
    : TestMemIoCtxImpl(client, pool_id, pool_name, pool),
      m_mock_client(mock_client), m_client(client) {
    default_to_parent();
  }

  MockTestMemRadosClient *get_mock_rados_client() {
    return m_mock_client;
  }

  virtual TestIoCtxImpl *clone() {
    TestIoCtxImpl *io_ctx_impl = new ::testing::NiceMock<MockTestMemIoCtxImpl>(
      m_mock_client, m_client, get_pool_id(), get_pool_name(), get_pool());
    io_ctx_impl->set_snap_read(get_snap_read());
    io_ctx_impl->set_snap_context(get_snap_context());
    return io_ctx_impl;
  }

  MOCK_METHOD7(exec, int(const std::string& oid,
                         TestClassHandler *handler,
                         const char *cls,
                         const char *method,
                         bufferlist& inbl,
                         bufferlist* outbl,
                         const SnapContext &snapc));
  int do_exec(const std::string& oid, TestClassHandler *handler,
              const char *cls, const char *method, bufferlist& inbl,
              bufferlist* outbl, const SnapContext &snapc) {
    return TestMemIoCtxImpl::exec(oid, handler, cls, method, inbl, outbl,
                                  snapc);
  }

  MOCK_METHOD4(read, int(const std::string& oid,
                         size_t len,
                         uint64_t off,
                         bufferlist *bl));
  int do_read(const std::string& oid, size_t len, uint64_t off,
              bufferlist *bl) {
    return TestMemIoCtxImpl::read(oid, len, off, bl);
  }

  MOCK_METHOD1(remove, int(const std::string& oid));
  int do_remove(const std::string& oid) {
    return TestMemIoCtxImpl::remove(oid);
  }

  MOCK_METHOD1(selfmanaged_snap_create, int(uint64_t *snap_id));
  int do_selfmanaged_snap_create(uint64_t *snap_id) {
    return TestMemIoCtxImpl::selfmanaged_snap_create(snap_id);
  }

  MOCK_METHOD1(selfmanaged_snap_remove, int(uint64_t snap_id));
  int do_selfmanaged_snap_remove(uint64_t snap_id) {
    return TestMemIoCtxImpl::selfmanaged_snap_remove(snap_id);
  }

  MOCK_METHOD3(write_full, int(const std::string& oid,
                               bufferlist& bl,
                               const SnapContext &snapc));
  int do_write_full(const std::string& oid, bufferlist& bl,
                    const SnapContext &snapc) {
    return TestMemIoCtxImpl::write_full(oid, bl, snapc);
  }

  void default_to_parent() {
    using namespace ::testing;

    ON_CALL(*this, exec(_, _, _, _, _, _, _)).WillByDefault(Invoke(this, &MockTestMemIoCtxImpl::do_exec));
    ON_CALL(*this, read(_, _, _, _)).WillByDefault(Invoke(this, &MockTestMemIoCtxImpl::do_read));
    ON_CALL(*this, remove(_)).WillByDefault(Invoke(this, &MockTestMemIoCtxImpl::do_remove));
    ON_CALL(*this, selfmanaged_snap_create(_)).WillByDefault(Invoke(this, &MockTestMemIoCtxImpl::do_selfmanaged_snap_create));
    ON_CALL(*this, selfmanaged_snap_remove(_)).WillByDefault(Invoke(this, &MockTestMemIoCtxImpl::do_selfmanaged_snap_remove));
    ON_CALL(*this, write_full(_, _, _)).WillByDefault(Invoke(this, &MockTestMemIoCtxImpl::do_write_full));
  }

private:
  MockTestMemRadosClient *m_mock_client;
  TestMemRadosClient *m_client;
};

} // namespace librados

#endif // LIBRADOS_TEST_STUB_MOCK_TEST_MEM_IO_CTX_IMPL_H
