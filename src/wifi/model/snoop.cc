/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
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
 * Authors: 
 *          
 */

#include "ns3/snoop.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/random-variable-stream.h"
#include "ns3/wifi-net-device.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/tcp-header.h"
#include "ns3/address.h"
#include "ns3/ptr.h"
#include "ns3/sequence-number.h"
#include <stdint.h>
#include "ns3/ipv4-header.h"
#include "ns3/tcp-l4-protocol.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Snoop");

NS_OBJECT_ENSURE_REGISTERED (Snoop);

//Snoop Header
SnoopHeader::SnoopHeader ()
  : m_seqNo (0),
    m_numRxmit (0),
    m_senderRxmit (0),
    m_sendTime (0)
{
}

SnoopHeader::SnoopHeader (uint32_t seqNo, uint32_t numRxmit, 
                          uint32_t senderRxmit, Time sendTime) :
  m_seqNo (seqNo), m_numRxmit (numRxmit), m_senderRxmit (senderRxmit), m_sendTime (sendTime)
{
}

SnoopHeader::~SnoopHeader ()
{
}

TypeId
SnoopHeader::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::SnoopHeader")
    .SetParent<Header> ()
    .SetGroupName("Wifi")
    .AddConstructor<SnoopHeader> ()
  ;
  return tid;
}

TypeId
SnoopHeader::GetInstanceTypeId () const
{
  return GetTypeId ();
}

uint32_t
SnoopHeader::GetSerializedSize () const
{
  return 16;
}

void
SnoopHeader::Serialize (Buffer::Iterator i) const
{
  i.WriteHtonU32 (m_seqNo);
  i.WriteHtonU32 (m_numRxmit);
  i.WriteHtonU32 (m_senderRxmit);
  i.WriteHtonU32 (m_sendTime. GetMilliSeconds ());
}

uint32_t
SnoopHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_seqNo = i.ReadNtohU32 ();
  m_numRxmit = i.ReadNtohU32 ();
  m_senderRxmit = i.ReadNtohU32 ();
  m_sendTime. From(i.ReadNtohU32 ());

  uint32_t dist = i.GetDistanceFrom (start);
  NS_ASSERT (dist == GetSerializedSize ());
  return dist;
}

void
SnoopHeader::Print (std::ostream &os) const
{
  os << "Sequence Number " << m_seqNo << " Number of Retransmissions " 
     << m_numRxmit << " Sender Retransmit " << m_senderRxmit 
     << " Send Time " << m_sendTime;
}

bool
SnoopHeader::operator== (SnoopHeader const & o) const
{
  return (m_seqNo == o.m_seqNo && m_numRxmit == o.m_numRxmit &&
          m_senderRxmit == o.m_senderRxmit && m_sendTime == o.m_sendTime);
}

//Snoop
Snoop::Snoop ():
  m_fState (0), 
  m_lastSeen (-1), 
  m_lastAck (-1), 
  m_expNextAck (0), 
  m_expDupAcks (0), 
  m_bufHead (0), 
  m_bufTail (0),
  m_wlState (SNOOP_WLEMPTY), 
  m_wlLastSeen (-1), 
  m_wlLastAck (-1), 
  m_wlBufHead (0), 
  m_wlBufTail (0),
  m_rxmitTimer (Timer::CANCEL_ON_DESTROY)
{
  for (uint32_t i = 0; i < SNOOP_MAXWIND; i++)           /* data from wired->wireless */
    {
      m_pkts[i] = 0;
    }
  for (uint32_t i = 0; i < SNOOP_WLSEQS; i++)            /* data from wireless->wired */
    {
      m_wlSeqs[i] = (Sequence *) malloc(sizeof(Sequence));
      m_wlSeqs[i]->m_seq = m_wlSeqs[i]->m_num = 0;
    }
  if (m_maxBufs == 0)
    {
      m_maxBufs = SNOOP_MAXWIND;
    }
}

Snoop::~Snoop ()
{
}

