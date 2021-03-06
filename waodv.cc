/*
 Copyright (c) 1997, 1998 Carnegie Mellon University.  All Rights
 Reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 3. The name of the author may not be used to endorse or promote products
 derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The AODV code developed by the CMU/MONARCH group was optimized and tuned by Samir Das and Mahesh Marina, University of Cincinnati. The work was partially done in Sun Microsystems. Modified for gratuitous replies by Anant Utgikar, 09/16/02.

 */

//#include <ip.h>
#include <waodv/waodv.h>
#include <waodv/waodv_packet.h>
#include <random.h>
#include <cmu-trace.h>
#include <iostream>
#include <common/mobilenode.h>
//#include <fstream>
//#include <energy-model.h>

#define max(a,b)        ( (a) > (b) ? (a) : (b) )
#define CURRENT_TIME    Scheduler::instance().clock()

//#define DEBUG
//#define ERROR
#define W1	0.6
#define W2	0.4
#define U	0.5
#define TRUSTUPDATEINTERVAL 	0.05
#define TRUSTTHRESHOLD	0
#define YIT	0.05

#ifdef DEBUG
static int extra_route_reply = 0;
static int limit_route_request = 0;
static int route_request = 0;
#endif

/*
 TCL Hooks
 */
//全局邻居列表 如果要获取节点x的邻居列表，调用方式为 WAODV_Neighbor *p = nblist[x];
WAODV_Neighbor * WAODV::nblist[50] = { NULL };
//全局hello包计数，如果要知道节点x的邻居列表，调用方式为 float p = hellocount[x];
float WAODV::hellocount[50]={1};
int hdr_waodv::offset_;
//下面两个类是tcl脚本接口，不需要改变
static class WAODVHeaderClass: public PacketHeaderClass {
public:
	WAODVHeaderClass() :
			PacketHeaderClass("PacketHeader/WAODV", sizeof(hdr_all_waodv)) {
		bind_offset(&hdr_waodv::offset_);
	}
} class_rtProtoWAODV_hdr;

static class WAODVclass: public TclClass {
public:
	WAODVclass() :
			TclClass("Agent/WAODV") {
	}
	TclObject* create(int argc, const char* const * argv) {
		assert(argc == 5);
		//return (new AODV((nsaddr_t) atoi(argv[4])));
		return (new WAODV((nsaddr_t) Address::instance().str2addr(argv[4])));
	}
} class_rtProtoWAODV;

int WAODV::command(int argc, const char* const * argv) {
	if (argc == 2) {
		Tcl& tcl = Tcl::instance();

		if (strncasecmp(argv[1], "id", 2) == 0) {
			tcl.resultf("%d", index);
			return TCL_OK;
		}

		if (strncasecmp(argv[1], "start", 2) == 0) {
			btimer.handle((Event*) 0);

#ifndef WAODV_LINK_LAYER_DETECTION
			htimer.handle((Event*) 0);
			ntimer.handle((Event*) 0);
#endif // LINK LAYER DETECTION

			rtimer.handle((Event*) 0);
			trustimer.sched((double) 0.1);
			hc.sched((double) 0.1);
			return TCL_OK;
		}
	} else if (argc == 3) {
		if (strcmp(argv[1], "index") == 0) {
			index = atoi(argv[2]);
			return TCL_OK;
		}

		else if (strcmp(argv[1], "log-target") == 0
				|| strcmp(argv[1], "tracetarget") == 0) {
			logtarget = (Trace*) TclObject::lookup(argv[2]);
			if (logtarget == 0)
				return TCL_ERROR;
			return TCL_OK;
		} else if (strcmp(argv[1], "drop-target") == 0) {
			int stat = rqueue.command(argc, argv);
			if (stat != TCL_OK)
				return stat;
			return Agent::command(argc, argv);
		} else if (strcmp(argv[1], "if-queue") == 0) {
			ifqueue = (PriQueue*) TclObject::lookup(argv[2]);

			if (ifqueue == 0)
				return TCL_ERROR;
			return TCL_OK;
		} else if (strcmp(argv[1], "port-dmux") == 0) {
			dmux_ = (PortClassifier *) TclObject::lookup(argv[2]);
			if (dmux_ == 0) {
				fprintf(stderr, "%s: %s lookup of %s failed\n", __FILE__,
						argv[1], argv[2]);
				return TCL_ERROR;
			}
			return TCL_OK;
		}

		//混杂监听
		else if (strcmp(argv[1], "install-tap") == 0) {
			//printf("执行install-tap /n");
			//ofstream fout ("/home/ldy/test.txt",ios::app);
//		if (fout){
//			fout<<"^_^!!"<<std::endl;
//
//			}
			//std::cout<<"install-tap^_^"<<endl;

			mac_ = (Mac*) TclObject::lookup(argv[2]);
			if (mac_ == 0)
				return TCL_ERROR;
			mac_->installTap(this);
			return TCL_OK;
		}
	}
	return Agent::command(argc, argv);
}
//混杂监听
void WAODV::tap(const Packet *p) {
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	//sendToWatchdog(p);
	if (ch->ptype() == PT_CBR) {
		/**
		 * 下面定义的变量用于测试输出到控制台用
		 */
		nsaddr_t index = this->index;
		nsaddr_t src_ = ih->saddr();
		nsaddr_t prev_hop_ = ch->prev_hop_;
		nsaddr_t next_hop_ = ch->next_hop_;
		nsaddr_t drc_ = ih->daddr();
		//混杂监听，更新当前节点的统计信息
//		std::cout<<"tap" <<" node"<<index<<" "<<ch->uid()<<" "<<prev_hop_<<" "<<next_hop_<<" "<<drc_<<" "<<CURRENT_TIME<<std::endl;
//		nr_update(p);
	}
}

/* 
 Constructor
 */

NR::NR() {
	float forwardto = 0.001;			//保证除数不为0
	float recvfrom = 0;
}
WAODV::WAODV(nsaddr_t id) :
		Agent(PT_WAODV), btimer(this), htimer(this), ntimer(this), rtimer(this), lrtimer(
				this), trustimer(this), hc(this), rqueue() {

	index = id;
	seqno = 2;
	bid = 1;

	LIST_INIT(&nbhead);
	LIST_INIT(&bihead);
	logtarget = 0;
	ifqueue = 0;
	minc = 10000000;
}

/*
 Timers
 */
void UpdateTrust::expire(Event *e) {
	//更新当前节点对其他节点的信任值
	agent->nr_trustupdate();
	agent->trustimer.resched((double) TRUSTUPDATEINTERVAL);
}
void HelloCount::expire(Event *e) {
	//统计hello包用于QoS计算
	agent->hcount();
	agent->hc.resched((double) 1);
}
void BroadcastTimerw::handle(Event*) {
	agent->id_purge();			//删除节点
	Scheduler::instance().schedule(this, &intr, BCAST_ID_SAVE);
}

void HelloTimerw::handle(Event*) {
	agent->sendHello();			//给包内各项赋值
	double interval = MinHelloInterval
			+ ((MaxHelloInterval - MinHelloInterval) * Random::uniform());
	assert(interval >= 0);
	Scheduler::instance().schedule(this, &intr, interval);
}

void NeighborTimerw::handle(Event*) {
	agent->nb_purge();
	Scheduler::instance().schedule(this, &intr, HELLO_INTERVAL);
}

