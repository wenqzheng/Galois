/*
 * This file belongs to the Galois project, a C++ library for exploiting parallelism.
 * The code is being released under the terms of the 3-Clause BSD License (a
 * copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2018, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 */

/**
 * @file DistributedGraph_CartesianCut.h
 *
 * Implements the cartesian cut partitioning scheme for DistGraph.
 */
#ifndef _GALOIS_DIST_HGRAPHCC_H
#define _GALOIS_DIST_HGRAPHCC_H

#define PHASE_BREAKDOWN 0

#include "galois/graphs/DistributedGraph.h"

namespace galois {
namespace graphs {

/**
 * Distributed graph class that implements a cartesian vertex cut as well
 * as a checkboard vertex cut.
 *
 * @tparam NodeTy type of node data for the graph
 * @tparam EdgeTy type of edge data for the graph
 * @tparam moreColumnHosts If true, swaps the number of rows and columns
 * block into. For example, if 2, then each block is decomposed into 2 more
 * columns and 2 more rows from normal
 */
template <typename NodeTy, typename EdgeTy, bool moreColumnHosts = false>
class DistGraphCartesianCut : public DistGraph<NodeTy, EdgeTy> {
  constexpr static const char* const GRNAME = "dGraph_cartesianCut";
  //! Vector of Uint64 Vectors
  using VectorOfVector64 = galois::gstl::Vector<galois::gstl::Vector<uint64_t>>;

public:
  //! @copydoc DistGraphEdgeCut::base_DistGraph
  using base_DistGraph = DistGraph<NodeTy, EdgeTy>;

private:
  unsigned numRowHosts;
  unsigned numColumnHosts;

  //! Factorize numHosts into rows and columns such that difference between
  //! factors is minimized
  void factorizeHosts() {
    numColumnHosts  = sqrt(base_DistGraph::numHosts);

    while ((base_DistGraph::numHosts % numColumnHosts) != 0)
      numColumnHosts--;

    numRowHosts = base_DistGraph::numHosts / numColumnHosts;
    assert(numRowHosts >= numColumnHosts);

    if (moreColumnHosts) {
      std::swap(numRowHosts, numColumnHosts);
    }

    if (base_DistGraph::id == 0) {
      galois::gPrint("Cartesian grid: ", numRowHosts, " x ", numColumnHosts,
                     "\n");
    }
  }

  //! Returns the grid row ID of this host
  unsigned gridRowID() const { return (base_DistGraph::id / numColumnHosts); }

  //! Returns the grid row ID of the specified host
  unsigned gridRowID(unsigned id) const { return (id / numColumnHosts); }

  //! Returns the grid column ID of this host
  unsigned gridColumnID() const {
    return (base_DistGraph::id % numColumnHosts);
  }

  //! Returns the grid column ID of the specified host
  unsigned gridColumnID(unsigned id) const {
    return (id % numColumnHosts);
  }

  //! Find the column of a particular node
  unsigned getColumnOfNode(uint64_t gid) const {
    assert(gid < base_DistGraph::numGlobalNodes);
    return gridColumnID(getHostID(gid));
  }

  //! gets the index of a particular node in that node's column
  uint32_t getColumnIndexOfNode(uint64_t gid) const {
    assert(gid < base_DistGraph::numGlobalNodes);
    auto hostID          = getHostID(gid);
    auto c               = gridColumnID(hostID);
    uint32_t columnIndex = 0;

    // loop through all hosts up to this current host
    for (auto h = 0U; h <= hostID; ++h) {
      // only consider hosts that belong to same column
      if (gridColumnID(h) == c) {
        uint64_t start, end;
        std::tie(start, end) = base_DistGraph::gid2host[h];
        if (gid < end) {
          // add nodes up to the node we want to consider
          columnIndex += gid - start;
          break; // break to escape redundant computation
        } else {
          // count all nodes in this host and add to running sum
          columnIndex += end - start;
        }
      }
    }

    return columnIndex;
  }

