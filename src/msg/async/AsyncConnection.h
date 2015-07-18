// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 UnitedStack <haomai@unitedstack.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_MSG_ASYNCCONNECTION_H
#define CEPH_MSG_ASYNCCONNECTION_H

#include <pthread.h>
#include <climits>
#include <list>
#include <map>
using namespace std;

#include "auth/AuthSessionHandler.h"
#include "common/Mutex.h"
#include "common/perf_counters.h"
#include "include/buffer.h"
#include "msg/Connection.h"
#include "msg/Messenger.h"

#include "Event.h"
#include "net_handler.h"

class AsyncMessenger;

/*
 * AsyncConnection maintains a logic session between two endpoints. In other
 * word, a pair of addresses can find the only AsyncConnection. AsyncConnection
 * will handle with network fault or read/write transactions. If one file
 * descriptor broken, AsyncConnection will maintain the message queue and
 * sequence, try to reconnect peer endpoint.
 */
class AsyncConnection : public Connection {

  int read_bulk(int fd, char *buf, int len);
  int do_sendmsg(struct msghdr &msg, int len, bool more);
  int try_send(bufferlist &bl, bool send=true) {
    Mutex::Locker l(write_lock);
    return _try_send(bl, send);
  }
  // if "send" is false, it will only append bl to send buffer
  // the main usage is avoid error happen outside messenger threads
  int _try_send(bufferlist &bl, bool send=true);
  int _send(Message *m);
  void prepare_send_message(uint64_t features, Message *m, bufferlist &bl);
  int read_until(uint64_t needed, char *p);
  int _process_connection();
  void _connect();
  void _stop();
  int handle_connect_reply(ceph_msg_connect &connect, ceph_msg_connect_reply &r);
  int handle_connect_msg(ceph_msg_connect &m, bufferlist &aubl, bufferlist &bl);
  void was_session_reset();
  void fault();
  void discard_out_queue();
  void discard_requeued_up_to(uint64_t seq);
  void requeue_sent();
  int randomize_out_seq();
  void handle_ack(uint64_t seq);
  void _send_keepalive_or_ack(bool ack=false, utime_t *t=NULL);
  int write_message(Message *m, bufferlist& bl);
  int _reply_accept(char tag, ceph_msg_connect &connect, ceph_msg_connect_reply &reply,
                    bufferlist authorizer_reply) {
    bufferlist reply_bl;
    reply.tag = tag;
    reply.features = ((uint64_t)connect.features & policy.features_supported) | policy.features_required;
    reply.authorizer_len = authorizer_reply.length();
    reply_bl.append((char*)&reply, sizeof(reply));
    if (reply.authorizer_len) {
      reply_bl.append(authorizer_reply.c_str(), authorizer_reply.length());
    }
    int r = try_send(reply_bl);
    if (r < 0)
      return -1;

    state = STATE_ACCEPTING_WAIT_CONNECT_MSG;
    return 0;
  }
  bool is_queued() {
    assert(write_lock.is_locked());
    return !out_q.empty() || outcoming_bl.length();
  }
  void shutdown_socket() {
    if (sd >= 0)
      ::shutdown(sd, SHUT_RDWR);
  }
  Message *_get_next_outgoing(bufferlist *bl) {
    assert(write_lock.is_locked());
    Message *m = 0;
    while (!m && !out_q.empty()) {
      map<int, list<pair<bufferlist, Message*> > >::reverse_iterator it = out_q.rbegin();
      if (!it->second.empty()) {
        list<pair<bufferlist, Message*> >::iterator p = it->second.begin();
        m = p->second;
        if (bl)
          bl->swap(p->first);
        it->second.erase(p);
      }
      if (it->second.empty())
        out_q.erase(it->first);
    }
    return m;
  }

 public:
  AsyncConnection(CephContext *cct, AsyncMessenger *m, EventCenter *c, PerfCounters *p);
  ~AsyncConnection();

  ostream& _conn_prefix(std::ostream *_dout);

