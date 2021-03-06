/*
  Author: Zeyu Chen 
  Based on TCP VENO
*/

#include "tcp-ictcp-improved.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/log.h"

namespace ns3 {

  NS_LOG_COMPONENT_DEFINE ("TcpIctcpImproved");
  NS_OBJECT_ENSURE_REGISTERED (TcpIctcpImproved); 

  TypeId TcpIctcpImproved::GetTypeId (void) {
    static TypeId tid = TypeId ("ns3::TcpIctcpImproved")
      .SetParent<TcpNewReno> ()
      .AddConstructor<TcpIctcpImproved> ()
      .SetGroupName ("Internet")
      .AddAttribute ("alpha", "Current measured throughput",
                     UintegerValue (3),
                     MakeUintegerAccessor (&TcpIctcpImproved::m_beta),
                     MakeUintegerChecker<uint32_t> ())
      .AddAttribute("Count", "Decrease after three times",
                     UintegerValue(0),
                     MakeUintegerAccessor(&TcpIctcpImproved::m_count),
                     MakeUintegerChecker<uint32_t>())
      .AddAttribute("throughput","measured throughtput",
                     DoubleValue(0),
                     MakeDoubleAccessor(&TcpIctcpImproved::m_measure_thru),
                     MakeDoubleChecker<double>());
    return tid;
  }

  TcpIctcpImproved::TcpIctcpImproved (void)
    : TcpNewReno (),
      m_measure_thru (0),   // newly add
      m_count (0), // newly add
      m_baseRtt (Time::Max ()),
      m_minRtt (Time::Max ()),
      m_cntRtt (0),
      m_doingVenoNow (true),
      m_diff (0),
      m_inc (true),
      m_ackCnt (0),
      m_beta (6)
  {
    NS_LOG_FUNCTION (this);
  }

  TcpIctcpImproved::TcpIctcpImproved (const TcpIctcpImproved& sock)
    : TcpNewReno (sock),
      m_measure_thru(sock.m_measure_thru), // newly add
      m_count(sock.m_count),
      m_baseRtt (sock.m_baseRtt),
      m_minRtt (sock.m_minRtt),
      m_cntRtt (sock.m_cntRtt),
      m_doingVenoNow (true),
      m_diff (0),
      m_inc (true),
      m_ackCnt (sock.m_ackCnt),
      m_beta (sock.m_beta)
  {
    NS_LOG_FUNCTION (this);
  }

  TcpIctcpImproved::~TcpIctcpImproved (void) {
    NS_LOG_FUNCTION (this);
  }

  Ptr<TcpCongestionOps> TcpIctcpImproved::Fork (void) {
    return CopyObject<TcpIctcpImproved> (this);
  }