  //! Returns true if this host has nothing to send to the specified host
  //! given a particular communication pattern
  bool isNotCommunicationPartner(unsigned host,
                                 typename base_DistGraph::SyncType syncType,
                                 WriteLocation writeLocation,
                                 ReadLocation readLocation) {
    if (base_DistGraph::transposed) {
      if (syncType == base_DistGraph::syncReduce) {
        switch (writeLocation) {
        case writeSource:
          return (gridColumnID() != gridColumnID(host));
        case writeDestination:
          return (gridRowID() != gridRowID(host));
        case writeAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      } else { // syncBroadcast
        switch (readLocation) {
        case readSource:
          if (base_DistGraph::currentBVFlag != nullptr) {
            galois::runtime::make_src_invalid(base_DistGraph::currentBVFlag);
          }

          return (gridColumnID() != gridColumnID(host));
        case readDestination:
          if (base_DistGraph::currentBVFlag != nullptr) {
            galois::runtime::make_dst_invalid(base_DistGraph::currentBVFlag);
          }

          return (gridRowID() != gridRowID(host));
        case readAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      }
    } else {
      if (syncType == base_DistGraph::syncReduce) {
        switch (writeLocation) {
        case writeSource:
          return (gridRowID() != gridRowID(host));
        case writeDestination:
          return (gridColumnID() != gridColumnID(host));
        case writeAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      } else { // syncBroadcast
        switch (readLocation) {
        case readSource:
          if (base_DistGraph::currentBVFlag != nullptr) {
            galois::runtime::make_dst_invalid(base_DistGraph::currentBVFlag);
          }

          return (gridRowID() != gridRowID(host));
        case readDestination:
          if (base_DistGraph::currentBVFlag != nullptr) {
            galois::runtime::make_src_invalid(base_DistGraph::currentBVFlag);
          }

          return (gridColumnID() != gridColumnID(host));
        case readAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      }
    }
    return false;
  }

public:
  //! GID of node = localToGlobalVector[LID]
  std::vector<uint64_t> localToGlobalVector; // TODO use LargeArray instead
  //! LID of node = globalToLocalMap[GID]
  std::unordered_map<uint64_t, uint32_t> globalToLocalMap;

  //! number of nodes on local to this host
  uint32_t numNodes;
  //! number of edges on local to this host
  uint64_t numEdges;

  //! @copydoc DistGraphEdgeCut::getHostID
  unsigned getHostID(uint64_t gid) const {
    assert(gid < base_DistGraph::numGlobalNodes);
    // for (auto h = 0U; h < base_DistGraph::numHosts; ++h) {
    for (auto h = 0U; h < base_DistGraph::numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = base_DistGraph::gid2host[h];
      if (gid >= start && gid < end) {
        return h;
      }
    }
    assert(false);
    return base_DistGraph::numHosts;
  }

  //! @copydoc DistGraphEdgeCut::isOwned
  bool isOwned(uint64_t gid) const {
    uint64_t start, end;
    std::tie(start, end) = base_DistGraph::gid2host[base_DistGraph::id];

    if (gid >= start && gid < end) return true;
    return false;
  }

  //! @copydoc DistGraphEdgeCut::isLocal
  virtual bool isLocal(uint64_t gid) const {
    assert(gid < base_DistGraph::numGlobalNodes);
    if (isOwned(gid))
      return true;
    return (globalToLocalMap.find(gid) != globalToLocalMap.end());
  }

  //! @copydoc DistGraphEdgeCut::G2L
  virtual uint32_t G2L(uint64_t gid) const {
    assert(isLocal(gid));
    return globalToLocalMap.at(gid);
  }

  //! @copydoc DistGraphEdgeCut::L2G
  virtual uint64_t L2G(uint32_t lid) const { return localToGlobalVector[lid]; }

  // requirement: for all X and Y,
  // On X, nothingToSend(Y) <=> On Y, nothingToRecv(X)
  // Note: templates may not be virtual, so passing types as arguments
  virtual bool nothingToSend(unsigned host,
                             typename base_DistGraph::SyncType syncType,
                             WriteLocation writeLocation,
                             ReadLocation readLocation) {
    auto& sharedNodes = (syncType == base_DistGraph::syncReduce)
                            ? base_DistGraph::mirrorNodes
                            : base_DistGraph::masterNodes;

    if (sharedNodes[host].size() > 0) {
      return isNotCommunicationPartner(host, syncType, writeLocation,
                                       readLocation);
    }

    return true;
  }

  virtual bool nothingToRecv(unsigned host,
                             typename base_DistGraph::SyncType syncType,
                             WriteLocation writeLocation,
                             ReadLocation readLocation) {
    auto& sharedNodes = (syncType == base_DistGraph::syncReduce)
                            ? base_DistGraph::masterNodes
                            : base_DistGraph::mirrorNodes;

    if (sharedNodes[host].size() > 0) {
      return isNotCommunicationPartner(host, syncType, writeLocation,
                                       readLocation);
    }

    return true;
  }

  /**
   * Constructor for cartesian cut.
   *
   * @param filename Graph file to read
   * @param host the host id of the caller
   * @param _numHosts total number of hosts in the system
   * @param scalefactor Specifies if certain hosts should get more nodes
   * than others
   * @param transpose true if graph being read needs to have an in-memory
   * transpose done after reading
   * @param readFromFile true if you want to read the local graph from a file
   * @param localGraphFileName the local file to read if readFromFile is set
   * to true
   *
   * @todo get rid of string argument (2nd one)
   */
  DistGraphCartesianCut(const std::string& filename, const std::string&,
                        unsigned host, unsigned _numHosts,
                        std::vector<unsigned>& scalefactor,
                        bool transpose = false, bool readFromFile = false,
                        std::string localGraphFileName = "local_graph")
      : base_DistGraph(host, _numHosts) {
    galois::CondStatTimer<MORE_DIST_STATS> Tgraph_construct(
        "GraphPartitioningTime", GRNAME);

    Tgraph_construct.start();

    if (readFromFile) {
      galois::gPrint("[", base_DistGraph::id,
                     "] Reading local graph from file : ",
                     localGraphFileName, "\n");
      base_DistGraph::read_local_graph_from_file(localGraphFileName);
      Tgraph_construct.stop();
      return;
    }

    // only used to determine node splits among hosts; abandonded later
    // for the BufferedGraph
    galois::graphs::OfflineGraph g(filename);

    base_DistGraph::numGlobalNodes = g.size();
    base_DistGraph::numGlobalEdges = g.sizeEdges();

    factorizeHosts();

    base_DistGraph::computeMasters(g, scalefactor, false);

    // at this point gid2Host has pairs for how to split nodes among
    // hosts; pair has begin and end
    uint64_t nodeBegin = base_DistGraph::gid2host[base_DistGraph::id].first;
    uint64_t nodeEnd = base_DistGraph::gid2host[base_DistGraph::id].second;

    typename galois::graphs::OfflineGraph::edge_iterator edgeBegin;
    edgeBegin = g.edge_begin(nodeBegin);
    typename galois::graphs::OfflineGraph::edge_iterator edgeEnd;
    edgeEnd   = g.edge_begin(nodeEnd);

    galois::Timer inspectionTimer;
    galois::CondStatTimer<PHASE_BREAKDOWN> loadStatTimer("LoadStats", GRNAME);
    galois::CondStatTimer<PHASE_BREAKDOWN> loadGraphTimer("LoadPartialGraph", GRNAME);

    inspectionTimer.start();

    // graph that loads assigned region into memory
    galois::graphs::BufferedGraph<EdgeTy> bufGraph;

    loadGraphTimer.start();
    bufGraph.loadPartialGraph(
        filename, nodeBegin, nodeEnd, *edgeBegin, *edgeEnd,
        base_DistGraph::numGlobalNodes, base_DistGraph::numGlobalEdges);
    loadGraphTimer.stop();

    std::vector<uint64_t> prefixSumOfEdges;

    loadStatTimer.start();
    // first pass of the graph file
    loadStatistics(bufGraph, prefixSumOfEdges, inspectionTimer);
    loadStatTimer.stop();

    // allocate memory for our underlying graph representation
    base_DistGraph::graph.allocateFrom(numNodes, numEdges);

    assert(prefixSumOfEdges.size() == numNodes);

    if (numNodes > 0) {
      base_DistGraph::graph.constructNodes();

      auto& base_graph = base_DistGraph::graph;
      galois::do_all(
          galois::iterate((uint32_t)0, numNodes),
          [&](auto n) { base_graph.fixEndEdge(n, prefixSumOfEdges[n]); },
#if MORE_DIST_STATS
          galois::loopname("ConstructionEndEdgeFix"),
#endif
          galois::no_stats());
    }

    if (base_DistGraph::numOwned != 0) {
      base_DistGraph::beginMaster =
          G2L(base_DistGraph::gid2host[base_DistGraph::id].first);
    } else {
      // no owned nodes; therefore, empty masters
      base_DistGraph::beginMaster = 0;
    }

    base_DistGraph::printStatistics();

    // second pass of the graph file
    loadEdges(base_DistGraph::graph, bufGraph);

    // reclaim memory from buffered graphs
    bufGraph.resetAndFree();

    if (transpose) {
      // consider all nodes to have outgoing edges
      // TODO: renumber nodes so that all nodes with outgoing edges are at the beginning?
      base_DistGraph::numNodesWithEdges = numNodes;
      base_DistGraph::graph.transpose(GRNAME);
      base_DistGraph::transposed = true;
    }

    fillMirrorNodes(base_DistGraph::mirrorNodes);

    galois::CondStatTimer<MORE_DIST_STATS> Tthread_ranges("ThreadRangesTime",
                                                          GRNAME);
    Tthread_ranges.start();
    base_DistGraph::determineThreadRanges();
    Tthread_ranges.stop();

    base_DistGraph::determineThreadRangesMaster();
    base_DistGraph::determineThreadRangesWithEdges();
    base_DistGraph::initializeSpecificRanges();

    Tgraph_construct.stop();

    galois::CondStatTimer<MORE_DIST_STATS> Tgraph_construct_comm(
        "GraphCommSetupTime", GRNAME);
    Tgraph_construct_comm.start();
    base_DistGraph::setup_communication();
    Tgraph_construct_comm.stop();
  }

private:
  /**
   * Create metadata containers for which local nodes have incoming edges as
   * well as a counter for how many local edges nodes have.
   */
  void inOutMetadataInitialization(
    std::vector<galois::DynamicBitSet>& hasIncomingEdge,
    VectorOfVector64& numOutgoingEdges
  ) {
    // create bitmaps to mark which nodes have incoming edges for each column
    // of hosts
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      uint64_t columnBlockSize = 0;
      // count number of vertices in a column
      for (auto h = 0U; h < base_DistGraph::numHosts; ++h) {
        if (gridColumnID(h) == i) {
          uint64_t start, end;
          std::tie(start, end) = base_DistGraph::gid2host[h];
          columnBlockSize += end - start;
        }
      }
      // resize bitset representing column to correct number of vertices
      hasIncomingEdge[i].resize(columnBlockSize);
      hasIncomingEdge[i].reset();
    }

    // create vectors specifying how many outgoing edges will be sent out
    // to particular columns from THIS host
    numOutgoingEdges.resize(numColumnHosts);
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      numOutgoingEdges[i].assign(
          (base_DistGraph::gid2host[base_DistGraph::id].second -
           base_DistGraph::gid2host[base_DistGraph::id].first),
          0);
    }
  }

  /**
   * Loop through this host's edges and tally which columns will get incoming
   * edges from this host + count the number of outgoing edges each node in
   * each column will have
   */
  void edgeInspection(
    galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
    std::vector<galois::DynamicBitSet>& hasIncomingEdge,
    VectorOfVector64& numOutgoingEdges,
    galois::Timer& inspectionTimer
  ) {
    bufGraph.resetReadCounters();
    uint64_t rowOffset = base_DistGraph::gid2host[base_DistGraph::id].first;

    galois::do_all(
        galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                        base_DistGraph::gid2host[base_DistGraph::id].second),
        [&](auto src) {
          auto ii = bufGraph.edgeBegin(src);
          auto ee = bufGraph.edgeEnd(src);
          for (; ii < ee; ++ii) {
            auto dst = bufGraph.edgeDestination(*ii);
            auto c   = this->getColumnOfNode(dst);

            hasIncomingEdge[c].set(this->getColumnIndexOfNode(dst));
            numOutgoingEdges[c][src - rowOffset]++;
          }
        },
#if MORE_DIST_STATS
        galois::loopname("AssignEdges"),
#endif
        galois::steal(),
        galois::no_stats());

    inspectionTimer.stop();

    uint64_t allBytesRead = bufGraph.getBytesRead();

    galois::gPrint(
        "[", base_DistGraph::id,
        "] Edge inspection time: ", inspectionTimer.get_usec() / 1000000.0f,
        " seconds to read ", allBytesRead, " bytes (",
        allBytesRead / (float)inspectionTimer.get_usec(), " MBPS)\n");
  }

  /**
   * Communicate the metadata concerning local nodes/edges to other hosts which
   * will eventually receive these local nodes/edges
   */
  void communicateColumnMetadata(
    std::vector<galois::DynamicBitSet>& hasIncomingEdge,
    VectorOfVector64& numOutgoingEdges
  ) {
    auto& net = galois::runtime::getSystemNetworkInterface();
    // from this column, send data to hosts in same row as this one
    // for hosts r1..rk on some row, host r1 gets column 1, host r2 gets
    // column 2, etc.

    galois::GAccumulator<uint64_t> bytesSent;
    bytesSent.reset();

    for (unsigned col = 0; col < numColumnHosts; ++col) {
      unsigned h = (gridRowID() * numColumnHosts) + col;
      if (h == base_DistGraph::id) continue;
      galois::runtime::SendBuffer buf;
      galois::runtime::gSerialize(buf, numOutgoingEdges[col]);
      galois::runtime::gSerialize(buf, hasIncomingEdge[col]);
      bytesSent.update(buf.size());
      net.sendTagged(h, galois::runtime::evilPhase, buf);
    }
    net.flush();

    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeInspectionBytesSent"), bytesSent.reduce()
    );


    // receive from other columns in this row: get info on nodes that this
    // host will receive (note it overwrites old info; that info has already
    // been sent out, so it can be overwritten safely)
    for (unsigned i = 1; i < numColumnHosts; ++i) {
      decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
      do {
        p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
      } while (!p);
      unsigned col = p->first % numColumnHosts;
      auto& buf    = p->second;
      galois::runtime::gDeserialize(buf, numOutgoingEdges[col]);
      galois::runtime::gDeserialize(buf, hasIncomingEdge[col]);
    }
    base_DistGraph::increment_evilPhase();

    // merge all received bitsets into 1
    for (unsigned i = 1; i < numColumnHosts; ++i) {
      hasIncomingEdge[0].bitwise_or(hasIncomingEdge[i]);
    }
  }