  bool is_connected() {
    Mutex::Locker l(lock);
    return state >= STATE_OPEN && state <= STATE_OPEN_TAG_CLOSE;
  }

  // Only call when AsyncConnection first construct
  void connect(const entity_addr_t& addr, int type) {
    set_peer_type(type);
    set_peer_addr(addr);
    policy = msgr->get_policy(type);
    _connect();
  }
  // Only call when AsyncConnection first construct
  void accept(int sd);
  int send_message(Message *m);

  void send_keepalive();
  void mark_down();
  void mark_disposable() {
    Mutex::Locker l(lock);
    policy.lossy = true;
  }

 private:
  enum {
    STATE_NONE,
    STATE_OPEN,
    STATE_OPEN_KEEPALIVE2,
    STATE_OPEN_KEEPALIVE2_ACK,
    STATE_OPEN_TAG_ACK,
    STATE_OPEN_MESSAGE_HEADER,
    STATE_OPEN_MESSAGE_THROTTLE_MESSAGE,
    STATE_OPEN_MESSAGE_THROTTLE_BYTES,
    STATE_OPEN_MESSAGE_READ_FRONT,
    STATE_OPEN_MESSAGE_READ_MIDDLE,
    STATE_OPEN_MESSAGE_READ_DATA_PREPARE,
    STATE_OPEN_MESSAGE_READ_DATA,
    STATE_OPEN_MESSAGE_READ_FOOTER_AND_DISPATCH,
    STATE_OPEN_TAG_CLOSE,
    STATE_WAIT_SEND,
    STATE_CONNECTING,
    STATE_CONNECTING_WAIT_BANNER,
    STATE_CONNECTING_WAIT_IDENTIFY_PEER,
    STATE_CONNECTING_SEND_CONNECT_MSG,
    STATE_CONNECTING_WAIT_CONNECT_REPLY,
    STATE_CONNECTING_WAIT_CONNECT_REPLY_AUTH,
    STATE_CONNECTING_WAIT_ACK_SEQ,
    STATE_CONNECTING_READY,
    STATE_ACCEPTING,
    STATE_ACCEPTING_WAIT_BANNER_ADDR,
    STATE_ACCEPTING_WAIT_CONNECT_MSG,
    STATE_ACCEPTING_WAIT_CONNECT_MSG_AUTH,
    STATE_ACCEPTING_WAIT_SEQ,
    STATE_ACCEPTING_READY,
    STATE_STANDBY,
    STATE_CLOSED,
    STATE_WAIT,       // just wait for racing connection
  };

  static const int TCP_PREFETCH_MIN_SIZE;
  static const char *get_state_name(int state) {
      const char* const statenames[] = {"STATE_NONE",
                                        "STATE_OPEN",
                                        "STATE_OPEN_KEEPALIVE2",
                                        "STATE_OPEN_KEEPALIVE2_ACK",
                                        "STATE_OPEN_TAG_ACK",
                                        "STATE_OPEN_MESSAGE_HEADER",
                                        "STATE_OPEN_MESSAGE_THROTTLE_MESSAGE",
                                        "STATE_OPEN_MESSAGE_THROTTLE_BYTES",
                                        "STATE_OPEN_MESSAGE_READ_FRONT",
                                        "STATE_OPEN_MESSAGE_READ_MIDDLE",
                                        "STATE_OPEN_MESSAGE_READ_DATA_PREPARE",
                                        "STATE_OPEN_MESSAGE_READ_DATA",
                                        "STATE_OPEN_MESSAGE_READ_FOOTER_AND_DISPATCH",
                                        "STATE_OPEN_TAG_CLOSE",
                                        "STATE_WAIT_SEND",
                                        "STATE_CONNECTING",
                                        "STATE_CONNECTING_WAIT_BANNER",
                                        "STATE_CONNECTING_WAIT_IDENTIFY_PEER",
                                        "STATE_CONNECTING_SEND_CONNECT_MSG",
                                        "STATE_CONNECTING_WAIT_CONNECT_REPLY",
                                        "STATE_CONNECTING_WAIT_CONNECT_REPLY_AUTH",
                                        "STATE_CONNECTING_WAIT_ACK_SEQ",
                                        "STATE_CONNECTING_READY",
                                        "STATE_ACCEPTING",
                                        "STATE_ACCEPTING_WAIT_BANNER_ADDR",
                                        "STATE_ACCEPTING_WAIT_CONNECT_MSG",
                                        "STATE_ACCEPTING_WAIT_CONNECT_MSG_AUTH",
                                        "STATE_ACCEPTING_WAIT_SEQ",
                                        "STATE_ACCEPTING_READY",
                                        "STATE_STANDBY",
                                        "STATE_CLOSED",
                                        "STATE_WAIT"};
      return statenames[state];
  }

