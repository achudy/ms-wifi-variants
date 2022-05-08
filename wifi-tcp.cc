/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2015, IMDEA Networks Institute
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Hany Assasa <hany.assasa@gmail.com>
.*
 * This is a simple example to test TCP over 802.11n (with MPDU aggregation enabled).
 *
 * Network topology:
 *
 *   Ap    STA
 *   *      *
 *   |      |
 *   n1     n2
 *
 * In this example, an HT station sends TCP packets to the access point.
 * We report the total throughput received during a window of 100ms.
 * The user can specify the application data rate and choose the variant
 * of TCP i.e. congestion control algorithm to use.
 */

#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/string.h"
#include "ns3/log.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ssid.h"
#include "ns3/mobility-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/mobility-model.h"
#include "ns3/packet-sink.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/tcp-westwood.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"

#define MAXFLOWS 100

NS_LOG_COMPONENT_DEFINE("wifi-tcp");

using namespace ns3;

Ptr<PacketSink> sink;     /* Pointer to the packet sink application */
uint64_t lastTotalRx = 0; /* The value of the last total received bytes */
FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor;
std::ofstream myfile;
void PrintFlowMonitorStats();
void CalculateThroughput();
uint32_t rxBytesWarmup[MAXFLOWS] = {0};
uint32_t rxBytesPrev = 0;
uint32_t warmupTime = 10;
uint32_t interval = 1;

int main(int argc, char *argv[])
{
  uint32_t nWifi = 15;
  uint32_t payloadSize = 1472;           /* Transport layer payload size in bytes. */
  std::string dataRate = "100Mbps";      /* Application layer datarate. */
  std::string tcpVariant = "TcpNewReno"; /* TCP variant type. */
  std::string phyRate = "HtMcs7";        /* Physical layer bitrate. */
  double simulationTime = 10;            /* Simulation time in seconds. */
  bool pcapTracing = false;              /* PCAP Tracing is enabled or not. */

  /* Command line argument parser setup. */
  CommandLine cmd(__FILE__);
  cmd.AddValue("nWifi", "Number of station", nWifi);
  cmd.AddValue("payloadSize", "Payload size in bytes", payloadSize);
  cmd.AddValue("dataRate", "Application data ate", dataRate);
  cmd.AddValue("tcpVariant", "Transport protocol to use: TcpNewReno, "
                             "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                             "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat ",
               tcpVariant);
  cmd.AddValue("phyRate", "Physical layer bitrate", phyRate);
  cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue("pcap", "Enable/disable PCAP Tracing", pcapTracing);
  cmd.Parse(argc, argv);
  std::cout << "- used TCP variant: " << tcpVariant << std::endl;
  std::cout << "- number of transmitting stations: " << nWifi << std::endl;

  tcpVariant = std::string("ns3::") + tcpVariant;
  // Select TCP variant
  if (tcpVariant.compare("ns3::TcpWestwoodPlus") == 0)
  {
    // TcpWestwoodPlus is not an actual TypeId name; we need TcpWestwood here
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpWestwood::GetTypeId()));
    // the default protocol type in ns3::TcpWestwood is WESTWOOD
    Config::SetDefault("ns3::TcpWestwood::ProtocolType", EnumValue(TcpWestwood::WESTWOODPLUS));
  }
  else
  {
    TypeId tcpTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(tcpVariant, &tcpTid), "TypeId " << tcpVariant << " not found");
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName(tcpVariant)));
  }

  /* Configure TCP Options */
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));

  WifiMacHelper wifiMac;
  WifiHelper wifiHelper;
  wifiHelper.SetStandard(WIFI_STANDARD_80211ax_5GHZ);

  /* Set up Legacy Channel */
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel", "Frequency", DoubleValue(5e9));

  /* Setup Physical Layer */
  YansWifiPhyHelper wifiPhy;
  wifiPhy.SetChannel(wifiChannel.Create());
  wifiPhy.SetErrorRateModel("ns3::YansErrorRateModel");
  wifiHelper.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode", StringValue(phyRate),
                                     "ControlMode", StringValue("HtMcs0"));

  //NodeContainer networkNodes;
  //networkNodes.Create(1);
  NodeContainer wifiApNode;
  wifiApNode.Create (1);
  //NodeContainer wifiApNode = networkNodes;
  //Ptr<Node> apWifiNode = networkNodes.Get(0);
  //
  // Ptr<Node> staWifiNode = networkNodes.Get (1);
  //
  NodeContainer wifiStaNodes;
  wifiStaNodes.Create(nWifi);

  /* Configure AP */
  Ssid ssid = Ssid("network");
  wifiMac.SetType("ns3::ApWifiMac",
                  "Ssid", SsidValue(ssid));

  NetDeviceContainer apDevice;
  apDevice = wifiHelper.Install(wifiPhy, wifiMac, wifiApNode);

  /* Configure STA */
  wifiMac.SetType("ns3::StaWifiMac",
                  "Ssid", SsidValue(ssid));

  NetDeviceContainer staDevices;
  staDevices = wifiHelper.Install(wifiPhy, wifiMac, wifiStaNodes);

  /* Mobility model */
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
  positionAlloc->Add(Vector(0.0, 0.0, 0.0));
  positionAlloc->Add(Vector(1.0, 1.0, 0.0));

  mobility.SetPositionAllocator(positionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(wifiApNode);
  mobility.Install(wifiStaNodes);

  /* Internet stack */
  InternetStackHelper stack;
  stack.Install(wifiApNode);

  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface;
  apInterface = address.Assign(apDevice);
  Ipv4InterfaceContainer staInterface;
  staInterface = address.Assign(staDevices);

  /* Populate routing table */
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  /* Install TCP Receiver on the access point */
  // PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), 9));
  // ApplicationContainer sinkApp = sinkHelper.Install (apWifiNode);
  // sink = StaticCast<PacketSink> (sinkApp.Get (0));

  ApplicationContainer sourceApplications, sinkApplications;
  uint32_t portNumber = 9;
  std::string socketFactory;
  socketFactory = "ns3::TcpSocketFactory";
  for (uint32_t index = 0; index < nWifi; ++index) // Loop over all stations (which transmit to the AP)
  {
    auto ipv4 = wifiApNode.Get(0)->GetObject<Ipv4>();       // Get destination's IP interface
    const auto address = ipv4->GetAddress(1, 0).GetLocal(); // Get destination's IP address
    InetSocketAddress sinkSocket(address, portNumber++);
    /* Install TCP/UDP Transmitter on the station */
    OnOffHelper server("ns3::TcpSocketFactory", (InetSocketAddress(apInterface.GetAddress(0), 9)));
    server.SetAttribute("PacketSize", UintegerValue(payloadSize));
    server.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    server.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    server.SetAttribute("DataRate", DataRateValue(DataRate(dataRate)));
    // ApplicationContainer serverApp = server.Install (wifiStaNodes.Get (index));
    sourceApplications.Add(server.Install(wifiStaNodes.Get(index)));
    PacketSinkHelper packetSinkHelper("ns3::UdpSocketFactory", sinkSocket); // Configure traffic sink
    sinkApplications.Add(packetSinkHelper.Install(wifiApNode.Get(0)));      // Install traffic sink on AP
  }

  /* Start Applications */
  sinkApplications.Start(Seconds(0.0));
  sinkApplications.Stop(Seconds(simulationTime + 1));
  sourceApplications.Start(Seconds(1.0));
  sourceApplications.Stop(Seconds(simulationTime + 1));
  Simulator::Schedule(Seconds(1.1), &CalculateThroughput);

  // Install FlowMonitor
  monitor = flowmon.InstallAll();

  /* Enable Traces */
  if (pcapTracing)
  {
    wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
    wifiPhy.EnablePcap("AccessPoint", apDevice);
    wifiPhy.EnablePcap("Station", staDevices);
  }

  /* Start Simulation */
  Simulator::Stop(Seconds(simulationTime + 1));
  Simulator::Run();

  double averageThroughput = ((sink->GetTotalRx() * 8) / (1e6 * simulationTime));

  double throughput = 0;
  for (uint32_t index = 0; index < sinkApplications.GetN(); ++index) // Loop over all traffic sinks
  {
    uint64_t totalBytesThrough = DynamicCast<PacketSink>(sinkApplications.Get(index))->GetTotalRx(); // Get amount of bytes received
    std::cout << "Bytes received: " << totalBytesThrough << std::endl;
    throughput += ((totalBytesThrough * 8) / (simulationTime * 1000000.0)); // Mbit/s
  }

  // Print results
  std::cout << "Results: " << std::endl;
  std::cout << "- network throughput: " << throughput << " Mbit/s" << std::endl;
  std::cout << "\nAverage throughput: " << averageThroughput << " Mbit/s" << std::endl;

  Simulator::Destroy();

  if (averageThroughput < 50)
  {
    NS_LOG_ERROR("Obtained throughput is not in the expected boundaries!");
    exit(1);
  }

  return 0;
}

