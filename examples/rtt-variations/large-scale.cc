#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"

#include <vector>
#include <map>
#include <utility>
#include <set>
#include <ctime>
#include <sstream>
#include <string>

// The CDF in TrafficGenerator
extern "C"
{
#include "cdf.h"
}

#define ENABLE_TOR_SPINE_MONITOR 1
#define ENABLE_SERVER_TOR_MONITOR 1
#define ENABLE_QUEUE_MONITOR  1
#define ENABLE_FLOW_MONITOR   1
#define LINK_CAPACITY_BASE    1000000000          // 1Gbps
#define BUFFER_SIZE 250                           // 250 packets

// The flow port range, each flow will be assigned a random port number within this range

static uint16_t PORT = 1000;

#define PACKET_SIZE 1400

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("LargeScale");

std::vector<std::map<uint32_t, uint32_t> > all_bytes_counters;

// Acknowledged to https://github.com/HKUST-SING/TrafficGenerator/blob/master/src/common/common.c
double poission_gen_interval(double avg_rate)
{
  if (avg_rate > 0)
    return -logf(1.0 - (double)rand() / RAND_MAX) / avg_rate;
  else
    return 0;
}

template<typename T>
T rand_range (T min, T max)
{
  return min + ((double)max - min) * rand () / RAND_MAX;
}

void install_incast (NodeContainer servers, int SERVER_COUNT, int LEAF_COUNT, double START_TIME, double END_TIME, double FLOW_LAUNCH_END_TIME)
{
  // Install 1 incast 
  uint32_t n_incast = 1;
  uint32_t fanout = 60;
  // Flow size: 500 KB
  uint32_t flowSize = 500 * 1000;
  for (uint32_t i = 0; i < n_incast; ++i) {
    // First, select the receiver
    uint32_t rcv_idx = rand() % SERVER_COUNT;
    Ptr<Node> destServer = servers.Get (rcv_idx);
    Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
    Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1,0);
    Ipv4Address destAddress = destInterface.GetLocal ();
    NS_LOG_INFO("Select server " << rcv_idx << " " << destAddress << " as receiver!");

    std::set<uint32_t> snd_idx_set;
    while (snd_idx_set.size() < fanout)
    {
      uint32_t send_idx = rand() % SERVER_COUNT;
      if (snd_idx_set.find(send_idx) == snd_idx_set.end()) {
        snd_idx_set.insert(send_idx);
      }
    }
    double startTime = START_TIME + static_cast<double> (rand () % 100) / 1000000;
    NS_LOG_INFO("The incast will start at time " << startTime * 1e6 << " us");
    for (std::set<uint32_t>::iterator it = snd_idx_set.begin(); it != snd_idx_set.end(); it++) {
      NS_LOG_INFO("Select server " << *it << " as the sender!");
      uint32_t senderIndex = *it;
      uint16_t port = PORT++;
      BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (destAddress, port));
          
      source.SetAttribute ("SendSize", UintegerValue (PACKET_SIZE));
      source.SetAttribute ("MaxBytes", UintegerValue(flowSize));
      source.SetAttribute ("SimpleTOS", UintegerValue (rand() % 5));

      // Install apps
      ApplicationContainer sourceApp = source.Install (servers.Get (senderIndex));
      sourceApp.Start (Seconds (startTime));
      sourceApp.Stop (Seconds (END_TIME));

      // Install packet sinks
      PacketSinkHelper sink ("ns3::TcpSocketFactory",
                             InetSocketAddress (Ipv4Address::GetAny (), port));
      ApplicationContainer sinkApp = sink.Install (servers. Get (rcv_idx));
      sinkApp.Start (Seconds (START_TIME));
      sinkApp.Stop (Seconds (END_TIME));
    }

    
  }
}
// void install_incast_applications (NodeContainer servers, long &flowCount, int SERVER_COUNT, int LEAF_COUNT, double START_TIME, double END_TIME, double FLOW_LAUNCH_END_TIME)
// {
//   NS_LOG_INFO ("Install incast applications:");
//   for (int i = 0; i < SERVER_COUNT; i++)
//     {
//       Ptr<Node> destServer = servers.Get (i);
//       Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
//       Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1,0);
//       Ipv4Address destAddress = destInterface.GetLocal ();
// 
//       uint32_t fanout = rand () % 50 + 100;
//       for (uint32_t j = 0; j < fanout; j++)
//         {
//           double startTime = START_TIME + static_cast<double> (rand () % 100) / 1000000;
//           while (startTime < FLOW_LAUNCH_END_TIME)
//             {
//               flowCount ++;
//               uint32_t fromServerIndex = rand () % SERVER_COUNT;
//               uint16_t port = PORT++;
// 
//               BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (destAddress, port));
//               uint32_t flowSize = rand () % 10000;
//               uint32_t tos = rand() % 5;
// 
//               source.SetAttribute ("SendSize", UintegerValue (PACKET_SIZE));
//               source.SetAttribute ("MaxBytes", UintegerValue(flowSize));
//               source.SetAttribute ("SimpleTOS", UintegerValue (tos));
// 
//               // Install apps
//               ApplicationContainer sourceApp = source.Install (servers.Get (fromServerIndex));
//               sourceApp.Start (Seconds (startTime));
//               sourceApp.Stop (Seconds (END_TIME));
// 
//               // Install packet sinks
//               PacketSinkHelper sink ("ns3::TcpSocketFactory",
//                                      InetSocketAddress (Ipv4Address::GetAny (), port));
//               ApplicationContainer sinkApp = sink.Install (servers. Get (i));
//               sinkApp.Start (Seconds (START_TIME));
//               sinkApp.Stop (Seconds (END_TIME));
// 
//               startTime += static_cast<double> (rand () % 1000) / 1000000;
//             }
// 
//         }
// 
//     }
// }