TypeId
Snoop::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::Snoop")
    .SetParent<Object> ()
    .SetGroupName("Wifi")
    .AddConstructor<Snoop> ()
    .AddAttribute ("SnoopDisable", "Disable snoop for this mobile.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&Snoop::m_snoopDisable),
                   MakeBooleanChecker ())
    .AddAttribute ("SRTT", "Smoothed RTT estimate.",
                   TimeValue (Seconds (0.1)),
                   MakeTimeAccessor (&Snoop::m_srtt),
                   MakeTimeChecker ())
    .AddAttribute ("RTTVariation", "Linear Deviation.",
                   TimeValue (Seconds (0.25)),
                   MakeTimeAccessor (&Snoop::m_rttVar),
                   MakeTimeChecker ())
    .AddAttribute ("MaximumBuffers", "Maximum number of packet buffers.",
                   IntegerValue (0),
                   MakeIntegerAccessor (&Snoop::m_maxBufs),
                   MakeIntegerChecker<int32_t> ())
    .AddAttribute ("SnoopTick", "Minimum retransmission timer granularity.",
                   TimeValue (Seconds (0.1)),
                   MakeTimeAccessor (&Snoop::m_snoopTick),
                   MakeTimeChecker ())
    .AddAttribute ("Gain", "Gain in EWMA for m_srtt and m_rttvar.",
                   DoubleValue (0.125),
                   MakeDoubleAccessor (&Snoop::m_g),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("TailTime", "Time at which earliest unacknowledged packet sent.",
                   TimeValue (Seconds (0)),
                   MakeTimeAccessor (&Snoop::m_tailTime),
                   MakeTimeChecker ())
    .AddAttribute ("RxmitStatus", "Retransmission status.",
                   IntegerValue (0),
                   MakeIntegerAccessor (&Snoop::m_rxmitStatus),
                   MakeIntegerChecker<int32_t> ())
    .AddAttribute ("LRU", "Indicates whether the cache is LRU.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&Snoop::m_lru),
                   MakeBooleanChecker ())
    .AddAttribute ("Integrate", "Integrate",
                   IntegerValue (0),
                   MakeIntegerAccessor (&Snoop::m_integrate),
                   MakeIntegerChecker<int32_t> (0))
    
  ;
  return tid;
}

void
Snoop::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  m_rxmitTimer.SetFunction (&Snoop::RxmitTimerExpire, this);
}

void 
Snoop::WlReset ()
{
  m_wlState = SNOOP_WLEMPTY;
  m_wlBufHead = m_wlBufTail = 0;
  for (uint32_t i = 0; i < SNOOP_WLSEQS; i++) 
    {
      m_wlSeqs[i]->m_seq = m_wlSeqs[i]->m_num = 0;
    }
}

uint32_t 
Snoop::HandlePacket (Ptr<Packet> p, Callback< bool, Ptr<Packet>,
                     const Address &, uint16_t > callback, Mac48Address from)
{
  Ipv4Header ipHeader;
  p->RemoveHeader (ipHeader);
  uint32_t prop = SNOOP_PROPAGATE;           // by default;  propagate ack or packet
  if (ipHeader.GetProtocol () == IPPROTO_TCP)
    {
      TcpHeader tcpHeader;
      p->RemoveHeader (tcpHeader);
      if (tcpHeader.GetFlags () & TcpHeader::ACK)
        {
	  prop = SnoopAck (p, callback, from);
        } 
      else 
        {
	  SnoopWlessData (p);
        }
      p->AddHeader (tcpHeader);
    }
  p->AddHeader (ipHeader);
  return prop;                            //can we return prop and check in LLSnoop?
  /*if (prop == SNOOP_PROPAGATE)
    {
		s.schedule(recvtarget_, e, parent_->delay());
    }
  else 
    {			// suppress ack
		        // printf("---- %f suppressing ack %d\n", s.clock(), seq);
		Packet::free(p);
    }*/
}