void RouteCacheTimerw::handle(Event*) {
	agent->rt_purge();
#define FREQUENCY 0.5 // sec
	Scheduler::instance().schedule(this, &intr, FREQUENCY);
}

void LocalRepairTimerw::handle(Event* p) {  // SRD: 5/4/99
	waodv_rt_entry *rt;
	struct hdr_ip *ih = HDR_IP((Packet * )p);

	/* you get here after the timeout in a local repair attempt */
	/*	fprintf(stderr, "%s\n", __FUNCTION__); */

	rt = agent->rtable.rt_lookup(ih->daddr());

	if (rt && rt->rt_flags != RTF_UP) {
		// route is yet to be repaired
		// I will be conservative and bring down the route
		// and send route errors upstream.
		/* The following assert fails, not sure why */
		/* assert (rt->rt_flags == RTF_IN_REPAIR); */

		//rt->rt_seqno++;
		agent->rt_down(rt);
		// send RERR
#ifdef DEBUG
		fprintf(stderr,"Node %d: Dst - %d, failed local repair\n",index, rt->rt_dst);
#endif      
	}
	Packet::free((Packet *) p);
}

/*
 Broadcast ID Management  Functions
 */

void WAODV::id_insert(nsaddr_t id, u_int32_t bid) {
	BroadcastID *b = new BroadcastID(id, bid);

	assert(b);
	b->expire = CURRENT_TIME+ BCAST_ID_SAVE;
	LIST_INSERT_HEAD(&bihead, b, link);
}

/* SRD */
bool WAODV::id_lookup(nsaddr_t id, u_int32_t bid) {
	BroadcastID *b = bihead.lh_first;

	// Search the list for a match of source and bid
	for (; b; b = b->link.le_next) {
		if ((b->src == id) && (b->id == bid))
			return true;
	}
	return false;
}

void WAODV::id_purge() {
	BroadcastID *b = bihead.lh_first;
	BroadcastID *bn;
	double now = CURRENT_TIME;

	for (; b; b = bn) {
		bn = b->link.le_next;
		if (b->expire <= now) {
			LIST_REMOVE(b, link);
			delete b;
		}
	}
}

/*
 Helper Functions
 */

double WAODV::PerHopTime(waodv_rt_entry *rt) {
	int num_non_zero = 0, i;
	double total_latency = 0.0;

	if (!rt)
		return ((double) NODE_TRAVERSAL_TIME);

	for (i = 0; i < MAX_HISTORY; i++) {
		if (rt->rt_disc_latency[i] > 0.0) {
			num_non_zero++;
			total_latency += rt->rt_disc_latency[i];
		}
	}
	if (num_non_zero > 0)
		return (total_latency / (double) num_non_zero);
	else
		return ((double) NODE_TRAVERSAL_TIME);

}

/*
 Link Failure Management Functions
 */

static void waodv_rt_failed_callback(Packet *p, void *arg) {
	((WAODV*) arg)->rt_ll_failed(p);
}

/*
 * This routine is invoked when the link-layer reports a route failed.
 */
void WAODV::rt_ll_failed(Packet *p) {
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	waodv_rt_entry *rt;
	nsaddr_t broken_nbr = ch->next_hop_;

#ifndef WAODV_LINK_LAYER_DETECTION
	drop(p, DROP_RTR_MAC_CALLBACK);
#else 

	/*
	 * Non-data packets and Broadcast Packets can be dropped.
	 */
	if(! DATA_PACKET(ch->ptype()) ||
			(u_int32_t) ih->daddr() == IP_BROADCAST) {
		drop(p, DROP_RTR_MAC_CALLBACK);
		return;
	}
	log_link_broke(p);
	if((rt = rtable.rt_lookup(ih->daddr())) == 0) {
		drop(p, DROP_RTR_MAC_CALLBACK);
		return;
	}
	log_link_del(ch->next_hop_);

#ifdef WAODV_LOCAL_REPAIR
	/* if the broken link is closer to the dest than source,
	 attempt a local repair. Otherwise, bring down the route. */

	if (ch->num_forwards() > rt->rt_hops) {
		local_rt_repair(rt, p); // local repair
		// retrieve all the packets in the ifq using this link,
		// queue the packets for which local repair is done,
		return;
	}
	else
#endif // LOCAL REPAIR	

	{
		drop(p, DROP_RTR_MAC_CALLBACK);
		// Do the same thing for other packets in the interface queue using the
		// broken link -Mahesh
		while((p = ifqueue->filter(broken_nbr))) {
			drop(p, DROP_RTR_MAC_CALLBACK);
		}
		nb_delete(broken_nbr);
	}

#endif // LINK LAYER DETECTION
}

void WAODV::handle_link_failure(nsaddr_t id) {
	waodv_rt_entry *rt, *rtn;
	Packet *rerr = Packet::alloc();
	struct hdr_waodv_error *re = HDR_WAODV_ERROR(rerr);

	re->DestCount = 0;
	for (rt = rtable.head(); rt; rt = rtn) {  // for each rt entry
		rtn = rt->rt_link.le_next;
		if ((rt->rt_hops != INFINITY2) && (rt->rt_nexthop == id)) {
			assert(rt->rt_flags == RTF_UP);
			assert((rt->rt_seqno%2) == 0);
			rt->rt_seqno++;
			re->unreachable_dst[re->DestCount] = rt->rt_dst;
			re->unreachable_dst_seqno[re->DestCount] = rt->rt_seqno;
#ifdef DEBUG
			fprintf(stderr, "%s(%f): %d\t(%d\t%u\t%d)\n", __FUNCTION__, CURRENT_TIME,
					index, re->unreachable_dst[re->DestCount],
					re->unreachable_dst_seqno[re->DestCount], rt->rt_nexthop);
#endif // DEBUG
			re->DestCount += 1;
			rt_down(rt);
		}
		// remove the lost neighbor from all the precursor lists
		rt->pc_delete(id);
	}

	if (re->DestCount > 0) {
#ifdef DEBUG
		fprintf(stderr, "%s(%f): %d\tsending RERR...\n", __FUNCTION__, CURRENT_TIME, index);
#endif // DEBUG
		sendError(rerr, false);
	} else {
		Packet::free(rerr);
	}
}

void WAODV::local_rt_repair(waodv_rt_entry *rt, Packet *p) {
#ifdef DEBUG
	fprintf(stderr,"%s: Dst - %d\n", __FUNCTION__, rt->rt_dst);
#endif  
	// Buffer the packet
	rqueue.enque(p);

	// mark the route as under repair
	rt->rt_flags = RTF_IN_REPAIR;

	sendRequest(rt->rt_dst);

	// set up a timer interrupt
	Scheduler::instance().schedule(&lrtimer, p->copy(), rt->rt_req_timeout);
}

void WAODV::rt_update(waodv_rt_entry *rt, u_int32_t seqnum, u_int16_t metric,
		nsaddr_t nexthop, double expire_time) {

	rt->rt_seqno = seqnum;
	rt->rt_hops = metric;
	rt->rt_flags = RTF_UP;
	rt->rt_nexthop = nexthop;
	rt->rt_expire = expire_time;
}