  AsyncMessenger *async_msgr;
  PerfCounters *logger;
  int global_seq;
  __u32 connect_seq, peer_global_seq;
  atomic_t out_seq;
  atomic_t ack_left, in_seq;
  int state;
  int state_after_send;
  int sd;
  int port;
  Messenger::Policy policy;

  Mutex write_lock;
  enum {
    NOWRITE,
    CANWRITE,
    CLOSED
  } can_write;
  bool open_write;
  map<int, list<pair<bufferlist, Message*> > > out_q;  // priority queue for outbound msgs
  list<Message*> sent; // the first bufferlist need to inject seq
  list<Message*> local_messages;    // local deliver
  bufferlist outcoming_bl;
  bool keepalive;

  Mutex lock;
  utime_t backoff;         // backoff time
  EventCallbackRef read_handler;
  EventCallbackRef write_handler;
  EventCallbackRef reset_handler;
  EventCallbackRef remote_reset_handler;
  EventCallbackRef connect_handler;
  EventCallbackRef local_deliver_handler;
  EventCallbackRef wakeup_handler;
  struct iovec msgvec[IOV_MAX];
  char *recv_buf;
  uint32_t recv_max_prefetch;
  uint32_t recv_start;
  uint32_t recv_end;
  set<uint64_t> register_time_events; // need to delete it if stop

  // Tis section are temp variables used by state transition

  // Open state
  utime_t recv_stamp;
  utime_t throttle_stamp;
  uint64_t msg_left;
  ceph_msg_header current_header;
  bufferlist data_buf;
  bufferlist::iterator data_blp;
  bufferlist front, middle, data;
  ceph_msg_connect connect_msg;
  // Connecting state
  bool got_bad_auth;
  AuthAuthorizer *authorizer;
  ceph_msg_connect_reply connect_reply;
  // Accepting state
  entity_addr_t socket_addr;
  CryptoKey session_key;
  bool replacing;    // when replacing process happened, we will reply connect
                     // side with RETRY tag and accept side will clear replaced
                     // connection. So when connect side reissue connect_msg,
                     // there won't exists conflicting connection so we use
                     // "replacing" to skip RESETSESSION to avoid detect wrong
                     // presentation
  bool is_reset_from_peer;
  bool once_ready;

  // used only for local state, it will be overwrite when state transition
  char *state_buffer;
  // used only by "read_until"
  uint64_t state_offset;
  NetHandler net;
  EventCenter *center;
  ceph::shared_ptr<AuthSessionHandler> session_security;

 public:
  // used by eventcallback
  void handle_write();
  void process();
  void wakeup_from(uint64_t id);
  void local_deliver();
  void stop() {
    lock.Lock();
    if (state != STATE_CLOSED)
      center->dispatch_event_external(reset_handler);
    lock.Unlock();
    mark_down();
  }
  void cleanup_handler() {
    read_handler.reset();
    write_handler.reset();
    reset_handler.reset();
    remote_reset_handler.reset();
    connect_handler.reset();
    local_deliver_handler.reset();
    wakeup_handler.reset();
  }
  PerfCounters *get_perf_counter() {
    return logger;
  }
}; /* AsyncConnection */

typedef boost::intrusive_ptr<AsyncConnection> AsyncConnectionRef;

#endif
