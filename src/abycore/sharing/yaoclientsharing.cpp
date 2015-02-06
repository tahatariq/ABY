/**
 \file 		yaoclientsharing.cpp
 \author	michael.zohner@ec-spride.de
 \copyright	________________
 \brief		Yao Client Sharing class implementation.
 */
#include "yaoclientsharing.h"

void YaoClientSharing::InitClient() {

	m_nChoiceBitCtr = 0;
	m_vROTCtr = 0;

	m_nClientSndOTCtr = 0;
	m_nClientRcvKeyCtr = 0;
	m_nServerInBitCtr = 0;
	m_nClientOutputShareCtr = 0;
	m_nServerOutputShareCtr = 0;
	m_nClientOUTBitCtr = 0;

	m_nKeyInputRcvIdx = 0;

	m_vClientKeyRcvBuf.resize(2);

	fMaskFct = new XORMasking(m_cCrypto->get_seclvl().symbits);

	//TODO free memory after use
	m_vTmpEncBuf = (uint8_t**) malloc(sizeof(uint8_t*) * KEYS_PER_GATE_IN_TABLE);
	for(uint32_t i = 0; i < KEYS_PER_GATE_IN_TABLE; i++)
		m_vTmpEncBuf[i] = (uint8_t*) malloc(sizeof(uint8_t) * AES_BYTES);

}

//Pre-set values for new layer
void YaoClientSharing::InitNewLayer() {
	m_nServerInBitCtr = 0;
	m_vServerInputGates.clear();

	m_nServerOutputShareCtr = 0;

}

/* Send a new task for pre-computing the OTs in the setup phase */
void YaoClientSharing::PrepareSetupPhase(ABYSetup* setup) {
	BYTE* buf;
	uint64_t gt_size;
	m_nANDGates = m_cBoolCircuit->GetNumANDGates();

	gt_size = ((uint64_t) m_nANDGates) * KEYS_PER_GATE_IN_TABLE * m_nSecParamBytes;

	if (m_cBoolCircuit->GetMaxDepth() == 0)
		return;

	//TODO figure out which parts of the init can be moved to prepareonlinephase
	/* Preset the number of input bits for client and server */
	m_nServerInputBits = m_cBoolCircuit->GetNumInputBitsForParty(SERVER);
	m_nClientInputBits = m_cBoolCircuit->GetNumInputBitsForParty(CLIENT);
	m_nConversionInputBits = m_cBoolCircuit->GetNumB2YGates() + m_cBoolCircuit->GetNumA2YGates();

	m_vGarbledCircuit.Create(0); //m_nANDGates * KEYS_PER_GATE_IN_TABLE * m_sSecLvl.symbits);
	buf = (BYTE*) malloc(gt_size);
	m_vGarbledCircuit.AttachBuf(buf, gt_size);

	m_vOutputShareRcvBuf.Create((uint32_t) m_cBoolCircuit->GetNumOutputBitsForParty(CLIENT));
	m_vOutputShareSndBuf.Create((uint32_t) m_cBoolCircuit->GetNumOutputBitsForParty(SERVER));
	m_vROTSndBuf.Create((uint32_t) m_cBoolCircuit->GetNumInputBitsForParty(CLIENT) + m_nConversionInputBits);

	m_vROTMasks.Create((m_nClientInputBits + m_nConversionInputBits) * m_cCrypto->get_seclvl().symbits); //TODO: do a bit more R-OTs to get the offset right

	m_vChoiceBits.Create(m_nClientInputBits + m_nConversionInputBits, m_cCrypto);

#ifdef DEBUGYAOCLIENT
	cout << "OT Choice bits: " << endl;
	m_vChoiceBits.Print(0, m_nClientInputBits + m_nConversionInputBits);
#endif
	/* Use the standard XORMasking function */

	/* Define the new OT tasks that will be done when the setup phase is performed*/
	OTTask* task = (OTTask*) malloc(sizeof(OTTask));
	task->bitlen = m_cCrypto->get_seclvl().symbits;
	task->ottype = R_OT;
	task->numOTs = m_nClientInputBits + m_nConversionInputBits;
	task->mskfct = fMaskFct;
	task->pval.rcvval.C = &(m_vChoiceBits);
	task->pval.rcvval.R = &(m_vROTMasks);

	setup->AddOTTask(task, 0);
}

