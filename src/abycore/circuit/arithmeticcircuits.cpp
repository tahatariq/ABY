/**
 \file 		arithmeticcircuits.cpp
 \author	michael.zohner@ec-spride.de
 \copyright	________________
 \brief		Arithmetic Circuit class.
 */

#include "arithmeticcircuits.h"

void ArithmeticCircuit::Init() {
	m_nMULs = 0;
	m_nCONVGates = 0;

	if (m_eContext == S_ARITH) {
		m_nRoundsAND = 1;
		m_nRoundsXOR = 1;
		m_nRoundsIN.resize(2, 1);
		m_nRoundsOUT.resize(3, 1);
	} else { //m_tContext == S_YAO
		//unknown
		cerr << "Sharing type not implemented with arithmetic circuits" << endl;
		exit(0);
	}
}

void ArithmeticCircuit::Cleanup() {
	//TODO implement
}

share* ArithmeticCircuit::PutMULGate(share* ina, share* inb, uint32_t mindepth) {
	share* shr = new arithshare(this);
	shr->set_gate(0, PutMULGate(ina->get_gate(0), inb->get_gate(0), mindepth));
	return shr;
}

uint32_t ArithmeticCircuit::PutMULGate(uint32_t inleft, uint32_t inright, uint32_t mindepth) {
	uint32_t gateid = m_cCircuit->PutPrimitiveGate(G_NON_LIN, inleft, inright, m_nRoundsAND, mindepth);
	UpdateInteractiveQueue(gateid);

	if (m_pGates[gateid].nvals != INT_MAX) {
		//TODO implement for NON_LIN_VEC
		m_nMULs += m_pGates[gateid].nvals;
	}
	return gateid;
}

uint32_t ArithmeticCircuit::PutADDGate(uint32_t inleft, uint32_t inright, uint32_t mindepth) {
	uint32_t gateid = m_cCircuit->PutPrimitiveGate(G_LIN, inleft, inright, m_nRoundsXOR, mindepth);
	UpdateLocalQueue(gateid);
	return gateid;
}

share* ArithmeticCircuit::PutADDGate(share* ina, share* inb, uint32_t mindepth) {
	share* shr = new arithshare(this);
	shr->set_gate(0, PutADDGate(ina->get_gate(0), inb->get_gate(0), mindepth));
	return shr;
}

uint32_t ArithmeticCircuit::PutINGate(uint32_t ninvals, e_role src) {
	uint32_t gateid = m_cCircuit->PutINGate(m_eContext, ninvals, m_nShareBitLen, src, m_nRoundsIN[src]);
	UpdateInteractiveQueue(gateid);
	switch (src) {
	case SERVER:
		m_vInputGates[0].push_back(gateid);
		m_vInputBits[0] += (m_pGates[gateid].nvals * m_nShareBitLen);
		break;
	case CLIENT:
		m_vInputGates[1].push_back(gateid);
		m_vInputBits[1] += (m_pGates[gateid].nvals * m_nShareBitLen);
		break;
	case ALL:
		m_vInputGates[0].push_back(gateid);
		m_vInputGates[1].push_back(gateid);
		m_vInputBits[0] += (m_pGates[gateid].nvals * m_nShareBitLen);
		m_vInputBits[1] += (m_pGates[gateid].nvals * m_nShareBitLen);
		break;
	default:
		cerr << "Role not recognized" << endl;
		break;
	}

	return gateid;
}

template<class T> uint32_t ArithmeticCircuit::PutINGate(uint32_t nvals, T val) {
	uint32_t gateid = PutINGate(nvals, m_eMyRole);
	GATE* gate = m_pGates + gateid;
	gate->gs.ishare.inval = (UGATE_T*) calloc(ceil_divide(nvals * m_nShareBitLen, sizeof(UGATE_T) * 8), sizeof(UGATE_T));

	*gate->gs.ishare.inval = (UGATE_T) val;
	gate->instantiated = true;
	return 1;
}

template<class T> uint32_t ArithmeticCircuit::PutINGate(uint32_t nvals, T val, e_role role) {
	uint32_t gateid = PutINGate(nvals, role);
	if (role == m_eMyRole) {
		GATE* gate = m_pGates + gateid;
		gate->gs.ishare.inval = (UGATE_T*) calloc(ceil_divide(nvals * m_nShareBitLen, sizeof(UGATE_T) * 8), sizeof(UGATE_T));

		*gate->gs.ishare.inval = (UGATE_T) val;
		gate->instantiated = true;
	}

	return gateid;
}

share* ArithmeticCircuit::PutINGate(uint32_t nvals, uint32_t val, uint32_t bitlen, e_role role) {
	share* shr = new arithshare(this);
	shr->set_gate(0, PutINGate(nvals, val, role));
	return shr;
}

//TODO: hack around: works only if val consists of a single input value
share* ArithmeticCircuit::PutINGate(uint32_t nvals, uint8_t* val, uint32_t bitlen, e_role role) {
	assert(bitlen <= m_nShareBitLen);
	return PutINGate(nvals, *((uint32_t*) val), bitlen, role);
}