void install_applications (int fromLeafId, NodeContainer servers, double requestRate, struct cdf_table *cdfTable,
                           long &flowCount, long &totalFlowSize, int SERVER_COUNT, int LEAF_COUNT, double START_TIME, double END_TIME, double FLOW_LAUNCH_END_TIME)
{
  NS_LOG_INFO ("Install applications:");
  for (int i = 0; i < SERVER_COUNT; i++)
    {
      int fromServerIndex = fromLeafId * SERVER_COUNT + i;

      double startTime = START_TIME + poission_gen_interval (requestRate);
      while (startTime < FLOW_LAUNCH_END_TIME)
        {
          flowCount ++;
          uint16_t port = PORT++;

          int destServerIndex = fromServerIndex;
          while (destServerIndex >= fromLeafId * SERVER_COUNT && destServerIndex < fromLeafId * SERVER_COUNT + SERVER_COUNT)
            {
              destServerIndex = rand_range (0, SERVER_COUNT * LEAF_COUNT);
            }

          Ptr<Node> destServer = servers.Get (destServerIndex);
          Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4> ();
          Ipv4InterfaceAddress destInterface = ipv4->GetAddress (1,0);
          Ipv4Address destAddress = destInterface.GetLocal ();

          BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (destAddress, port));
          uint32_t flowSize = gen_random_cdf (cdfTable);
          uint32_t tos = rand() % 5;

          totalFlowSize += flowSize;

          source.SetAttribute ("SendSize", UintegerValue (PACKET_SIZE));
          source.SetAttribute ("MaxBytes", UintegerValue(flowSize));
          source.SetAttribute ("SimpleTOS", UintegerValue (tos));

          // Install apps
          ApplicationContainer sourceApp = source.Install (servers.Get (fromServerIndex));
          sourceApp.Start (Seconds (startTime));
          sourceApp.Stop (Seconds (END_TIME));

          // Install packet sinks
          PacketSinkHelper sink ("ns3::TcpSocketFactory",
                                 InetSocketAddress (Ipv4Address::GetAny (), port));
          ApplicationContainer sinkApp = sink.Install (servers. Get (destServerIndex));
          sinkApp.Start (Seconds (START_TIME));
          sinkApp.Stop (Seconds (END_TIME));

          startTime += poission_gen_interval (requestRate);
        }
    }
}

void printPktsInQueue(std::string buf, unsigned int val1, unsigned int val2) {
  if (val2 >= 100) {
      NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << " us, " << buf << " qlen" << val2);
  }
}

void pollBytesInLeafSpine(Time window, Ptr<QueueDisc> queue, int queue_id) {
  uint32_t qBytes = queue->GetNBytes();
  if (qBytes > 0) {
    #if ENABLE_TOR_SPINE_MONITOR == 1
      NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << " us, " << " qId " << queue_id << ", qBytes " << qBytes);
    #endif
  }
  Simulator::Schedule(window, &pollBytesInLeafSpine, window, queue, queue_id);
}