/* If played as server send the garbled table, if played as client receive the garbled table */
void YaoClientSharing::PerformSetupPhase(ABYSetup* setup) {
	if (m_cBoolCircuit->GetMaxDepth() == 0)
		return;
	ReceiveGarbledCircuitAndOutputShares(setup);
}

void YaoClientSharing::PrepareOnlinePhase() {
	InitNewLayer();
}

void YaoClientSharing::ReceiveGarbledCircuitAndOutputShares(ABYSetup* setup) {
	if (m_nANDGates > 0)
		setup->AddReceiveTask(m_vGarbledCircuit.GetArr(), ((uint64_t) m_nANDGates) * m_nSecParamBytes * KEYS_PER_GATE_IN_TABLE);
	if (m_cBoolCircuit->GetNumOutputBitsForParty(CLIENT) > 0)
		setup->AddReceiveTask(m_vOutputShareRcvBuf.GetArr(), ceil_divide(m_cBoolCircuit->GetNumOutputBitsForParty(CLIENT), 8));
	setup->WaitForTransmissionEnd();
#ifdef DEBUGYAOCLIENT
	cout << "Received Garbled Circuit: ";
	m_vGarbledCircuit.PrintHex();
	cout << "Received my output shares: ";
	m_vOutputShareRcvBuf.Print(0, m_cBoolCircuit->GetNumOutputBitsForParty(CLIENT));
#endif
}

void YaoClientSharing::FinishSetupPhase(ABYSetup* setup) {
	//Do nothing
#ifdef DEBUGYAOCLIENT
	if(m_cBoolCircuit->GetMaxDepth() == 0)
	return;
	cout << "Choice bits in OT: ";
	m_vChoiceBits.Print(0, m_nClientInputBits);
	cout << "Resulting R from OT: ";
	m_vROTMasks.PrintHex();
#endif
}
void YaoClientSharing::EvaluateLocalOperations(uint32_t depth) {

	deque<uint32_t> localops = m_cBoolCircuit->GetLocalQueueOnLvl(depth);

	//cout << "In total I have " <<  localops.size() << " local operations to evaluate on this level " << endl;
	for (uint32_t i = 0; i < localops.size(); i++) {
		GATE* gate = m_pGates + localops[i];
		//cout << "Evaluating gate " << localops[i] << " with context = " << gate->context << endl;
		if (gate->type == G_LIN) {
			EvaluateXORGate(gate);
		} else if (gate->type == G_NON_LIN) {
			EvaluateANDGate(gate);
		} else if (gate->type == G_CONSTANT) {
			//ReceiveServerKeys(gate);
			UGATE_T constval = gate->gs.constval;
			InstantiateGate(gate);
			BYTE res = (BYTE) (constval * 0xFF);
			memset(gate->gs.yval, res, m_nSecParamBytes * gate->nvals);
		} else if (IsSIMDGate(gate->type)) {
			//cout << "Evaluating SIMD gate" << endl;
			EvaluateSIMDGate(localops[i]);
		} else if (gate->type == G_INV) {
			//only copy values, SERVER did the inversion
			uint32_t parentid = gate->ingates.inputs.parent; // gate->gs.invinput;
			InstantiateGate(gate);
			memcpy(gate->gs.yval, m_pGates[parentid].gs.yval, m_nSecParamBytes * gate->nvals);
			UsedGate(parentid);
		} else {
			cerr << "Operation not recognized: " << (uint32_t) gate->type << "(" << GetOpName(gate->type) << ")" << endl;
		}
	}
}