void WAODV::rt_update(waodv_rt_entry *rt, u_int32_t seqnum, u_int16_t metric,
		nsaddr_t nexthop, double expire_time, float ct) {

	rt->rt_seqno = seqnum;
	rt->rt_hops = metric;
	rt->rt_flags = RTF_UP;
	rt->rt_nexthop = nexthop;
	rt->rt_expire = expire_time;
	rt->ct = ct;
}

void WAODV::rt_down(waodv_rt_entry *rt) {
	/*
	 *  Make sure that you don't "down" a route more than once.
	 */

	if (rt->rt_flags == RTF_DOWN) {
		return;
	}

	// assert (rt->rt_seqno%2); // is the seqno odd?
	rt->rt_last_hop_count = rt->rt_hops;
	rt->rt_hops = INFINITY2;
	rt->rt_flags = RTF_DOWN;
	rt->rt_nexthop = 0;
	rt->rt_expire = 0;

} /* rt_down function */

/*
 Route Handling Functions
 */

void WAODV::rt_resolve(Packet *p) {
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	waodv_rt_entry *rt;

	/*
	 *  Set the transmit failure callback.  That
	 *  won't change.
	 */
	ch->xmit_failure_ = waodv_rt_failed_callback;
	ch->xmit_failure_data_ = (void*) this;
	rt = rtable.rt_lookup(ih->daddr());
	if (rt == 0) {
		rt = rtable.rt_add(ih->daddr());
	}

	/*
	 * If the route is up, forward the packet
	 */

	if (rt->rt_flags == RTF_UP) {
		assert(rt->rt_hops != INFINITY2);
		//ch->prev_hop_=this->index;
		forward(rt, p, NO_DELAY);
	}
	/*
	 *  if I am the source of the packet, then do a Route Request.
	 */
	else if (ih->saddr() == index) {
		rqueue.enque(p);
		sendRequest(rt->rt_dst);
	}
	/*
	 *	A local repair is in progress. Buffer the packet.
	 */
	else if (rt->rt_flags == RTF_IN_REPAIR) {
		rqueue.enque(p);
	}

	/*
	 * I am trying to forward a packet for someone else to which
	 * I don't have a route.
	 */
	else {
		Packet *rerr = Packet::alloc();
		struct hdr_waodv_error *re = HDR_WAODV_ERROR(rerr);
		/*
		 * For now, drop the packet and send error upstream.
		 * Now the route errors are broadcast to upstream
		 * neighbors - Mahesh 09/11/99
		 */

		assert(rt->rt_flags == RTF_DOWN);
		re->DestCount = 0;
		re->unreachable_dst[re->DestCount] = rt->rt_dst;
		re->unreachable_dst_seqno[re->DestCount] = rt->rt_seqno;
		re->DestCount += 1;
#ifdef DEBUG
		fprintf(stderr, "%s: sending RERR...\n", __FUNCTION__);
#endif
		sendError(rerr, false);

		drop(p, DROP_RTR_NO_ROUTE);
	}

}

void WAODV::rt_purge() {
	waodv_rt_entry *rt, *rtn;
	double now = CURRENT_TIME;
	double delay = 0.0;
	Packet *p;

	for (rt = rtable.head(); rt; rt = rtn) {  // for each rt entry
		rtn = rt->rt_link.le_next;
		if ((rt->rt_flags == RTF_UP) && (rt->rt_expire < now)) {
			// if a valid route has expired, purge all packets from
			// send buffer and invalidate the route.
			assert(rt->rt_hops != INFINITY2);
			while ((p = rqueue.deque(rt->rt_dst))) {
#ifdef DEBUG
				fprintf(stderr, "%s: calling drop()\n",
						__FUNCTION__);
#endif // DEBUG
				drop(p, DROP_RTR_NO_ROUTE);   //丢弃分组
			}
			rt->rt_seqno++;
			assert(rt->rt_seqno%2); //assert：如果条件返回错误，则终止程序。rt->rt_seqno%2？？？？？？？？？？？？？？？？？？？？？？？？？？？
			rt_down(rt);   //重置路由表项
		} else if (rt->rt_flags == RTF_UP) {
			// If the route is not expired,
			// and there are packets in the sendbuffer waiting,
			// forward them. This should not be needed, but this extra
			// check does no harm.
			assert(rt->rt_hops != INFINITY2);
			while ((p = rqueue.deque(rt->rt_dst))) {
				forward(rt, p, delay);
				delay += ARP_DELAY;
			}
		} else if (rqueue.find(rt->rt_dst))
			// If the route is down and
			// if there is a packet for this destination waiting in
			// the sendbuffer, then send out route request. sendRequest
			// will check whether it is time to really send out request
			// or not.
			// This may not be crucial to do it here, as each generated
			// packet will do a sendRequest anyway.

			sendRequest(rt->rt_dst);
	}

}

/*
 Packet Reception Routines
 */

void WAODV::recv(Packet *p, Handler*) {
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);

	assert(initialized());
	//assert(p->incoming == 0);
	// XXXXX NOTE: use of incoming flag has been depracated; In order to track direction of pkt flow, direction_ in hdr_cmn is used instead. see packet.h for details.

	if (ch->ptype() == PT_WAODV) {
		ih->ttl_ -= 1;
		recvWAODV(p);
		return;
	}

	/*
	 *  Must be a packet I'm originating...
	 */
	if ((ih->saddr() == index) && (ch->num_forwards() == 0)) {
		/*
		 * Add the IP Header.
		 * TCP adds the IP header too, so to avoid setting it twice, we check if
		 * this packet is not a TCP or ACK segment.
		 */

		if (ch->ptype() != PT_TCP && ch->ptype() != PT_ACK) {
			ch->size() += IP_HDR_LEN;
		}
		// Added by Parag Dadhania && John Novatnack to handle broadcasting
		if ((u_int32_t) ih->daddr() != IP_BROADCAST) {
			ih->ttl_ = NETWORK_DIAMETER;
		}
	}
	/*
	 *  I received a packet that I sent.  Probably
	 *  a routing loop.
	 */
	else if (ih->saddr() == index) {
		drop(p, DROP_RTR_ROUTE_LOOP);
		return;
	}
	/*
	 *  Packet I'm forwarding...
	 */
	else {
		/*
		 *  Check the TTL.  If it is zero, then discard.
		 */
		if (--ih->ttl_ == 0) {
			drop(p, DROP_RTR_TTL);
			return;
		}
	}
// Added by Parag Dadhania && John Novatnack to handle broadcasting
	if ((u_int32_t) ih->daddr() != IP_BROADCAST) {
		rt_resolve(p);

	} else

		forward((waodv_rt_entry*) 0, p, NO_DELAY);

}

void WAODV::recvWAODV(Packet *p) {
	struct hdr_waodv *ah = HDR_WAODV(p);

	assert(HDR_IP (p)->sport() == RT_PORT);
	assert(HDR_IP (p)->dport() == RT_PORT);

	/*
	 * Incoming Packets.
	 */
	switch (ah->ah_type) {

	case WAODVTYPE_RREQ:
		recvRequest(p);
		break;

	case WAODVTYPE_RREP:
		recvReply(p);
		break;

	case WAODVTYPE_RERR:
		recvError(p);
		break;

	case WAODVTYPE_HELLO:
		recvHello(p);
		break;

	default:
		fprintf(stderr, "Invalid WAODV type (%x)\n", ah->ah_type);
		exit(1);
	}

}