void PrintFlowMonitorStats()
{
  double flowThr = 0;
  double totalThr = 0;
  uint32_t rxBytes = 0;

  std::map<FlowId, FlowMonitor::FlowStats> flowStats = monitor->GetFlowStats();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

  if (Simulator::Now().GetSeconds() == warmupTime)
  { // First function call, need to initialize rxBytesWarmup
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator stats = flowStats.begin(); stats != flowStats.end(); ++stats)
    {
      rxBytesWarmup[stats->first - 1] = stats->second.rxBytes;
      rxBytesPrev += stats->second.rxBytes;
    }
  }
  else
  {
    myfile << Simulator::Now().GetSeconds() << ",";
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator stats = flowStats.begin(); stats != flowStats.end(); ++stats)
    {
      flowThr = (stats->second.rxBytes - rxBytesWarmup[stats->first - 1]) * 8.0 / ((Simulator::Now().GetSeconds() - warmupTime) * 1e6);
      myfile << flowThr << ", ";
      if (stats->second.rxBytes != 0)
      {
        rxBytes += stats->second.rxBytes;
        totalThr += flowThr;
      }
    }
    myfile << ((rxBytes - rxBytesPrev) * 8 / (interval * 1e6)) << "," << totalThr << std::endl;
    rxBytesPrev = rxBytes;
  }

  Simulator::Schedule(Seconds(interval), &PrintFlowMonitorStats); // Schedule next stats printout
}

void CalculateThroughput()
{
  Time now = Simulator::Now();                                       /* Return the simulator's virtual time. */
  double cur = (sink->GetTotalRx() - lastTotalRx) * (double)8 / 1e5; /* Convert Application RX Packets to MBits. */
  std::cout << now.GetSeconds() << "s: \t" << cur << " Mbit/s" << std::endl;
  lastTotalRx = sink->GetTotalRx();
  Simulator::Schedule(MilliSeconds(100), &CalculateThroughput);
}