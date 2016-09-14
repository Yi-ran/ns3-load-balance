/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-tlb.h"

#include "ns3/log.h"
#include "ns3/node.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4TLB");

NS_OBJECT_ENSURE_REGISTERED (Ipv4TLB);

Ipv4TLB::Ipv4TLB ():
    m_S (1000000)
{
    NS_LOG_FUNCTION (this);
}

Ipv4TLB::Ipv4TLB (const Ipv4TLB &other):
    m_S (other.m_S)
{
    NS_LOG_FUNCTION (this);
}

TypeId
Ipv4TLB::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::Ipv4TLB")
        .SetParent<Object> ()
        .SetGroupName ("TLB")
        .AddConstructor<Ipv4TLB> ();

    return tid;
}

void
Ipv4TLB::AddAddressWithTor (Ipv4Address address, uint32_t torId)
{
    m_ipTorMap[address] = torId;
}

uint32_t
Ipv4TLB::GetPath (uint32_t flowId, Ipv4Address daddr)
{
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return 0;
    }

    std::map<uint32_t, TLBFlowInfo>::iterator flowItr = m_flowInfo.find (flowId);

    // First check if the flow is a new flow
    if (flowItr == m_flowInfo.end ())
    {
        // New flow
        uint32_t newPath = 0;
        if (!Ipv4TLB::WhereToChange (newPath))
        {
            newPath = Ipv4TLB::SelectRandomPath ();
        }
        Ipv4TLB::UpdateFlowPath (flowId, newPath);
        Ipv4TLB::AssignFlowToPath (flowId, destTor, newPath);
        return newPath;
    }
    else
    {
        // Old flow
        uint32_t oldPath = (flowItr->second).path;
        if (Ipv4TLB::JudgePath (oldPath)  == BadPath && (flowItr->second).size >= m_S)
        {
            uint32_t newPath = 0;
            if (Ipv4TLB::WhereToChange (newPath))
            {
                // Change path
                Ipv4TLB::UpdateFlowPath (flowId, newPath);
                Ipv4TLB::RemoveFlowFromPath (flowId, destTor, oldPath);
                Ipv4TLB::AssignFlowToPath (flowId, destTor, newPath);
                return newPath;
            }
            else
            {
                // Do not change path
                return oldPath;
            }
        }
        else
        {
            return oldPath;
        }
    }
}

void
Ipv4TLB::FlowRecv (uint32_t flowId, uint32_t path, Ipv4Address daddr, uint32_t size, bool withECN, Time rtt)
{
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }

    Ipv4TLB::PacketReceive (flowId, path, destTor, size, withECN, rtt, false);
}

void
Ipv4TLB::FlowRetransmission (uint32_t flowId, Ipv4Address daddr, uint32_t path)
{
     uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }

    bool notChangePath = Ipv4TLB::RetransFlow (flowId, path);
    if (!notChangePath)
    {
        NS_LOG_LOGIC ("The flow has changed the path");
    }
    Ipv4TLB::RetransPath (destTor, path);

}

void
Ipv4TLB::FlowTimeout (uint32_t flowId, Ipv4Address daddr, uint32_t path)
{
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }

    bool notChangePath = Ipv4TLB::TimeoutFlow (flowId, path);
    if (!notChangePath)
    {
        NS_LOG_LOGIC ("The flow has changed the path");
    }
    Ipv4TLB::TimeoutPath (destTor, path, false);
}

uint32_t
Ipv4TLB::GetProbingPath (Ipv4Address daddr)
{
    return 0;
}

void
Ipv4TLB::ProbeRecv (uint32_t path, Ipv4Address daddr, uint32_t size, bool withECN, Time rtt)
{
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }
    Ipv4TLB::PacketReceive (0, path, destTor, size, withECN, rtt, true);
}

void
Ipv4TLB::ProbeTimeout (uint32_t path, Ipv4Address daddr)
{

}


void
Ipv4TLB::SetNode (Ptr<Node> node)
{
    m_node = node;
}

void
Ipv4TLB::PacketReceive (uint32_t flowId, uint32_t path, uint32_t destTorId,
                        uint32_t size, bool withECN, Time rtt, bool isProbing)
{
    // If the packet acks the current path the flow goes, update the flow table and path table
    // If not or the packet is a probing, update the path table
    if (!isProbing)
    {
        bool notChangePath = Ipv4TLB::UpdateFlowInfo (flowId, path, size, withECN);
        if (!notChangePath)
        {
            NS_LOG_LOGIC ("The flow has changed the path");
        }
    }
    Ipv4TLB::UpdatePathInfo (destTorId, path, size, withECN, rtt);
}