void WAODV::recvRequest(Packet *p) {
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_waodv_request *rq = HDR_WAODV_REQUEST(p);
	waodv_rt_entry *rt;

	/*
	 * Drop if:
	 *      - I'm the source
	 *      - I recently heard this request.
	 */

	if (rq->rq_src == index) {
#ifdef DEBUG
		fprintf(stderr, "%s: got my own REQUEST\n", __FUNCTION__);
#endif // DEBUG
		Packet::free(p);
		return;
	}

	if (rq->rq_dst != index && id_lookup(rq->rq_src, rq->rq_bcast_id)) {

#ifdef DEBUG
		fprintf(stderr, "%s: discarding request\n", __FUNCTION__);
#endif // DEBUG

		Packet::free(p);
		return;
	}

	/*
	 * Cache the broadcast ID
	 */
	if (index != rq->rq_dst)
		id_insert(rq->rq_src, rq->rq_bcast_id);

	/*
	 * We are either going to forward the REQUEST or generate a
	 * REPLY. Before we do anything, we make sure that the REVERSE
	 * route is in the route table.
	 */
	waodv_rt_entry *rt0; // rt0 is the reverse route

	rt0 = rtable.rt_lookup(rq->rq_src);
	if (rt0 == 0) { /* if not in the route table */
		// create an entry for the reverse route.
		rt0 = rtable.rt_add(rq->rq_src);
	}

	rt0->rt_expire = max(rt0->rt_expire, (CURRENT_TIME + REV_ROUTE_LIFE));
	//与论文中实现不太一样，缺少信任值
	//缺少上游节点对自身的信任值
	WAODV_Neighbor *pretrust = nblist[ih->saddr()];
	while (pretrust!=NULL){
		if (pretrust->nb_addr == this->index){
			break;
		}
		pretrust=pretrust->nb_link.le_next;
	}

	if ((rq->rq_src_seqno > rt0->rt_seqno)
			|| ((rq->rq_src_seqno == rt0->rt_seqno)
					&& (rq->rq_hop_count < rt0->rt_hops))) {
		// If we have a fresher seq no. or lesser #hops for the
		// same seq no., update the rt entry. Else don't bother.
		if (index == rq->rq_dst && rq->ct < rt0->ct) {
			/*struct list * p =rq->head;
			 while (p!=NULL){
			 std::cout <<p->addr<<" ";
			 p=p->next;
			 }
			 std::cout<<std::endl;*/
			rt_update(rt0, rq->rq_src_seqno, rq->rq_hop_count, ih->saddr(),
					max(rt0->rt_expire, (CURRENT_TIME + REV_ROUTE_LIFE)),
					rq->ct);
		} else
			rt_update(rt0, rq->rq_src_seqno, rq->rq_hop_count, ih->saddr(),
					max(rt0->rt_expire, (CURRENT_TIME + REV_ROUTE_LIFE)));
		if (rt0->rt_req_timeout > 0.0) {
			// Reset the soft state and
			// Set expiry time to CURRENT_TIME + ACTIVE_ROUTE_TIMEOUT
			// This is because route is used in the forward direction,
			// but only sources get benefited by this change
			rt0->rt_req_cnt = 0;
			rt0->rt_req_timeout = 0.0;
			rt0->rt_req_last_ttl = rq->rq_hop_count;
			rt0->rt_expire = CURRENT_TIME+ ACTIVE_ROUTE_TIMEOUT;
		}

		/* Find out whether any buffered packet can benefit from the
		 * reverse route.
		 * May need some change in the following code - Mahesh 09/11/99
		 */
		assert(rt0->rt_flags == RTF_UP);
		Packet *buffered_pkt;
		while ((buffered_pkt = rqueue.deque(rt0->rt_dst))) {
			if (rt0 && (rt0->rt_flags == RTF_UP)) {
				assert(rt0->rt_hops != INFINITY2);
				forward(rt0, buffered_pkt, NO_DELAY);
			}
		}
	}
	// End for putting reverse route in rt table

	/*
	 * We have taken care of the reverse route stuff.
	 * Now see whether we can send a route reply.
	 */

	rt = rtable.rt_lookup(rq->rq_dst);




	// First check if I am the destination ..

	if (rq->rq_dst == index) {

#ifdef DEBUG
		fprintf(stderr, "%d - %s: destination sending reply\n",
				index, __FUNCTION__);
#endif // DEBUG

		// Just to be safe, I use the max. Somebody may have
		// incremented the dst seqno.
		seqno = max(seqno, rq->rq_dst_seqno) + 1;
		if (seqno % 2)
			seqno++;

		sendReply(rq->rq_src,           // IP Destination
				1,                    // Hop Count
				index,                // Dest IP Address
				seqno,                // Dest Sequence Num
				MY_ROUTE_TIMEOUT,     // Lifetime
				rq->rq_timestamp);    // timestamp

		Packet::free(p);
	}

	// I am not the destination, but I may have a fresh enough route.

	else if (rt && (rt->rt_hops != INFINITY2)
			&& (rt->rt_seqno >= rq->rq_dst_seqno)) {

		//assert (rt->rt_flags == RTF_UP);
		assert(rq->rq_dst == rt->rt_dst);
		//assert ((rt->rt_seqno%2) == 0);	// is the seqno even?
		sendReply(rq->rq_src, rt->rt_hops + 1, rq->rq_dst, rt->rt_seqno,
				(u_int32_t) (rt->rt_expire - CURRENT_TIME),
		//             rt->rt_expire - CURRENT_TIME,
		rq->rq_timestamp);
		// Insert nexthops to RREQ source and RREQ destination in the
		// precursor lists of destination and source respectively
		rt->pc_insert(rt0->rt_nexthop); // nexthop to RREQ source
		rt0->pc_insert(rt->rt_nexthop); // nexthop to RREQ destination

#ifdef RREQ_GRAT_RREP  

		sendReply(rq->rq_dst,
				rq->rq_hop_count,
				rq->rq_src,
				rq->rq_src_seqno,
				(u_int32_t) (rt->rt_expire - CURRENT_TIME),
				//             rt->rt_expire - CURRENT_TIME,
				rq->rq_timestamp);
#endif

// TODO: send grat RREP to dst if G flag set in RREQ using rq->rq_src_seqno, rq->rq_hop_counT

// DONE: Included gratuitous replies to be sent as per IETF aodv draft specification. As of now, G flag has not been dynamically used and is always set or reset in aodv-packet.h --- Anant Utgikar, 09/16/02.

		Packet::free(p);
	}
	/*
	 * Can't reply. So forward the  Route Request
	 */
	else {
		nsaddr_t prehop = ih->saddr();
		ih->saddr() = index;
		float ct = rq->ct;
		WAODV_Neighbor *tmp = nbhead.lh_first;
		while (tmp != NULL) {
			if (tmp->trust_info.trust > TRUSTTHRESHOLD && tmp->nb_addr != prehop) {
				rq->trust *= tmp->trust_info.trust;
				ih->daddr() = tmp->nb_addr;
				rq->rq_hop_count += 1;
				ch->addr_type() = NS_AF_NONE;

				ch->direction() = hdr_cmn::DOWN;
				/* struct list *l = (struct list *)malloc(sizeof (struct list));
				 l->addr=index;
				 l->next=rq->head;
				 rq->head=l;*/
				// Maximum sequence number seen en route
				if (rt)
					rq->rq_dst_seqno = max(rt->rt_seqno, rq->rq_dst_seqno);
				//forward((waodv_rt_entry*) 0, p, DELAY);

				rq->ct = ct + tmp->ext * ((CURRENT_TIME-rq->delay_time)+YIT)*(1-pretrust->trust_info.trust);
					rq->delay_time = CURRENT_TIME;
				Scheduler::instance().schedule(target_, p->copy(),
						0.01 * Random::uniform());
				//std::cout<<"f"<< index<<"--->"<<tmp->nb_addr<<std::endl;
			}
			tmp = tmp->nb_link.le_next;
		}
		Packet::free(p);
	}

}

