/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
 * Copyright (c) 2019 Tom Henderson (update to IETF draft -10)
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
 * Author: Shravya K.S. <shravya.ks0@gmail.com>
 *
 */

#include "dual-pi2-queue-disc.h"

#include "ns3/abort.h"
#include "ns3/assert.h"
#include "ns3/double.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/enum.h"
#include "ns3/fatal-error.h"
#include "ns3/log.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/object-factory.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <cmath>
#include <cstddef>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DualPi2QueueDisc");

NS_OBJECT_ENSURE_REGISTERED(DualPi2QueueDisc);

// Used as an index into the GetInternalQueue() method
constexpr std::size_t CLASSIC = 0;
constexpr std::size_t L4S = 1;
constexpr std::size_t NONE = 2;

TypeId
DualPi2QueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::DualPi2QueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<DualPi2QueueDisc>()
            .AddAttribute("Mtu",
                          "Device MTU (bytes); if zero, will be automatically configured",
                          UintegerValue(0),
                          MakeUintegerAccessor(&DualPi2QueueDisc::m_mtu),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("A",
                          "Value of alpha (Hz)",
                          DoubleValue(0.15),
                          MakeDoubleAccessor(&DualPi2QueueDisc::m_alpha),
                          MakeDoubleChecker<double>())
            .AddAttribute("B",
                          "Value of beta (Hz)",
                          DoubleValue(3),
                          MakeDoubleAccessor(&DualPi2QueueDisc::m_beta),
                          MakeDoubleChecker<double>())
            .AddAttribute("Tupdate",
                          "Time period to calculate drop probability",
                          TimeValue(Seconds(0.015)),
                          MakeTimeAccessor(&DualPi2QueueDisc::m_tUpdate),
                          MakeTimeChecker())
            .AddAttribute("QueueLimit",
                          "Queue limit in bytes",
                          UintegerValue(1562500), // 250 ms at 50 Mbps
                          MakeUintegerAccessor(&DualPi2QueueDisc::m_queueLimit),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Target",
                          "PI AQM Classic queue delay target",
                          TimeValue(MilliSeconds(15)),
                          MakeTimeAccessor(&DualPi2QueueDisc::m_target),
                          MakeTimeChecker())
            .AddAttribute("MinTh",
                          "Laqm marking threshold",
                          TimeValue(MicroSeconds(800)),
                          MakeTimeAccessor(&DualPi2QueueDisc::m_minTh),
                          MakeTimeChecker())
            .AddAttribute("Range",
                          "Laqm marking range",
                          TimeValue(MicroSeconds(400)),
                          MakeTimeAccessor(&DualPi2QueueDisc::m_range),
                          MakeTimeChecker())
            .AddAttribute("K",
                          "Coupling factor",
                          DoubleValue(2),
                          MakeDoubleAccessor(&DualPi2QueueDisc::m_k),
                          MakeDoubleChecker<double>())
            .AddAttribute("StartTime", // Only if user wants to change queue start time
                          "Simulation time to start scheduling the update timer",
                          TimeValue(Seconds(0.0)),
                          MakeTimeAccessor(&DualPi2QueueDisc::m_startTime),
                          MakeTimeChecker())
            .AddAttribute("SchedulingWeight",
                          "Scheduling weight to apply to WDRR L4S quantum (number of L4S quantums "
                          "per CLASSIC quantum)",
                          DoubleValue(9), // 90% weight (9/(9+1))
                          MakeDoubleAccessor(&DualPi2QueueDisc::m_schedulingWeight),
                          MakeDoubleChecker<double>(1, std::numeric_limits<double>::max()))
            .AddAttribute("DrrQuantum",
                          "Quantum used in weighted DRR policy (bytes)",
                          UintegerValue(1500),
                          MakeUintegerAccessor(&DualPi2QueueDisc::m_drrQuantum),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("DisableLaqm",
                          "Whether to disable Laqm calculation and return zero",
                          BooleanValue(false),
                          MakeBooleanAccessor(&DualPi2QueueDisc::m_disableLaqm),
                          MakeBooleanChecker())
            .AddAttribute(
                "EnableWifiClassicLatencyEstimator",
                "Whether to enable alternative latency estimator",
                BooleanValue(false),
                MakeBooleanAccessor(&DualPi2QueueDisc::m_enableWifiClassicLatencyEstimator),
                MakeBooleanChecker())
            .AddTraceSource("ProbCL",
                            "Coupled probability (p_CL)",
                            MakeTraceSourceAccessor(&DualPi2QueueDisc::m_pCL),
                            "ns3::TracedValueCallback::Double")
            .AddTraceSource("ProbL",
                            "L4S mark probability (p_L)",
                            MakeTraceSourceAccessor(&DualPi2QueueDisc::m_pL),
                            "ns3::TracedValueCallback::Double")
            .AddTraceSource("ProbC",
                            "Classic drop/mark probability (p_C)",
                            MakeTraceSourceAccessor(&DualPi2QueueDisc::m_pC),
                            "ns3::TracedValueCallback::Double")
            .AddTraceSource("ClassicSojournTime",
                            "Sojourn time of the last packet dequeued from the Classic queue",
                            MakeTraceSourceAccessor(&DualPi2QueueDisc::m_traceClassicSojourn),
                            "ns3::Time::TracedCallback")
            .AddTraceSource("L4sSojournTime",
                            "Sojourn time of the last packet dequeued from the L4S queue",
                            MakeTraceSourceAccessor(&DualPi2QueueDisc::m_traceL4sSojourn),
                            "ns3::Time::TracedCallback");
    return tid;
}

DualPi2QueueDisc::DualPi2QueueDisc()
    : QueueDisc()
{
    NS_LOG_FUNCTION(this);
    m_rtrsEvent = Simulator::Schedule(m_startTime, &DualPi2QueueDisc::DualPi2Update, this);
}

DualPi2QueueDisc::~DualPi2QueueDisc()
{
    NS_LOG_FUNCTION(this);
}

void
DualPi2QueueDisc::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_rtrsEvent.Cancel();
    QueueDisc::DoDispose();
}