bool
Ipv4TLB::UpdateFlowInfo (uint32_t flowId, uint32_t path, uint32_t size, bool withECN)
{
    std::map<uint32_t, TLBFlowInfo>::iterator itr = m_flowInfo.find (flowId);
    if (itr == m_flowInfo.end ())
    {
        NS_LOG_ERROR ("Cannot update info for a non-existing flow");
        return false;
    }
    if ((itr->second).path != path)
    {
        return false;
    }
    (itr->second).size += size;
    if (withECN)
    {
        (itr->second).ecnSize += size;
    }
    return true;
}

void
Ipv4TLB::UpdatePathInfo (uint32_t destTor, uint32_t path, uint32_t size, bool withECN, Time rtt)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);

    TLBPathInfo pathInfo;
    if (itr == m_pathInfo.end ())
    {
        pathInfo.pathId = path;
        pathInfo.size = 0;
        pathInfo.ecnSize = 0;
        pathInfo.minRtt = Seconds (666);
        pathInfo.isRetransmission = false;
        pathInfo.isTimeout = false;
        pathInfo.isProbingTimeout = false;
        pathInfo.flowCounter = 0; // XXX Notice the flow count will be update using Add/Remove Flow To/From Path method
    }
    else
    {
        pathInfo = itr->second;
    }

    pathInfo.size += size;
    if (withECN)
    {
        pathInfo.ecnSize += size;
    }
    if (rtt < pathInfo.minRtt)
    {
        pathInfo.minRtt = rtt;
    }

    m_pathInfo[key] = pathInfo;
}

bool
Ipv4TLB::TimeoutFlow (uint32_t flowId, uint32_t path)
{
    std::map<uint32_t, TLBFlowInfo>::iterator itr = m_flowInfo.find (flowId);
    if (itr == m_flowInfo.end ())
    {
        NS_LOG_ERROR ("Cannot timeout a non-existing flow");
        return false;
    }
    if ((itr->second).path != path)
    {
        return false;
    }
    (itr->second).isTimeout = true;
    return true;
}


bool
Ipv4TLB::RetransFlow (uint32_t flowId, uint32_t path)
{
    std::map<uint32_t, TLBFlowInfo>::iterator itr = m_flowInfo.find (flowId);
    if (itr == m_flowInfo.end ())
    {
        NS_LOG_ERROR ("Cannot retransmit a non-existing flow");
        return false;
    }
    if ((itr->second).path != path)
    {
        return false;
    }
    (itr->second).retransmissionCount ++;
    return true;
}


void
Ipv4TLB::TimeoutPath (uint32_t destTor, uint32_t path, bool isProbing)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);
    if (itr == m_pathInfo.end ())
    {
        NS_LOG_ERROR ("Cannot timeout a non-existing path");
        return;
    }
    if (!isProbing)
    {
        (itr->second).isTimeout = true;
    }
    else
    {
        (itr->second).isProbingTimeout = true;
    }
}

void
Ipv4TLB::RetransPath (uint32_t destTor, uint32_t path)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);
    if (itr == m_pathInfo.end ())
    {
        NS_LOG_ERROR ("Cannot timeout a non-existing path");
        return;
    }
    (itr->second).isRetransmission = true;
}

void
Ipv4TLB::UpdateFlowPath (uint32_t flowId, uint32_t path)
{
    TLBFlowInfo flowInfo;
    flowInfo.path = path;
    flowInfo.size = 0;
    flowInfo.ecnSize = 0;
    flowInfo.retransmissionCount = 0;
    flowInfo.isTimeout = false;

    m_flowInfo[flowId] = flowInfo;
}

void
Ipv4TLB::AssignFlowToPath (uint32_t flowId, uint32_t destTor, uint32_t path)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);
    if (itr == m_pathInfo.end ())
    {
        NS_LOG_ERROR ("Cannot assign flow to a non-existing path");
        return;
    }
    (itr->second).flowCounter ++;
}

void
Ipv4TLB::RemoveFlowFromPath (uint32_t flowId, uint32_t destTor, uint32_t path)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);
    if (itr == m_pathInfo.end ())
    {
        NS_LOG_ERROR ("Cannot remove flow from a non-existing path");
        return;
    }
    if ((itr->second).flowCounter == 0)
    {
        NS_LOG_ERROR ("Cannot decrease from counter while it has reached 0");
        return;
    }
    (itr->second).flowCounter --;

}

bool
Ipv4TLB::FindTorId (Ipv4Address daddr, uint32_t &destTorId)
{
    std::map<Ipv4Address, uint32_t>::iterator torItr = m_ipTorMap.find (daddr);

    if (torItr == m_ipTorMap.end ())
    {
        return false;
    }
    destTorId = torItr->second;
    return true;
}

}