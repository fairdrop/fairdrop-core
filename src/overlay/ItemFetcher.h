#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <map>
#include <deque>
#include <functional>
#include "xdr/Stellar-SCP.h"
#include "overlay/Peer.h"
#include "util/Timer.h"
#include "util/NonCopyable.h"
#include <util/optional.h>
#include "util/HashOfHash.h"

namespace medida
{
class Counter;
}

namespace stellar
{
class TxSetFrame;
struct SCPQuorumSet;
using TxSetFramePtr = std::shared_ptr<TxSetFrame>;
using SCPQuorumSetPtr = std::shared_ptr<SCPQuorumSet>;
using AskPeer = std::function<void(Peer::pointer, Hash)>;

/**
 * @class Tracker
 *
 * Asks peers for given data set. If a peer does not have given data set,
 * asks another one. If no peer does have given data set, it starts again
 * with new set of peers (possibly overlapping, as peers may learned about
 * this data set in meantime).
 *
 * For asking a AskPeer delegate is used.
 *
 * Tracker keeps list of envelopes that requires given data set to be
 * fully resolved. When data is received each envelope is resend to Herder
 * so it can check if it has all required data and then process envelope.
 * @see listen(Peer::pointer) is used to add envelopes to that list.
 */
class Tracker
{
  private:
    AskPeer mAskPeer;

  protected:
    friend class ItemFetcher;
    Application& mApp;
    Peer::pointer mLastAskedPeer;
    int mNumListRebuild;
    std::deque<Peer::pointer> mPeersToAsk;
    VirtualTimer mTimer;
    std::vector<std::pair<Hash, SCPEnvelope>> mWaitingEnvelopes;
    Hash mItemHash;
    medida::Meter& mTryNextPeerReset;
    medida::Meter& mTryNextPeer;

    /**
     * Called periodically to remove old envelopes from list (with ledger id
     * below some @p slotIndex).
     *
     * Returns true if at least one envelope remained in list.
     */
    bool clearEnvelopesBelow(uint64 slotIndex);

    /**
     * Add @p env to list of envelopes that will be resend to Herder when data
     * is received.
     */
    void listen(const SCPEnvelope& env);

    /**
     * Called when given @p peer informs that it does not have given data.
     * Next peer will be tried if available.
     */
    void doesntHave(Peer::pointer peer);

    /**
     * Called either when @see doesntHave(Peer::pointer) was received or
     * request to peer timed out.
     */
    void tryNextPeer();

  public:
    /**
     * Create Tracker that tracks data identified by @p hash. @p askPeer
     * delegate is used to fetch the data.
     */
    explicit Tracker(Application& app, Hash const& hash, AskPeer &askPeer);
    virtual ~Tracker();

    /**
     * Return true if any data is
     */
    bool hasWaitingEnvelopes() const { return mWaitingEnvelopes.size() > 0; }
};

/**
 * @class ItemFetcher
 *
 * Manages asking for Transaction or Quorum sets from Peers
 *
 * The ItemFetcher keeps instances of the Tracker class. There exists exactly
 * one Tracker per item. The tracker is used to maintain the state of the
 * search.
 */
class ItemFetcher : private NonMovableOrCopyable
{
  public:
    using TrackerPtr = std::shared_ptr<Tracker>;

    /**
     * Create ItemFetcher that fetches data using @p askPeer delegate.
     */
    explicit ItemFetcher(Application& app, AskPeer askPeer);

    /**
     * Fetch data identified by @p hash and needed by @p envelope. Multiple
     * envelopes may require one set of data.
     */
    void fetch(Hash itemHash, const SCPEnvelope& envelope);

    /**
     * Check if data identified by @p hash is currently being fetched.
     */
    bool isFetching(Hash itemHash) const;

    /**
     * Called periodically to remove old envelopes from list (with ledger id
     * below some @p slotIndex). Can also remove @see Tracker instances when
     * non needed anymore.
     */
    void stopFetchingBelow(uint64 slotIndex);

    /**
     * Called when given @p peer informs that it does not have data identified
     * by @p itemHash.
     */
    void doesntHave(Hash const& itemHash, Peer::pointer peer);

    /**
     * Called when data with given @p itemHash was received. All envelopes
     * added before with @see fetch and the same @p itemHash will be resent
     * to Herder, matching @see Tracker will be cleaned up.
     */
    void recv(Hash itemHash);

  protected:
    void stopFetchingBelowInternal(uint64 slotIndex);

    Application& mApp;
    std::map<Hash, std::shared_ptr<Tracker>> mTrackers;

    // NB: There are many ItemFetchers in the system at once, but we are sharing
    // a single counter for all the items being fetched by all of them. Be
    // careful, therefore, to only increment and decrement this counter, not set
    // it absolutely.
    medida::Counter& mItemMapSize;

  private:
    AskPeer mAskPeer;
};

}