void pollBytesInServer(Time window, Ptr<QueueDisc> queue, int queue_id) {
  uint32_t qBytes = queue->GetNBytes();
  if (qBytes > 0) {
    #if ENABLE_TOR_SPINE_MONITOR == 1
      NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << " us, " << " server qId " << queue_id << ", qBytes " << qBytes);
    #endif
  }
  Simulator::Schedule(window, &pollBytesInServer, window, queue, queue_id);
}


void pollBytesInQueue(Ipv4Address serverIpAddr, Time window, Ptr<QueueDisc> queue, int queue_id, Ptr<Ipv4FlowClassifier> classifier) {
  uint32_t qBytes = queue->GetNBytes();
  std::map<uint32_t, uint32_t> &bytes_counters = all_bytes_counters[queue_id];
  if (qBytes >= 100 * 1000) {
    #if ENABLE_SERVER_TOR_MONITOR == 1
      NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << " us, " << " qId " << queue_id << ", qBytes " << qBytes);
    #endif
    
  }

  if (bytes_counters.size() > 0) {
    #if ENABLE_SERVER_TOR_MONITOR == 1
      NS_LOG_INFO("-----------------------------------------------------");
      NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << " us," << "qId " << queue_id);
      for (std::map<uint32_t, uint32_t>::iterator it = bytes_counters.begin(); it != bytes_counters.end(); ++it) {
        Ipv4FlowClassifier::FiveTuple flowTuple = classifier->FindFlow(it->first);
        NS_LOG_INFO ("Flowid: " << it->first << ", bytes: " << it->second << " (" << flowTuple.sourceAddress << " -> " << flowTuple.destinationAddress << ")");
        if (serverIpAddr == flowTuple.sourceAddress) {
          NS_LOG_INFO ("[[[[[[[[[[[[[[[[[[[[[SRC ADDR MATCH]]]]]]]]]]]]]]]]]]]");
        } 
        if (serverIpAddr == flowTuple.destinationAddress) {
          NS_LOG_INFO ("[[[[[[[[[[[[[[[[[[[[[DST ADDR MATCH]]]]]]]]]]]]]]]]]]]");
        }
      }
      NS_LOG_INFO("-----------------------------------------------------");
      #endif
  }
  bytes_counters.clear();
  Simulator::Schedule(window, &pollBytesInQueue, serverIpAddr, window, queue, queue_id, classifier);
}

void p4Program(Ptr<QueueDisc> queue, Ptr<Ipv4FlowClassifier> classifier, int queue_id, Ptr<QueueItem const> item) {
  Ptr<Packet> p = item->GetPacket();
  // Extract ip header
  Ptr<Ipv4QueueDiscItem const> ipv4Item = DynamicCast<Ipv4QueueDiscItem const> (item);
  Ipv4Header header = ipv4Item -> GetHeader ();

  uint32_t flowId, pktId;
  bool flag = classifier->Classify(header, p, &flowId, &pktId);
  if (flag) {
    // Successfully extract the flow ID
    std::map<uint32_t, uint32_t> &bytes_counters = all_bytes_counters[queue_id];

    if (bytes_counters.find(flowId) == bytes_counters.end()) {
      bytes_counters[flowId] = p->GetSize();
    } else {
      bytes_counters[flowId] += p->GetSize();
    }

  }
  return;
}

void LeafDownIncomingPkt(int queue_id, Ptr<Ipv4FlowClassifier> classifier, Ptr<QueueItem const> item) {
  Ptr<Packet> p = item->GetPacket();
  Ptr<Ipv4QueueDiscItem const> ipv4Item = DynamicCast<Ipv4QueueDiscItem const> (item);
  Ipv4Header header = ipv4Item -> GetHeader ();
  uint32_t flowId, pktId;
  bool flag = classifier->Classify(header, p, &flowId, &pktId);
  if (flag) {
    NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << "us [leaf downlink " << queue_id <<  " ] Packet with flowID " << flowId << " pktId " << pktId);
  }
}