void YaoClientSharing::EvaluateInteractiveOperations(uint32_t depth) {
	deque<uint32_t> interactiveops = m_cBoolCircuit->GetInteractiveQueueOnLvl(depth);

	//cout << "In total I have " <<  localops.size() << " local operations to evaluate on this level " << endl;
	for (uint32_t i = 0; i < interactiveops.size(); i++) {
		GATE* gate = m_pGates + interactiveops[i];

		if (gate->type == G_IN) {
			if (gate->gs.ishare.src == SERVER) {
				ReceiveServerKeys(interactiveops[i]);
				//Receive servers input shares;
			} else {
				ReceiveClientKeys(interactiveops[i]);
				//Receive servers input shares;
			}
		} else if (gate->type == G_OUT) {
#ifdef DEBUGYAOCLIENT
			cout << "Obtained output gate with key = ";
			PrintKey(m_pGates[gate->ingates.inputs.parent].gs.yval);
			cout << endl;
#endif
			if (gate->gs.oshare.dst == SERVER) {
				EvaluateServerOutputGate(gate);
			} else if (gate->gs.oshare.dst == ALL) {
				//cout << "Output gate for both of us, sending server output for gateid: " << interactiveops[i] << endl;
				EvaluateServerOutputGate(gate);
				//cout << "Setting my output gate" << endl;
				EvaluateClientOutputGate(interactiveops[i]);
				//cout << "finished setting my output" <<endl;
			} else {
				//ouput reconstruction
				EvaluateClientOutputGate(interactiveops[i]);
			}
		} else if (gate->type == G_CONV) {
			EvaluateConversionGate(interactiveops[i]);
		} else {
			cerr << "Operation not recognized: " << (uint32_t) gate->type << "(" << GetOpName(gate->type) << ")" << endl;
		}
	}
}

void YaoClientSharing::EvaluateXORGate(GATE* gate) {
	uint32_t nvals = gate->nvals;
	uint32_t idleft = gate->ingates.inputs.twin.left; //gate->gs.ginput.left;
	uint32_t idright = gate->ingates.inputs.twin.right; //gate->gs.ginput.right;

	InstantiateGate(gate);
	//TODO: optimize for UINT64_T pointers, there might be some problems here, code is untested
	/*for(uint32_t i = 0; i < m_nSecParamBytes * nvals; i++) {
	 gate->gs.yval[i] = m_pGates[idleft].gs.yval[i] ^ m_pGates[idright].gs.yval[i];
	 }*/
	//cout << "doing " << m_nSecParamIters << "iters on " << nvals << " vals " << endl;
	for (uint32_t i = 0; i < m_nSecParamIters * nvals; i++) {
		((UGATE_T*) gate->gs.yval)[i] = ((UGATE_T*) m_pGates[idleft].gs.yval)[i] ^ ((UGATE_T*) m_pGates[idright].gs.yval)[i];
	}
	//cout << "Keyval (" << 0 << ")= " << (gate->gs.yval[m_nSecParamBytes-1] & 0x01)  << endl;
	//cout << (gate->gs.yval[m_nSecParamBytes-1] & 0x01);
#ifdef DEBUGYAOCLIENT
	PrintKey(gate->gs.yval);
	cout << " = ";
	PrintKey(m_pGates[idleft].gs.yval);
	cout << " (" << idleft << ") ^ ";
	PrintKey(m_pGates[idright].gs.yval);
	cout << " (" << idright << ")" << endl;
#endif

	UsedGate(idleft);
	UsedGate(idright);
}

void YaoClientSharing::EvaluateANDGate(GATE* gate) {
	uint32_t idleft = gate->ingates.inputs.twin.left; //gate->gs.ginput.left;
	uint32_t idright = gate->ingates.inputs.twin.right; //gate->gs.ginput.right;
	GATE* gleft = m_pGates + idleft;
	GATE* gright = m_pGates + idright;

	//evaluate garbled table
	InstantiateGate(gate);
	for (uint32_t g = 0; g < gate->nvals; g++) {
		EvaluateGarbledTable(gate, g, gleft, gright);
		m_nGarbledTableCtr++;

		//Pipelined receive - TODO: outsource in own thread
		/*if(andctr >= GARBLED_TABLE_WINDOW) {
		 gtsize = min(remandgates, GARBLED_TABLE_WINDOW);
		 sock.Receive(m_vGarbledTables.GetArr(), gtsize * KEYS_PER_GATE_IN_TABLE * BYTES_SSP);
		 remandgates -= gtsize;
		 andctr=0;
		 }*/

	}
	UsedGate(idleft);
	UsedGate(idright);
}