void WAODV::recvReply(Packet *p) {
//struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_waodv_reply *rp = HDR_WAODV_REPLY(p);
	waodv_rt_entry *rt;
	char suppress_reply = 0;
	double delay = 0.0;

#ifdef DEBUG
	fprintf(stderr, "%d - %s: received a REPLY\n", index, __FUNCTION__);
#endif // DEBUG

	/*
	 *  Got a reply. So reset the "soft state" maintained for
	 *  route requests in the request table. We don't really have
	 *  have a separate request table. It is just a part of the
	 *  routing table itself.
	 */
	// Note that rp_dst is the dest of the data packets, not the
	// the dest of the reply, which is the src of the data packets.
	rt = rtable.rt_lookup(rp->rp_dst);

	/*
	 *  If I don't have a rt entry to this host... adding
	 */
	if (rt == 0) {
		rt = rtable.rt_add(rp->rp_dst);
	}

	/*
	 * Add a forward route table entry... here I am following
	 * Perkins-Royer AODV paper almost literally - SRD 5/99
	 */

	if ((rt->rt_seqno < rp->rp_dst_seqno) ||   // newer route
			((rt->rt_seqno == rp->rp_dst_seqno)
					&& (rt->rt_hops > rp->rp_hop_count))) { // shorter or better route

		// Update the rt entry
		rt_update(rt, rp->rp_dst_seqno, rp->rp_hop_count, rp->rp_src,
				CURRENT_TIME+ rp->rp_lifetime);

		// reset the soft state
		rt->rt_req_cnt = 0;
		rt->rt_req_timeout = 0.0;
		rt->rt_req_last_ttl = rp->rp_hop_count;

		if (ih->daddr() == index) { // If I am the original source
			// Update the route discovery latency statistics
			// rp->rp_timestamp is the time of request origination

			rt->rt_disc_latency[(unsigned char)rt->hist_indx] = (CURRENT_TIME - rp->rp_timestamp)
			/ (double) rp->rp_hop_count;
			// increment indx for next time
			rt->hist_indx = (rt->hist_indx + 1) % MAX_HISTORY;
		}

		/*
		 * Send all packets queued in the sendbuffer destined for
		 * this destination.
		 * XXX - observe the "second" use of p.
		 */
		Packet *buf_pkt;
		while((buf_pkt = rqueue.deque(rt->rt_dst))) {
			if(rt->rt_hops != INFINITY2) {
				assert (rt->rt_flags == RTF_UP);
				// Delay them a little to help ARP. Otherwise ARP
				// may drop packets. -SRD 5/23/99
				forward(rt, buf_pkt, delay);
				delay += ARP_DELAY;
			}
		}
	}
	else {
		suppress_reply = 1;
	}

	/*
	 * If reply is for me, discard it.
	 */

	if (ih->daddr() == index || suppress_reply) {
		Packet::free(p);
	}
	/*
	 * Otherwise, forward the Route Reply.
	 */
	else {
		// Find the rt entry
		waodv_rt_entry *rt0 = rtable.rt_lookup(ih->daddr());
		// If the rt is up, forward
		if (rt0 && (rt0->rt_hops != INFINITY2)) {
			assert(rt0->rt_flags == RTF_UP);
			rp->rp_hop_count += 1;
			rp->rp_src = index;
			forward(rt0, p, NO_DELAY);
			// Insert the nexthop towards the RREQ source to
			// the precursor list of the RREQ destination
			rt->pc_insert(rt0->rt_nexthop); // nexthop to RREQ source

		} else {
			// I don't know how to forward .. drop the reply.
#ifdef DEBUG
			fprintf(stderr, "%s: dropping Route Reply\n", __FUNCTION__);
#endif // DEBUG
			drop(p, DROP_RTR_NO_ROUTE);
		}
	}
}

void WAODV::recvError(Packet *p) {
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_waodv_error *re = HDR_WAODV_ERROR(p);
	waodv_rt_entry *rt;
	u_int8_t i;
	Packet *rerr = Packet::alloc();
	struct hdr_waodv_error *nre = HDR_WAODV_ERROR(rerr);

	nre->DestCount = 0;

	for (i = 0; i < re->DestCount; i++) {
		// For each unreachable destination
		rt = rtable.rt_lookup(re->unreachable_dst[i]);
		if (rt && (rt->rt_hops != INFINITY2) && (rt->rt_nexthop == ih->saddr())
				&& (rt->rt_seqno <= re->unreachable_dst_seqno[i])) {
			assert(rt->rt_flags == RTF_UP);
			assert((rt->rt_seqno%2) == 0); // is the seqno even?
#ifdef DEBUG
					fprintf(stderr, "%s(%f): %d\t(%d\t%u\t%d)\t(%d\t%u\t%d)\n", __FUNCTION__,CURRENT_TIME,
							index, rt->rt_dst, rt->rt_seqno, rt->rt_nexthop,
							re->unreachable_dst[i],re->unreachable_dst_seqno[i],
							ih->saddr());
#endif // DEBUG
			rt->rt_seqno = re->unreachable_dst_seqno[i];
			rt_down(rt);

			// Not sure whether this is the right thing to do
			Packet *pkt;
			while ((pkt = ifqueue->filter(ih->saddr()))) {
				drop(pkt, DROP_RTR_MAC_CALLBACK);
			}

			// if precursor list non-empty add to RERR and delete the precursor list
			if (!rt->pc_empty()) {
				nre->unreachable_dst[nre->DestCount] = rt->rt_dst;
				nre->unreachable_dst_seqno[nre->DestCount] = rt->rt_seqno;
				nre->DestCount += 1;
				rt->pc_delete();
			}
		}
	}

	if (nre->DestCount > 0) {
#ifdef DEBUG
		fprintf(stderr, "%s(%f): %d\t sending RERR...\n", __FUNCTION__, CURRENT_TIME, index);
#endif // DEBUG
		sendError(rerr);
	} else {
		Packet::free(rerr);
	}

	Packet::free(p);
}

/*
 Packet Transmission Routines
 */