/*
void
Snoop::SnoopData (Ptr<Packet> p)
{
  TcpHeader tcpHeader;
  p->RemoveHeader (tcpHeader);
  SequenceNumber32 seq = tcpHeader.GetSequenceNumber ();
  uint32_t resetPending = 0;
  if (m_fState & SNOOP_ALIVE && seq == 0)
    {
      DoInitialize ();
    }
  m_fState |= SNOOP_ALIVE;		
  if ((m_fState & SNOOP_FULL) && !m_lru) 
    {
      //printf("snoop full, fwd'ing\n t %d h %d", buftail_, bufhead_);
      if (seq > m_lastSeen)
        {
          m_lastSeen = seq;
        }
      return;
    }

	
	//	printf("%x snoop_data: %f sending packet %d\n", this, s.clock(), seq);
	
	fstate_ |= SNOOP_ALIVE;
	if ((fstate_ & SNOOP_FULL) && !lru_) {
//		printf("snoop full, fwd'ing\n t %d h %d", buftail_, bufhead_);
		if (seq > lastSeen_)
			lastSeen_ = seq;
		return;
	}
	/ 
	 * Only if the ifq is NOT full do we insert, since otherwise we want
	 * congestion control to kick in.
	 /

	if (parent_->ifq()->length() < parent_->ifq()->limit()-1)
		resetPending = snoop_insert(p);
	if (toutPending_ && resetPending == SNOOP_TAIL) {
		s.cancel(toutPending_);
		// xxx: I think that toutPending_ doesn't need to be freed because snoop didn't allocate it (but I'm not sure).
		toutPending_ = 0;
	}
	if (!toutPending_ && !empty_()) {
		toutPending_ = (Event *) (pkts_[buftail_]);
		s.schedule(rxmitHandler_, toutPending_, timeout());
                m_rxmitTimer.Schedule (Timeout ());
	}
	return;
}
*/

uint32_t
Snoop::SnoopInsert(Ptr<Packet> p)
{
  uint32_t i, retval=0;
  TcpHeader tcpHeader;
  p->RemoveHeader (tcpHeader);
  SequenceNumber32 seq = tcpHeader.GetSequenceNumber ();
  p->AddHeader (tcpHeader);
  if ((int32_t)seq.GetValue () <= m_lastAck)
    { 
      return retval;
    }
  if (m_fState & SNOOP_FULL) 
    {
      /* free tail and go on */
      //printf("snoop full, making room\n");
      //Packet::free(pkts_[buftail_]);                    // yet to do (dropping the packet)
      m_pkts[m_bufTail] = 0;
      m_bufTail = Next (m_bufTail);
      m_fState |= ~SNOOP_FULL;
    }
  SnoopHeader snoopHeader;
  m_pkts[m_bufTail]->RemoveHeader (snoopHeader);
  if ((int32_t)seq.GetValue () > m_lastSeen || m_pkts[m_bufTail] == 0) 
    {
      m_pkts[m_bufTail]->AddHeader (snoopHeader);    
      // in-seq or empty cache
      i = m_bufHead;
      m_bufHead = Next (m_bufHead);
    } 
  else if (seq.GetValue () < snoopHeader.GetSeqNo ()) 
    {
      m_bufTail = Prev (m_bufTail);
      i = m_bufTail;
      m_pkts[m_bufTail]->AddHeader (snoopHeader);
    } 
  else 
    {
      m_pkts[m_bufTail]->AddHeader (snoopHeader);
      for (i = m_bufTail; i != m_bufHead; i = Next (i)) 
        {
          SnoopHeader sh;
          m_pkts[i]->RemoveHeader (sh);
          if (sh.GetSeqNo () == seq.GetValue ()) 
            {  // cached before
              sh.SetNumRxmit (0);
	      sh.SetSenderRxmit (1); //must be a sender retr
              sh.SetSendTime(Simulator :: Now ());
              m_pkts[i]->AddHeader (sh);
	      return SNOOP_TAIL;
	    } 
          else if (sh.GetSeqNo () > seq.GetValue ()) 
            {
              Ptr<Packet> temp = m_pkts[Prev (m_bufTail)];
              for (uint32_t j = m_bufTail; j != i; j = Next (j))
                {
                  m_pkts[Prev (j)] = m_pkts[j];
                }
		i = Prev (i);
		m_pkts[i] = temp;   
		m_bufTail = Prev (m_bufTail);
		break;
	    }
          m_pkts[i]->AddHeader (sh);
        } 
        if (i == m_bufHead)
	  {
            m_bufHead = Next (m_bufHead);
          }
    }
    // save in the buffer
    SavePacket (p, seq.GetValue (), i);
    if (m_bufHead == m_bufTail)
      {
        m_fState |= SNOOP_FULL;
      }
    /* 
     * If we have one of the following packets:
     * 1. a network-out-of-order packet, or
     * 2. a fast rxmit packet, or 3. a sender retransmission 
     * AND it hasn't already been buffered, 
     * then seq will be < lastSeen_. 
     * We mark this packet as having been due to a sender rexmit 
     * and use this information in snoop_ack(). We let the dupacks
     * for this packet go through according to expDupacks_.
     */
    if ((int32_t)seq.GetValue () < m_lastSeen) 
      { 
        /* not in-order -- XXX should it be <= ? */
	if (m_bufTail == i) 
          {
            SnoopHeader sh;
            m_pkts[i]->RemoveHeader (sh);
            sh.SetSenderRxmit (1);
            sh.SetNumRxmit (0);
            m_pkts[i]->AddHeader (sh);
	  }
	m_expNextAck = m_bufTail;
	retval = SNOOP_TAIL;
      } 
    else
      {
        m_lastSeen = (int32_t)seq.GetValue ();
      }
  p->AddHeader (tcpHeader);
  return retval;		
}

