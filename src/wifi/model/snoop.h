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
#ifndef SNOOP_H
#define SNOOP_H

#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/net-device.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/timer.h"
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
/* Snoop states */

#define SNOOP_ACTIVE    0x01	/* connection active */
#define SNOOP_CLOSED    0x02	/* connection closed */
#define SNOOP_NOACK     0x04	/* no ack seen yet */
#define SNOOP_FULL      0x08	/* snoop cache full */
#define SNOOP_RTTFLAG   0x10	/* can compute RTT if this is set */
#define SNOOP_ALIVE     0x20	/* connection has been alive past 1 sec */
#define SNOOP_WLALIVE   0x40	/* wl connection has been alive past 1 sec */
#define SNOOP_WLEMPTY   0x80

#define SNOOP_MAXWIND   100	/* XXX */
#define SNOOP_WLSEQS    8
#define SNOOP_MIN_TIMO  0.100	/* in seconds */
#define SNOOP_MAX_RXMIT 10	/* quite arbitrary at this point */
#define SNOOP_PROPAGATE 1
#define SNOOP_SUPPRESS  2

#define SNOOP_MAKEHANDLER 1
#define SNOOP_TAIL 1

#define IPPROTO_TCP 6

namespace ns3 {

struct Sequence 
{
  uint32_t m_seq;
  uint32_t m_num;
};

class SnoopHeader : public Header
{ 
public:
/// c-tor
  SnoopHeader (uint32_t seqNo, uint32_t numRxmit, uint32_t senderRxmit, Time sendTime);
  SnoopHeader ();
  virtual ~SnoopHeader ();
  // Header serialization/deserialization
  static TypeId GetTypeId ();
  TypeId GetInstanceTypeId () const;
  uint32_t GetSerializedSize () const;
  void Serialize (Buffer::Iterator start) const;
  uint32_t Deserialize (Buffer::Iterator start);
  void Print (std::ostream &os) const;

  /// Return fields
  uint32_t GetSeqNo () const { return m_seqNo; }
  uint32_t GetNumRxmit () const { return m_numRxmit; }
  uint32_t GetSenderRxmit () const { return m_senderRxmit; }
  Time GetSendTime () const { return m_sendTime; }