  /**
   * Create metadata of all master nodes on this host.
   */
  void inspectLocalNodes(VectorOfVector64& numOutgoingEdges,
                        std::vector<uint64_t>& prefixSumOfEdges) {
    galois::gDebug("Creating local node mapping");
    unsigned hostID      = base_DistGraph::id;
    uint64_t startSrc    = base_DistGraph::gid2host[hostID].first;
    unsigned myColumn    = gridColumnID();
    uint32_t myNodeCount = numOutgoingEdges[myColumn].size();

    // resize vectors to appropriate size; assumes they are currently empty
    prefixSumOfEdges.resize(myNodeCount);
    localToGlobalVector.resize(myNodeCount);

    // parallel local mapping, prefix sum calc
    galois::do_all(
      galois::iterate(0u, myNodeCount),
      [&] (uint32_t node) {
        uint64_t myEdges = numOutgoingEdges[myColumn][node];
        localToGlobalVector[node] = startSrc + node;
        prefixSumOfEdges[node] = myEdges;
      },
      galois::loopname("InspectLocalNodesLoop"),
      galois::no_stats()
    );

    numNodes += myNodeCount;
  }


  /**
   * Create metadata of the source nodes of edges that we are responsible for.
   */
  void inspectOutgoingNodes(std::vector<galois::DynamicBitSet>& hasIncomingEdge,
                           VectorOfVector64& numOutgoingEdges,
                           std::vector<uint64_t>& prefixSumOfEdges) {
    galois::gDebug("Creating outgoing node mapping");
    // first host id of the row that this host is on
    unsigned leaderHostID = gridRowID(base_DistGraph::id) * numColumnHosts;

    // loop through data from all hosts associated with this row (ignore
    // self because already handled) and count edges, create nodes, and
    // keep a running prefix sum
    for (unsigned curColumn = 0; curColumn < numColumnHosts; ++curColumn) {
      unsigned hostID = leaderHostID + curColumn;
      if (hostID == base_DistGraph::id) continue;
      uint64_t src = base_DistGraph::gid2host[hostID].first;

      uint32_t activeThreads = galois::getActiveThreads();
      std::vector<uint64_t> threadPrefixSums(activeThreads);
      size_t columnSize = numOutgoingEdges[curColumn].size();

      // for each thread, figure out how many items it will work with
      galois::on_each(
        [&](unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          std::tie(beginNode, endNode) = galois::block_range((size_t)0,
                                             columnSize, tid, nthreads);
          uint64_t count = 0;
          for (size_t i = beginNode; i < endNode; i++) {
            if (numOutgoingEdges[curColumn][i] > 0) {
              count++;
            }
          }

          threadPrefixSums[tid] = count;
        }
      );

      // get prefix sums
      for (unsigned int i = 1; i < threadPrefixSums.size(); i++) {
        threadPrefixSums[i] += threadPrefixSums[i - 1];
      }

      assert(prefixSumOfEdges.size() == numNodes);
      assert(localToGlobalVector.size() == numNodes);

      uint32_t newOutgoingNodes = threadPrefixSums[activeThreads - 1];
      // increase size of prefix sum + mapping vector
      prefixSumOfEdges.resize(numNodes + newOutgoingNodes);
      localToGlobalVector.resize(numNodes + newOutgoingNodes);
      uint32_t startingNodeIndex = numNodes;

      // do actual work, second on_each
      galois::on_each(
        [&](unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          std::tie(beginNode, endNode) = galois::block_range((size_t)0,
                                             columnSize, tid, nthreads);

          // start location to start adding things into prefix sums/vectors
          uint32_t threadStartLocation = 0;
          if (tid != 0) {
            threadStartLocation = threadPrefixSums[tid - 1];
          }

          uint32_t handledNodes = 0;

          for (size_t i = beginNode; i < endNode; i++) {
            uint64_t myEdges = numOutgoingEdges[curColumn][i];
            if (myEdges > 0) {
              prefixSumOfEdges[startingNodeIndex + threadStartLocation +
                               handledNodes] = myEdges;
              localToGlobalVector[startingNodeIndex + threadStartLocation +
                                  handledNodes] = src + i;

              handledNodes++;
            }
          }
        }
      );

      numNodes += newOutgoingNodes;
    }
  }