void
Snoop::SavePacket (Ptr<Packet> p, uint32_t seq, int32_t i)
{
  m_pkts[i] = p->Copy ();
  Ptr<Packet> pkt = m_pkts[i];
  SnoopHeader snoopHeader (seq, 0, 0, Simulator::Now ());
  pkt->AddHeader (snoopHeader);
}

/*
 * Ack processing in snoop protocol.  We know for sure that this is an ack.
 * Return SNOOP_SUPPRESS if ack is to be suppressed and SNOOP_PROPAGATE o.w.
 */
int
Snoop::SnoopAck (Ptr<Packet> p, Callback< bool, Ptr<Packet>,
                 const Address &, uint16_t > callback, Mac48Address from)
{
  Ptr<Packet> pkt;
  TcpHeader tcpHeader;
  p->RemoveHeader (tcpHeader);
  SequenceNumber32 ack = tcpHeader.GetSequenceNumber ();
  p->AddHeader (tcpHeader);	
  /*
   * There are 3 cases:
   * 1. lastAck_ > ack.  In this case what has happened is
   *    that the acks have come out of order, so we don't
   *    do any local processing but forward it on.
   * 2. lastAck_ == ack.  This is a duplicate ack. If we have
   *    the packet we resend it, and drop the dupack.
   *    Otherwise we never got it from the fixed host, so we
   *    need to let the dupack get through.
   *    Set expDupacks_ to number of packets already sent
   *    This is the number of dup acks to ignore.
   * 3. lastAck_ < ack.  Set lastAck_ = ack, and update
   *    the head of the buffer queue. Also clean up ack'd packets.
   */      
  if (m_fState & SNOOP_CLOSED || m_lastAck > (int32_t)ack.GetValue ()) 
    {
      return SNOOP_PROPAGATE;	// send ack onward     
    }
  if (m_lastAck == (int32_t)ack.GetValue ()) 
    {	
      /* A duplicate ack; pure window updates don't occur in ns. */
      pkt = m_pkts[m_bufTail];
      if (pkt == 0)
        { 
	  return SNOOP_PROPAGATE;
        }
      SnoopHeader sh;
      pkt->RemoveHeader (sh);
      if (pkt == 0 || sh.GetSeqNo () > ack.GetValue () + 1) 
        {
	  /* don't have packet, letting thru' */
          pkt->AddHeader (sh);
          return SNOOP_PROPAGATE;
        }
      /* 
       * We have the packet: one of 3 possibilities:
       * 1. We are not expecting any dupacks (expDupacks_ == 0)
       * 2. We are expecting dupacks (expDupacks_ > 0)
       * 3. We are in an inconsistent state (expDupacks_ == -1)
       */		
      if (m_expDupAcks == 0) 
        {	
          // not expecting it 
          #define RTX_THRESH 1                  //yet to do
	  static uint32_t thresh = 0;
	  if (thresh++ < RTX_THRESH)
            { 
	      /* no action if under RTX_THRESH */
              pkt->AddHeader (sh);
	      return SNOOP_PROPAGATE;
            }
	  thresh = 0;	
	  // if the packet is a sender retransmission, pass on
	  if (sh.GetSenderRxmit ())
            { 
              pkt->AddHeader (sh);
	      return SNOOP_PROPAGATE;
            }
	  /*
           * Otherwise, not triggered by sender.  If this is
	   * the first dupack recd., we must determine how many
	   * dupacks will arrive that must be ignored, and also
	   * rexmit the desired packet.  Note that expDupacks_
	   * will be -1 if we miscount for some reason.
           */			
	  m_expDupAcks = m_bufHead - m_expNextAck;
	  if (m_expDupAcks < 0)
            {
	      m_expDupAcks += SNOOP_MAXWIND;
            }
	  m_expDupAcks -= RTX_THRESH + 1;
	  m_expNextAck = Next (m_bufTail);
	  if (sh.GetNumRxmit () == 0)
            { 
              pkt->AddHeader (sh);
	      return SnoopRxmit (pkt, callback, from);
            }
	} 
      else if (m_expDupAcks > 0) 
        {
	  m_expDupAcks--;
          pkt->AddHeader (sh);
	  return SNOOP_SUPPRESS;
	} 
      else if (m_expDupAcks == -1) 
        {
	  if (sh.GetNumRxmit () < 2) 
            {
              pkt->AddHeader (sh);
	      return SnoopRxmit (pkt, callback, from);
	    }
	} 
      else
        {
          pkt->AddHeader (sh);		
          // let sender deal with it
	  return SNOOP_PROPAGATE;
	}
      pkt->AddHeader (sh); 
    }
  else 
    {		
      // a new ack
      m_fState &= ~SNOOP_NOACK; // have seen at least 1 new ack
      /* free buffers */
      Time sndTime = SnoopCleanBufs (ack,callback,from);       
      if (sndTime != 0)
        {
       	  SnoopRtt(sndTime);
        }
       m_expDupAcks = 0;
       m_expNextAck = m_bufTail;
       m_lastAck = (int32_t)ack.GetValue ();
    }
  return SNOOP_PROPAGATE;
}