BOOL YaoClientSharing::EvaluateGarbledTable(GATE* gate, uint32_t pos, GATE* gleft, GATE* gright)
{

	uint8_t *lkey, *rkey, *okey, *gtptr;
	uint8_t lpbit, rpbit;

	okey = gate->gs.yval + pos * m_nSecParamBytes;
	lkey = gleft->gs.yval + pos * m_nSecParamBytes;
	rkey = gright->gs.yval + pos * m_nSecParamBytes;
	gtptr = m_vGarbledCircuit.GetArr() + m_nSecParamBytes * KEYS_PER_GATE_IN_TABLE * m_nGarbledTableCtr;

	lpbit = lkey[m_nSecParamBytes-1] & 0x01;
	rpbit = rkey[m_nSecParamBytes-1] & 0x01;

	assert(lpbit < 2 && rpbit < 2);

	EncryptWire(m_vTmpEncBuf[0], lkey, KEYS_PER_GATE_IN_TABLE*m_nGarbledTableCtr);
	EncryptWire(m_vTmpEncBuf[1], rkey, KEYS_PER_GATE_IN_TABLE*m_nGarbledTableCtr+1);

	m_pKeyOps->XOR(okey, m_vTmpEncBuf[0], m_vTmpEncBuf[1]);//gc_xor(okey, encbuf[0], encbuf[1]);

	if(lpbit) {
		m_pKeyOps->XOR(okey, okey, gtptr);//gc_xor(okey, okey, gtptr);
	}
	if(rpbit) {
		m_pKeyOps->XOR(okey, okey, gtptr+m_nSecParamBytes);//gc_xor(okey, okey, gtptr+BYTES_SSP);
		m_pKeyOps->XOR(okey, okey, lkey);//gc_xor(okey, okey, gtptr+BYTES_SSP);
	}

#ifdef DEBUGYAOCLIENT
		cout << " using: ";
		PrintKey(lkey);
		cout << " (" << (uint32_t) lpbit << ") and : ";
		PrintKey(rkey);
		cout << " (" << (uint32_t) rpbit << ") to : ";
		PrintKey(okey);
		cout << " (" << (uint32_t) (okey[m_nSecParamBytes-1] & 0x01) << ")" << endl;
		cout << "A: ";
		PrintKey(m_vTmpEncBuf[0]);
		cout << "; B: ";
		PrintKey(m_vTmpEncBuf[1]);
		cout << endl;
		cout << "Table A: ";
		PrintKey(gtptr);
		cout << "; Table B: ";
		PrintKey(gtptr+m_nSecParamBytes);
		cout << endl;
#endif

	return true;
}

/* Evaluate the gate and use the servers output permutation bits to compute the output */
void YaoClientSharing::EvaluateClientOutputGate(uint32_t gateid) {
	GATE* gate = m_pGates + gateid;
	uint32_t parentid = gate->ingates.inputs.parent; //gate->gs.oshare.parentgate;
	uint32_t in;
	InstantiateGate(gate);

#ifdef DEBUGYAOCLIENT
	cout << "ClientOutput: ";
#endif
	for (uint32_t i = 0; i < gate->nvals; i++) {
		in = (m_pGates[parentid].gs.yval[(i + 1) * m_nSecParamBytes - 1] & 0x01);
		gate->gs.val[i / GATE_T_BITS] ^= ((((UGATE_T) m_pGates[parentid].gs.yval[(i + 1) * m_nSecParamBytes - 1] & 0x01)
				^ ((UGATE_T) m_vOutputShareRcvBuf.GetBit(m_nClientOUTBitCtr))) << (i % GATE_T_BITS));
#ifdef DEBUGYAOCLIENT
		cout << (uint32_t) gate->gs.val[i/GATE_T_BITS] << " = " << in << " ^ " << (uint32_t) m_vOutputShareRcvBuf.GetBit(m_nClientOUTBitCtr) << endl;
#endif
		m_nClientOUTBitCtr++;
	}

	UsedGate(parentid);
}

/* Copy the output shares for the server and send them later on */
void YaoClientSharing::EvaluateServerOutputGate(GATE* gate) {
	uint32_t parentid = gate->ingates.inputs.parent;	//gate->gs.oshare.parentgate;

	for (uint32_t i = 0; i < gate->nvals; i++, m_nServerOutputShareCtr++) {
		m_vOutputShareSndBuf.SetBit(m_nServerOutputShareCtr, m_pGates[parentid].gs.yval[((i + 1) * m_nSecParamBytes) - 1] & 0x01);
#ifdef DEBUGYAOCLIENT
		cout << "Setting ServerOutputShare to " << ((uint32_t) m_pGates[parentid].gs.yval[((i+1)*m_nSecParamBytes) - 1] & 0x01) << endl;
#endif
	}

	//TODO: is the gate is an output gate for both parties, uncommenting this will crash the program. FIX!
	//UsedGate(parentid);
}