  /**
   * Create metadata for endpoints of edges that we are responsible for.
   */
  void inspectIncomingNodes(
    std::vector<galois::DynamicBitSet>& hasIncomingEdge,
    std::vector<uint64_t>& prefixSumOfEdges
  ) {
    galois::gDebug("Creating incoming node mapping");
    // check hosts in different rows but on same column
    for (unsigned curRowHost = 0; curRowHost < numRowHosts; ++curRowHost) {
      unsigned hostID = (curRowHost * numColumnHosts) + gridColumnID();
      if (hostID == base_DistGraph::id) continue;

      // disjoint set of nodes from nodes on this host
      uint64_t dst     = base_DistGraph::gid2host[hostID].first;
      uint64_t dst_end = base_DistGraph::gid2host[hostID].second;

      uint32_t activeThreads = galois::getActiveThreads();
      std::vector<uint64_t> threadPrefixSums(activeThreads);

      galois::on_each(
        [&](unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          std::tie(beginNode, endNode) = galois::block_range((size_t)dst,
                                             dst_end, tid, nthreads);

          uint64_t count = 0;
          for (size_t i = beginNode; i < endNode; i++) {
            if (hasIncomingEdge[0].test(getColumnIndexOfNode(i))) {
              count++;
            }
          }

          threadPrefixSums[tid] = count;
        }
      );

      // get prefix sums
      for (unsigned int i = 1; i < threadPrefixSums.size(); i++) {
        threadPrefixSums[i] += threadPrefixSums[i - 1];
      }

      assert(prefixSumOfEdges.size() == numNodes);
      assert(localToGlobalVector.size() == numNodes);
      uint32_t newIncomingNodes = threadPrefixSums[activeThreads - 1];
      // increase size of prefix sum + mapping vector
      prefixSumOfEdges.resize(numNodes + newIncomingNodes);
      localToGlobalVector.resize(numNodes + newIncomingNodes);
      uint32_t startingNodeIndex = numNodes;

      galois::on_each(
        [&](unsigned tid, unsigned nthreads) {
          size_t beginNode;
          size_t endNode;
          std::tie(beginNode, endNode) = galois::block_range((size_t)dst,
                                             dst_end, tid, nthreads);

          uint32_t threadStartLocation = 0;
          if (tid != 0) {
            threadStartLocation = threadPrefixSums[tid - 1];
          }

          uint64_t handledNodes = 0;
          for (size_t i = beginNode; i < endNode; i++) {
            if (hasIncomingEdge[0].test(getColumnIndexOfNode(i))) {
              localToGlobalVector[startingNodeIndex + threadStartLocation +
                                  handledNodes] = i;
              prefixSumOfEdges[startingNodeIndex + threadStartLocation +
                              handledNodes] = 0;
              handledNodes++;
            }
          }
        }
      );

      numNodes += newIncomingNodes;
    }
  }


  /**
   * Create the global to local map (from a completed local to global map) and
   * calculate the final prefix sum of edges.
   */
  void finalizeNodeMapping(std::vector<uint64_t>& prefixSumOfEdges) {
    if (numNodes == 0) {
      return;
    }
    galois::gDebug("Finalizing node mapping");
    assert(prefixSumOfEdges.size() == numNodes);
    assert(localToGlobalVector.size() == numNodes);

    globalToLocalMap.reserve(numNodes);
    // first iteration lifted out of loop to fuse global/local loop and prefix
    // sum loop together
    globalToLocalMap[localToGlobalVector[0]] = 0;
    for (unsigned i = 1; i < numNodes; i++) {
      // finalize prefix sum
      prefixSumOfEdges[i] += prefixSumOfEdges[i - 1];
      // global to local map construction
      globalToLocalMap[localToGlobalVector[i]] = i;
    }
    numEdges = prefixSumOfEdges.back();
  }