void WAODV::forward(waodv_rt_entry *rt, Packet *p, double delay) {
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);

	if (ih->ttl_ == 0) {

#ifdef DEBUG
		fprintf(stderr, "%s: calling drop()\n", __PRETTY_FUNCTION__);
#endif // DEBUG

		drop(p, DROP_RTR_TTL);
		return;
	}

	if (ch->ptype() != PT_WAODV && ch->direction() == hdr_cmn::UP
			&& ((u_int32_t) ih->daddr() == IP_BROADCAST)
			|| (ih->daddr() == here_.addr_)) {
		dmux_->recv(p, 0);
		return;
	}

	if (rt) {
		assert(rt->rt_flags == RTF_UP);
		rt->rt_expire = CURRENT_TIME+ ACTIVE_ROUTE_TIMEOUT;
		ch->next_hop_ = rt->rt_nexthop;
		ch->addr_type() = NS_AF_INET;
		ch->direction() = hdr_cmn::DOWN; //important: change the packet's direction
	} else { // if it is a broadcast packet
		// assert(ch->ptype() == PT_AODV); // maybe a diff pkt type like gaf
		assert(ih->daddr() == (nsaddr_t) IP_BROADCAST);
		ch->addr_type() = NS_AF_NONE;
		ch->direction() = hdr_cmn::DOWN; //important: change the packet's direction
	}

	if (ih->daddr() == (nsaddr_t) IP_BROADCAST) {
		// If it is a broadcast packet
		assert(rt == 0);
		if (ch->ptype() == PT_WAODV) {
			/*
			 *  Jitter the sending of AODV broadcast packets by 10ms
			 */
			Scheduler::instance().schedule(target_, p,
					0.01 * Random::uniform());
		} else {
			Scheduler::instance().schedule(target_, p, 0.);  // No jitter
		}
	} else { // Not a broadcast packet
		if (ch->ptype() == PT_CBR) {
			//std::cout<<"forward "<<ih->saddr()<<" "<<index<<" "<<ch->next_hop_<<" "<<ch->uid()<<" "<<CURRENT_TIME<<" "<<std::endl;
			//如果是源节点，把上一跳的地址设置成-1，这样没有节点会处理这个包
			if (index == ih->saddr()){
				ch->prev_hop_=-1;
			}
			ch->prev_hop_ = ih->saddr();
			ih->saddr()=index;
			nr_add(p);
			//如果下一跳是目的节点，由于目的节点不再转发CBR包，本节点监听不到目的节点转发的包，故在此假设其能投递成功！
			if (ch->next_hop_ == ih->daddr()) {
				NR *r = nr_find(ih->daddr());
				r->recvfrom++;
			}
		}

		if (delay > 0.0) {
			Scheduler::instance().schedule(target_, p, delay);
		} else {
			// Not a broadcast packet, no delay, send immediately
			Scheduler::instance().schedule(target_, p, 0.);
		}

	}

}

void WAODV::sendRequest(nsaddr_t dst) {
// Allocate a RREQ packet 
	Packet *p = Packet::alloc();
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_waodv_request *rq = HDR_WAODV_REQUEST(p);
	waodv_rt_entry *rt = rtable.rt_lookup(dst);

	assert(rt);

	/*更多知道相关问题
	 *  Rate limit sen更多知道相关问题>>
	 *  ding of Route Requests. We are very conservative
	 *  about sending out route requests.
	 */

	if (rt->rt_flags == RTF_UP) {
		assert(rt->rt_hops != INFINITY2);
		Packet::free((Packet *) p);
		return;
	}

	if (rt->rt_req_timeout > CURRENT_TIME) {
		Packet::free((Packet *)p);
		return;
	}

	// rt_req_cnt is the no. of times we did network-wide broadcast
	// RREQ_RETRIES is the maximum number we will allow before
	// going to a long timeout.

	if (rt->rt_req_cnt > RREQ_RETRIES) {
		rt->rt_req_timeout = CURRENT_TIME+ MAX_RREQ_TIMEOUT;
		rt->rt_req_cnt = 0;
		Packet *buf_pkt;
		while ((buf_pkt = rqueue.deque(rt->rt_dst))) {
			drop(buf_pkt, DROP_RTR_NO_ROUTE);
		}
		Packet::free((Packet *)p);
		return;
	}

#ifdef DEBUG
	fprintf(stderr, "(%2d) - %2d sending Route Request, dst: %d\n",
			++route_request, index, rt->rt_dst);
#endif // DEBUG

	// Determine the TTL to be used this time.
	// Dynamic TTL evaluation - SRD

	rt->rt_req_last_ttl = max(rt->rt_req_last_ttl, rt->rt_last_hop_count);

	if (0 == rt->rt_req_last_ttl) {
		// first time query broadcast
		ih->ttl_ = TTL_START;
	} else {
		// Expanding ring search.
		if (rt->rt_req_last_ttl < TTL_THRESHOLD)
			ih->ttl_ = rt->rt_req_last_ttl + TTL_INCREMENT;
		else {
			// network-wide broadcast
			ih->ttl_ = NETWORK_DIAMETER;
			rt->rt_req_cnt += 1;
		}
	}

	// remember the TTL used  for the next time
	rt->rt_req_last_ttl = ih->ttl_;

	// PerHopTime is the roundtrip time per hop for route requests.
	// The factor 2.0 is just to be safe .. SRD 5/22/99
	// Also note that we are making timeouts to be larger if we have
	// done network wide broadcast before.

	rt->rt_req_timeout = 2.0 * (double) ih->ttl_ * PerHopTime(rt);
	if (rt->rt_req_cnt > 0)
		rt->rt_req_timeout *= rt->rt_req_cnt;
	rt->rt_req_timeout += CURRENT_TIME;

	// Don't let the timeout to be too large, however .. SRD 6/8/99
	if (rt->rt_req_timeout > CURRENT_TIME+ MAX_RREQ_TIMEOUT)
	rt->rt_req_timeout = CURRENT_TIME + MAX_RREQ_TIMEOUT;
	rt->rt_expire = 0;

#ifdef DEBUG
	fprintf(stderr, "(%2d) - %2d sending Route Request, dst: %d, tout %f ms\n",
			++route_request,
			index, rt->rt_dst,
			rt->rt_req_timeout - CURRENT_TIME);
#endif	// DEBUG

	// Fill out the RREQ packet
	// ch->uid() = 0;
	ch->ptype() = PT_WAODV;
	ch->size() = IP_HDR_LEN + rq->size();
	ch->iface() = -2;
	ch->error() = 0;
	ch->addr_type() = NS_AF_NONE;
	ch->prev_hop_ = index;          // AODV hack

	ih->saddr() = index;
	ih->daddr() = IP_BROADCAST;
	ih->sport() = RT_PORT;
	ih->dport() = RT_PORT;

	// Fill up some more fields.
	rq->rq_type = WAODVTYPE_RREQ;
	rq->rq_hop_count = 1;
	rq->rq_bcast_id = bid++;
	rq->rq_dst = dst;
	rq->rq_dst_seqno = (rt ? rt->rt_seqno : 0);
	rq->rq_src = index;
	seqno += 2;
	assert((seqno%2) == 0);
	rq->rq_src_seqno = seqno;
	rq->rq_timestamp = CURRENT_TIME;
	rq->ct = 0;
	rq->head = NULL;
	rq->delay_time = CURRENT_TIME;
	WAODV_Neighbor *tmp = nbhead.lh_first;
	while (tmp != NULL) {
		if (tmp->trust_info.trust > TRUSTTHRESHOLD) {
			rq->trust = tmp->trust_info.trust;
			ih->daddr() = tmp->nb_addr;
			/* struct list *l = (struct list *)malloc(sizeof (struct list));
			 l->addr=index;
			 l->next=rq->head;
			 rq->head=l;*/
			Scheduler::instance().schedule(target_, p->copy(), 0.);
			//  std::<<"s"<<index<<"--->"<<tmp->nb_addr<<std::endl;
		}
		tmp = tmp->nb_link.le_next;
	}
	Packet::free((Packet *) p);
}

