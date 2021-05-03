// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * simtransport.cc:
 *   simulated message-passing interface for testing use
 *
 * Copyright 2013 Dan R. K. Ports  <drkp@cs.washington.edu>
 *                Jialin Li        <lijl@cs.washington.edu>
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

#include "lib/assert.h"
#include "lib/message.h"
#include "lib/simtransport.h"
#include <google/protobuf/message.h>

SimulatedTransportAddress::SimulatedTransportAddress(int addr)
    : addr(addr)
{

}

int
SimulatedTransportAddress::GetAddr() const
{
    return addr;
}

SimulatedTransportAddress *
SimulatedTransportAddress::clone() const
{
    SimulatedTransportAddress *c = new SimulatedTransportAddress(addr);
    return c;
}

std::string
SimulatedTransportAddress::Serialize() const
{
    return "";
}

bool
SimulatedTransportAddress::operator==(const SimulatedTransportAddress &other) const
{
    return addr == other.addr;
}

SimulatedTransport::SimulatedTransport(bool continuous)
    : continuous(continuous)
{
    lastAddr = -1;
    lastTimerId = 0;
    vtime = 0;
    processTimers = true;
    fcAddress = -1;

    sequencerID = 0;
    running = false;
}

SimulatedTransport::~SimulatedTransport()
{

}

void
SimulatedTransport::RegisterInternal(TransportReceiver *receiver,
                                     const specpaxos::ReplicaAddress *addr,
                                     int groupIdx, int replicaIdx)
{
    // Allocate an endpoint
    ++lastAddr;
    int saddr = lastAddr;
    endpoints[saddr] = receiver;

    // Store address for future lookups
    if (addr != nullptr) {
        addrLookupMap.insert(std::make_pair(*addr, SimulatedTransportAddress(saddr)));
    }
    // Tell the receiver its address
    receiver->SetAddress(new SimulatedTransportAddress(saddr));
    // If this is registered as a replica, record the index
    replicaIdxs[saddr] = std::make_pair(groupIdx, replicaIdx);
}

bool
SimulatedTransport::SendMessageInternal(TransportReceiver *src,
                                        const SimulatedTransportAddress &dstAddr,
                                        const Message &m)
{
    multistamp_t stamp;
    return _SendMessageInternal(src, dstAddr, m, stamp);
}

bool
SimulatedTransport::SendBuffer(TransportReceiver *src, const TransportAddress &dst,
                               const void *buf, size_t len)
{
    Panic("SendBuffer not implemented for SimulatedTransport");
}

bool
SimulatedTransport::_SendMessageInternal(TransportReceiver *src,
                                         const SimulatedTransportAddress &dstAddr,
                                         const Message &m,
                                         const multistamp_t &stamp)
{
    int dst = dstAddr.addr;
    if (dst < 0) {
        Panic("Sending message to unknwon address");
    }

    Message *msg = m.New();
    msg->CheckTypeAndMergeFrom(m);

    int srcAddr =
        dynamic_cast<const SimulatedTransportAddress &>(src->GetAddress()).addr;

    uint64_t delay = 0;
    for (auto f : filters) {
        if (!f.second(src, replicaIdxs[srcAddr],
                      endpoints[dst], replicaIdxs[dst],
                      *msg, delay, stamp)) {
            // Message dropped by filter
            // XXX Should we return failure?
            delete msg;
            return true;
        }
    }

    string msgData;
    msg->SerializeToString(&msgData);
    delete msg;

    QueuedMessage q(dst, srcAddr, m.GetTypeName(), msgData, stamp);

    if (delay == 0) {
        queue.push_back(q);
    } else {
        Timer(delay, [ = ]() {
            queue.push_back(q);
        });
    }
    return true;
}

bool
SimulatedTransport::OrderedMulticast(TransportReceiver *src,
                                     const std::vector<int> &groups,
                                     const Message &m)
{
    multistamp_t stamp;
    stamp.sessnum = this->sequencerID;
    for (int groupIdx : groups) {
        if (this->noCounters.find(groupIdx) == this->noCounters.end()) {
            this->noCounters[groupIdx] = 0;
        }
        this->noCounters[groupIdx]++;
        stamp.seqnums[groupIdx] = this->noCounters[groupIdx];
    }

    const specpaxos::Configuration *cfg = this->configurations[src];
    ASSERT(cfg != NULL);

    if (!this->replicaAddressesInitialized) {
        LookupAddresses();
    }

    const SimulatedTransportAddress &srcAddr = dynamic_cast<const SimulatedTransportAddress &>(src->GetAddress());
    for (int groupIdx : groups) {
        for (auto &kv : this->replicaAddresses[cfg][groupIdx]) {
            if (srcAddr == kv.second) {
                continue;
            }
            if (!_SendMessageInternal(src, kv.second, m, stamp)) {
                return false;
            }
        }
    }
    return true;
}

SimulatedTransportAddress
SimulatedTransport::LookupAddress(const specpaxos::ReplicaAddress &addr)
{
    if (addrLookupMap.find(addr) != addrLookupMap.end()) {
        return addrLookupMap.at(addr);
    }

    Warning("No address with host %s port %s was registered",
            addr.host.c_str(), addr.port.c_str());
    return SimulatedTransportAddress(-1);
}

void
SimulatedTransport::Run()
{
    LookupAddresses();
    running = true;

    do {
        // Process queue
        while (!queue.empty()) {
            QueuedMessage &q = queue.front();
            TransportReceiver *dst = endpoints[q.dst];
            dst->ReceiveMessage(SimulatedTransportAddress(q.src),
                                q.type,
                                q.msg,
                                &(q.stamp));
            queue.pop_front();
        }

        // If there's a timer, deliver the earliest one only
        if (processTimers) {
            this->timersLock.lock();
            if (!timers.empty()) {
                auto iter = timers.begin();
                ASSERT(iter->second.when >= vtime);
                vtime = iter->second.when;
                timer_callback_t cb = iter->second.cb;
                timers.erase(iter);
                this->timersLock.unlock();
                cb();
            } else {
                this->timersLock.unlock();
            }
        }

        // ...then retry to see if there are more queued messages to
        // deliver first
    } while (continuous ?
             running :
             (!queue.empty() || (processTimers && !timers.empty())));
    running = false;
}

void
SimulatedTransport::Stop()
{
    running = false;
}

void
SimulatedTransport::AddFilter(int id, filter_t filter)
{
    filters.insert(std::pair<int, filter_t>(id, filter));
}

void
SimulatedTransport::RemoveFilter(int id)
{
    filters.erase(id);
}


int
SimulatedTransport::Timer(uint64_t ms, timer_callback_t cb)
{
    std::lock_guard<std::mutex> l(this->timersLock);
    ++lastTimerId;
    int id = lastTimerId;
    PendingTimer t;
    t.when = vtime + ms;
    t.cb = cb;
    t.id = id;
    timers.insert(std::pair<uint64_t, PendingTimer>(t.when, t));
    return id;
}

bool
SimulatedTransport::CancelTimer(int id)
{
    std::lock_guard<std::mutex> l(this->timersLock);
    bool found = false;
    for (auto iter = timers.begin(); iter != timers.end();) {
        if (iter->second.id == id) {
            found = true;
            iter = timers.erase(iter);
        } else {
            iter++;
        }
    }

    return found;
}

void
SimulatedTransport::CancelAllTimers()
{
    timers.clear();
    processTimers = false;
}

void
SimulatedTransport::SessionChange()
{
    ++this->sequencerID;
    noCounters.clear();
}
