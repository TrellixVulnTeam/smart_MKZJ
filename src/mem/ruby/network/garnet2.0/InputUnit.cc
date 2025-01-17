/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Niket Agarwal
 *          Tushar Krishna
 */


#include "mem/ruby/network/garnet2.0/InputUnit.hh"

#include "base/stl_helpers.hh"
#include "debug/FlitOrder.hh"
#include "debug/RubyNetwork.hh"
#include "debug/SMART.hh"
#include "debug/VC.hh"
#include "mem/ruby/network/garnet2.0/Credit.hh"
#include "mem/ruby/network/garnet2.0/Router.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

InputUnit::InputUnit(int id, PortDirection direction, Router *router)
            : Consumer(router)
{
    m_id = id;
    m_direction = direction;
    m_router = router;
    m_num_vcs = m_router->get_num_vcs();
    m_vc_per_vnet = m_router->get_vc_per_vnet();

    m_num_buffer_reads.resize(m_num_vcs/m_vc_per_vnet);
    m_num_buffer_writes.resize(m_num_vcs/m_vc_per_vnet);
    for (int i = 0; i < m_num_buffer_reads.size(); i++) {
        m_num_buffer_reads[i] = 0;
        m_num_buffer_writes[i] = 0;
    }

    creditQueue = new flitBuffer();
    // Instantiating the virtual channels
    m_vcs.resize(m_num_vcs);
    for (int i=0; i < m_num_vcs; i++) {
        m_vcs[i] = new VirtualChannel(i);
    }
    currentPacket = -1;
    lastflit = NULL;
}

InputUnit::~InputUnit()
{
    delete creditQueue;
    deletePointers(m_vcs);
}

/*
 * The InputUnit wakeup function reads the input flit from its input link.
 * Each flit arrives with an input VC.
 * For HEAD/HEAD_TAIL flits, performs route computation,
 * and updates route in the input VC.
 * The flit is buffered for (m_latency - 1) cycles in the input VC
 * and marked as valid for SwitchAllocation starting that cycle.
 *
 */

void
InputUnit::wakeup()
{
    // dangling pointer
    flit *t_flit = new flit();
    DPRINTF(VC, "[IU] Calling wakeup %#x\n", this);
    if (m_in_link->isReady(m_router->curCycle())) {

        t_flit = m_in_link->peekLink();
        if ( t_flit->get_pid() == 4) DPRINTF(VC, "[IU] we peek %s,"
                " current packet %d\n", *t_flit, currentPacket);

 //       t_flit = m_in_link->consumeLink();

        int vc = t_flit->get_vc();

        // Moved to crossbar
        // Counting hops as network hops
        // Not counting hops from/to NI
        //t_flit->increment_hops(); // for stats

    DPRINTF(RubyNetwork, "Router %d Inport %s received flit %s at link %d\n",
        m_router->get_id(), m_direction, *t_flit, m_in_link->get_id());

        DPRINTF(FlitOrder, "[IU] flit %d-%d entering port %d at router %d\n",
                t_flit->get_pid(), t_flit->get_id(),
                this->get_id(),
                m_router->get_id());

        if ((t_flit->get_type() == HEAD_) ||
            (t_flit->get_type() == HEAD_TAIL_)) {

            assert(m_vcs[vc]->get_state() == IDLE_);
            if (currentPacket != -1){
                if ( t_flit->get_pid() == 4)
                    DPRINTF(VC, "Return w/o consuming %s\n", *t_flit);
                return;
            }
            set_vc_active(vc, m_router->curCycle());

            // Route computation for this vc
            int outport = m_router->route_compute(t_flit->get_route(),
                m_id, m_direction);

            // Update output port in VC
            // All flits in this packet will use this output port
            // The output port field in the flit is updated after it wins SA
            grant_outport(vc, outport);

            if (t_flit->get_type() == HEAD_){
                currentPacket = t_flit->get_pid();
                lastflit = t_flit;
                DPRINTF(FlitOrder, "[IU] flit %d-%d\n",
                        t_flit->get_pid(), t_flit->get_id());
            } else{
                currentPacket = -1;
            }

           m_in_link->consumeLink();

        } else {
            DPRINTF(SMART, "[InputUnit] Body/Tail flit %s\n", *t_flit);

//            DPRINTF(FlitOrder, "[InputUnit] currentPacket %d flit PID %d\n",
//                    currentPacket, t_flit->get_pid());

            // This is not a HEAD flit
            // lastflit must not be null
            if (lastflit == NULL){
                DPRINTF(FlitOrder, "IU flit %d-%d caused fault at Router %d\n",
                        t_flit->get_pid(),
                        t_flit->get_id(),
                        m_router->get_id());
                assert(0);
            }

            if ( ! ((t_flit->get_id() == lastflit->get_id()) ||
                    (t_flit->get_id() == lastflit->get_id() + 1))){
                DPRINTF(FlitOrder, "flit %d-%d lastflit %d-%d stalled\n",
                        t_flit->get_pid(), t_flit->get_id(),
                        lastflit->get_pid(), lastflit->get_id());
                DPRINTF(VC, "returing w/o consuming lastflit %s\n", *lastflit);
                return;
            }
            lastflit = t_flit;
            assert(currentPacket == t_flit->get_pid());
            if (t_flit->get_type() == TAIL_){
                currentPacket = -1;
            }
            m_in_link->consumeLink();
            int state = m_vcs[vc]->get_state();
            switch(state){
                case 0: DPRINTF(SMART, "[InputUnit] IDLE\n"); break;
                case 1: DPRINTF(SMART, "[InputUnit] VC_AB\n");
                        break;
                case 2: DPRINTF(SMART, "[InputUnit] ACTIVE\n");
                        break;
                default: DPRINTF(SMART, "[InputUnit] UNKNOWN\n");
                        break;
            }
            // IDLE_, VC_AB_, ACTIVE_, NUM_VC_STATE_TYPE_
            // m_vcs[vc]->set_state(ACTIVE_, m_router->curCycle());
            // set_vc_active(vc, m_router->curCycle());
            assert(m_vcs[vc]->get_state() == ACTIVE_);
        }


        // Buffer the flit
        m_vcs[vc]->insertFlit(t_flit);

        int vnet = vc/m_vc_per_vnet;
        // number of writes same as reads
        // any flit that is written will be read only once
        m_num_buffer_writes[vnet]++;
        m_num_buffer_reads[vnet]++;

        Cycles pipe_stages = m_router->get_pipe_stages();
        if (pipe_stages == 1) {
            // 1-cycle router
            // Flit goes for SA directly
            t_flit->advance_stage(SA_, m_router->curCycle());
            DPRINTF(SMART, "[InputUnit] One Cycle Router\n");
        } else {
            assert(pipe_stages > 1);
            // Router delay is modeled by making flit wait in buffer for
            // (pipe_stages cycles - 1) cycles before going for SA

            Cycles wait_time = pipe_stages - Cycles(1);
            t_flit->advance_stage(SA_, m_router->curCycle() + wait_time);

            // Wakeup the router in that cycle to perform SA
            m_router->schedule_wakeup(Cycles(wait_time));
        }
     DPRINTF(SMART, "[InputUnit] Returned from wakeup\n");
     DPRINTF(FlitOrder, "[IU] flit %d-%d leaving port %d at router %d\n",
                t_flit->get_pid(), t_flit->get_id(),
                this->get_id(),
                m_router->get_id());

    } else {
        DPRINTF(VC, "[IU] Not ready to wakeup %#x\n", this);
    }
}