void
DualPi2QueueDisc::SetQueueLimit(uint32_t lim)
{
    NS_LOG_FUNCTION(this << lim);
    m_queueLimit = lim;
}

void
DualPi2QueueDisc::SetAggregationBufferLimit(uint32_t limit)
{
    NS_LOG_FUNCTION(this << limit);
    m_aggBufferLimit = limit;
}

uint32_t
DualPi2QueueDisc::GetQueueSize() const
{
    NS_LOG_FUNCTION(this);
    return (GetInternalQueue(CLASSIC)->GetNBytes() + GetInternalQueue(L4S)->GetNBytes());
}

void
DualPi2QueueDisc::PendingDequeueCallback(uint32_t pendingBytes)
{
    NS_LOG_FUNCTION(this << pendingBytes);
    // For alternative classic latency estimator
    Ptr<const QueueDiscItem> item;
    if ((item = GetInternalQueue(CLASSIC)->Peek()))
    {
        m_cLatencySample = Simulator::Now() - item->GetTimeStamp();
    }
    else
    {
        m_cLatencySample = Seconds(0);
    }
    if ((item = GetInternalQueue(L4S)->Peek()))
    {
        m_lLatencySample = Simulator::Now() - item->GetTimeStamp();
    }
    else
    {
        m_lLatencySample = Seconds(0);
    }
    m_cBytesSample = GetInternalQueue(CLASSIC)->GetNBytes();
    if (!GetNetDeviceQueueInterface() || !GetNetDeviceQueueInterface()->GetTxQueue(0)->IsStopped())
    {
        NS_LOG_DEBUG("Queue is not stopped so no need to process the value");
        return;
    }
    else
    {
        NS_LOG_DEBUG("Queue is stopped; process the reported value " << pendingBytes);
        // pendingBytes represents the Wi-Fi framed value of any packet
        // For every QueueDiscItem packet in this queue, add 38 bytes
        // for its queue size below.
        NS_LOG_DEBUG("QueueDisc holds " << GetNBytes() << " bytes in " << GetNPackets()
                                        << " packets");
        uint32_t queueDiscPending = GetNBytes() + 38 * GetNPackets();
        NS_LOG_DEBUG("The amount to be queued at WifiMacQueue is " << queueDiscPending);
        if (pendingBytes > queueDiscPending)
        {
            NS_LOG_DEBUG("WifiMacQueue can handle the pending " << pendingBytes);
            return;
        }
    }
    // The current queue size exceeds the pending dequeue.  Determine which
    // packets will be dequeued, and which packets should be marked.
    uint32_t lBytes [[maybe_unused]] = GetInternalQueue(L4S)->GetNBytes();
    uint32_t lPackets [[maybe_unused]] = GetInternalQueue(L4S)->GetNPackets();
    uint32_t cBytes [[maybe_unused]] = GetInternalQueue(CLASSIC)->GetNBytes();
    uint32_t cPackets [[maybe_unused]] = GetInternalQueue(CLASSIC)->GetNPackets();

    NS_LOG_DEBUG("State before PendingDequeue logic: pendingBytes "
                 << pendingBytes << " l4sBytes " << lBytes << " l4sPackets " << lPackets
                 << " classicBytes " << cBytes << " cPackets " << cPackets);

    // Dequeue enough packets to use up to queueDiscPending bytes, and add to staging queues
    // Keep track of how many of these are L4S packets and are marked
    // Dequeue using the scheduler logic which will apply Laqm and coupled marking, and drops

    uint32_t pendingBytesLeft = pendingBytes;
    uint32_t markedCount = 0;      // How many of those L4S packets are marked
    uint32_t maxIterations = 1000; // Prevent a deadlock simulation loop
    for (uint32_t i = 0; i <= maxIterations; i++)
    {
        NS_ASSERT_MSG(i < maxIterations, "Error: infinite loop");
        auto eligible = CanSchedule(pendingBytesLeft);
        if (!eligible.first && !eligible.second)
        {
            NS_LOG_DEBUG("Cannot schedule further with pendingBytesLeft " << pendingBytesLeft);
            break;
        }
        auto scheduled = Scheduler(eligible);
        if (scheduled == L4S)
        {
            bool marked = false;
            auto qdItem = DequeueFromL4sQueue(marked);
            if (!qdItem) // Check for the drop!
            {
                continue;
            }
            NS_ASSERT_MSG(qdItem, "Error, scheduler failed");
            NS_ASSERT_MSG(qdItem->GetSize() + 38 <= pendingBytesLeft,
                          "Error, insufficient pending bytes");
            AddToL4sStagingQueue(qdItem);
            pendingBytesLeft -= (qdItem->GetSize() + 38); // 38 bytes per packet will be added
            if (marked)
            {
                NS_LOG_INFO("Moved marked L4S packet to staging queue; size "
                            << qdItem->GetSize() << "; timestamp "
                            << qdItem->GetTimeStamp().GetMicroSeconds() << " us");
                markedCount++;
            }
            else
            {
                NS_LOG_INFO("Moved unmarked L4S packet to staging queue; size "
                            << qdItem->GetSize() << "; timestamp "
                            << qdItem->GetTimeStamp().GetMicroSeconds() << " us");
            }
        }
        else if (scheduled == CLASSIC)
        {
            bool dropped [[maybe_unused]] = false;
            auto qdItem = DequeueFromClassicQueue(dropped);
            if (!qdItem) // Check for the drop!
            {
                continue;
            }
            NS_ASSERT_MSG(qdItem, "Error, scheduler failed");
            NS_ASSERT_MSG(qdItem->GetSize() + 38 <= pendingBytesLeft,
                          "Error, insufficient pending bytes");
            NS_LOG_INFO("Moved CLASSIC packet to staging queue; size "
                        << qdItem->GetSize() << "; timestamp "
                        << qdItem->GetTimeStamp().GetMicroSeconds() << " us");
            AddToClassicStagingQueue(qdItem);
            pendingBytesLeft -= (qdItem->GetSize() + 38);
        }
        else
        {
            break;
        }
    }
    // There are 'markedCount' packets marked in the staging queue.  These will have been
    // marked if there is any coupled marking probability.
    if (markedCount)
    {
        NS_ASSERT_MSG(m_pCL > 0, "There should not be any marks if coupling probability is zero");
    }
    // We want the number of marks in the pending queue to at least equal the number of
    // remaining packets in the L queue.  If markedCount >= remainingPackets already,
    // do nothing; otherwise, traverse the L staging queue and mark until 'remainingPackets'
    // worth of marks are made in the staging queue.
    if (GetInternalQueue(L4S)->GetNPackets() > markedCount)
    {
        uint32_t pendingMarks = GetInternalQueue(L4S)->GetNPackets() - markedCount;
        NS_LOG_DEBUG("After PendingDequeue logic:  Apply " << pendingMarks << " more marks");
        auto it = m_l4sStagingQueue.begin();
        while (it != m_l4sStagingQueue.end() && pendingMarks)
        {
            uint8_t tosByte = 0;
            if ((*it)->GetUint8Value(QueueItem::IP_DSFIELD, tosByte) && ((tosByte & 0x3) == 1))
            {
                (*it)->Mark();
                pendingMarks--;
            }
            it++;
        }
    }
    else
    {
        NS_LOG_DEBUG("After PendingDequeue logic:  No further marks needed");
    }
}