void LeafUpIncomingPkt(int queue_id, Ptr<Ipv4FlowClassifier> classifier, Ptr<QueueItem const> item) {
  Ptr<Packet> p = item->GetPacket();
  Ptr<Ipv4QueueDiscItem const> ipv4Item = DynamicCast<Ipv4QueueDiscItem const> (item);
  Ipv4Header header = ipv4Item -> GetHeader ();
  uint32_t flowId, pktId;
  bool flag = classifier->Classify(header, p, &flowId, &pktId);
  if (flag) {
    NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << "us [leaf uplink " << queue_id <<  " ] Packet with flowID " << flowId << " pktId " << pktId);
  }
}

void SpineDownIncomingPkt(int queue_id, Ptr<Ipv4FlowClassifier> classifier, Ptr<QueueItem const> item) {
  Ptr<Packet> p = item->GetPacket();
  Ptr<Ipv4QueueDiscItem const> ipv4Item = DynamicCast<Ipv4QueueDiscItem const> (item);
  Ipv4Header header = ipv4Item -> GetHeader ();
  uint32_t flowId, pktId;
  bool flag = classifier->Classify(header, p, &flowId, &pktId);
  if (flag) {
    NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << "us [spine downlink " << queue_id <<  " ] Packet with flowID " << flowId << " pktId " << pktId);
  }
}


void ServerDownIncomingPkt(int queue_id, Ptr<Ipv4FlowClassifier> classifier, Ptr<QueueItem const> item) {
  Ptr<Packet> p = item->GetPacket();
  Ptr<Ipv4QueueDiscItem const> ipv4Item = DynamicCast<Ipv4QueueDiscItem const> (item);
  Ipv4Header header = ipv4Item -> GetHeader ();
  uint32_t flowId, pktId;
  bool flag = classifier->Classify(header, p, &flowId, &pktId);
  if (flag) {
    NS_LOG_INFO(Simulator::Now().GetMicroSeconds() << "us [server downlink " << queue_id <<  " ] Packet with flowID " << flowId << " pktId " << pktId);
  }
}

