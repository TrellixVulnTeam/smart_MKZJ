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


#include "mem/ruby/network/garnet2.0/OutputUnit.hh"

#include "base/stl_helpers.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet2.0/Credit.hh"
#include "mem/ruby/network/garnet2.0/Router.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

OutputUnit::OutputUnit(int id, PortDirection direction, Router *router)
    : Consumer(router)
{
    m_id = id;
    m_direction = direction;
    m_router = router;
    m_num_vcs = m_router->get_num_vcs();
    m_vc_per_vnet = m_router->get_vc_per_vnet();
    m_out_buffer = new flitBuffer();

    for (int i = 0; i < m_num_vcs; i++) {
        m_outvc_state.push_back(new OutVcState(i, m_router->get_net_ptr()));
    }
}

OutputUnit::~OutputUnit()
{
    delete m_out_buffer;
    deletePointers(m_outvc_state);
}

void
OutputUnit::decrement_credit(int out_vc)
{
    DPRINTF(RubyNetwork, "Router %d OutputUnit %d decrementing credit for "
            "outvc %d at time: %lld\n",
            m_router->get_id(), m_id, out_vc, m_router->curCycle());

    m_outvc_state[out_vc]->decrement_credit();
    DPRINTF(RubyNetwork, "decrement completed\n");
}

void
OutputUnit::increment_credit(int out_vc)
{
    DPRINTF(RubyNetwork, "Router %d OutputUnit %d incrementing credit for "
            "outvc %d at time: %lld\n",
            m_router->get_id(), m_id, out_vc, m_router->curCycle());

    m_outvc_state[out_vc]->increment_credit();
}

// Check if the output VC (i.e., input VC at next router)
// has free credits (i..e, buffer slots).
// This is tracked by OutVcState
bool
OutputUnit::has_credit(int out_vc)
{
    assert(m_outvc_state[out_vc]->isInState(ACTIVE_, m_router->curCycle()));
    return m_outvc_state[out_vc]->has_credit();
}


// Check if the output port (i.e., input port at next router) has free VCs.
bool
OutputUnit::has_free_vc(int vnet)
{
    int vc_base = vnet*m_vc_per_vnet;
    for (int vc = vc_base; vc < vc_base + m_vc_per_vnet; vc++) {
        if (is_vc_idle(vc, m_router->curCycle()))
            return true;
    }

    return false;
}

// Assign a free output VC to the winner of Switch Allocation
int
OutputUnit::select_free_vc(int vnet)
{
    int vc_base = vnet*m_vc_per_vnet;
    for (int vc = vc_base; vc < vc_base + m_vc_per_vnet; vc++) {
        if (is_vc_idle(vc, m_router->curCycle())) {
            m_outvc_state[vc]->setState(ACTIVE_, m_router->curCycle());
            return vc;
        }
    }

    return -1;
}

/*
 * The wakeup function of the OutputUnit reads the credit signal from the
 * downstream router for the output VC (i.e., input VC at downstream router).
 * It increments the credit count in the appropriate output VC state.
 * If the credit carries is_free_signal as true,
 * the output VC is marked IDLE.
 */

void
OutputUnit::wakeup()
{
    // SMART can send 2 credits in the same cycle
    // One from SA-L.
    // One during bypass.
    // Hence while loop
    int num_credits = 0;
    while (m_credit_link->isReady(m_router->curCycle())) {
        Credit *t_credit = (Credit*) m_credit_link->consumeLink();
        increment_credit(t_credit->get_vc());

        if (t_credit->is_free_signal())
            set_vc_state(IDLE_, t_credit->get_vc(), m_router->curCycle());

        delete t_credit;

        num_credits++;
    }

    if (num_credits > 1) {
//        assert(m_router->get_net_ptr()->isSMART());
        assert(num_credits == 2);
    }
}

flitBuffer*
OutputUnit::getOutQueue()
{
    return m_out_buffer;
}

void
OutputUnit::set_out_link(NetworkLink *link)
{
    m_out_link = link;
}

void
OutputUnit::set_credit_link(CreditLink *credit_link)
{
    m_credit_link = credit_link;
}

// SMART NoC
void
OutputUnit::insertSSR(SSR *t_ssr)
{
    ssr_reqs.push(t_ssr);

    // Wake up Router next cycle for SA-G
    m_router->schedule_wakeup(Cycles(1));
}

// Is there a valid SSR for this cycle
bool
OutputUnit::isReadySSR()
{
    // delete invalid SSRs
    while (!ssr_reqs.empty()) {
        SSR *t_ssr = ssr_reqs.top();
        if (t_ssr->get_time() < m_router->curCycle()) {
            ssr_reqs.pop();
            delete t_ssr;
        } else if (t_ssr->get_time() == m_router->curCycle())
            return true;
        else
            break;
    }

    return false;
}

// Get highest priority SSR
SSR*
OutputUnit::getTopSSR()
{
    SSR *t_ssr = ssr_reqs.top();
    ssr_reqs.pop();

    return t_ssr;
}

// SSR has been granted
// clear SSR request queue for this cycle
void
OutputUnit::clearSSRreqs()
{
    while (!ssr_reqs.empty()) {
        SSR *t_ssr = ssr_reqs.top();
        if (t_ssr->get_time() <= m_router->curCycle()) {
            ssr_reqs.pop();
            delete t_ssr;
        } else {
            break;
        }
    }
}

// Multi-hop bypass
void
OutputUnit::smart_bypass(flit *t_flit)
{
    m_out_buffer->insert(t_flit);
    m_out_link->wakeup(); // wakeup this cycle
}

uint32_t
OutputUnit::functionalWrite(Packet *pkt)
{
    return m_out_buffer->functionalWrite(pkt);
}
