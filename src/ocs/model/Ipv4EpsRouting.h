//
// Created by schedule on 6/1/23.
//

#ifndef NS3_IPV4EPSROUTING_H
#define NS3_IPV4EPSROUTING_H

#include "ns3/ipv4-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/ptr.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/socket.h"
#include "ns3/node-container.h"
#include "ns3/random-variable-stream.h"

#include <list>
#include <stdint.h>
#include <utility>
#include <vector>
#include <map>

namespace ns3{
class Packet;
class NetDevice;
class Ipv4Interface;
class Ipv4Address;
class Ipv4Header;
class Ipv4RoutingTableEntry;
class Ipv4MulticastRoutingTableEntry;
class Node;

class Flow : public Object
{
  public:
    Flow(Ipv4Address srcIp,Ipv4Address destIp,uint8_t protocol, uint16_t src_port, uint16_t dst_port){
        this->srcip = srcIp;
        this->destip = destIp;
        this->srcport = src_port;
        this->destport = dst_port;
        this->proto = protocol;
        this->left_ack = this->right_ack = 0;
        this->max_cwnd = 0;
    }
    ~Flow(){};

    //return a hash value of a packet
    static size_t HashPacket(Ptr<Packet> p){
        Ipv4Header ip;
        p->RemoveHeader(ip);
        auto proto = ip.GetProtocol();
        if(proto == 6){
            TcpHeader tcp;
            p->PeekHeader(tcp);
            p->AddHeader(ip);
            return HashPacket(ip.GetSource(),ip.GetDestination(),ip.GetProtocol(),tcp.GetSourcePort(),tcp.GetDestinationPort());
        }
        //udp
        else if(proto == 17)
        {
            UdpHeader udp;
            p->PeekHeader(udp);
            p->AddHeader(ip);
            return HashPacket(ip.GetSource(),ip.GetDestination(),ip.GetProtocol(),udp.GetSourcePort(),udp.GetDestinationPort());
        }
        return -1;
    }

    static size_t HashPacket(Ipv4Address srcIp,Ipv4Address destIp, uint8_t protocol, uint16_t src_port, uint16_t dst_port){
        return std::hash<std::string>{} (std::to_string(srcIp.Get())+std::to_string(destIp.Get())+std::to_string(protocol)+std::to_string(src_port)+std::to_string(dst_port));
    }

    //set and return the hash of this flow
    size_t HashFLow(){
        this->hash_value = std::hash<std::string>{} (std::to_string(this->srcip.Get())+std::to_string(this->destip.Get())+std::to_string(this->proto)+std::to_string(this->srcport)+std::to_string(this->destport));
        return hash_value;
    }

    bool EqualFLow(Ptr<Packet> p){
        Ipv4Header ip;
        p->RemoveHeader(ip);
        auto protocol = ip.GetProtocol();
        if(protocol == 6){
            TcpHeader tcp;
            p->PeekHeader(tcp);
            p->AddHeader(ip);
            return EqualFLow(ip.GetSource(),ip.GetDestination(),ip.GetProtocol(),tcp.GetSourcePort(),tcp.GetDestinationPort());
        }
        //udp
        else if(protocol == 17)
        {
            UdpHeader udp;
            p->PeekHeader(udp);
            p->AddHeader(ip);
            return EqualFLow(ip.GetSource(),ip.GetDestination(),ip.GetProtocol(),udp.GetSourcePort(),udp.GetDestinationPort());
        }
        return false;
    }


    bool EqualFLow(Ipv4Address srcIp,Ipv4Address destIp, uint16_t src_port, uint16_t dst_port, uint8_t protocol){
        return (this->srcip==srcIp)&&(this->destip==destIp)&&(this->srcport=src_port)&&(this->destport==dst_port)&&(this->proto == protocol);
    }

    void DropPacket(){
        this->enqueued = false;
        this->left_ack = this->right_ack;
        this->max_cwnd = 0;
    }

    bool GetEnqueueStatus(){return this->enqueued;}

    size_t GetCwnd() {return this->max_cwnd;}

    //below 2 functions
    //receive data packet, update right_ack
    void ReceiveSequence(uint32_t sequence,uint32_t length) {
        //retransmission
        if(sequence+length <= right_ack)
        {
            left_ack = right_ack;
            return ;
        }
        if(sequence >= right_ack)
        {
            left_ack = sequence;
            right_ack = sequence+ length;
        }
        this->right_ack = sequence+length;
        if(right_ack - left_ack > max_cwnd)
            max_cwnd = right_ack - left_ack;
    }
    //receive ack packet, update right ack
    void ReceiveAck(uint32_t acknumber){
        if(acknumber <= right_ack && acknumber>left_ack)
            this->left_ack = acknumber;
    }