bool
DualPi2QueueDisc::IsL4s(Ptr<QueueDiscItem> item)
{
    uint8_t tosByte = 0;
    if (item->GetUint8Value(QueueItem::IP_DSFIELD, tosByte))
    {
        // ECT(1) or CE
        if ((tosByte & 0x3) == 1 || (tosByte & 0x3) == 3)
        {
            NS_LOG_DEBUG("L4S detected: " << static_cast<uint16_t>(tosByte & 0x3));
            return true;
        }
    }
    NS_LOG_DEBUG("Classic detected; TOS byte: " << static_cast<uint16_t>(tosByte & 0x3));
    return false;
}

bool
DualPi2QueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);
    auto queueNumber = CLASSIC;

    auto nQueued = GetQueueSize();
    // in pseudocode, it compares to MTU, not packet size
    if (nQueued + item->GetSize() > m_queueLimit)
    {
        // Drops due to queue limit
        NS_LOG_INFO("Drop packet due to queue limit " << m_queueLimit);
        DropBeforeEnqueue(item, FORCED_DROP);
        return false;
    }
    else
    {
        if (IsL4s(item))
        {
            queueNumber = L4S;
        }
    }

    bool retval = GetInternalQueue(queueNumber)->Enqueue(item);
    NS_LOG_INFO("Enqueue packet in queue "
                << queueNumber << " size: " << item->GetSize()
                << " nPackets: " << GetInternalQueue(queueNumber)->GetNPackets());
    return retval;
}