  /**
   * Pass to determine where the edges that this host will read will go and
   * prepare metadata required to constructing the graph and sending off
   * edges this host reads that do not belong to this host.
   */
  void loadStatistics(galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
                      std::vector<uint64_t>& prefixSumOfEdges,
                      galois::Timer& inspectionTimer) {
    // setup numOwned variable
    base_DistGraph::numOwned =
      base_DistGraph::gid2host[base_DistGraph::id].second -
      base_DistGraph::gid2host[base_DistGraph::id].first;

    // initialize metadata tracking for columns
    std::vector<galois::DynamicBitSet> hasIncomingEdge(numColumnHosts);
    VectorOfVector64 numOutgoingEdges;
    inOutMetadataInitialization(hasIncomingEdge, numOutgoingEdges);

    // EDGE INSPECTION AND SENDING OF METADATA TO OTHERS
    galois::CondStatTimer<PHASE_BREAKDOWN> p1CommTimer("P1Comm", GRNAME);
    // edge inspection for metadata
    edgeInspection(bufGraph, hasIncomingEdge, numOutgoingEdges,
                   inspectionTimer);
    p1CommTimer.start();
    // send out data to other hosts in same column
    communicateColumnMetadata(hasIncomingEdge, numOutgoingEdges);
    p1CommTimer.stop();

    // SPACE ALLOCATION
    galois::StatTimer mapTimer("NodeMapping", GRNAME);
    mapTimer.start();

    // reserve space for maximum amount of nodes possible
    auto max_nodes = hasIncomingEdge[0].size();
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      max_nodes += numOutgoingEdges[i].size();
    }
    localToGlobalVector.reserve(max_nodes);
    prefixSumOfEdges.reserve(max_nodes);

    numNodes           = 0;
    numEdges           = 0;

    // NODE METADATA CREATION
    galois::CondStatTimer<PHASE_BREAKDOWN> cLocalTimer("CreateLocalTimer", GRNAME);
    galois::CondStatTimer<PHASE_BREAKDOWN> cOutTimer("CreateOutgoingTimer", GRNAME);
    galois::CondStatTimer<PHASE_BREAKDOWN> cInTimer("CreateInTimer", GRNAME);
    galois::CondStatTimer<PHASE_BREAKDOWN> finalizeTimer("FinalizeTimer", GRNAME);
    // master nodes
    cLocalTimer.start();
    inspectLocalNodes(numOutgoingEdges, prefixSumOfEdges);
    cLocalTimer.stop();

    // nodes along the row
    cOutTimer.start();
    inspectOutgoingNodes(hasIncomingEdge, numOutgoingEdges, prefixSumOfEdges);
    cOutTimer.stop();

    // numNodes should now have counted master nodes as well as nodes with edges
    base_DistGraph::numNodesWithEdges = numNodes;
    cInTimer.start();
    // nodes along the column
    inspectIncomingNodes(hasIncomingEdge, prefixSumOfEdges);
    cInTimer.stop();

    finalizeTimer.start();
    finalizeNodeMapping(prefixSumOfEdges);
    finalizeTimer.stop();

    mapTimer.stop();
  }

  //! Load our assigned edges and construct them in-memory. Receive edges read
  //! by other hosts that belong to us and construct them as well.
  template <typename GraphTy>
  void loadEdges(GraphTy& graph,
                 galois::graphs::BufferedGraph<EdgeTy>& bufGraph) {
    if (base_DistGraph::id == 0) {
      if (std::is_void<typename GraphTy::edge_data_type>::value) {
        galois::gPrint("Loading void edge-data while creating edges\n");
      } else {
        galois::gPrint("Loading edge-data while creating edges\n");
      }
    }

    bufGraph.resetReadCounters();

    galois::StatTimer loadTimer("EdgeLoading", GRNAME);
    galois::CondStatTimer<PHASE_BREAKDOWN> edgeSendsTimer(
      "EdgeLoadingSends", GRNAME);
    galois::CondStatTimer<PHASE_BREAKDOWN> edgeRecvsTimer(
      "EdgeLoadingReceives", GRNAME);
    galois::PerThreadTimer<PHASE_BREAKDOWN> recvTagTimer1(
      GRNAME, "EdgeRecvTaggedOverlap");
    galois::PerThreadTimer<PHASE_BREAKDOWN> recvComputeTimer1(
      GRNAME, "EdgeRecvProcessOverlap");
    galois::PerThreadTimer<PHASE_BREAKDOWN> recvTagTimer2(
      GRNAME, "EdgeRecvTaggedNonoverlap");
    galois::PerThreadTimer<PHASE_BREAKDOWN> recvComputeTimer2(
      GRNAME, "EdgeRecvProcessNonoverlap");

    std::atomic<uint32_t> numNodesWithEdges;
    numNodesWithEdges = base_DistGraph::numOwned;

#if PHASE_BREAKDOWN
    galois::runtime::getHostBarrier().wait();
#endif
    loadTimer.start();

    // read and send edges
    edgeSendsTimer.start();
    loadEdgesFromFile(graph, bufGraph, numNodesWithEdges, recvTagTimer1,
                      recvComputeTimer1);
    edgeSendsTimer.stop();

    edgeRecvsTimer.start();
    // receive all edges
    galois::on_each([&](unsigned tid, unsigned nthreads) {
      receiveEdges(graph, numNodesWithEdges, recvTagTimer2, recvComputeTimer2);
    });
    edgeRecvsTimer.stop();

    base_DistGraph::increment_evilPhase();

    loadTimer.stop();

    galois::gPrint(
        "[", base_DistGraph::id,
        "] Edge loading time: ", loadTimer.get_usec() / 1000000.0f,
        " seconds to read ", bufGraph.getBytesRead(), " bytes (",
        bufGraph.getBytesRead() / (float)loadTimer.get_usec(), " MBPS)\n");
  }