/* Store the input bits of my gates to send the correlation with the R-OTs later on */
void YaoClientSharing::ReceiveClientKeys(uint32_t gateid) {
	GATE* gate = m_pGates + gateid;
	UGATE_T* input = gate->gs.ishare.inval;
	m_vROTSndBuf.SetBits((BYTE*) input, (int) m_nClientSndOTCtr, gate->nvals);
	m_nClientSndOTCtr += gate->nvals;
	m_vClientSendCorrectionGates.push_back(gateid);
}

/* Add the servers input keys to the queue to receive them later on */
void YaoClientSharing::ReceiveServerKeys(uint32_t gateid) {
	GATE* gate = m_pGates + gateid;

	m_vServerInputGates.push_back(gateid);
	m_nServerInBitCtr += gate->nvals;
}

void YaoClientSharing::EvaluateConversionGate(uint32_t gateid) {
	GATE* gate = m_pGates + gateid;
	GATE* parent = m_pGates + gate->ingates.inputs.parents[0];
	assert(parent->instantiated);
	UGATE_T* val = parent->gs.val;

	if (parent->context == S_ARITH && (gate->gs.pos & 0x01) == 0) {
#ifdef DEBUGYAOCLIENT
		cout << "Server conversion gate with pos = " << gate->gs.pos << endl;
#endif
		m_vServerInputGates.push_back(gateid);
		m_nServerInBitCtr += gate->nvals;
	} else {
#ifdef DEBUGYAOCLIENT
		cout << "Client conversion gate with pos = " << gate->gs.pos << endl;
#endif
		if (parent->context == S_ARITH) {
			uint32_t id, tval;
			id = gate->gs.pos >> 1;
			tval = (val[id / GATE_T_BITS] >> (id % GATE_T_BITS)) & 0x01; //TODO: not going to work for nvals > 1
			m_vROTSndBuf.SetBits((BYTE*) &tval, (int) m_nClientSndOTCtr, gate->nvals);
#ifdef DEBUGYAOCLIENT
			cout << "value of conversion gate: " << tval << endl;
#endif
		} else {
			m_vROTSndBuf.SetBits((BYTE*) val, (int) m_nClientSndOTCtr, gate->nvals);
#ifdef DEBUGYAOCLIENT
			cout << "value of conversion gate: " << val[0] << endl;
#endif
		}
		m_nClientSndOTCtr += gate->nvals;
		m_vClientSendCorrectionGates.push_back(gateid);
	}
}

//TODO bits in ROTMasks are not going to be aligned later on, recheck
void YaoClientSharing::GetDataToSend(vector<BYTE*>& sendbuf, vector<uint32_t>& sndbytes) {
	//Send the correlation bits with the random OTs
	if (m_nClientSndOTCtr > 0) {
#ifdef DEBUGYAOCLIENT
		cout << "want to send client OT-bits which are of size " << m_nClientSndOTCtr << " bits" << endl;
#endif
		m_vROTSndBuf.XORBitsPosOffset(m_vChoiceBits.GetArr(), m_nChoiceBitCtr, 0, m_nClientSndOTCtr);
#ifdef DEBUGYAOCLIENT
		cout << "Sending corrections: ";
		m_vROTSndBuf.Print(0, m_nClientSndOTCtr);
		cout << " = value ^ ";
		m_vChoiceBits.Print(m_nChoiceBitCtr, m_nChoiceBitCtr + m_nClientSndOTCtr);
#endif
		sendbuf.push_back(m_vROTSndBuf.GetArr());
		sndbytes.push_back(ceil_divide(m_nClientSndOTCtr, 8));
		m_nChoiceBitCtr += m_nClientSndOTCtr;
	}

	if (m_nServerOutputShareCtr > 0) {
#ifdef DEBUGYAOCLIENT
		cout << "want to send server output shares which are of size " << m_nServerOutputShareCtr << " bits" << endl;
#endif
		sendbuf.push_back(m_vOutputShareSndBuf.GetArr());
		sndbytes.push_back(ceil_divide(m_nServerOutputShareCtr, 8));
	}

#ifdef DEBUGYAO
	if(m_nInputShareSndSize > 0) {
		cout << "Sending " << m_nInputShareSndSize << " Input shares : ";
		m_vInputShareSndBuf.Print(0, m_nInputShareSndSize);
	}
	if(m_nOutputShareSndSize > 0) {
		cout << "Sending " << m_nOutputShareSndSize << " Output shares : ";
		m_vOutputShareSndBuf.Print(0, m_nOutputShareSndSize);
	}
#endif
}