  private:
    Ipv4Address srcip,destip;
    uint16_t srcport,destport;
    uint8_t proto;
    size_t hash_value;
    //status
    bool enqueued;
    size_t left_ack,right_ack;
    size_t max_cwnd;
};


class Ipv4EpsRouting : public Ipv4RoutingProtocol
{
  public:
    static TypeId GetTypeId();
    Ipv4EpsRouting();
    Ipv4EpsRouting(int queue_number,uint32_t mice_threshold, double queue_space_threshold,double bypasspro);
    ~Ipv4EpsRouting() override;

    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;

    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& ipHeader,
                    Ptr<const NetDevice> idev,
                    UnicastForwardCallback ucb,
                    MulticastForwardCallback mcb,
                    LocalDeliverCallback lcb,
                    ErrorCallback ecb) override;

    void NotifyInterfaceUp(uint32_t interface) override;
    void NotifyInterfaceDown(uint32_t interface) override;
    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void SetIpv4(Ptr<Ipv4> ipv4) override;
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;

    void AddNetworkRouteTo(Ipv4Address network,
                           Ipv4Mask networkMask,
                           Ipv4Address nextHop,
                           uint32_t interface,
                           uint32_t metric = 0);

    void AddNetworkRouteTo(Ipv4Address network,
                           Ipv4Mask networkMask,
                           uint32_t interface,
                           uint32_t metric = 0);

    void SetDefaultRoute(Ipv4Address nextHop, uint32_t interface, uint32_t metric = 0);
    uint32_t GetNRoutes() const;
    Ipv4RoutingTableEntry GetRoute(uint32_t i) const;
    uint32_t GetMetric(uint32_t index) const;
    void RemoveRoute(uint32_t i);

    //Get into night,and return the working queue index
    uint32_t BeginConfig(Ipv4Address new_addr);
    //
    uint32_t BeginConfig();
    //Get into day
    void BeginWork();
    //
    void SetDevicetoOcs(Ptr<NetDevice> nd){
        this->to_ocs_device = nd;
    }

    uint32_t GetQueueNumber(){return this->queue_number;}

    void AddDestAddr(Ipv4Address ip){this->ocs_dest_addr.emplace_back(ip);}

    enum BypassStrategy{nobypass,randomize,cwndbased};

    void SetBypassStrategy(BypassStrategy bs) {this->m_bypass = bs;}

  protected:
    void DoDispose() override;


  private:
    /// Container for the network routes
    typedef std::list<std::pair<Ipv4RoutingTableEntry*, uint32_t>> NetworkRoutes;
    typedef std::list<std::pair<Ipv4RoutingTableEntry*, uint32_t>>::const_iterator NetworkRoutesCI;
    typedef std::list<std::pair<Ipv4RoutingTableEntry*, uint32_t>>::iterator NetworkRoutesI;
    bool LookupRoute(const Ipv4RoutingTableEntry& route, uint32_t metric);
    Ptr<Ipv4Route> LookupEps(Ipv4Address dest, Ptr<NetDevice> oif = nullptr);
    bool SearchFlowEnqueued(Ipv4Address srcIp,Ipv4Address destIp, uint16_t src_port, uint16_t dst_port, uint8_t protocol,uint32_t q_idx );
    void AddEnqueuedFlow(uint32_t q_idx,Ipv4Address srcIp,Ipv4Address destIp, uint16_t src_port, uint16_t dst_port, uint8_t protocol  );
    Ptr<Flow> AddUpFlow(Ipv4Address srcIp,Ipv4Address destIp, uint16_t src_port, uint16_t dst_port, uint8_t protocol  );
    double GetBypassRandom(){return this->bypassrandom->GetValue();}
    uint32_t GetQueueIdx ( Ipv4Address dest);
//    uint32_t GetLeftFlowSize( Ptr< Packet> p,uint16_t q_idx);
    double GetQueueSpace(uint32_t q_idx);
    //get flow from all up flows
    Ptr<Flow> Getflow(size_t hash){
        auto pf = this->all_upflows.find(hash);
        if(pf == this->all_upflows.end())
            return nullptr;
        else
            return pf->second;
    };

    //this route only aim to one device, so we do not need a
    NetworkRoutes m_networkRoutes;

    Ptr<Ipv4> m_ipv4;
    //the device connect to ocs switch
    Ptr<NetDevice> to_ocs_device;
    // addrs which could be in touch by ocs in n days(n is the number of the queues)
    std::vector<Ipv4Address> ocs_dest_addr;
    //the addr which is connected by ocs now, use an additionnal index to avoid circulating write of the vector above

    // number of queues
    uint32_t queue_number;
    // the index of the working queue
    uint32_t working_queue_index;

    //the threshold of mice/elephant flow, for now , its unit is packet number
    uint32_t mice_thresh;
    //
    double queue_space_thresh;

    // should be a list or map, we need to classify flows by their dest TOR
    //depend on the number of flow, if many flows, map, else list
    std::vector<std::map<size_t,Ptr<Flow>>> enqueued_flows;

    //all flows
    std::map<size_t,Ptr<Flow>> all_upflows;


    //when at night
    bool atnight;

    //random variable for bypass
    Ptr<UniformRandomVariable> bypassrandom;
    //
    double bypassprobility;
    //
    BypassStrategy m_bypass;
    //
    uint32_t cwnd_thresh;
};
}



#endif // NS3_IPV4EPSROUTING_H