void
DualPi2QueueDisc::InitializeParams()
{
    if (m_mtu == 0)
    {
        Ptr<NetDeviceQueueInterface> ndqi = GetNetDeviceQueueInterface();
        Ptr<NetDevice> dev;
        // if the NetDeviceQueueInterface object is aggregated to a
        // NetDevice, get the MTU of such NetDevice
        if (ndqi && (dev = ndqi->GetObject<NetDevice>()))
        {
            m_mtu = dev->GetMtu();
        }
    }
    NS_ABORT_MSG_IF(m_mtu < 68, "Error: MTU does not meet RFC 791 minimum");
    m_thLen = 1; // packets
    m_prevQ = Time(Seconds(0));
    m_pCL = 0;
    m_pC = 0;
    m_pL = 0;
    m_pCmax = std::min<double>((1 / (m_k * m_k)), 1);
    m_pLmax = 1;
}

void
DualPi2QueueDisc::DualPi2Update()
{
    NS_LOG_FUNCTION(this);

    // Use queuing time of first-in Classic packet
    Ptr<const QueueDiscItem> item;
    Time curQ;
    Time cQ;
    Time lQ;

    if (m_enableWifiClassicLatencyEstimator)
    {
        NS_ABORT_MSG_UNLESS(m_aggBufferLimit,
                            "Configuration error: aggregation buffer limit is not set");
        // Upon blockAck, store max sojourn time of head-of-queue packet
        // for L & C and queueBytes (C queue only) prior to re-filling
        // aggregation buffer.  Upon PI update, use most recent samples
        // of sojourn time and queueBytes to form this latency estimate:
        // l1 = sojournTime
        // l2 = queueBytes * latencyTarget / queueBytesTarget
        // latency_estimate = min(l1,l2)
        //
        Time l1 = std::max<Time>(m_cLatencySample, m_lLatencySample);
        // The following expression avoids conversion to floating point
        Time l2 = NanoSeconds((m_cBytesSample * m_target.GetNanoSeconds()) / m_aggBufferLimit);
        cQ = std::min<Time>(l1, l2);
    }
    else
    {
        if ((item = GetInternalQueue(CLASSIC)->Peek()))
        {
            cQ = Simulator::Now() - item->GetTimeStamp();
        }
    }
    if ((item = GetInternalQueue(L4S)->Peek()))
    {
        lQ = Simulator::Now() - item->GetTimeStamp();
    }
    curQ = std::max<Time>(cQ, lQ);

    m_baseProb = m_baseProb + m_alpha * (curQ - m_target).GetSeconds() +
                 m_beta * (curQ - m_prevQ).GetSeconds();
    // clamp p' to within [0,1]; page 34 of Internet-Draft
    m_baseProb = std::max<double>(m_baseProb, 0);
    m_baseProb = std::min<double>(m_baseProb, 1);
    m_pCL = m_baseProb * m_k;
    m_pCL = std::min<double>(m_pCL, 1);
    m_pC = m_baseProb * m_baseProb;
    m_prevQ = curQ;
    m_rtrsEvent = Simulator::Schedule(m_tUpdate, &DualPi2QueueDisc::DualPi2Update, this);
}