  //! Read in our assigned edges, constructing them if they belong to this host
  //! and sending them off to the correct host otherwise
  //! Edge-data version
  template <typename GraphTy,
            typename std::enable_if<!std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void loadEdgesFromFile(
      GraphTy& graph,
      galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
      std::atomic<uint32_t>& numNodesWithEdges,
      galois::PerThreadTimer<PHASE_BREAKDOWN>& recvTagTimer,
      galois::PerThreadTimer<PHASE_BREAKDOWN>& recvComputeTimer
  ) {
    galois::CondStatTimer<PHASE_BREAKDOWN> clearReserveTimer(
      GRNAME, "EdgeSendClearReserveTime"
    );
    galois::CondStatTimer<PHASE_BREAKDOWN> sendTaggedTimer(
      GRNAME, "EdgeSendComm"
    );

    auto& net = galois::runtime::getSystemNetworkInterface();

    // XXX h_offset not correct
    unsigned h_offset = gridRowID() * numColumnHosts;
    galois::substrate::PerThreadStorage<VectorOfVector64> gdst_vecs(
        numColumnHosts);
    typedef std::vector<std::vector<typename GraphTy::edge_data_type>>
        DataVecVecTy;
    galois::substrate::PerThreadStorage<DataVecVecTy> gdata_vecs(
        numColumnHosts);
    typedef std::vector<galois::runtime::SendBuffer> SendBufferVecTy;
    galois::substrate::PerThreadStorage<SendBufferVecTy> sb(numColumnHosts);

    // reserve space for send buffers
    clearReserveTimer.start();
    galois::on_each([&](unsigned tid, unsigned nthreads) {
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        auto& b = (*sb.getLocal())[i];
        b.reserve(edgePartitionSendBufSize * 1.25);
      }
    });
    clearReserveTimer.stop();

    // thread timers for detailed breakdown of this loop
    galois::PerThreadTimer<PHASE_BREAKDOWN> edgeSendSerializeTimerT(
      GRNAME, "EdgeSendSerialize"
    );
    galois::PerThreadTimer<PHASE_BREAKDOWN> edgeSendCommTimerT(
      GRNAME, "EdgeSendComm"
    );
    galois::PerThreadTimer<PHASE_BREAKDOWN> clearReserveTimerT(
      GRNAME, "EdgeSendClearReserveTime"
    );

    galois::GAccumulator<uint64_t> messagesSent;
    galois::GAccumulator<uint64_t> bytesSent;
    galois::GReduceMax<uint64_t> maxBytesSent;
    messagesSent.reset();
    bytesSent.reset();
    maxBytesSent.reset();

    const unsigned& id =
        base_DistGraph::id; // manually copy it because it is protected

    galois::do_all(
        galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                        base_DistGraph::gid2host[base_DistGraph::id].second),
        [&](auto n) {
          auto& gdst_vec  = *gdst_vecs.getLocal();
          auto& gdata_vec = *gdata_vecs.getLocal();
          uint32_t lsrc   = 0;
          uint64_t cur    = 0;
          if (this->isLocal(n)) {
            lsrc = this->G2L(n);
            cur  = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
          }
          auto ii = bufGraph.edgeBegin(n);
          auto ee = bufGraph.edgeEnd(n);
          for (unsigned i = 0; i < numColumnHosts; ++i) {
            gdst_vec[i].clear();
            gdata_vec[i].clear();
            gdst_vec[i].reserve(std::distance(ii, ee));
            gdata_vec[i].reserve(std::distance(ii, ee));
          }
          for (; ii < ee; ++ii) {
            uint64_t gdst = bufGraph.edgeDestination(*ii);
            auto gdata    = bufGraph.edgeData(*ii);
            int i         = this->getColumnOfNode(gdst);

            if ((h_offset + i) == id) {
              assert(this->isLocal(n));
              uint32_t ldst = this->G2L(gdst);
              graph.constructEdge(cur++, ldst, gdata);
            } else {
              gdst_vec[i].push_back(gdst);
              gdata_vec[i].push_back(gdata);
            }
          }
          for (unsigned i = 0; i < numColumnHosts; ++i) {
            if (gdst_vec[i].size() > 0) {
              auto& b = (*sb.getLocal())[i];
              edgeSendSerializeTimerT.start();
              galois::runtime::gSerialize(b, n);
              galois::runtime::gSerialize(b, gdst_vec[i]);
              galois::runtime::gSerialize(b, gdata_vec[i]);
              edgeSendSerializeTimerT.stop();

              if (b.size() > edgePartitionSendBufSize) {
                messagesSent += 1;
                bytesSent.update(b.size());
                maxBytesSent.update(b.size());

                edgeSendCommTimerT.start();
                net.sendTagged(h_offset + i, galois::runtime::evilPhase, b);
                edgeSendCommTimerT.stop();

                clearReserveTimerT.start();
                b.getVec().clear();
                b.getVec().reserve(edgePartitionSendBufSize * 1.25);
                clearReserveTimerT.stop();
              }
            }
          }

          if (this->isLocal(n)) {
            assert(cur == (*graph.edge_end(lsrc)));
          }

          // TODO don't have to receive every iteration
          recvTagTimer.start();
          auto buffer =
              net.recieveTagged(galois::runtime::evilPhase, nullptr);
          recvTagTimer.stop();
          this->processReceivedEdgeBuffer(buffer, graph, numNodesWithEdges,
                                          recvComputeTimer);
        },
#if MORE_DIST_STATS
        galois::loopname("EdgeLoadingSendsLoop"),
#endif
        galois::no_stats());

