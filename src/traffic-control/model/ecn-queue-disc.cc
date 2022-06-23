#include "ecn-queue-disc.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/drop-tail-queue.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ECNQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (ECNQueueDisc);

TypeId
ECNQueueDisc::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::ECNQueueDisc")
        .SetParent<QueueDisc> ()
        .SetGroupName ("TrafficControl")
        .AddConstructor<ECNQueueDisc> ()
        .AddAttribute ("Mode", "Whether to use Bytes (see MaxBytes) or Packets (see MaxPackets) as the maximum queue size metric.",
                        EnumValue (Queue::QUEUE_MODE_BYTES),
                        MakeEnumAccessor (&ECNQueueDisc::m_mode),
                        MakeEnumChecker (Queue::QUEUE_MODE_BYTES, "QUEUE_MODE_BYTES",
                                         Queue::QUEUE_MODE_PACKETS, "QUEUE_MODE_PACKETS"))
        .AddAttribute ("MaxPackets", "The maximum number of packets accepted by this ECNQueueDisc.",
                        UintegerValue (100),
                        MakeUintegerAccessor (&ECNQueueDisc::m_maxPackets),
                        MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("MaxBytes", "The maximum number of bytes accepted by this ECNQueueDisc.",
                        UintegerValue (1500 * 100),
                        MakeUintegerAccessor (&ECNQueueDisc::m_maxBytes),
                        MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("EcnBytes",
                       "The ECN bytes marking threshold",
                        UintegerValue (100),
                        MakeUintegerAccessor (&ECNQueueDisc::m_ecnBytes),
                        MakeUintegerChecker<uint32_t> ())
    ;
    return tid;
}

ECNQueueDisc::ECNQueueDisc ()
    : QueueDisc (),
      m_ecnBytes (0)
{
    NS_LOG_FUNCTION (this);
}

ECNQueueDisc::~ECNQueueDisc ()
{
    NS_LOG_FUNCTION (this);
}

bool
ECNQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION (this << item);

    Ptr<Packet> p = item->GetPacket ();
    if (m_mode == Queue::QUEUE_MODE_PACKETS && (GetInternalQueue (0)->GetNPackets () + 1 > m_maxPackets))
    {
        Drop (item);
        return false;
    }

    if (m_mode == Queue::QUEUE_MODE_BYTES && (GetInternalQueue (0)->GetNBytes () + item->GetPacketSize () > m_maxBytes))
    {
        Drop (item);
        return false;
    }
    
    // Larger than ECN marking threshold
    if (GetInternalQueue (0)->GetNBytes () + item->GetPacketSize () > m_ecnBytes)
    {
        NS_LOG_INFO ("Mark ECN for this packet!");
        MarkingECN(item);
    }

    GetInternalQueue (0)->Enqueue (item);

    return true;

}

Ptr<QueueDiscItem>
ECNQueueDisc::DoDequeue (void)
{
    NS_LOG_FUNCTION (this);

    Time now = Simulator::Now ();

    if (GetInternalQueue (0)->IsEmpty ())
    {
        return NULL;
    }

    Ptr<QueueDiscItem> item = StaticCast<QueueDiscItem> (GetInternalQueue (0)->Dequeue ());
    return item;

}

Ptr<const QueueDiscItem>
ECNQueueDisc::DoPeek (void) const
{
    NS_LOG_FUNCTION (this);
    if (GetInternalQueue (0)->IsEmpty ())
    {
        return NULL;
    }

    Ptr<const QueueDiscItem> item = StaticCast<const QueueDiscItem> (GetInternalQueue (0)->Peek ());

    return item;

}

bool
ECNQueueDisc::CheckConfig (void)
{
    if (GetNInternalQueues () == 0)
    {
        Ptr<Queue> queue = CreateObjectWithAttributes<DropTailQueue> ("Mode", EnumValue (m_mode));
        if (m_mode == Queue::QUEUE_MODE_PACKETS)
        {
            queue->SetMaxPackets (m_maxPackets);
        }
        else
        {
            queue->SetMaxBytes (m_maxBytes);
        }
        AddInternalQueue (queue);
    }

    if (GetNInternalQueues () != 1)
    {
        NS_LOG_ERROR ("ECNQueueDisc needs 1 internal queue");
        return false;
    }

    return true;

}

void
ECNQueueDisc::InitializeParams (void)
{
    NS_LOG_FUNCTION (this);
}

bool
ECNQueueDisc::MarkingECN (Ptr<QueueDiscItem> item)
{
    Ptr<Ipv4QueueDiscItem> ipv4Item = DynamicCast<Ipv4QueueDiscItem> (item);
    if (ipv4Item == 0)   {
        NS_LOG_ERROR ("Cannot convert the queue disc item to ipv4 queue disc item");
        return false;
    }

    Ipv4Header header = ipv4Item -> GetHeader ();

    if (header.GetEcn () != Ipv4Header::ECN_ECT1)   {
        NS_LOG_ERROR ("Cannot mark because the ECN field is not ECN_ECT1");
        return false;
    }

    header.SetEcn(Ipv4Header::ECN_CE);
    ipv4Item->SetHeader(header);
    return true;
}

}