void
DualPi2QueueDisc::AddToL4sStagingQueue(Ptr<QueueDiscItem> qdItem)
{
    NS_ASSERT_MSG(qdItem, "Error, tried to add a null QueueDiscItem to staging queue");
    m_l4sStagingQueue.push_back(qdItem);
}

void
DualPi2QueueDisc::AddToClassicStagingQueue(Ptr<QueueDiscItem> qdItem)
{
    NS_ASSERT_MSG(qdItem, "Error, tried to add a null QueueDiscItem to staging queue");
    m_classicStagingQueue.push_back(qdItem);
}

Ptr<QueueDiscItem>
DualPi2QueueDisc::DequeueFromL4sStagingQueue()
{
    NS_LOG_FUNCTION(this);
    if (!m_l4sStagingQueue.empty())
    {
        auto qdItem = m_l4sStagingQueue.front();
        NS_LOG_DEBUG("Dequeue from L4S staging queue; timestamp "
                     << qdItem->GetTimeStamp().GetMicroSeconds() << " us");
        m_l4sStagingQueue.pop_front();
        return qdItem;
    }
    return nullptr;
}

Ptr<QueueDiscItem>
DualPi2QueueDisc::DequeueFromClassicStagingQueue()
{
    NS_LOG_FUNCTION(this);
    if (!m_classicStagingQueue.empty())
    {
        auto qdItem = m_classicStagingQueue.front();
        m_classicStagingQueue.pop_front();
        NS_LOG_DEBUG("Dequeue from CLASSIC staging queue; timestamp "
                     << qdItem->GetTimeStamp().GetMicroSeconds() << " us");
        return qdItem;
    }
    return nullptr;
}

std::pair<bool, bool>
DualPi2QueueDisc::CanSchedule(uint32_t byteLimit) const
{
    NS_LOG_FUNCTION(this << byteLimit);
    bool canScheduleL4s = false;
    bool canScheduleClassic = false;
    if (GetNPackets() == 0)
    {
        NS_LOG_DEBUG("Cannot schedule from an empty queue");
        return std::make_pair(false, false);
    }
    uint32_t l4sHolWifiSize = 0; // Head of line's size as will be framed in Wi-Fi layer
    if (GetInternalQueue(L4S)->Peek())
    {
        l4sHolWifiSize = GetInternalQueue(L4S)->Peek()->GetSize() + 38;
    }
    uint32_t classicHolWifiSize = 0; // Head of line's size as will be framed in Wi-Fi layer
    if (GetInternalQueue(CLASSIC)->Peek())
    {
        classicHolWifiSize = GetInternalQueue(CLASSIC)->Peek()->GetSize() + 38;
    }
    if (l4sHolWifiSize && l4sHolWifiSize <= byteLimit)
    {
        canScheduleL4s = true;
        NS_LOG_DEBUG("Can schedule L4S size " << l4sHolWifiSize << " for limit " << byteLimit);
    }
    if (classicHolWifiSize && classicHolWifiSize <= byteLimit)
    {
        canScheduleClassic = true;
        NS_LOG_DEBUG("Can schedule Classic size " << classicHolWifiSize << " for limit "
                                                  << byteLimit);
    }
    return std::make_pair(canScheduleClassic, canScheduleL4s);
}