// Send a credit back to upstream router for this VC.
// Called by SwitchAllocator when the flit in this VC wins the Switch.
void
InputUnit::increment_credit(int in_vc, bool free_signal,
        Cycles curTime, flit * t_flit)
{
    Credit *t_credit = new Credit(in_vc,
            free_signal, curTime + Cycles(1), t_flit->get_pid(),
            t_flit->get_id());
    creditQueue->insert(t_credit);
    m_credit_link->scheduleEventAbsolute(m_router->clockEdge(Cycles(1)));
}


uint32_t
InputUnit::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;
    for (int i=0; i < m_num_vcs; i++) {
        num_functional_writes += m_vcs[i]->functionalWrite(pkt);
    }

    return num_functional_writes;
}

void
InputUnit::resetStats()
{
    for (int j = 0; j < m_num_buffer_reads.size(); j++) {
        m_num_buffer_reads[j] = 0;
        m_num_buffer_writes[j] = 0;
    }
}

// SMART NoC
bool
InputUnit::try_smart_bypass(flit *t_flit)
{
    // Check if router is setup for SMART bypass this cycle
    DPRINTF(RubyNetwork, "[InputUnit] Router %d Inport %s"
            " trying to bypass flit %s\n",
                 m_router->get_id(), m_direction, *t_flit);
   DPRINTF(FlitOrder, "IU flit %d-%d arrived at IU::try_smart_bypass"
           " at Router %d\n",
           t_flit->get_pid(),
           t_flit->get_id(),
           m_router->get_id());
    // Check SSR Grant for this cycle
    if (ssr_grant.empty()){
        DPRINTF(FlitOrder,"IU SSR grant is empty for flit %d-%d\n",
                t_flit->get_pid(), t_flit->get_id());
    }
    while (!ssr_grant.empty()) {
        SSR *t_ssr = ssr_grant.top();
        /*if (t_ssr->get_time() < m_router->curCycle()) {
            ssr_grant.pop();
            delete t_ssr;
        } else */
        if (t_ssr->get_time() <= m_router->curCycle()) {
            if (t_ssr->get_ref_flit() != t_flit) {
                // (i) this flit lost arbitration to a local flit, or
                // (ii) wanted to stop, and hence its SSR was not sent to 
                //      this router, and some other SSR won.

                DPRINTF(FlitOrder, "IU flit %d-%d lost"
                        " arbitration at Router %d\n",
                        t_flit->get_pid(),
                        t_flit->get_id(),
                        m_router->get_id());

                return false;
            } else {

                DPRINTF(FlitOrder, "IU flit %d-%d trying try_smart_bypass \n",
                        t_flit->get_pid(), t_flit->get_id());

                // SSR for this flit won arbitration last cycle
                // and wants to bypass this router
                assert(t_ssr->get_bypass_req());
                bool smart_bypass =
                    m_router->try_smart_bypass(m_id,
                                               t_ssr->get_outport_dirn(),
                                               t_flit);

                ssr_grant.pop();
                delete t_ssr;
                return smart_bypass;
            }
        } else {
            break;
        }
    }
    DPRINTF(FlitOrder, "IU flit %d-%d failed at try_smart_bypass"
            " at Router %d\n",
            t_flit->get_pid(),
            t_flit->get_id(),
            m_router->get_id());

    return false;
}

void    
InputUnit::grantSSR(SSR *t_ssr)
{           
    DPRINTF(RubyNetwork, "Router %d Inport %s granted SSR"
            "for flit %d from src_hops %d for bypass = %d for Outport %s\n",
            m_router->get_id(), m_direction, *(t_ssr->get_ref_flit()),
            t_ssr->get_src_hops(),
            t_ssr->get_bypass_req(), t_ssr->get_outport_dirn());


    // Update valid time to next cycle
    t_ssr->set_time(m_router->curCycle() + Cycles(1));        
    ssr_grant.push(t_ssr);
}