void WAODV::sendReply(nsaddr_t ipdst, u_int32_t hop_count, nsaddr_t rpdst,
		u_int32_t rpseq, u_int32_t lifetime, double timestamp) {
	Packet *p = Packet::alloc();
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_waodv_reply *rp = HDR_WAODV_REPLY(p);
	waodv_rt_entry *rt = rtable.rt_lookup(ipdst);

#ifdef DEBUG
	fprintf(stderr, "sending Reply from %d at %.2f\n", index, Scheduler::instance().clock());
#endif // DEBUG
	assert(rt);

	rp->rp_type = WAODVTYPE_RREP;
	//rp->rp_flags = 0x00;
	rp->rp_hop_count = hop_count;
	rp->rp_dst = rpdst;
	rp->rp_dst_seqno = rpseq;
	rp->rp_src = index;
	rp->rp_lifetime = lifetime;
	rp->rp_timestamp = timestamp;

	// ch->uid() = 0;
	ch->ptype() = PT_WAODV;
	ch->size() = IP_HDR_LEN + rp->size();
	ch->iface() = -2;
	ch->error() = 0;
	ch->addr_type() = NS_AF_INET;
	ch->next_hop_ = rt->rt_nexthop;
	ch->prev_hop_ = index;          // AODV hack
	ch->direction() = hdr_cmn::DOWN;

	ih->saddr() = index;
	ih->daddr() = ipdst;
	ih->sport() = RT_PORT;
	ih->dport() = RT_PORT;
	ih->ttl_ = NETWORK_DIAMETER;

	Scheduler::instance().schedule(target_, p, 0.);

}

void WAODV::sendError(Packet *p, bool jitter) {
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_waodv_error *re = HDR_WAODV_ERROR(p);

#ifdef ERROR
	fprintf(stderr, "sending Error from %d at %.2f\n", index, Scheduler::instance().clock());
#endif // DEBUG

	re->re_type = WAODVTYPE_RERR;
	//re->reserved[0] = 0x00; re->reserved[1] = 0x00;
	// DestCount and list of unreachable destinations are already filled

	// ch->uid() = 0;
	ch->ptype() = PT_WAODV;
	ch->size() = IP_HDR_LEN + re->size();
	ch->iface() = -2;
	ch->error() = 0;
	ch->addr_type() = NS_AF_NONE;
	ch->next_hop_ = 0;
	ch->prev_hop_ = index;          // AODV hack
	ch->direction() = hdr_cmn::DOWN;  //important: change the packet's direction

	ih->saddr() = index;
	ih->daddr() = IP_BROADCAST;
	ih->sport() = RT_PORT;
	ih->dport() = RT_PORT;
	ih->ttl_ = 1;

	// Do we need any jitter? Yes
	if (jitter)
		Scheduler::instance().schedule(target_, p, 0.01 * Random::uniform());
	else
		Scheduler::instance().schedule(target_, p, 0.0);

}

/*
 Neighbor Management Functions
 */
void WAODV::sendHello(int test) {
	Packet *p = Packet::alloc();
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_waodv_reply *rh = HDR_WAODV_REPLY(p);

#ifdef DEBUG
	fprintf(stderr, "sending Hello from %d at %.2f\n", index, Scheduler::instance().clock());
#endif // DEBUG
	rh->rp_type = WAODVTYPE_HELLO;
	//rh->rp_flags = 0x00;
	rh->rp_hop_count = 1;
	rh->rp_dst = index;
	rh->rp_dst_seqno = seqno;
	rh->rp_lifetime = (1 + ALLOWED_HELLO_LOSS) * HELLO_INTERVAL;
	// ch->uid() = 0;
	ch->ptype() = PT_WAODV;
	ch->size() = IP_HDR_LEN + rh->size();
	ch->iface() = -2;
	ch->error() = 0;
	ch->addr_type() = NS_AF_NONE;       //0
	ch->prev_hop_ = index;          // AODV hack

	ih->saddr() = index;
	ih->daddr() = IP_BROADCAST;
	ih->sport() = RT_PORT;
	ih->dport() = RT_PORT;
	ih->ttl_ = 1;
	rh->nb = nbhead.lh_first;
	Scheduler::instance().schedule(target_, p, 0.0);
}
void WAODV::sendHello() {
	Packet *p = Packet::alloc();
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_waodv_reply *rh = HDR_WAODV_REPLY(p);

#ifdef DEBUG
	fprintf(stderr, "sending Hello from %d at %.2f\n", index, Scheduler::instance().clock());
#endif // DEBUG
	rh->rp_type = WAODVTYPE_HELLO;
	//rh->rp_flags = 0x00;
	rh->rp_hop_count = 1;
	rh->rp_dst = index;
	rh->rp_dst_seqno = seqno;
	rh->rp_lifetime = (1 + ALLOWED_HELLO_LOSS) * HELLO_INTERVAL;

	// ch->uid() = 0;
	ch->ptype() = PT_WAODV;
	ch->size() = IP_HDR_LEN + rh->size();
	ch->iface() = -2;
	ch->error() = 0;
	ch->addr_type() = NS_AF_NONE;          //0
	ch->prev_hop_ = index;          // AODV hack

	ih->saddr() = index;
	ih->daddr() = IP_BROADCAST;
	ih->sport() = RT_PORT;
	ih->dport() = RT_PORT;
	ih->ttl_ = 1;
	//统计发出的hello包总数
	hellocount[index]++;
	Scheduler::instance().schedule(target_, p, 0.0);
}