void
Snoop::SnoopWlessData (Ptr<Packet> p)
{
  TcpHeader tcpHeader;
  p->RemoveHeader (tcpHeader);
  uint32_t i;
  SequenceNumber32 seq = tcpHeader.GetSequenceNumber ();
  p->AddHeader (tcpHeader);
  if (m_wlState & SNOOP_WLALIVE && seq.GetValue () == 0)
    {
      WlReset();
    }
  m_wlState |= SNOOP_WLALIVE;
  if (m_wlState & SNOOP_WLEMPTY && (int32_t)seq.GetValue () >= m_wlLastAck) 
    {
      m_wlSeqs[m_wlBufHead]->m_seq = seq.GetValue ();
      m_wlSeqs[m_wlBufHead]->m_num = 1;
      m_wlBufTail = m_wlBufHead;
      m_wlBufHead = WlNext (m_wlBufHead);
      m_wlLastSeen = (int32_t)seq.GetValue ();
      m_wlState &= ~SNOOP_WLEMPTY;
      return;
    }
  /* WL data list definitely not empty at this point. */
  if ((int32_t)seq.GetValue () >= m_wlLastSeen) 
    {
      m_wlLastSeen = (int32_t)seq.GetValue ();
      i = WlPrev (m_wlBufHead);
      if (m_wlSeqs[i]->m_seq + m_wlSeqs[i]->m_num == seq.GetValue ()) 
        {
	  m_wlSeqs[i]->m_num++;
	  return;
	}
      i = m_wlBufHead;
      m_wlBufHead = WlNext (m_wlBufHead);
    } 
  else if (seq.GetValue () == m_wlSeqs[i = m_wlBufTail]->m_seq - 1) 
    {
    } 
  else
    {
      return;
    }
  m_wlSeqs[i]->m_seq = seq.GetValue ();
  m_wlSeqs[i]->m_num++;

  /* Ignore network out-of-ordering and retransmissions for now */
  return;
}

