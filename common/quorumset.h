/***********************************************************************
 *
 * quorumset.h:
 *   utility type for tracking sets of messages received from other
 *   replicas and determining whether a quorum of responses has been met
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

#ifndef _COMMON_QUORUMSET_H_
#define _COMMON_QUORUMSET_H_

#include <map>
#include <set>

#include "lib/assert.h"

namespace dsnet {

template <class IDTYPE, class MSGTYPE>
class QuorumSet {
 public:
  QuorumSet(int numRequired) : numRequired(numRequired) {}

  void Clear() { messages.clear(); }

  void Clear(IDTYPE vs) {
    std::map<int, MSGTYPE> &vsmessages = messages[vs];
    vsmessages.clear();
  }

  int NumRequired() const { return numRequired; }

  const std::map<int, MSGTYPE> &GetMessages(IDTYPE vs) { return messages[vs]; }

  const std::map<int, MSGTYPE> *CheckForQuorum(IDTYPE vs) {
    std::map<int, MSGTYPE> &vsmessages = messages[vs];
    int count = vsmessages.size();
    if (count >= numRequired) {
      return &vsmessages;
    } else {
      return NULL;
    }
  }

  const std::map<int, MSGTYPE> *AddAndCheckForQuorum(IDTYPE vs, int replicaIdx,
                                                     const MSGTYPE &msg) {
    std::map<int, MSGTYPE> &vsmessages = messages[vs];
    if (vsmessages.find(replicaIdx) != vsmessages.end()) {
      // This is a duplicate message

      // But we'll ignore that, replace the old message from
      // this replica, and proceed.
      //
      // XXX Is this the right thing to do? It is for
      // speculative replies in SpecPaxos...
    }

    vsmessages[replicaIdx] = msg;

    return CheckForQuorum(vs);
  }

  void Add(IDTYPE vs, int replicaIdx, const MSGTYPE &msg) {
    AddAndCheckForQuorum(vs, replicaIdx, msg);
  }

 public:
  int numRequired;

 private:
  std::map<IDTYPE, std::map<int, MSGTYPE>> messages;
};

template <typename SeqNumType, typename MsgType>
class ByzantineQuorumSet {
 private:
  std::map<SeqNumType, std::map<MsgType, std::set<int>>> messages;
  int numRequired;

 public:
  ByzantineQuorumSet(int numRequired) : numRequired(numRequired) {}
  void Clear() { messages.clear(); }
  void Clear(SeqNumType seqNum) { messages[seqNum].clear(); }

 private:
  bool CheckForQuorum(SeqNumType seqNum, const MsgType &msg) {
    // should always check immediately
    Assert(messages[seqNum][msg].count() <= numRequired);
    return messages[seqNum][msg].count() == numRequired;
  }

 public:
  bool Add(SeqNumType seqNum, int replicaId, const MsgType &msg) {
    messages[seqNum][msg].insert(replicaId);
    return CheckForQuorum(seqNum, msg);
  }
};

}  // namespace dsnet

#endif  // _COMMON_QUORUMSET_H_