void WAODV::recvHello(Packet *p) {
//struct hdr_ip *ih = HDR_IP(p);
	struct hdr_waodv_reply *rp = HDR_WAODV_REPLY(p);
	WAODV_Neighbor *nb;

	nb = nb_lookup(rp->rp_dst);
	if (nb == 0) {
		nb = nb_insert(rp->rp_dst, rp->nb);
	} else {
		nb->nb_expire = CURRENT_TIME+
		(1.5 * ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
	}
	//统计收到的hello包总数
	nb->count++;
	nb->time = CURRENT_TIME;
	Packet::free(p);
}

void WAODV::nb_insert(nsaddr_t id) {
	WAODV_Neighbor *nb = new WAODV_Neighbor(id);

	assert(nb);
	nb->nb_expire = CURRENT_TIME+
	(1.5 * ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
	LIST_INSERT_HEAD(&nbhead, nb, nb_link);
	seqno += 2;             // set of neighbors changed
	assert((seqno%2) == 0);
}
WAODV_Neighbor *
WAODV::nb_insert(nsaddr_t id, WAODV_Neighbor *nb1) {
	WAODV_Neighbor *nb = new WAODV_Neighbor(id);

	assert(nb);
	nb->nb_expire = CURRENT_TIME+
	(1.5 * ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
	LIST_INSERT_HEAD(&nbhead, nb, nb_link);
	seqno += 2;             // set of neighbors changed
	assert((seqno%2) == 0);
	//添加新邻居节点时，从其收到的hello包数目归零
	nb->count = 0;
	//更新当前节点的邻居列表到全局记录数组
	nblist[index] = nbhead.lh_first;
	return nb;
}

WAODV_Neighbor*
WAODV::nb_lookup(nsaddr_t id) {
	WAODV_Neighbor *nb = nbhead.lh_first;

	for (; nb; nb = nb->nb_link.le_next) {
		if (nb->nb_addr == id)
			break;
	}
	return nb;
}

/*
 * Called when we receive *explicit* notification that a Neighbor
 * is no longer reachable.
 */
void WAODV::nb_delete(nsaddr_t id) {
	WAODV_Neighbor *nb = nbhead.lh_first;

	log_link_del(id);
	seqno += 2;     // Set of neighbors changed
	assert((seqno%2) == 0);

	for (; nb; nb = nb->nb_link.le_next) {
		if (nb->nb_addr == id) {
			LIST_REMOVE(nb, nb_link);
			delete nb;
			break;
		}
	}
	nblist[index] = nbhead.lh_first;
	handle_link_failure(id);

}

/*
 * Purges all timed-out Neighbor Entries - runs every
 * HELLO_INTERVAL * 1.5 seconds.
 */
void WAODV::nb_purge() {
	WAODV_Neighbor *nb = nbhead.lh_first;
	WAODV_Neighbor *nbn;
	double now = CURRENT_TIME;

	for (; nb; nb = nbn) {
		nbn = nb->nb_link.le_next;
		if (nb->nb_expire <= now) {
			nb_delete(nb->nb_addr);
		}
	}

}

NR*
WAODV::nr_find(nsaddr_t nb_addr) {     //寻找是否有数据包号的记录
	NR *r = nr_head;
	while (r != NULL) {
		if (r->nr_nb_id == nb_addr) {
			return r;
		}
		r = r->next;
	}
	return NULL;
}

void WAODV::nr_add(const Packet *p) {

	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	NR *nr = nr_head;
	while (nr != NULL) {
		if (nr->nr_nb_id == ch->next_hop_)
			break;
		nr = nr->next;
	}
	if (nr == NULL) {
		nr = new NR();
		nr->nr_nb_id = ch->next_hop_;
		nr->recvfrom = 0;
		nr->forwardto = 0.001;
		nr->next = nr_head;
		nr_head = nr;
	}
	nr->packet_id = ch->uid();
	nr->forwardto++;
}
/**
 * 更新函数
 */
void WAODV::nr_update(const Packet *p) {
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	NR * r = nr_head;
	/**
	 * 根据数据包中记录的上一跳信息，查找是否是自己发出去的包，如果是自己发出去的包，则邻居正确的转发了该包
	 * 正确转发包字段+1
	 */
	//监听到不是当前节点发送的包，直接返回
	if (index != ch->prev_hop_){
		return ;
	}

	while (r != NULL) {
		if (r->nr_nb_id == ih->saddr()){
			break;
		}
		r = r->next;
	}
	if (r != NULL) {
		r->packet_id=ch->uid();
		r->recvfrom++;
	}
	//nr_print(p);
}
/**
 *打印统计的转发信息
 */
void WAODV::nr_print(const Packet *p) {

	NR * r = nr_head;
	while (r != NULL) {
		std::cout << "r  " << r->packet_id << " " << r->nr_nb_id << " "
				<< r->forwardto << " " << r->recvfrom << " " << "node" << index
				<< " " << CURRENT_TIME<<std::endl;
		r=r->next;
	}
}

void WAODV::nr_trustupdate() {
	WAODV_Neighbor *nb = nbhead.lh_first;
	struct it {
		float sum;
		float num;
	};
	//直接信任
	float d_trust = 0.;
	//间接信任
	float in_trust = 0.;
	struct it *nraddr[60];
	//for循环统计间接信任信息,初始值为-1
	for (int i = 0; i < 60; i++) {
		nraddr[i] = 0;
	}
	/**
	 * 下面这个循环把当前节点的邻居的邻居信息统计到一个数组里，为了计算间接信任
	 * 比如节点1的邻居有2,3，则把2,3的邻居们用一个数组统计他们出现的次数，数组的下标是节点的地址。
	 */
	while (nb != 0) {
		/**这里需要判断当前节点是否与其邻居节传输过数据包
		 * 如果没有传过数据包，那么对其邻居节点的直接信任值为0.5
		 * 如果传过数据包，则使用记录的数据算直接信任
		 */
		NR* r = nr_find(nb->nb_addr);
		if (r != NULL) {
			nb->trust_info.d_trust = (r->recvfrom / r->forwardto);     //计算直接信任值
		}
			WAODV_Neighbor *p = nblist[nb->nb_addr];     //获取邻居节点列表
			while (p != NULL) {
				if (nraddr[p->nb_addr] == 0) {
					struct it *tmp = (struct it *) malloc(sizeof(struct it));
					tmp->num = 1;
					tmp->sum = p->trust_info.trust;
					nraddr[p->nb_addr] = tmp;

				} else {
					nraddr[p->nb_addr]->num++;
					nraddr[p->nb_addr]->sum += p->trust_info.trust;
				}
				p = p->nb_link.le_next;
			}
		//}
		nb = nb->nb_link.le_next;
	}

	nb = nbhead.lh_first;
	while (nb != 0) {
		float d_trust = nb->trust_info.d_trust;
		float in_trust = 0;
		float trust_t = 0;
		float trust = nb->trust_info.trust;
		if (nraddr[nb->nb_addr] != 0) {
			struct it * tmp = nraddr[nb->nb_addr];
			in_trust = tmp->sum / tmp->num;
		}
		trust_t = W1 * d_trust + W2 * in_trust;
		nb->trust_info.trust = U * trust + (1 - U) * trust_t;
//		std::cout << "t " << index << "-->" << nb->nb_addr << " " << d_trust
//				<< " " << in_trust << " " << nb->trust_info.trust << " "
//				<< trust_t <<" "<< CURRENT_TIME<<std::endl;
		nb = nb->nb_link.le_next;
	}
	//释放统计表内存
	for (int i = 0; i < 60; i++) {
		if (nraddr[i])
			free(nraddr[i]);
	}
}

void WAODV::hcount() {
	WAODV_Neighbor *nb = nbhead.lh_first;
	float forward = 1.;     //前向比
	float backward = 1.;     //后向比
	while (nb != NULL) {
		//当前节点收到邻居节点的包的数目/邻居发送的总数目
		/*这里出现一种情况是,backward的值大于1，出现这种情况的原因是
		 * 邻居节点和当前节点的hello包的更新不同步导致
		 *
		 */
		backward = nb->count/hellocount[nb->nb_addr];

		//查找邻居列表
		WAODV_Neighbor *tmp = nblist[nb->nb_addr];
		while (tmp != 0) {
			if (tmp->nb_addr == index)
				break;
			tmp = tmp->nb_link.le_next;
		}
		if (tmp != 0) {
			//邻居节点收到的当前节点发送的数目/当前节点发送的总数目
			forward = tmp->count/hellocount[index];
		}
		nb->ext = 1/(backward * forward);
		//收到的邻居节点的数目包数目清零
		nb->count=1;
		nb = nb->nb_link.le_next;

	}
	//重新计数
	hellocount[index] = 1;
}