int main (int argc, char *argv[])
{
#if 1
  LogComponentEnable ("LargeScale", LOG_LEVEL_INFO);
#endif

  time_t t1 = time(NULL);
  // Command line parameters parsing
  std::string id = "undefined";
  unsigned randomSeed = 0;
  std::string cdfFileName = "examples/rtt-variations/DCTCP_CDF.txt";
  double load = 0.0;
  std::string transportProt = "DcTcp";
  Time window = MicroSeconds(50);

  // The simulation starting and ending time
  double START_TIME = 0.0;
  double END_TIME = 0.5;

  double FLOW_LAUNCH_END_TIME = 0.2;

  uint32_t linkLatency = 10;

  int SERVER_COUNT = 8;
  int SPINE_COUNT = 4;
  int LEAF_COUNT = 4;
  int LINK_COUNT = 1;

  uint64_t spineLeafCapacity = 10;
  uint64_t leafServerCapacity = 10;


  CommandLine cmd;
  cmd.AddValue ("StartTime", "Start time of the simulation", START_TIME);
  cmd.AddValue ("EndTime", "End time of the simulation", END_TIME);
  cmd.AddValue ("FlowLaunchEndTime", "End time of the flow launch period", FLOW_LAUNCH_END_TIME);
  cmd.AddValue ("randomSeed", "Random seed, 0 for random generated", randomSeed);
  cmd.AddValue ("cdfFileName", "File name for flow distribution", cdfFileName);
  cmd.AddValue ("load", "Load of the network, 0.0 - 1.0", load);
  cmd.AddValue ("transportProt", "Transport protocol to use: Tcp, DcTcp", transportProt);
  cmd.AddValue ("linkLatency", "Link latency, should be in MicroSeconds", linkLatency);

  cmd.AddValue ("serverCount", "The Server count", SERVER_COUNT);
  cmd.AddValue ("spineCount", "The Spine count", SPINE_COUNT);
  cmd.AddValue ("leafCount", "The Leaf count", LEAF_COUNT);
  cmd.AddValue ("linkCount", "The Link count", LINK_COUNT);

  cmd.AddValue ("spineLeafCapacity", "Spine <-> Leaf capacity in Gbps", spineLeafCapacity);
  cmd.AddValue ("leafServerCapacity", "Leaf <-> Server capacity in Gbps", leafServerCapacity);




  cmd.Parse (argc, argv);

  uint64_t SPINE_LEAF_CAPACITY = spineLeafCapacity * LINK_CAPACITY_BASE;
  uint64_t LEAF_SERVER_CAPACITY = leafServerCapacity * LINK_CAPACITY_BASE;
  Time LINK_LATENCY = MicroSeconds (linkLatency);

  if (load <= 0.0 || load >= 1.0)
    {
      NS_LOG_ERROR ("The network load should within 0.0 and 1.0");
      return 0;
    }


  if (transportProt.compare ("DcTcp") == 0)
    {
      NS_LOG_INFO ("Enabling DcTcp");
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpDCTCP::GetTypeId ()));

      // ECN Configuration
      Config::SetDefault ("ns3::ECNQueueDisc::Mode", StringValue ("QUEUE_MODE_PACKETS"));
      Config::SetDefault ("ns3::ECNQueueDisc::MaxPackets", UintegerValue (BUFFER_SIZE));
      Config::SetDefault ("ns3::ECNQueueDisc::EcnBytes", UintegerValue (84 * 1000));
    }

  NS_LOG_INFO ("Config parameters");
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue(PACKET_SIZE));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
  Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (5)));

  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (5)));
  Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (MicroSeconds (100)));
  Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (80)));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (160000000));
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (160000000));

  Config::SetDefault ("ns3::Ipv4GlobalRouting::PerflowEcmpRouting", BooleanValue(true));

  NodeContainer spines;
  spines.Create (SPINE_COUNT);
  NodeContainer leaves;
  leaves.Create (LEAF_COUNT);
  NodeContainer servers;
  servers.Create (SERVER_COUNT * LEAF_COUNT);

  NS_LOG_INFO ("Install Internet stacks");
  InternetStackHelper internet;
  Ipv4GlobalRoutingHelper globalRoutingHelper;

  internet.SetRoutingHelper (globalRoutingHelper);


  internet.Install (servers);
  internet.Install (spines);
  internet.Install (leaves);

  NS_LOG_INFO ("Install channels and assign addresses");

  PointToPointHelper p2p;
  Ipv4AddressHelper ipv4;

  NS_LOG_INFO ("Configuring servers");
  // Setting servers
  p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (LEAF_SERVER_CAPACITY)));
  p2p.SetChannelAttribute ("Delay", TimeValue(LINK_LATENCY));
  p2p.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (10));

  ipv4.SetBase ("10.1.0.0", "255.255.255.0");

  #if ENABLE_FLOW_MONITOR == 1
    NS_LOG_INFO ("Enabling flow monitor");

    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());

    flowMonitor->CheckForLostPackets ();

    std::stringstream flowMonitorFilename;

    flowMonitorFilename << "Large_Scale_" <<id << "_" << LEAF_COUNT << "X" << SPINE_COUNT << "_" << transportProt << "_" << load << ".xml";
  #else
    NS_LOG_INFO ("Disabling flow monitor");
  #endif



  for (int i = 0; i < LEAF_COUNT; i++)
    {
      ipv4.NewNetwork ();

      for (int j = 0; j < SERVER_COUNT; j++)
        {
          int serverIndex = i * SERVER_COUNT + j;
          NodeContainer nodeContainer = NodeContainer (servers.Get (serverIndex), leaves.Get (i));
          NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

          //TODO We should change this, at endhost we are not going to mark ECN but add delay using delay queue disc

          Ptr<DelayQueueDisc> delayQueueDisc = CreateObject<DelayQueueDisc> ();
          Ptr<Ipv4SimplePacketFilter> filter = CreateObject<Ipv4SimplePacketFilter> ();

          delayQueueDisc->AddPacketFilter (filter);

          delayQueueDisc->AddDelayClass (0, MicroSeconds (1));
          delayQueueDisc->AddDelayClass (1, MicroSeconds (20));
          delayQueueDisc->AddDelayClass (2, MicroSeconds (50));
          delayQueueDisc->AddDelayClass (3, MicroSeconds (80));
          delayQueueDisc->AddDelayClass (4, MicroSeconds (160));

          ObjectFactory switchSideQueueFactory;

          switchSideQueueFactory.SetTypeId ("ns3::ECNQueueDisc");

          Ptr<QueueDisc> switchSideQueueDisc = switchSideQueueFactory.Create<QueueDisc> ();

          Ptr<NetDevice> netDevice0 = netDeviceContainer.Get (0);
          Ptr<TrafficControlLayer> tcl0 = netDevice0->GetNode ()->GetObject<TrafficControlLayer> ();

          delayQueueDisc->SetNetDevice (netDevice0);
          tcl0->SetRootQueueDiscOnDevice (netDevice0, delayQueueDisc);

          Ptr<NetDevice> netDevice1 = netDeviceContainer.Get (1);
          Ptr<TrafficControlLayer> tcl1 = netDevice1->GetNode ()->GetObject<TrafficControlLayer> ();
          switchSideQueueDisc->SetNetDevice (netDevice1);
          tcl1->SetRootQueueDiscOnDevice (netDevice1, switchSideQueueDisc);

          Ipv4InterfaceContainer interfaceContainer = ipv4.Assign (netDeviceContainer);

          #if ENABLE_QUEUE_MONITOR == 1
            // Register callback function
            Ipv4Address serverIpAddr =  interfaceContainer.GetAddress (0);

            // sstm_leaf <<  "leafQueue (leafId " << i << ", serverId " << j << " " << serverIpAddr << ")";
            // Enqueue operation (maintain per-flow bytes counter)

            all_bytes_counters.push_back(std::map<uint32_t, uint32_t>());

            switchSideQueueDisc->TraceConnectWithoutContext("Enqueue", MakeBoundCallback(&p4Program, switchSideQueueDisc, classifier, i * SERVER_COUNT + j));
            // switchSideQueueDisc->TraceConnectWithoutContext("Enqueue", MakeBoundCallback(&LeafDownIncomingPkt, i * SERVER_COUNT + j, classifier));
            // delayQueueDisc->TraceConnectWithoutContext("Enqueue", MakeBoundCallback(&ServerDownIncomingPkt, i * SERVER_COUNT + j, classifier));
            // switchSideQueueDisc->TraceConnectWithoutContext("PacketsInQueue", MakeBoundCallback(&printPktsInQueue, sstm_leaf.str()));
            Simulator::Schedule(window, &pollBytesInQueue, serverIpAddr, window, switchSideQueueDisc, i * SERVER_COUNT + j, classifier);
          #endif

          NS_LOG_INFO ("Leaf - " << i << " is connected to Server - " << j << " with address "
                       << interfaceContainer.GetAddress(1) << " <-> " << interfaceContainer.GetAddress (0)
                       << " with port " << netDeviceContainer.Get (1)->GetIfIndex () << " <-> " << netDeviceContainer.Get (0)->GetIfIndex ());
        }
    }

  NS_LOG_INFO ("Configuring switches");
  // Setting up switches
  p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (SPINE_LEAF_CAPACITY)));

  for (int i = 0; i < LEAF_COUNT; i++)
    {
      for (int j = 0; j < SPINE_COUNT; j++)
        {

          for (int l = 0; l < LINK_COUNT; l++)
            {
              ipv4.NewNetwork ();

              NodeContainer nodeContainer = NodeContainer (leaves.Get (i), spines.Get (j));
              NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);
              ObjectFactory switchSideQueueFactory;

              switchSideQueueFactory.SetTypeId ("ns3::ECNQueueDisc");

              Ptr<QueueDisc> leafQueueDisc = switchSideQueueFactory.Create<QueueDisc> ();

              Ptr<NetDevice> netDevice0 = netDeviceContainer.Get (0);
              Ptr<TrafficControlLayer> tcl0 = netDevice0->GetNode ()->GetObject<TrafficControlLayer> ();
              leafQueueDisc->SetNetDevice (netDevice0);
              tcl0->SetRootQueueDiscOnDevice (netDevice0, leafQueueDisc);

              Ptr<QueueDisc> spineQueueDisc = switchSideQueueFactory.Create<QueueDisc> ();

              Ptr<NetDevice> netDevice1 = netDeviceContainer.Get (1);
              Ptr<TrafficControlLayer> tcl1 = netDevice1->GetNode ()->GetObject<TrafficControlLayer> ();
              spineQueueDisc->SetNetDevice (netDevice1);
              tcl1->SetRootQueueDiscOnDevice (netDevice1, spineQueueDisc);

              #if ENABLE_QUEUE_MONITOR == 1
                // Register callback function
                // std::stringstream sstm_leaf;
                // std::stringstream sstm_spine;
                // sstm_leaf <<  "leafQueue (leafId " << i << ", spineId " << j << ")";
                // sstm_spine << "spineQueue (leafId " << i << ", spineId " << j << ")";
                // leafQueueDisc->TraceConnectWithoutContext("PacketsInQueue", MakeBoundCallback(&printPktsInQueue, sstm_leaf.str()));
                // spineQueueDisc->TraceConnectWithoutContext("PacketsInQueue", MakeBoundCallback(&printPktsInQueue, sstm_spine.str()));
                // leafQueueDisc->TraceConnectWithoutContext("Enqueue", MakeBoundCallback(&LeafUpIncomingPkt, i * SPINE_COUNT + j, classifier));
                // spineQueueDisc->TraceConnectWithoutContext("Enqueue", MakeBoundCallback(&SpineDownIncomingPkt, i * SPINE_COUNT + j, classifier));
                // Simulator::Schedule(window, &pollBytesInLeafSpine, window, leafQueueDisc, (i * SPINE_COUNT + j) << 1);
                // Simulator::Schedule(window, &pollBytesInLeafSpine, window, spineQueueDisc, ((i * SPINE_COUNT + j) << 1) + 1);
              #endif

              Ipv4InterfaceContainer ipv4InterfaceContainer = ipv4.Assign (netDeviceContainer);
              NS_LOG_INFO ("Leaf - " << i << " is connected to Spine - " << j << " with address "
                           << ipv4InterfaceContainer.GetAddress(0) << " <-> " << ipv4InterfaceContainer.GetAddress (1)
                           << " with port " << netDeviceContainer.Get (0)->GetIfIndex () << " <-> " << netDeviceContainer.Get (1)->GetIfIndex ()
                           << " with data rate " << spineLeafCapacity);

            }
        }
    }

  NS_LOG_INFO ("Populate global routing tables");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  double oversubRatio = static_cast<double>(SERVER_COUNT * LEAF_SERVER_CAPACITY) / (SPINE_LEAF_CAPACITY * SPINE_COUNT * LINK_COUNT);
  NS_LOG_INFO ("Over-subscription ratio: " << oversubRatio);

  NS_LOG_INFO ("Initialize CDF table");
  struct cdf_table* cdfTable = new cdf_table ();
  init_cdf (cdfTable);
  load_cdf (cdfTable, cdfFileName.c_str ());

  NS_LOG_INFO ("Calculating request rate");
  double requestRate = load * LEAF_SERVER_CAPACITY * SERVER_COUNT / oversubRatio / (8 * avg_cdf (cdfTable)) / SERVER_COUNT;
  NS_LOG_INFO ("Average request rate: " << requestRate << " per second");

  NS_LOG_INFO ("Initialize random seed: " << randomSeed);
  if (randomSeed == 0)
    {
      srand ((unsigned)time (NULL));
    }
  else
    {
      srand (randomSeed);
    }

  NS_LOG_INFO ("Create applications (cdf)");

  long flowCount = 0;
  long totalFlowSize = 0;

  for (int fromLeafId = 0; fromLeafId < LEAF_COUNT; fromLeafId ++)
    {
      install_applications(fromLeafId, servers, requestRate, cdfTable, flowCount, totalFlowSize, SERVER_COUNT, LEAF_COUNT, START_TIME, END_TIME, FLOW_LAUNCH_END_TIME);
    }

  NS_LOG_INFO ("Total flow: " << flowCount);

  NS_LOG_INFO ("Actual average flow size: " << static_cast<double> (totalFlowSize) / flowCount);

  NS_LOG_INFO ("Create applications (incast)");
  install_incast(servers, SERVER_COUNT * LEAF_COUNT, LEAF_COUNT, START_TIME, END_TIME, FLOW_LAUNCH_END_TIME);


  NS_LOG_INFO ("Start simulation");
  Simulator::Stop (Seconds (END_TIME));
  Simulator::Run ();

  #if ENABLE_FLOW_MONITOR == 1
    flowMonitor->SerializeToXmlFile(flowMonitorFilename.str (), true, true);
  #endif

  Simulator::Destroy ();
  free_cdf (cdfTable);
  NS_LOG_INFO ("Stop simulation");
  // Monitor the simulation costing time
  time_t delta = time(NULL) - t1;
  NS_LOG_INFO ("The whole simulation costs " << delta << " seconds!");
}