    // flush out all buffers
    for (unsigned t = 0; t < sb.size(); ++t) {
      auto& sbr = *sb.getRemote(t);
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        auto& b = sbr[i];
        if (b.size() > 0) {
          messagesSent += 1;
          bytesSent.update(b.size());
          maxBytesSent.update(b.size());

          sendTaggedTimer.start();
          net.sendTagged(h_offset + i, galois::runtime::evilPhase, b);
          sendTaggedTimer.stop();

          clearReserveTimer.start();
          b.getVec().clear();
          clearReserveTimer.stop();
        }
      }
    }
    net.flush();

    // TODO make conditional?
    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeLoadingMessagesSent"), messagesSent.reduce()
    );
    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeLoadingBytesSent"), bytesSent.reduce()
    );
    galois::runtime::reportStat_Tmax(
      GRNAME, std::string("EdgeLoadingMaxBytesSent"), maxBytesSent.reduce()
    );
  }

  //! Read in our assigned edges, constructing them if they belong to this host
  //! and sending them off to the correct host otherwise
  //! No edge data version
  template <typename GraphTy,
            typename std::enable_if<std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void loadEdgesFromFile(
      GraphTy& graph,
      galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
      std::atomic<uint32_t>& numNodesWithEdges,
      galois::PerThreadTimer<PHASE_BREAKDOWN>& recvTagTimer,
      galois::PerThreadTimer<PHASE_BREAKDOWN>& recvComputeTimer
  ) {
    galois::CondStatTimer<PHASE_BREAKDOWN> clearReserveTimer(
      GRNAME, "EdgeSendClearReserveTime"
    );
    galois::CondStatTimer<PHASE_BREAKDOWN> sendTaggedTimer(
      GRNAME, "EdgeSendComm"
    );

    auto& net = galois::runtime::getSystemNetworkInterface();

    unsigned h_offset = gridRowID() * numColumnHosts;
    galois::substrate::PerThreadStorage<VectorOfVector64> gdst_vecs(
        numColumnHosts);
    typedef std::vector<galois::runtime::SendBuffer> SendBufferVecTy;
    galois::substrate::PerThreadStorage<SendBufferVecTy> sb(numColumnHosts);

    // reserve space for send buffers
    clearReserveTimer.start();
    galois::on_each([&](unsigned tid, unsigned nthreads) {
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        auto& b = (*sb.getLocal())[i];
        b.reserve(edgePartitionSendBufSize * 1.25);
      }
    });
    clearReserveTimer.stop();

    // thread timers for detailed breakdown of this loop
    galois::PerThreadTimer<PHASE_BREAKDOWN> edgeSendSerializeTimerT(
      GRNAME, "EdgeSendSerialize"
    );
    galois::PerThreadTimer<PHASE_BREAKDOWN> edgeSendCommTimerT(
      GRNAME, "EdgeSendComm"
    );
    galois::PerThreadTimer<PHASE_BREAKDOWN> clearReserveTimerT(
      GRNAME, "EdgeSendClearReserveTime"
    );

    galois::GAccumulator<uint64_t> messagesSent;
    galois::GAccumulator<uint64_t> bytesSent;
    galois::GReduceMax<uint64_t> maxBytesSent;
    messagesSent.reset();
    bytesSent.reset();
    maxBytesSent.reset();

    const unsigned& id =
        base_DistGraph::id; // manually copy it because it is protected

    galois::do_all(
        galois::iterate(base_DistGraph::gid2host[base_DistGraph::id].first,
                        base_DistGraph::gid2host[base_DistGraph::id].second),
        [&](auto n) {
          auto& gdst_vec = *gdst_vecs.getLocal();
          uint32_t lsrc  = 0;
          uint64_t cur   = 0;
          if (this->isLocal(n)) {
            lsrc = this->G2L(n);
            cur  = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
          }
          auto ii = bufGraph.edgeBegin(n);
          auto ee = bufGraph.edgeEnd(n);
          for (unsigned i = 0; i < numColumnHosts; ++i) {
            gdst_vec[i].clear();
            gdst_vec[i].reserve(std::distance(ii, ee));
          }
          for (; ii < ee; ++ii) {
            uint64_t gdst = bufGraph.edgeDestination(*ii);

            int i         = this->getColumnOfNode(gdst);
            if ((h_offset + i) == (id)) {
              assert(this->isLocal(n));
              uint32_t ldst = this->G2L(gdst);
              graph.constructEdge(cur++, ldst);
            } else {
              gdst_vec[i].push_back(gdst);
            }
          }
          for (unsigned i = 0; i < numColumnHosts; ++i) {
            if (gdst_vec[i].size() > 0) {
              auto& b = (*sb.getLocal())[i];

              edgeSendSerializeTimerT.start();
              galois::runtime::gSerialize(b, n);
              galois::runtime::gSerialize(b, gdst_vec[i]);
              edgeSendSerializeTimerT.stop();

              if (b.size() > edgePartitionSendBufSize) {
                messagesSent += 1;
                bytesSent.update(b.size());
                maxBytesSent.update(b.size());

                edgeSendCommTimerT.start();
                net.sendTagged(h_offset + i, galois::runtime::evilPhase, b);
                edgeSendCommTimerT.stop();

                clearReserveTimerT.start();
                b.getVec().clear();
                b.getVec().reserve(edgePartitionSendBufSize * 1.25);
                clearReserveTimerT.stop();
              }

            }
          }
          if (this->isLocal(n)) {
            assert(cur == (*graph.edge_end(lsrc)));
          }

          // TODO don't have to receive every iteration
          recvTagTimer.start();
          auto buffer =
              net.recieveTagged(galois::runtime::evilPhase, nullptr);
          recvTagTimer.stop();
          this->processReceivedEdgeBuffer(buffer, graph, numNodesWithEdges,
                                          recvComputeTimer);
        },
#if MORE_DIST_STATS
        galois::loopname("EdgeLoadingSendsLoop"),
#endif
        galois::no_stats());

    // flush out all buffers
    for (unsigned t = 0; t < sb.size(); ++t) {
      auto& sbr = *sb.getRemote(t);
      for (unsigned i = 0; i < numColumnHosts; ++i) {
        auto& b = sbr[i];
        if (b.size() > 0) {
          messagesSent += 1;
          bytesSent.update(b.size());
          maxBytesSent.update(b.size());

          sendTaggedTimer.start();
          net.sendTagged(h_offset + i, galois::runtime::evilPhase, b);
          sendTaggedTimer.stop();

          clearReserveTimer.start();
          b.getVec().clear();
          clearReserveTimer.stop();
        }
      }
    }
    net.flush();

    // TODO make conditional?
    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeLoadingMessagesSent"), messagesSent.reduce()
    );
    galois::runtime::reportStat_Tsum(
      GRNAME, std::string("EdgeLoadingBytesSent"), bytesSent.reduce()
    );
    galois::runtime::reportStat_Tmax(
      GRNAME, std::string("EdgeLoadingMaxBytesSent"), maxBytesSent.reduce()
    );
  }

  //! Optional type
  //! @tparam T type that the variable may possibly take
  template <typename T>
#if __GNUC__ > 5 || (__GNUC__ == 5 && __GNUC_MINOR__ > 1)
  using optional_t = std::experimental::optional<T>;
#else
  using optional_t = boost::optional<T>;
