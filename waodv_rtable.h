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

The AODV code developed by the CMU/MONARCH group was optimized and tuned by Samir Das and Mahesh Marina, University of Cincinnati. The work was partially done in Sun Microsystems.
*/


#ifndef __waodv_rtable_h__
#define __waodv_rtable_h__

#include <assert.h>
#include <sys/types.h>
#include <config.h>
#include <lib/bsd-list.h>
#include <scheduler.h>
#include <waodv/waodv_trust.h>

#define CURRENT_TIME    Scheduler::instance().clock()
#define INFINITY2        0xff


//信任信息表和qos信息表结构

struct trust_info{
	float trust;
	float d_trust;
	float in_trust;
	int current_time;
	int lastupdate_time;
};

struct qos_info{
	float ext;
	float trans_delay;
	float board_delay;
	int current_time;
	int lastupdate_time;
};

/*
   WAODV Neighbor Cache Entry
*/
class WAODV_Neighbor {
        friend class WAODV;
        friend class waodv_rt_entry;
 public:
        WAODV_Neighbor(u_int32_t a) {
        nb_addr = a;
        trust_info.trust=1;
        trust_info.d_trust=0.5;
        trust_info.in_trust=0.5;
        ext =0;
        }
        nsaddr_t getnbaddr();
        struct trust_info trust_info;
        struct qos_info qos_info;
        float count;
        float time;
        float ext;

 protected:
        LIST_ENTRY(WAODV_Neighbor) nb_link;
        nsaddr_t        nb_addr;
        double          nb_expire;      // ALLOWED_HELLO_LOSS * HELLO_INTERVAL


};

LIST_HEAD(waodv_ncache, WAODV_Neighbor);

/*
   WAODV Precursor list data structure
*/
class WAODV_Precursor {
        friend class WAODV;
        friend class waodv_rt_entry;
 public:
        WAODV_Precursor(u_int32_t a) { pc_addr = a; }

 protected:
        LIST_ENTRY(WAODV_Precursor) pc_link;
        nsaddr_t        pc_addr;	// precursor address
};

LIST_HEAD(waodv_precursors, WAODV_Precursor);


/*
  Route Table Entry
*/

class waodv_rt_entry {
        friend class waodv_rtable;
        friend class WAODV;
	friend class LocalRepairTimerw;
 public:
        waodv_rt_entry();
        ~waodv_rt_entry();

        void            nb_insert(nsaddr_t id);
        WAODV_Neighbor*  nb_lookup(nsaddr_t id);

        void            pc_insert(nsaddr_t id);
        WAODV_Precursor* pc_lookup(nsaddr_t id);
        void 		pc_delete(nsaddr_t id);
        void 		pc_delete(void);
        bool 		pc_empty(void);

        double          rt_req_timeout;         // when I can send another req
        u_int8_t        rt_req_cnt;             // number of route requests
	
 protected:
        LIST_ENTRY(waodv_rt_entry) rt_link;

        nsaddr_t        rt_dst;
        u_int32_t       rt_seqno;
	/* u_int8_t 	rt_interface; */
        u_int16_t       rt_hops;       		// hop count
	int 		rt_last_hop_count;	// last valid hop count
        nsaddr_t        rt_nexthop;    		// next hop IP address
	/* list of precursors */ 
        waodv_precursors rt_pclist;
        double          rt_expire;     		// when entry expires
        u_int8_t        rt_flags;
        float			ct;

#define RTF_DOWN 0
#define RTF_UP 1
#define RTF_IN_REPAIR 2

        /*
         *  Must receive 4 errors within 3 seconds in order to mark
         *  the route down.
        u_int8_t        rt_errors;      // error count
        double          rt_error_time;
#define MAX_RT_ERROR            4       // errors
#define MAX_RT_ERROR_TIME       3       // seconds
         */

#define MAX_HISTORY	3
	double 		rt_disc_latency[MAX_HISTORY];
	char 		hist_indx;
        int 		rt_req_last_ttl;        // last ttl value used
	// last few route discovery latencies
	// double 		rt_length [MAX_HISTORY];
	// last few route lengths

        /*
         * a list of neighbors that are using this route.
         */
        waodv_ncache          rt_nblist;
};


/*
  The Routing Table
*/

class waodv_rtable {
 public:
	waodv_rtable() { LIST_INIT(&rthead); }

        waodv_rt_entry*       head() { return rthead.lh_first; }

        waodv_rt_entry*       rt_add(nsaddr_t id);
        void                 rt_delete(nsaddr_t id);
        waodv_rt_entry*       rt_lookup(nsaddr_t id);

 private:
        LIST_HEAD(waodv_rthead, waodv_rt_entry) rthead;
};

#endif /* _waodv__rtable_h__ */