void 
Snoop::SnoopWiredAck (Ptr<Packet> p)
{
  TcpHeader tcpHeader;
  p->RemoveHeader (tcpHeader);
  SequenceNumber32 ack = tcpHeader.GetSequenceNumber ();
  uint32_t i;
  if ((int32_t)ack.GetValue () == m_wlLastAck && SnoopWlessLoss (ack.GetValue ())) 
    {
      tcpHeader.SetFlags (tcpHeader.GetFlags () | TcpHeader::ECE);
    } 
  else if ((int32_t)ack.GetValue () > m_wlLastAck) 
    {
      /* update info about unack'd data */
      for (i = m_wlBufTail; i != m_wlBufHead; i = WlNext (i)) 
        {
          Sequence *t = m_wlSeqs[i];
	  if (t->m_seq + t->m_num - 1 <= ack.GetValue ()) 
            {
              t->m_seq = t->m_num = 0;
	    } 
          else if (ack.GetValue () < t->m_seq) 
            {
	      break;
	    } 
          else if (ack.GetValue () < t->m_seq + t->m_num - 1) 
            {
	      /* ack for part of a block */
	      t->m_num -= ack.GetValue () - t->m_seq + 1;
	      t->m_seq = ack.GetValue () + 1;
	      break;
	    }
        }
	m_wlBufTail = i;
	if (m_wlBufTail == m_wlBufHead)
	  {		
            m_wlState |= SNOOP_WLEMPTY;
          }
	m_wlLastAck = (int32_t)ack.GetValue ();
	/* Even a new ack could cause an ELN to be set. */
	if (m_wlBufHead != m_wlBufTail && SnoopWlessLoss (ack.GetValue ()))
          {
	    tcpHeader.SetFlags (tcpHeader.GetFlags () | TcpHeader::ECE);
          } 
    }
  p->AddHeader (tcpHeader);
}

/* 
 * Return 1 if we think this packet loss was not congestion-related, and 
 * 0 otherwise.  This function simply implements the lookup into the table
 * that maintains this info; most of the hard work is done in 
 * snoop_wless_data() and snoop_wired_ack().
 */
int
Snoop::SnoopWlessLoss (uint32_t ack)
{
  if ((m_wlBufHead == m_wlBufTail) || m_wlSeqs[m_wlBufTail]->m_seq > ack+1)
    {
      return 1;
    }
  return 0;
}

/*
 * clean snoop cache of packets that have been acked.
 */
Time
Snoop::SnoopCleanBufs (SequenceNumber32 ack, Callback< bool, Ptr<Packet>,
                 const Address &, uint16_t > callback, Mac48Address from)
{
  //Scheduler &s = Scheduler::instance();           // how to use scheduler in ns-3
  Time sndTime = Time ();
  //if (toutPending_) {
  //	s.cancel(toutPending_);
  //	// xxx: I think that toutPending_ doesn't need to be freed because snoop didn't allocate it (but I'm not sure).
  //	toutPending_ = 0;
  //};
  
  if (IsEmpty ())
    {
      return sndTime;
    }
  int i = m_bufTail;
  do 
    {
      SnoopHeader sh;
      m_pkts[i]->RemoveHeader (sh);
      TcpHeader tcpHeader;
      m_pkts[i]->RemoveHeader (tcpHeader);
      SequenceNumber32 seq = tcpHeader.GetSequenceNumber ();
      m_pkts[i]->AddHeader (tcpHeader);
      if (seq <= ack) 
        {
	  sndTime = sh.GetSendTime ();
	  //Packet::free(pkts_[i]);       //To be done -dropping the packet.
	  m_pkts[i] = 0;
	  m_fState &= ~SNOOP_FULL;	/* XXX redundant? */
	} 
      else if (seq > ack)
        {
          break;
        }
      m_pkts[i]->AddHeader (sh);
      i = Next (i);
    } while (i != m_bufHead);

  if ((i != m_bufTail) || (m_bufHead != m_bufTail)) 
    {
      m_fState &= ~SNOOP_FULL;
      m_bufTail = i;
    }
  if (!IsEmpty ()) 
    {
      //toutPending_ = (Event *) (pkts_[buftail_]);
      //s.schedule(rxmitHandler_, toutPending_, timeout());
     m_rxmitTimer.SetArguments (callback, from);
      m_rxmitTimer.Schedule (Timeout ());
      SnoopHeader sh;
      m_pkts[m_bufTail]->RemoveHeader (sh);
      m_tailTime = sh.GetSendTime ();
      m_pkts[m_bufTail]->AddHeader (sh);
    }

  return sndTime;
}

