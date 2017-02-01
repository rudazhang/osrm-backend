// Implementation as a simple function.

#include "engine/routing_algorithms/routing_base.hpp"
#include "engine/search_engine_data.hpp"
#include "engine/engine.hpp"
#include "engine/data_watchdog.hpp"
#include "engine/datafacade/shared_memory_datafacade.hpp"
#include "util/integer_range.hpp"
#include "util/timing_util.hpp"
#include "util/typedefs.hpp"
#include "util/csv_reader.hpp"

#include <boost/assert.hpp>

#include <memory>
#include <vector>
#include <iterator>
#include <cstdlib>
#include <iostream>

using QueryHeap = osrm::engine::SearchEngineData::QueryHeap;
using DataWatchdog = osrm::engine::DataWatchdog;
using SharedMemoryDataFacade = osrm::engine::datafacade::SharedMemoryDataFacade;
using BaseDataFacade = osrm::engine::datafacade::BaseDataFacade;

using std::cout;
using std::endl;

//std::vector<NodeID> RouteDistribution(const ContiguousInternalMemoryDataFacadeBase &facade, const double** &trip_frequency)
//// consider add vector of aggregated trip duration
//{
//    QueryHeap query_heap;
//    std::vector<NodeID> distribution;
//    auto weight = INVALID_EDGE_WEIGHT;
//
//    // get all edgeBasedNode (core nodes) from facade
//
//    // for each source edgeBasedNode, do forward complete Dijkstra search
//
//    // set up query heap (forward_segment_id, reverse_segment_id)
//    // prevents forcing of loops when offsets are correct.
//    constexpr bool DO_NOT_FORCE_LOOPS = false;
//    if (source_phantom.forward_segment_id.enabled)
//    {
//        query_heap.Insert(source_phantom.forward_segment_id.id,
//                            -source_phantom.GetForwardWeightPlusOffset(),
//                            source_phantom.forward_segment_id.id);
//    }
//    if (source_phantom.reverse_segment_id.enabled)
//    {
//        query_heap.Insert(source_phantom.reverse_segment_id.id,
//                            -source_phantom.GetReverseWeightPlusOffset(),
//                            source_phantom.reverse_segment_id.id);
//    }
//
//    // Search(facade, query_heap, DO_NOT_FORCE_LOOPS; weight, route)
//    while (!query_heap.Empty())
//    {
//        // RoutingStep(facade, query_heap);
//        const NodeID node = query_heap.DeleteMin();
//        if (to_weight < query_heap.GetKey(to))
//        {
//            query_heap.GetData(to).parent = node;
//            query_heap.DecreaseKey(to, to_weight);
//        }
//
//    }
//
//    // Retrieve path from heap (search_heap, target_node_id) -> path
//    std::vector<NodeID> route;
//    for (auto node = target_node_id; node != search_heap.GetData(node).parent; )
//    {
//        node = search_heap.GetData(node).parent;
//        path.emplace_back(node);
//    }
//
//    // weight with trip frequency
//    return distribution;
//}

int main()
{
    // use shared memory
    if (!DataWatchdog::TryConnect())
        throw osrm::util::exception("No shared memory blocks found, have you ran osrm-datastore?");
    auto watchdog = std::make_unique<DataWatchdog>();
    BOOST_ASSERT(watchdog);

    DataWatchdog::RegionsLock lock;
    std::shared_ptr<BaseDataFacade> facade;
    std::tie(lock, facade) = watchdog->GetDataFacade();
    auto shared_facade = std::dynamic_pointer_cast<SharedMemoryDataFacade>(facade);

    // OSRM graph statistics and representation;
    shared_facade->PrintStatistics();
//    shared_facade->PrintLocations();
//    shared_facade->PrintCompressedGeometry();
//    shared_facade->WriteCompressedGeometry();

    // Map matching
    std::ifstream location_file{"locations.csv"};
    if (!location_file) throw osrm::util::exception("locations.csv not found!");
    // discard header line
    location_file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    // format: lng,lat
    csv<int, int> lnglat_table(location_file, ',');
    std::ofstream edge_file{"nearest_edge.csv"};
    if (!edge_file) throw osrm::util::exception("nearest_edge.csv cannot be created!");

    TIMER_START(matching);
    auto fp_multiplier = 1E5;
    edge_file << "lng,lat,edge" << endl;
    for (auto row : lnglat_table) {
        using std::get;
        auto fp_lng = get<0>(row);
        auto fp_lat = get<1>(row);
        // matching to nearest compressed geometry EdgeID;
        // 23th St between 6th & 7th Ave: (-73.99417, 40.74351)
        auto edge = shared_facade->NearestEdge(fp_lng / fp_multiplier, fp_lat / fp_multiplier).packed_geometry_id;
        edge_file << fp_lng << ',' << fp_lat << ',' << edge << endl;
    }
    TIMER_STOP(matching);
    // 418 sec for 1.6M locations
    cout << "Geometry matching: " << TIMER_SEC(matching) << " seconds" << endl;

    cout << "m_query_graph: (.hsgr)" << endl;
    const auto N = shared_facade->GetNumberOfNodes();
    for (auto n = 0u; n < N; n++) {
        cout << '\t' << "Node " << n << ": ";
        auto edge_range = shared_facade->GetAdjacentEdgeRange(n);
        cout << "first edge " << edge_range.front() << ", ";
        cout << "out degree " << shared_facade->GetOutDegree(n) << endl;
        BOOST_ASSERT_MSG(edge_range.size() == shared_facade->GetOutDegree(n), "Edge range does not match out degree!");
    }
    const auto E = shared_facade->GetNumberOfEdges();
    auto counts = 0u;
    for (auto e = 0u; e < E; e++) {
        cout << '\t' << "Edge " << e << ": ";
        cout << "target " << shared_facade->GetTarget(e) << "; ";
        auto edge_data = shared_facade->GetEdgeData(e);
        if (edge_data.forward) cout << "forward ";
        if (edge_data.backward) cout << "backward ";
        cout << "weight " << edge_data.weight << "; ";
        if (edge_data.shortcut) {
            counts++;
            cout << "is shortcut, ";
            cout << "the contracted node of the shortcut " << shared_facade->GetEdgeData(e).id << "; ";
        } else {
            cout << "source directed compressed segment " << edge_data.id << "; ";
        }
        cout << endl;
    }
    cout << "Number of shortcut edges in core network: " << counts << endl;
    cout << facade->GetNumberOfNodes() << endl;

//    auto supply_rate = RouteDistribution(facade, trip_frequency);
    return EXIT_SUCCESS;
}