  void TcpIctcpImproved::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) {
    NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);

    if (rtt.IsZero ()) {
        return;
      }

    m_minRtt = std::min (m_minRtt, rtt);
    NS_LOG_DEBUG ("Updated m_minRtt= " << m_minRtt);


    m_baseRtt = std::min (m_baseRtt, rtt);
    NS_LOG_DEBUG ("Updated m_baseRtt= " << m_baseRtt);

    // Update RTT counter
    m_cntRtt++;
    NS_LOG_DEBUG ("Updated m_cntRtt= " << m_cntRtt);
  }

  void TcpIctcpImproved::EnableIctcp (Ptr<TcpSocketState> tcb) {
    NS_LOG_FUNCTION (this << tcb);
    m_doingVenoNow = true;
    m_cntRtt = 0;
    m_minRtt = Time::Max ();
  }

  void TcpIctcpImproved::DisableIctcp () {
    NS_LOG_FUNCTION (this);

    m_doingVenoNow = false;
  }

  void TcpIctcpImproved::CongestionStateSet (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState) {
    NS_LOG_FUNCTION (this << tcb << newState);
    if (newState == TcpSocketState::CA_OPEN) {
        EnableIctcp (tcb);
        NS_LOG_LOGIC ("TcpIctcpImproved is now on.");
      }
    else {
        DisableIctcp ();
        NS_LOG_LOGIC ("TcpIctcpImproved is turned off.");
      }
  }

  // My Code
  /////////////////////////////////////////////////////////////////////////////////////////
  std::string TcpIctcpImproved::GetName () const {
    return "TcpIctcpImproved";
  }

  double TcpIctcpImproved::ComputeThroughputDiff (Ptr<const TcpSocketState> tcb) {
    // Declare
    double beta = 0.5;             // exponential factor
    double curr_thru = 0.0;        // current throughput
    double expec_thru = 0.0;       // expected throughput
    double thru_diff = 0.0;        // throughput difference
    uint32_t curr_cwnd = 0;        // current window

    // Compute the throughput difference based on paper
    // Compute the current cwnd
    curr_cwnd = tcb->GetCwndInSegments();
    curr_thru = m_baseRtt.GetSeconds()*curr_cwnd;

    // Get meausred throughput
    m_measure_thru = std::max (curr_thru, beta*m_measure_thru + (1 - beta)*curr_thru);
    // Get expected throughput
    expec_thru = std::max (m_measure_thru, (double)(tcb->m_rcvWnd.Get()/m_baseRtt.GetSeconds()));
    // Compute throughput difference
    thru_diff = (expec_thru - m_measure_thru)/expec_thru;

    return thru_diff;
  }

  void TcpIctcpImproved::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) {
    NS_LOG_FUNCTION (this << tcb << segmentsAcked); 

    // Get throughput difference
    double thru_diff = ComputeThroughputDiff(tcb);
    double threshhold1 = 0.1;

    // Case 1: Increase the receive window, there is enough quota
    if (thru_diff <= threshhold1 || thru_diff <= tcb->m_segmentSize / tcb->m_rcvWnd) { 
      if (tcb->m_rcvWnd < tcb->m_ssThresh) {
      // Slow start mode. Veno employs same slow start algorithm as NewReno's.
        NS_LOG_LOGIC ("We are in slow start, behave like NewReno.");
        segmentsAcked = RxSlowStart (tcb, segmentsAcked);
      }
      // Otherwise: Keep the window
      else { 
      // Congestion avoidance mode
        NS_LOG_LOGIC ("We are in congestion avoidance, execute Veno additive "
                      "increase algo.");
        RxCongestionAvoidance (tcb, segmentsAcked);
      }
      NS_LOG_LOGIC ("Stage1");
      TcpNewReno::IncreaseWindow (tcb, segmentsAcked);
    }
    // Reset cntRtt & minRtt every RTT
    m_cntRtt = 0;
    m_minRtt = Time::Max ();
  }

  uint32_t TcpIctcpImproved::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) {
    
    NS_LOG_FUNCTION (this << tcb << bytesInFlight);
      double threshhold2 = 0.5;
      double thru_diff = ComputeThroughputDiff(tcb);
      // Case 2: Decrease the receive window, thru_diff > threshhold2 when 3 continued RTT
      if (thru_diff > threshhold2 && m_count >= 2) {
        m_count=0;
        return tcb->m_rcvWnd.Get() - tcb->m_segmentSize;
      }
      // Otherwise: Keep the window 
      else if (thru_diff > threshhold2) { 
        m_count++;
        return tcb->m_rcvWnd.Get();
      }
      // Otherwise: Keep the window 
      else {
        return tcb->m_rcvWnd.Get();
      }
  }

  void TcpIctcpImproved::RxCongestionAvoidance (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) {
    NS_LOG_FUNCTION (this << tcb << segmentsAcked);

    if (segmentsAcked > 0) {
        double adder = static_cast<double> (tcb->m_segmentSize * tcb->m_segmentSize) / tcb->m_rcvWnd.Get ();
        adder = std::max (1.0, adder);
        tcb->m_rcvWnd += static_cast<uint32_t> (adder);
        NS_LOG_INFO ("In RxCongAvoid, updated to rwnd " << tcb->m_rcvWnd <<
                     " ssthresh " << tcb->m_ssThresh);
      }
  }

  uint32_t TcpIctcpImproved::RxSlowStart (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) {
    NS_LOG_FUNCTION (this << tcb << segmentsAcked);

    if (segmentsAcked >= 1) {
        tcb->m_rcvWnd += tcb->m_segmentSize;
        NS_LOG_INFO ("In RxSlowStart, updated to rwnd " << tcb->m_rcvWnd << " ssthresh " << tcb->m_ssThresh);
        return segmentsAcked - 1;
      }

    return 0;
  }

} // namespace ns3