#endif
  //! @copydoc DistGraphHybridCut::processReceivedEdgeBuffer
  template <typename GraphTy>
  void processReceivedEdgeBuffer(
      optional_t<std::pair<uint32_t, galois::runtime::RecvBuffer>>& buffer,
      GraphTy& graph, std::atomic<uint32_t>& numNodesWithEdges,
      galois::PerThreadTimer<PHASE_BREAKDOWN>& recvComputeTimer) {
    if (buffer) {
      auto& rb = buffer->second;

      recvComputeTimer.start();
      while (rb.r_size() > 0) {
        uint64_t n;
        std::vector<uint64_t> gdst_vec;
        galois::runtime::gDeserialize(rb, n);
        galois::runtime::gDeserialize(rb, gdst_vec);
        assert(isLocal(n));
        uint32_t lsrc = G2L(n);
        uint64_t cur = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
        uint64_t cur_end = *graph.edge_end(lsrc);
        assert((cur_end - cur) == gdst_vec.size());
        deserializeEdges(graph, rb, gdst_vec, cur, cur_end);
        ++numNodesWithEdges;
      }
      recvComputeTimer.stop();
    }
  }

  /**
   * Receive the edge dest/data assigned to this host from other hosts
   * that were responsible for reading them.
   */
  template <typename GraphTy>
  void receiveEdges(GraphTy& graph, std::atomic<uint32_t>& numNodesWithEdges,
                    galois::PerThreadTimer<PHASE_BREAKDOWN>& recvTagTimer,
                    galois::PerThreadTimer<PHASE_BREAKDOWN>& recvComputeTimer
  ) {
    auto& net = galois::runtime::getSystemNetworkInterface();

    // receive edges for all mirror nodes
    while (numNodesWithEdges < base_DistGraph::numNodesWithEdges) {
      decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
      recvTagTimer.start();
      p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
      recvTagTimer.stop();

      processReceivedEdgeBuffer(p, graph, numNodesWithEdges, recvComputeTimer);
    }
  }

  /**
   * Deserialize received edges and constructs them in our graph. No edge-data
   * variant.
   */
  template <typename GraphTy,
            typename std::enable_if<!std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void deserializeEdges(GraphTy& graph, galois::runtime::RecvBuffer& b,
                        std::vector<uint64_t>& gdst_vec, uint64_t& cur,
                        uint64_t& cur_end) {
    std::vector<typename GraphTy::edge_data_type> gdata_vec;
    galois::runtime::gDeserialize(b, gdata_vec);
    uint64_t i = 0;
    while (cur < cur_end) {
      auto gdata    = gdata_vec[i];
      uint64_t gdst = gdst_vec[i++];
      uint32_t ldst = G2L(gdst);
      graph.constructEdge(cur++, ldst, gdata);
    }
  }

  /**
   * Deserialize received edges and constructs them in our graph. Edge-data
   * variant.
   */
  template <typename GraphTy,
            typename std::enable_if<std::is_void<
                typename GraphTy::edge_data_type>::value>::type* = nullptr>
  void deserializeEdges(GraphTy& graph, galois::runtime::RecvBuffer& b,
                        std::vector<uint64_t>& gdst_vec, uint64_t& cur,
                        uint64_t& cur_end) {
    uint64_t i = 0;
    while (cur < cur_end) {
      uint64_t gdst = gdst_vec[i++];
      uint32_t ldst = G2L(gdst);
      graph.constructEdge(cur++, ldst);
    }
  }

  /**
   * @copydoc DistGraphEdgeCut::fill_mirrorNodes
   */
  void fillMirrorNodes(std::vector<std::vector<size_t>>& mirrorNodes) {
    // mirrors for outgoing edges
    for (unsigned i = 0; i < numColumnHosts; ++i) {
      // unsigned hostID = (gridRowID() * numColumnHosts) + i;
      unsigned hostToExamine =
        (gridRowID(base_DistGraph::id) * numColumnHosts) + i;
      if (hostToExamine == base_DistGraph::id) continue;

      uint64_t src         = base_DistGraph::gid2host[hostToExamine].first;
      uint64_t src_end     = base_DistGraph::gid2host[hostToExamine].second;
      mirrorNodes[hostToExamine].reserve(mirrorNodes[hostToExamine].size() +
                                         src_end - src);
      for (; src < src_end; ++src) {
        if (globalToLocalMap.find(src) != globalToLocalMap.end()) {
          mirrorNodes[hostToExamine].push_back(src);
        }
      }
    }

    // mirrors for incoming edges
    for (unsigned i = 0; i < numRowHosts; ++i) {
      unsigned hostToExamine;
      hostToExamine = (i * numColumnHosts) + gridColumnID(base_DistGraph::id);
      if (hostToExamine == base_DistGraph::id) continue;

      uint64_t dst         = base_DistGraph::gid2host[hostToExamine].first;
      uint64_t dst_end     = base_DistGraph::gid2host[hostToExamine].second;
      mirrorNodes[hostToExamine].reserve(mirrorNodes[hostToExamine].size() +
                                         dst_end - dst);
      for (; dst < dst_end; ++dst) {
        if (globalToLocalMap.find(dst) != globalToLocalMap.end()) {
          mirrorNodes[hostToExamine].push_back(dst);
        }
      }
    }
  }

public:
  bool is_vertex_cut() const {
    if (moreColumnHosts) {
      // IEC and OEC will be reversed, so do not handle it as an edge-cut
      if ((numRowHosts == 1) && (numColumnHosts == 1))
        return false;
    } else {
      // IEC or OEC
      if ((numRowHosts == 1) || (numColumnHosts == 1))
        return false;
    }
    return true;
  }

  void reset_bitset(typename base_DistGraph::SyncType syncType,
                    void (*bitset_reset_range)(size_t, size_t)) const {
    if (base_DistGraph::numOwned != 0) {
      auto endMaster = base_DistGraph::beginMaster + base_DistGraph::numOwned;
      if (syncType == base_DistGraph::syncBroadcast) { // reset masters
        bitset_reset_range(base_DistGraph::beginMaster, endMaster - 1);
      } else { // reset mirrors
        assert(syncType == base_DistGraph::syncReduce);
        if (base_DistGraph::beginMaster > 0) {
          bitset_reset_range(0, base_DistGraph::beginMaster - 1);
        }
        if (endMaster < numNodes) {
          bitset_reset_range(endMaster, numNodes - 1);
        }
      }
    } else { // everything is a mirror
      if (syncType == base_DistGraph::syncReduce) {
        if (numNodes > 0) {
          bitset_reset_range(0, numNodes - 1);
        }
      }
    }
  }

  std::vector<std::pair<uint32_t, uint32_t>> getMirrorRanges() const {
    std::vector<std::pair<uint32_t, uint32_t>> mirrorRanges_vec;
    if (base_DistGraph::beginMaster > 0)
      mirrorRanges_vec.push_back(
          std::make_pair(0, base_DistGraph::beginMaster));
    auto endMaster = base_DistGraph::beginMaster + base_DistGraph::numOwned;
    if (endMaster < numNodes) {
      mirrorRanges_vec.push_back(std::make_pair(endMaster, numNodes));
    }
    return mirrorRanges_vec;
  }

  virtual void boostSerializeLocalGraph(boost::archive::binary_oarchive& ar,
                                        const unsigned int version = 0) const {
    // unsigned ints
    ar << numNodes;
    ar << numRowHosts;
    ar << numColumnHosts;
    // maps and vectors
    ar << localToGlobalVector;
    ar << globalToLocalMap;
  }

  virtual void boostDeSerializeLocalGraph(boost::archive::binary_iarchive& ar,
                                          const unsigned int version = 0) {
    // unsigned ints
    ar >> numNodes;
    ar >> numRowHosts;
    ar >> numColumnHosts;
    // maps and vectors
    ar >> localToGlobalVector;
    ar >> globalToLocalMap;
  }
};

// declaration so other places can access (e.g. stats)
template <typename NodeTy, typename EdgeTy, bool moreColumnHosts>
constexpr const char* const
    galois::graphs::DistGraphCartesianCut<NodeTy, EdgeTy, moreColumnHosts>::GRNAME;
} // end namespace graphs
} // end namespace galois
#endif