std::size_t
DualPi2QueueDisc::Scheduler(std::pair<bool, bool> eligible)
{
    NS_LOG_FUNCTION(this << eligible.first << eligible.second);
    NS_ASSERT_MSG(eligible.first || eligible.second, "Error: Neither queue is eligible");
    // A generic weighted deficit round robin queue with two bands.  If the
    // queue is non-empy, it should iterate until returning either L4S or
    // CLASSIC.
    // The 'eligible' parameter must be true for a given queue to be
    // scheduled:  eligible.first -> CLASSIC, eligible.second -> L4S
    uint32_t l4sHolSize = 0; // Head of line size
    if (GetInternalQueue(L4S)->Peek())
    {
        l4sHolSize = GetInternalQueue(L4S)->Peek()->GetSize();
    }
    uint32_t classicHolSize = 0; // Head of line size
    if (GetInternalQueue(CLASSIC)->Peek())
    {
        classicHolSize = GetInternalQueue(CLASSIC)->Peek()->GetSize();
    }
    if (GetNPackets() == 0)
    {
        NS_LOG_DEBUG("Trying to schedule from an empty queue");
        return NONE;
    }
    uint32_t maxIterations = 1000; // Prevent a deadlock simulation loop
    for (uint32_t i = 0; i < maxIterations; i++)
    {
        if (m_drrQueues.none())
        {
            NS_LOG_LOGIC("Start new round; LL deficit remaining before increment: "
                         << m_llDeficit << " classic deficit remaining: " << m_classicDeficit);
            m_drrQueues.set(L4S);
            m_drrQueues.set(CLASSIC);
            m_llDeficit += (m_drrQuantum * m_schedulingWeight);
            m_classicDeficit += m_drrQuantum;
            NS_LOG_LOGIC("Deficit after increment: LL deficit "
                         << m_llDeficit << " classic deficit " << m_classicDeficit);
        }
        if (l4sHolSize && eligible.second) // L4S
        {
            if (l4sHolSize <= m_llDeficit)
            {
                NS_LOG_LOGIC("Selecting LL queue");
                m_llDeficit -= l4sHolSize;
                NS_LOG_LOGIC("State after LL selection: LL deficit "
                             << m_llDeficit << " classic deficit " << m_classicDeficit);
                return L4S;
            }
            else
            {
                NS_LOG_LOGIC("End the L4S round; remaining deficit: " << m_llDeficit);
                m_drrQueues.reset(L4S);
            }
        }
        if (!l4sHolSize)
        {
            NS_LOG_LOGIC("L4S queue empty; end the L4S round");
            m_llDeficit = 0;
            m_drrQueues.reset(L4S);
        }
        if (classicHolSize && eligible.first) // CLASSIC
        {
            if (classicHolSize <= m_classicDeficit)
            {
                NS_LOG_LOGIC("Selecting classic queue");
                m_classicDeficit -= classicHolSize;
                NS_LOG_LOGIC("State after classic selection: LL deficit << "
                             << m_llDeficit << " classic deficit " << m_classicDeficit);
                return CLASSIC;
            }
            else
            {
                NS_LOG_LOGIC("End the classic round; remaining deficit: " << m_classicDeficit);
                m_drrQueues.reset(CLASSIC);
            }
        }
        if (!classicHolSize)
        {
            NS_LOG_LOGIC("classic queue empty; end the classic round");
            m_classicDeficit = 0;
            m_drrQueues.reset(CLASSIC);
        }
        maxIterations++;
    }
    // If the queue has a packet, 1000 rounds should be enough to select it
    NS_FATAL_ERROR("Error in DRR logic (should be unreachable)");
    return NONE;
}