/* Register the values that are to be received in this iteration */
void YaoClientSharing::GetBuffersToReceive(vector<BYTE*>& rcvbuf, vector<uint32_t>& rcvbytes) {
	//Receive servers keys
	if (m_nServerInBitCtr > 0) {
#ifdef DEBUGYAOCLIENT
		cout << "want to receive servers input keys which are of size " << (m_nServerInBitCtr * m_nSecParamBytes) << " bytes" << endl;
#endif
		m_vServerInputKeys.Create(m_nServerInBitCtr * m_cCrypto->get_seclvl().symbits);
		rcvbuf.push_back(m_vServerInputKeys.GetArr());
		rcvbytes.push_back(m_nServerInBitCtr * m_nSecParamBytes);
	}

	if (m_nClientRcvKeyCtr > 0) {
#ifdef DEBUGYAOCLIENT
		cout << "want to receive client input keys which are of size 2* " << m_nClientRcvKeyCtr * m_nSecParamBytes << " bytes" << endl;
#endif
		m_vClientKeyRcvBuf[0].Create(m_nClientRcvKeyCtr * m_cCrypto->get_seclvl().symbits);
		rcvbuf.push_back(m_vClientKeyRcvBuf[0].GetArr());
		rcvbytes.push_back(m_nClientRcvKeyCtr * m_nSecParamBytes);

		m_vClientKeyRcvBuf[1].Create(m_nClientRcvKeyCtr * m_cCrypto->get_seclvl().symbits);
		rcvbuf.push_back(m_vClientKeyRcvBuf[1].GetArr());
		rcvbytes.push_back(m_nClientRcvKeyCtr * m_nSecParamBytes);
	}
}

void YaoClientSharing::FinishCircuitLayer() {
	//Assign the servers input keys that were received this round
	if (m_nServerInBitCtr > 0)
		AssignServerInputKeys();

	//Assign the clients input keys that were received this round
	if (m_nClientRcvKeyCtr > 0) {
		AssignClientInputKeys();
	}

	//Assign the clients input keys to the gates
	if (m_nClientSndOTCtr > 0) {
		m_nClientRcvKeyCtr = m_nClientSndOTCtr;
		m_nClientSndOTCtr = 0;
		//TODO optimize
		for (uint32_t i = 0; i < m_vClientSendCorrectionGates.size(); i++) {
			m_vClientRcvInputKeyGates.push_back(m_vClientSendCorrectionGates[i]);
		}
		m_vClientSendCorrectionGates.clear();
	}

	InitNewLayer();
}
;

/* Assign the received server input keys to the pushed back gates in this round */
void YaoClientSharing::AssignServerInputKeys() {
	GATE* gate;
	for (uint32_t i = 0, offset = 0; i < m_vServerInputGates.size(); i++) {
		gate = m_pGates + m_vServerInputGates[i];
		InstantiateGate(gate);
		//Assign the keys to the gate
		memcpy(gate->gs.yval, m_vServerInputKeys.GetArr() + offset, m_nSecParamBytes * gate->nvals);
		offset += (m_nSecParamBytes * gate->nvals);
#ifdef DEBUGYAOCLIENT
		cout << "assigned server input key: ";
		PrintKey(gate->gs.yval);
		cout << endl;
#endif
	}
	m_vServerInputGates.clear();

	m_nServerInBitCtr = 0;
}