/* 
 * Calculate smoothed rtt estimate and linear deviation.
 */
void
Snoop::SnoopRtt (Time sndTime)
{
  double rtt = Simulator::Now ().GetMilliSeconds () - sndTime.GetMilliSeconds ();
  //if (parent_->integrate()) 
  //  {
  //    parent_->snoop_rtt(sndTime);
  //    return;
  //  }
	
  if (rtt > 0) 
    {
      m_srtt =  MilliSeconds (m_g * m_srtt.GetMilliSeconds () + (1-m_g) * rtt);
      double delta = rtt - m_srtt.GetMilliSeconds ();
      if (delta < 0)
        {
	  delta = -delta;
        }
      if (m_rttVar != 0)
        {
	  m_rttVar = MilliSeconds (m_g * delta + (1-m_g) * m_rttVar.GetMilliSeconds ());
        }
      else
        { 
	  m_rttVar = MilliSeconds (delta);
        }
    }
}

/*
 * Returns 1 if recent queue length is <= half the maximum and 0 otherwise.
 */
int32_t 
Snoop::SnoopQLong ()
{
  /* For now only instantaneous lengths */
  //	if (parent_->ifq()->length() <= 3*parent_->ifq()->limit()/4)
	
  return 1;
  //	return 0;
}

/*
 * Ideally, would like to schedule snoop retransmissions at higher priority.
 */
uint32_t
Snoop::SnoopRxmit (Ptr<Packet> pkt, Callback< bool, Ptr<Packet>,
                   const Address &, uint16_t > callback, Mac48Address from)
{
  if (pkt != 0) 
    {
      SnoopHeader sh;
      pkt->RemoveHeader (sh);
      if (sh.GetNumRxmit () < SNOOP_MAX_RXMIT && SnoopQLong ()) 
        {
	  sh.SetSendTime (Simulator::Now ());
          sh.SetNumRxmit (sh.GetNumRxmit () +1);
          pkt->AddHeader (sh);
          Ptr<Packet> p = pkt->Copy ();
          callback (p, from, 1);//parent_->sendDown(p);
	} 
      else
        { 
          pkt->AddHeader (sh);
	  return SNOOP_PROPAGATE;
        }
    }
  /* Reset timeout for later time. */
  //if (toutPending_) {
  //s.cancel(toutPending_);
  // xxx: I think that toutPending_ doesn't need to be freed because snoop didn't allocate it (but I'm not sure).
  //};
  //toutPending_ = (Event *)pkt;
  m_rxmitTimer.SetArguments (callback, from);  // done
  m_rxmitTimer.Schedule (Timeout ());
  
  return SNOOP_SUPPRESS;
}

void 
Snoop::SnoopCleanup ()
{
}



void
Snoop::RxmitTimerExpire ( Callback< bool, Ptr<Packet>,
                   const Address &, uint16_t > callback, Mac48Address from)
{
  Ptr<Packet> p= m_pkts[m_bufTail];
  if (p == 0)
    {
      return;
    }
  SnoopHeader sh;
  p->RemoveHeader (sh);
  if((int32_t)sh.GetSeqNo () != m_lastAck + 1) 
    {
      return;
    }
  if ((m_bufHead != m_bufTail) || (m_fState & SNOOP_FULL)) 
    {
      if (SnoopRxmit (p, callback, from) == SNOOP_SUPPRESS)
        {
          m_expNextAck = Next (m_bufTail);
	}
    }
  p->AddHeader (sh);
}

Time 
Snoop::Timeout ()
{
  //if (m_integrate)
    //{
      return Max(m_srtt+4*m_rttVar, m_snoopTick);
    //}
  		
}

}