Ptr<QueueDiscItem>
DualPi2QueueDisc::DequeueFromL4sQueue(bool& marked)
{
    NS_LOG_FUNCTION(this);
    auto qdItem = GetInternalQueue(L4S)->Dequeue();
    double pPrimeL{0}; // p'_L in RFC 9332
    while (qdItem)
    {
        if (m_pCL < m_pLmax) // Check for overload saturation
        {
            if (GetInternalQueue(L4S)->GetNPackets() > m_thLen) //> 1 packet queued
            {
                pPrimeL = Laqm(Simulator::Now() - qdItem->GetTimeStamp()); // Native LAQM, currently
            }
            else
            {
                pPrimeL = 0; // Suppress marking 1 pkt queue
            }

            if (pPrimeL > m_pCL)
            {
                NS_LOG_DEBUG("Laqm probability " << std::min<double>(pPrimeL, 1)
                                                 << " is driving p_L");
            }
            else
            {
                NS_LOG_DEBUG("coupled probability " << std::min<double>(m_pCL, 1)
                                                    << " is driving p_L");
            }

            double pL = std::max<double>(pPrimeL, m_pCL);
            pL = std::min<double>(pL, 1); // clamp p_L at 1
            m_pL = pL;                    // Trace the value of p_L
            if (Recur(m_l4sCount, pL))
            {
                marked = Mark(qdItem, UNFORCED_L4S_MARK);
                NS_ASSERT_MSG(marked == true, "Make sure we can mark in L4S queue");
                NS_LOG_DEBUG("L-queue packet is marked");
            }
        }
        else // overload saturation
        {
            if (Recur(m_l4sCount, m_pC)) // probability p_C = p'^2
            {
                NS_LOG_INFO("L4s drop due to recur function; queue length "
                            << GetInternalQueue(L4S)->GetNBytes());
                DropAfterDequeue(qdItem,
                                 UNFORCED_L4S_DROP); // Revert to classic drop due to overload
                qdItem = GetInternalQueue(L4S)->Dequeue();
                continue;
            }
            if (Recur(m_l4sCount, m_pCL)) // probability p_CL = k * p'
            {
                marked = Mark(qdItem, UNFORCED_L4S_MARK); // linear marking of remaining packets
                NS_ASSERT_MSG(marked == true, "Make sure we can mark in L4S queue");
                NS_LOG_DEBUG("L-queue packet is marked");
            }
            NS_LOG_DEBUG("L-queue packet is not marked");
        }
        return qdItem;
    }
    return nullptr;
}

Ptr<QueueDiscItem>
DualPi2QueueDisc::DequeueFromClassicQueue(bool& dropped)
{
    NS_LOG_FUNCTION(this);
    auto qdItem = GetInternalQueue(CLASSIC)->Dequeue();
    // Heuristic in Linux code; never drop if less than 2 MTU in queue
    if (GetInternalQueue(CLASSIC)->GetNBytes() < 2 * m_mtu)
    {
        return qdItem;
    }
    while (qdItem)
    {
        if (Recur(m_classicCount, m_pC) || (m_pC >= m_pCmax)) // overload disables ecn
        {
            NS_LOG_INFO("Classic drop due to recur function; queue length "
                        << GetInternalQueue(CLASSIC)->GetNBytes());
            DropAfterDequeue(qdItem, UNFORCED_CLASSIC_DROP);
            dropped = true;
            qdItem = GetInternalQueue(CLASSIC)->Dequeue();
            continue;
        }
        else
        {
            NS_LOG_DEBUG("C-queue packet is dequeued and returned");
            return qdItem;
        }
    }
    return nullptr;
}

Ptr<QueueDiscItem>
DualPi2QueueDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);
    auto qdItem = DequeueFromL4sStagingQueue();
    if (qdItem)
    {
        // Packets in staging queue have already been marked or not
        // and internal Laqm probabilities have been updated
        Time sojourn = Simulator::Now() - qdItem->GetTimeStamp();
        NS_LOG_INFO("Dequeue from L4S staging queue; timestamp "
                    << qdItem->GetTimeStamp().GetMicroSeconds() << " us; sojourn "
                    << sojourn.GetMicroSeconds() << " us");
        m_traceL4sSojourn(sojourn);
        return qdItem;
    }
    qdItem = DequeueFromClassicStagingQueue();
    if (qdItem)
    {
        Time sojourn = Simulator::Now() - qdItem->GetTimeStamp();
        NS_LOG_INFO("Dequeue from CLASSIC staging queue; timestamp "
                    << qdItem->GetTimeStamp().GetMicroSeconds() << " us; sojourn "
                    << sojourn.GetMicroSeconds() << " us");
        m_traceClassicSojourn(sojourn);
        return qdItem;
    }
    while (GetQueueSize() > 0)
    {
        auto selected = Scheduler({true, true});
        if (selected == L4S)
        {
            bool marked [[maybe_unused]];
            qdItem = DequeueFromL4sQueue(marked);
            // After implementing Edge Cases in L4S, the queue item can drop if an overload is
            // happened,
            // therefore check that an item was actually returned before tracing it
            if (qdItem)
            {
                Time sojourn = Simulator::Now() - qdItem->GetTimeStamp();
                NS_LOG_INFO("Dequeue from L4S queue; timestamp "
                            << qdItem->GetTimeStamp().GetMicroSeconds() << " us; sojourn "
                            << sojourn.GetMicroSeconds() << " us");
                m_traceL4sSojourn(sojourn);
                return qdItem;
            }
            else
            {
                NS_LOG_DEBUG("Drop occurred in L4S queue");
                // Do not return; continue with while() loop
            }
        }
        else if (selected == CLASSIC)
        {
            bool dropped [[maybe_unused]];
            qdItem = DequeueFromClassicQueue(dropped);
            // Since the CLASSIC queue can drop in DequeueFromClassicDequeue(), check
            // that an item was actually returned before tracing it
            if (qdItem)
            {
                Time sojourn = Simulator::Now() - qdItem->GetTimeStamp();
                NS_LOG_INFO("Dequeue from CLASSIC queue; timestamp "
                            << qdItem->GetTimeStamp().GetMicroSeconds() << " us; sojourn "
                            << sojourn.GetMicroSeconds() << " us");
                m_traceClassicSojourn(sojourn);
                return qdItem;
            }
            else
            {
                NS_LOG_DEBUG("Drop occurred in CLASSIC queue");
                // Do not return; continue with while() loop
            }
        }
        else
        {
            return nullptr;
        }
    }
    return nullptr;
}