share* ArithmeticCircuit::PutINGate(uint32_t nvals, uint32_t* val, uint32_t bitlen, e_role role) {
//TODO check for type-size issues!
	assert(bitlen <= m_nShareBitLen);
	share* shr = new arithshare(this);
	uint32_t gateid = PutINGate(nvals, role);
	uint32_t iters = sizeof(UGATE_T) / sizeof(uint32_t);
	assert(iters > 0);
	shr->set_gate(0, gateid);

	if (role == m_eMyRole) {
		GATE* gate = m_pGates + gateid;
		gate->gs.ishare.inval = (UGATE_T*) calloc(nvals, sizeof(UGATE_T));
		memcpy(gate->gs.ishare.inval, val, nvals * sizeof(uint32_t));

		gate->instantiated = true;
	}

	return shr;

}

uint32_t ArithmeticCircuit::PutOUTGate(uint32_t parentid, e_role dst) {
	uint32_t gateid = m_cCircuit->PutOUTGate(parentid, dst, m_nRoundsOUT[dst]);
	UpdateInteractiveQueue(gateid);

	switch (dst) {
	case SERVER:
		m_vOutputGates[0].push_back(gateid);
		m_vOutputBits[0] += (m_pGates[gateid].nvals * m_nShareBitLen);
		break;
	case CLIENT:
		m_vOutputGates[1].push_back(gateid);
		m_vOutputBits[1] += (m_pGates[gateid].nvals * m_nShareBitLen);
		break;
	case ALL:
		m_vOutputGates[0].push_back(gateid);
		m_vOutputGates[1].push_back(gateid);
		m_vOutputBits[0] += (m_pGates[gateid].nvals * m_nShareBitLen);
		m_vOutputBits[1] += (m_pGates[gateid].nvals * m_nShareBitLen);
		break;
	default:
		cerr << "Role not recognized" << endl;
		break;
	}

	return gateid;
}

share* ArithmeticCircuit::PutOUTGate(share* parent, e_role dst) {
	share* shr = new arithshare(this);
	for (uint32_t i = 0; i < parent->size(); i++)
		shr->set_gate(i, PutOUTGate(parent->get_gate(i), dst));

	return shr;
}

uint32_t ArithmeticCircuit::PutINVGate(uint32_t parentid, uint32_t mindepth) {
	uint32_t gateid = m_cCircuit->PutINVGate(parentid, mindepth);
	UpdateLocalQueue(gateid);
	return gateid;
}

uint32_t ArithmeticCircuit::PutCONVGate(vector<uint32_t>& parentids, uint32_t mindepth) {
	uint32_t gateid = m_cCircuit->PutCONVGate(parentids, 2, S_ARITH, m_nShareBitLen, mindepth);
	UpdateInteractiveQueue(gateid);
	m_nCONVGates += m_pGates[gateid].nvals;
	return gateid;
}

uint32_t ArithmeticCircuit::PutConstantGate(UGATE_T val, uint32_t nvals, uint32_t mindepth) {
	uint32_t gateid = m_cCircuit->PutConstantGate(m_eContext, val, nvals, m_nShareBitLen, mindepth);
	UpdateLocalQueue(gateid);
	return gateid;
}

uint32_t ArithmeticCircuit::PutB2AGate(vector<uint32_t> ina, uint32_t mindepth) {
	return PutCONVGate(ina, mindepth);;
}

share* ArithmeticCircuit::PutB2AGate(share* ina, uint32_t mindepth) {
	share* shr = new arithshare(this);
	shr->set_gate(0, PutCONVGate(ina->get_gates(), mindepth));
	return shr;
}

//enqueue interactive gate queue
void ArithmeticCircuit::UpdateInteractiveQueue(uint32_t gateid) {
	if (m_pGates[gateid].depth + 1 > m_vInteractiveQueueOnLvl.size()) {
		m_vInteractiveQueueOnLvl.resize(m_pGates[gateid].depth + 1);
		if (m_pGates[gateid].depth + 1 > m_nMaxDepth) {
			m_nMaxDepth = m_pGates[gateid].depth + 1;
		}
	}
	m_vInteractiveQueueOnLvl[m_pGates[gateid].depth].push_back(gateid);
}

//enqueue locally evaluated gate queue
void ArithmeticCircuit::UpdateLocalQueue(uint32_t gateid) {
	if (m_pGates[gateid].depth + 1 > m_vLocalQueueOnLvl.size()) {
		m_vLocalQueueOnLvl.resize(m_pGates[gateid].depth + 1);
		if (m_pGates[gateid].depth + 1 > m_nMaxDepth) {
			m_nMaxDepth = m_pGates[gateid].depth + 1;
		}
	}
	m_vLocalQueueOnLvl[m_pGates[gateid].depth].push_back(gateid);
}

void ArithmeticCircuit::Reset() {
	Circuit::Reset();
	m_nMULs = 0;
	m_nCONVGates = 0;
	m_nMaxDepth = 0;

	for (uint32_t i = 0; i < m_vLocalQueueOnLvl.size(); i++) {
		m_vLocalQueueOnLvl[i].clear();
	}
	m_vLocalQueueOnLvl.resize(0);
	for (uint32_t i = 0; i < m_vInteractiveQueueOnLvl.size(); i++) {
		m_vInteractiveQueueOnLvl[i].clear();
	}
	m_vInteractiveQueueOnLvl.resize(0);
	for (uint32_t i = 0; i < m_vInputGates.size(); i++) {
		m_vInputGates[i].clear();
	}
	for (uint32_t i = 0; i < m_vOutputGates.size(); i++) {
		m_vOutputGates[i].clear();
	}
	for (uint32_t i = 0; i < m_vInputBits.size(); i++)
		m_vInputBits[i] = 0;
	for (uint32_t i = 0; i < m_vOutputBits.size(); i++)
		m_vOutputBits[i] = 0;
}
