/***********************************************************************
 *
 * pbft/replica.h:
 *   PBFT protocol replica
 *   This is only a fast-path performance-equivalent implmentation. Noticable
 *   missing parts include recovery, resending and Byzantine
 *
 * Copyright 2013 Dan R. K. Ports  <drkp@cs.washington.edu>
 * Copyright 2021 Sun Guangda      <sung@comp.nus.edu.sg>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#ifndef _PBFT_REPLICA_H_
#define _PBFT_REPLICA_H_

#include "common/log.h"
#include "common/replica.h"
#include "lib/signature.h"
#include "replication/pbft/pbft-proto.pb.h"

namespace dsnet {
namespace pbft {

class PbftReplica : public Replica {
 public:
  PbftReplica(Configuration config, int myIdx, bool initialize,
              Transport *transport, AppReplica *app);
  void ReceiveMessage(const TransportAddress &remote, const string &type,
                      const string &data, void *meta_data) override;

 private:
  // fundamental
  Signer signer;
  Verifier verifier;

  // message handlers
  void HandleRequest(const TransportAddress &remote,
                     const proto::RequestMessage &msg);
  void HandleUnloggedRequest(const TransportAddress &remote,
                             const proto::UnloggedRequestMessage &msg);
  void HandlePrePrepare(const TransportAddress &remote,
                        const proto::PrePrepareMessage &msg);
  void HandlePrepare(const TransportAddress &remote,
                     const proto::PrepareMessage &msg);
  void HandleCommit(const TransportAddress &remote,
                    const proto::CommitMessage &msg);

  // timers and timeout handlers
  // Timeout *viewChangeTimeout;
  // void OnViewChange();

  // states and utils
  view_t view;
  opnum_t seqNum;           // only primary use this
  bool AmPrimary() const {  // following PBFT paper terminology
    return replicaIdx == configuration.GetLeaderIndex(view);
  };

  Log log;
  // key is seqNum, tables clean up when view changed
  std::map<uint64_t, proto::PrePrepareMessage> loggedPrePrepareMessageTable;
  std::map<uint64_t, proto::PrepareMessage> loggedPrepareMessageTable;
  std::map<uint64_t, std::set<int>> loggedPrepareReplicaTable;
  std::map<uint64_t, proto::CommitMessage> loggedCommitMessageTable;
  std::map<uint64_t, std::set<int>> loggedCommitReplicaTable;
  // prepared(m, v, n, i) where m (message), v(view) and i(replica index) should
  // be fixed for each calling
  bool prepared(opnum_t seqNum) {
    // omit match PrePrepare with Prepare, done when handling them
    return loggedPrePrepareMessageTable.count(seqNum) &&
           // assert seqNum in table
           loggedPrepareReplicaTable[seqNum].size() >= 2u * configuration.f;
  }
  void BroadcastCommitIfPrepared(opnum_t seqNum);
  // similar to prepared
  bool committedLocal(opnum_t seqNum) {
    return prepared(seqNum) &&
           loggedCommitReplicaTable[seqNum].size() >= 2u * configuration.f + 1;
  }
  void ExecuteAvailable();

  struct ClientTableEntry {
    uint64_t lastReqId;
    proto::ReplyMessage reply;
  };
  std::map<uint64_t, ClientTableEntry> clientTable;
  void UpdateClientTable(const Request &req, const proto::ReplyMessage &reply);
};

}  // namespace pbft
}  // namespace dsnet

#endif /* _PBFT_REPLICA_H_ */