double
DualPi2QueueDisc::Laqm(Time qDelay [[maybe_unused]]) const
{
    NS_LOG_FUNCTION(this << qDelay.As(Time::S));
    if (m_disableLaqm)
    {
        return 0;
    }
    else
    {
        if (qDelay >= m_minTh + m_range)
        {
            return 1;
        }
        else if (qDelay > m_minTh)
        {
            return (qDelay - m_minTh).GetSeconds() / m_range.GetSeconds();
        }
        else
        {
            return 0;
        }
    }
}

bool
DualPi2QueueDisc::Recur(double& count, double likelihood)
{
    NS_LOG_FUNCTION(this << likelihood);
    count += likelihood;
    if (count > 1)
    {
        count -= 1;
        return true;
    }
    return false;
}

Ptr<const QueueDiscItem>
DualPi2QueueDisc::DoPeek()
{
    NS_LOG_FUNCTION(this);
    Ptr<const QueueDiscItem> item;

    for (std::size_t i = 0; i < GetNInternalQueues(); i++)
    {
        if ((item = GetInternalQueue(i)->Peek()))
        {
            NS_LOG_LOGIC("Peeked from queue number " << i << ": " << item);
            NS_LOG_LOGIC("Number packets queue number " << i << ": "
                                                        << GetInternalQueue(i)->GetNPackets());
            NS_LOG_LOGIC("Number bytes queue number " << i << ": "
                                                      << GetInternalQueue(i)->GetNBytes());
            return item;
        }
    }

    NS_LOG_LOGIC("Queue empty");
    return item;
}

bool
DualPi2QueueDisc::CheckConfig()
{
    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0)
    {
        NS_LOG_ERROR("DualPi2QueueDisc cannot have classes");
        return false;
    }

    if (GetNPacketFilters() > 0)
    {
        NS_LOG_ERROR("DualPi2QueueDisc cannot have packet filters");
        return false;
    }

    if (GetNInternalQueues() == 0)
    {
        // Create 2 DropTail queues
        Ptr<InternalQueue> queue0 =
            CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>("MaxSize",
                                                                     QueueSizeValue(GetMaxSize()));
        Ptr<InternalQueue> queue1 =
            CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>("MaxSize",
                                                                     QueueSizeValue(GetMaxSize()));
        QueueSize queueSize(BYTES, m_queueLimit);
        queue0->SetMaxSize(queueSize);
        queue1->SetMaxSize(queueSize);
        AddInternalQueue(queue0);
        AddInternalQueue(queue1);
    }

    if (GetNInternalQueues() != 2)
    {
        NS_LOG_ERROR("DualPi2QueueDisc needs 2 internal queue");
        return false;
    }

    if (GetInternalQueue(CLASSIC)->GetMaxSize().GetValue() < m_queueLimit)
    {
        NS_LOG_ERROR(
            "The size of the internal Classic traffic queue is less than the queue disc limit");
        return false;
    }

    if (GetInternalQueue(L4S)->GetMaxSize().GetValue() < m_queueLimit)
    {
        NS_LOG_ERROR(
            "The size of the internal L4S traffic queue is less than the queue disc limit");
        return false;
    }

    return true;
}

} // namespace ns3