  void SetSeqNo (uint32_t seqNo) { m_seqNo = seqNo; }
  void SetNumRxmit (uint32_t numRxmit) { m_numRxmit = numRxmit; }
  void SetSenderRxmit (uint32_t senderRxmit) { m_senderRxmit = senderRxmit; } 
  void SetSendTime (Time sendTime) { m_sendTime = sendTime; } 
  bool operator== (SnoopHeader const & o) const;

private:
  uint32_t m_seqNo;
  uint32_t m_numRxmit;
  uint32_t m_senderRxmit;
  Time m_sendTime;
  int32_t  m_integrate;
};

/*class LLSnoop : public WifiNetDevice 
{
public:
	//LLSnoop() : LL() { bind("integrate_", &integrate_);}
  LLSnoop () : WifiNetDevice () {}
  bool Send (Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber);
  bool SendFrom (Ptr<Packet> packet, const Address& source, const Address& dest, uint16_t protocolNumber);
  void ForwardUp (Ptr<Packet> packet, Mac48Address from, Mac48Address to);
  void SnoopRtt (Time sendTime);
  Time GetTimeout () { return Max (m_srtt+ 4 * m_rttVar, m_snoopTick); }
  int32_t GetIntegrate () { return m_integrate; }
private:
  int32_t m_integrate;
  Time m_srtt;
  Time m_rttVar;
  double m_g;           //Time or double??
  Time m_snoopTick;	// minimum rxmission timer granularity 
};
*/
class Snoop : public Object 
{
public:
   /**
   * \brief Get the type ID.
   *
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  Snoop ();
  virtual ~Snoop (); 
  //void Receive (Ptr<Packet> p, const Address &from, const Address &to);		//void recv(Packet *, Handler *);  const Packet??
  uint32_t HandlePacket (Ptr<Packet> p,Callback< bool, Ptr<Packet>,const Address &, uint16_t > callback, Mac48Address from);				//void handle(Event *);
  uint32_t SnoopRxmit (Ptr<Packet> pkt, Callback< bool, Ptr<Packet>,const Address &, uint16_t > callback, Mac48Address from);
  uint32_t Next (uint32_t i) { return (i + 1) % m_maxBufs; }
  uint32_t Prev (uint32_t i) { return ((i == 0) ? m_maxBufs - 1 : i - 1); };
  uint32_t WlNext (uint32_t i) { return (i + 1) % SNOOP_WLSEQS; }
  uint32_t WlPrev (uint32_t i) { return ((i == 0) ? SNOOP_WLSEQS - 1 : i - 1);};

protected:
  virtual void DoInitialize (void);						//void Reset ();	or initialize??
  void WlReset ();
  void SnoopData (Ptr<Packet> p);
  int32_t  SnoopAck (Ptr<Packet> p,  Callback< bool, Ptr<Packet>,const Address &, uint16_t > callback, Mac48Address from);
  void SnoopWlessData (Ptr<Packet> p);
  void SnoopWiredAck (Ptr<Packet> p);
  int32_t  SnoopWlessLoss (uint32_t ack);
  Time SnoopCleanBufs ( SequenceNumber32 ack,Callback< bool, Ptr<Packet>,const Address &, uint16_t > callback,Mac48Address from);
  void SnoopRtt (Time sendTime);
  int32_t SnoopQLong ();
  uint32_t SnoopInsert (Ptr<Packet> p);
  bool IsEmpty () { return (m_bufHead == m_bufTail && !(m_fState & SNOOP_FULL));}
  void SavePacket (Ptr<Packet> p, uint32_t seq, int32_t i);
  void UpdateState ();
  Time Timeout ();								//Define in .cc
  void SnoopCleanup ();
  void RxmitTimerExpire (Callback< bool, Ptr<Packet>,const Address &, uint16_t > callback, Mac48Address from);
private:	
 // LLSnoop *m_parent;	/* the parent link layer object */
  //NsObject* recvtarget_;	/* where packet is passed up the stack */
  //Handler  *callback_;
  bool m_snoopDisable;		/* disable snoop for this mobile */
  uint16_t  m_fState;	        /* state of connection */
  int32_t m_lastSeen;	        /* first byte of last packet buffered */
  int32_t m_lastAck;	        /* last byte recd. by mh for sure */
  int32_t m_expNextAck; 	/* expected next ack after dup sequence */
  int16_t m_expDupAcks;		/* expected number of dup acks */
  Time m_srtt;			/* smoothed rtt estimate */
  Time m_rttVar;		/* linear deviation */
  Time m_tailTime;	        /* time at which earliest unack'd pkt sent */
  int32_t m_rxmitStatus;
  uint16_t m_bufHead;		/* next pkt goes here */
  //Event    *toutPending_;	/* # pending timeouts */
  uint16_t m_bufTail;		/* first unack'd pkt */
  Ptr<Packet> m_pkts[SNOOP_MAXWIND]; /* ringbuf of cached pkts */
  int32_t m_wlState;
  int32_t m_wlLastSeen;
  int32_t m_wlLastAck;
  uint32_t m_wlBufHead;
  uint32_t m_wlBufTail;
  Timer m_rxmitTimer;		//SnoopRxmitHandler *rxmitHandler_; /* used in rexmissions */
  //Timer m_persistTimer;         /* for connection (in)activity */
  Sequence *m_wlSeqs[SNOOP_WLSEQS];	/* ringbuf of wless data */
  int32_t m_maxBufs;	/* max number of pkt bufs */
  Time m_snoopTick;	/* minimum rxmission timer granularity */
  double m_g;		/* gain in EWMA for srtt_ and rttvar */
  int32_t m_integrate;	/* integrate loss rec across active conns */
  bool m_lru;		/* an lru cache?    snoop_wired_ack and snoopwirelessdata */
};

/*class SnoopRxmitHandler : public Handler {
  public:
	SnoopRxmitHandler(Snoop *s) : snoop_(s) {}
	void handle(Event *event);
  protected:
	Snoop *snoop_;
};

class SnoopPersistHandler : public Handler {
  public:
	SnoopPersistHandler(Snoop *s) : snoop_(s) {}
	//void handle(Event *);
  protected:
	Snoop *snoop_;
};
*/
} 

#endif 