/* Assign the received server input keys to the pushed back gates in this round */
void YaoClientSharing::AssignClientInputKeys() {
	GATE* gate, *parent;
	for (uint32_t i = 0, offset = 0; i < m_vClientRcvInputKeyGates.size(); i++) {
		gate = m_pGates + m_vClientRcvInputKeyGates[i];
		//input = ;

		InstantiateGate(gate);
		//Assign the keys to the gate, TODO XOR with R-OT masks
		for (uint32_t j = 0; j < gate->nvals; j++, m_nKeyInputRcvIdx++, offset++) {
			m_pKeyOps->XOR(gate->gs.yval + j * m_nSecParamBytes, m_vClientKeyRcvBuf[m_vChoiceBits.GetBitNoMask(m_nKeyInputRcvIdx)].GetArr() + offset * m_nSecParamBytes,
					m_vROTMasks.GetArr() + m_nKeyInputRcvIdx * m_nSecParamBytes);
#ifdef DEBUGYAOCLIENT
			cout << "assigned client input key to gate " << m_vClientRcvInputKeyGates[i] << ": ";
			PrintKey(gate->gs.yval);
			cout << " (" << (uint32_t) m_vChoiceBits.GetBitNoMask(m_nKeyInputRcvIdx) << ") = " << endl;
			PrintKey( m_vClientKeyRcvBuf[m_vChoiceBits.GetBitNoMask(m_nKeyInputRcvIdx)].GetArr() + offset * m_nSecParamBytes);
			cout << " ^ " << endl;
			PrintKey(m_vROTMasks.GetArr() + (m_nKeyInputRcvIdx) * m_nSecParamBytes);
			cout << endl;
#endif
		}
		if (gate->type == G_IN) {
			free(gate->gs.ishare.inval);
		}
		else {
			//G_CONV
			free(gate->ingates.inputs.parents);
		}
	}
	m_vClientRcvInputKeyGates.clear();

	m_nClientRcvKeyCtr = 0;
}

void YaoClientSharing::InstantiateGate(GATE* gate) {
	gate->instantiated = true;
	gate->gs.yval = (BYTE*) calloc(m_nSecParamIters * gate->nvals, sizeof(UGATE_T));
}

void YaoClientSharing::UsedGate(uint32_t gateid) {
	//Decrease the number of further uses of the gate
	m_pGates[gateid].nused--;
	//If the gate is needed in another subsequent gate, delete it
	if (!m_pGates[gateid].nused) {
		free(m_pGates[gateid].gs.yval);
	}
}

void YaoClientSharing::EvaluateSIMDGate(uint32_t gateid) {
	GATE* gate = m_pGates + gateid;
	if (gate->type == G_COMBINE) {
		uint32_t vsize = gate->nvals;
		uint32_t* inptr = gate->ingates.inputs.parents; //gate->gs.cinput;
		InstantiateGate(gate);
		BYTE* keyptr = gate->gs.yval;
		for (uint32_t g = 0; g < vsize; g++, keyptr += m_nSecParamBytes) {
			memcpy(keyptr, m_pGates[inptr[g]].gs.yval, m_nSecParamBytes);
			UsedGate(inptr[g]);
		}
		free(inptr);
	} else if (gate->type == G_SPLIT) {
		uint32_t pos = gate->gs.sinput.pos;
		uint32_t idleft = gate->ingates.inputs.parent; // gate->gs.sinput.input;
		InstantiateGate(gate);
		memcpy(gate->gs.yval, m_pGates[idleft].gs.yval + pos * m_nSecParamBytes, m_nSecParamBytes * gate->nvals);
		UsedGate(idleft);
	} else if (gate->type == G_REPEAT) {
		uint32_t idleft = gate->ingates.inputs.parent; //gate->gs.rinput;
		InstantiateGate(gate);
		BYTE* keyptr = gate->gs.yval;
		for (uint32_t g = 0; g < gate->nvals; g++, keyptr += m_nSecParamBytes) {
			memcpy(keyptr, m_pGates[idleft].gs.yval, m_nSecParamBytes);
		}
		UsedGate(idleft);
	} else if (gate->type == G_COMBINEPOS) {
		uint32_t* combinepos = gate->ingates.inputs.parents; //gate->gs.combinepos.input;
		uint32_t pos = gate->gs.combinepos.pos;
		InstantiateGate(gate);
		BYTE* keyptr = gate->gs.yval;
		for (uint32_t g = 0; g < gate->nvals; g++, keyptr += m_nSecParamBytes) {
			uint32_t idleft = combinepos[g];
			memcpy(keyptr, m_pGates[idleft].gs.yval + pos * m_nSecParamBytes, m_nSecParamBytes);
			UsedGate(idleft);
		}
		free(combinepos);
#ifdef ZDEBUG
		cout << "), size = " << size << ", and val = " << gate->gs.val[0]<< endl;
#endif
#ifdef DEBUGCLIENT
		cout << ", res: " << ((unsigned uint32_t) gate->gs.yval[BYTES_SSP-1] & 0x01) << " = " << ((unsigned uint32_t) gleft->gs.yval[BYTES_SSP-1] & 0x01) << " and " << ((unsigned uint32_t) gright->gs.yval[BYTES_SSP-1] & 0x01);
#endif
	}
}

uint32_t YaoClientSharing::AssignInput(CBitVector& inputvals) {
	deque<uint32_t> myingates = m_cBoolCircuit->GetInputGatesForParty(m_eRole);
	inputvals.Create(m_cBoolCircuit->GetNumInputBitsForParty(m_eRole), m_cCrypto);

	GATE* gate;
	uint32_t inbits = 0;
	for (uint32_t i = 0, inbitstart = 0, bitstocopy, len, lim; i < myingates.size(); i++) {
		gate = m_pGates + myingates[i];
		if (!gate->instantiated) {
			bitstocopy = gate->nvals * gate->sharebitlen;
			inbits += bitstocopy;
			lim = ceil_divide(bitstocopy, GATE_T_BITS);

			UGATE_T* inval = (UGATE_T*) calloc(lim, sizeof(UGATE_T));

			for (uint32_t j = 0; j < lim; j++, bitstocopy -= GATE_T_BITS) {
				len = min(bitstocopy, (uint32_t) GATE_T_BITS);
				inval[j] = inputvals.Get<UGATE_T>(inbitstart, len);
				inbitstart += len;
			}
			gate->gs.ishare.inval = inval;
		}
	}
	return inbits;
}

uint32_t YaoClientSharing::GetOutput(CBitVector& out) {
	deque<uint32_t> myoutgates = m_cBoolCircuit->GetOutputGatesForParty(m_eRole);
	uint32_t outbits = m_cBoolCircuit->GetNumOutputBitsForParty(m_eRole);
	out.Create(outbits);

	GATE* gate;
	for (uint32_t i = 0, outbitstart = 0, bitstocopy, len, lim; i < myoutgates.size(); i++) {
		gate = m_pGates + myoutgates[i];
		lim = gate->nvals * gate->sharebitlen;
		cout << "outgate no " << i << " : " << myoutgates[i] << " with nvals = " << gate->nvals << " and sharebitlen = " << gate->sharebitlen << endl;

		for (uint32_t j = 0; j < lim; j++, outbitstart++) {
			out.SetBitNoMask(outbitstart, (gate->gs.val[j / GATE_T_BITS] >> (j % GATE_T_BITS)) & 0x01);
		}
	}
	return outbits;
}

void YaoClientSharing::Reset() {
	m_vROTMasks.delCBitVector();
	m_nChoiceBitCtr = 0;
	m_vChoiceBits.delCBitVector();

	m_nServerInBitCtr = 0;
	m_nClientSndOTCtr = 0;
	m_nClientRcvKeyCtr = 0;
	m_nClientOutputShareCtr = 0;
	m_nServerOutputShareCtr = 0;

	m_nClientOUTBitCtr = 0;

	m_nKeyInputRcvIdx = 0;

	m_vServerKeyRcvBuf.delCBitVector();
	for (uint32_t i = 0; i < m_vClientKeyRcvBuf.size(); i++)
		m_vClientKeyRcvBuf[i].delCBitVector();

	m_nGarbledCircuitRcvCtr = 0;

	m_vOutputShareRcvBuf.delCBitVector();
	m_vOutputShareSndBuf.delCBitVector();

	m_vClientSendCorrectionGates.clear();
	m_vServerInputGates.clear();
	m_vANDGates.clear();
	m_vOutputShareGates.clear();

	m_vROTSndBuf.delCBitVector();
	m_vROTCtr = 0;

	m_nANDGates = 0;
	m_nXORGates = 0;

	m_nInputShareSndSize = 0;
	m_nOutputShareSndSize = 0;

	m_nInputShareRcvSize = 0;
	m_nOutputShareRcvSize = 0;

	m_nClientInputBits = 0;
	m_vClientInputKeys.delCBitVector();

	m_nServerInputBits = 0;
	m_vServerInputKeys.delCBitVector();

	m_vGarbledCircuit.delCBitVector();
	m_nGarbledTableCtr = 0;

	m_cBoolCircuit->Reset();
}
