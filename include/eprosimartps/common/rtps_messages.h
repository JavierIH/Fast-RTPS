/*************************************************************************
 * Copyright (c) 2014 eProsima. All rights reserved.
 *
 * This copy of eProsima RTPS is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/**
 * @file rtps_messages.h
 *	Messages structures definition.
 *  Created on: Feb 18, 2014
 *      Author: Gonzalo Rodriguez Canosa
 */

#ifndef RTPS_MESSAGES_H_
#define RTPS_MESSAGES_H_

#include <vector>
#include <iostream>
#include <bitset>

#include "../ParameterList_t.h"

namespace eprosima{
namespace rtps{

/** @defgroup RTPSMESSAGES RTPS Messages structures
  * @ingroup COMMONMODULE
 * Structures to hold different RTPS Messages and submessages structures.
 *  @{
 */

//!@brief Structure Header_t, RTPS Message Header Structure.
typedef struct Header_t{
	ProtocolVersion_t version;
	VendorId_t vendorId;;
	GuidPrefix_t guidPrefix;
	Header_t(){
		PROTOCOLVERSION(version);
		VENDORID_EPROSIMA(vendorId);
	}
	~Header_t(){
	}
	void print(){
		cout << "RTPS HEADER of Version: " << (int)version.major << "." << (int)version.minor;
		cout << "  || VendorId: " << (int)vendorId[0] << "." <<(int)vendorId[1] << endl;
		cout << "GuidPrefix: ";
		for(int i =0;i<12;i++)
			cout << (int)guidPrefix.value[i] << ".";
		cout << endl;
	}
}Header_t;




// //!@brief Enumeration of the different Submessages types
#define	PAD 0x01
#define	ACKNACK 0x06
#define	HEARTBEAT 0x07
#define	GAP 0x08
#define	INFO_TS 0x09
#define	INFO_SRC 0x0c
#define	INFO_REPLY_IP4 0x0d
#define	INFO_DST 0x0e
#define	INFO_REPLY 0x0f
#define	NACK_FRAG 0x12
#define	HEARTBEAT_FRAG 0x13
#define	DATA 0x15
#define	DATA_FRAG 0x16


//!@brief Structure SubmessageHeader_t.
typedef struct SubmessageHeader_t{
	octet submessageId;
	uint16_t submessageLength;
	SubmessageFlag flags;
	void print (){
		cout << "Submessage Header, ID: " << (int)submessageId;
		cout << " length: " << (int)submessageLength << " flags " << (bitset<8>) flags << endl;
	}

}SubmessageHeader_t;



// SUBMESSAGE types definition

//!@brief Structure SubmsgData_t, contains the information necessary to create a Data Submessage.
typedef struct SubmsgData_t{
	SubmessageHeader_t SubmessageHeader;
	bool endiannessFlag;
	bool inlineQosFlag;
	bool dataFlag;
	bool keyFlag;
	EntityId_t readerId;
	EntityId_t writerId;
	SequenceNumber_t writerSN;
	SerializedPayload_t serializedPayload;
	ParameterList_t inlineQos;
	void print(){
		cout << "DATA SubMsg,flags: E: " << endiannessFlag << " I: " << inlineQosFlag << " D: " << dataFlag << " K: " << keyFlag << endl;
		cout << "readerId: " << (int)readerId.value[0] << "." << (int)readerId.value[1] << "." << (int)readerId.value[2] << "." << (int)readerId.value[3];
		cout << " || writerId: " << (int)writerId.value[0] << "." << (int)writerId.value[1] << "." << (int)writerId.value[2] << "." << (int)writerId.value[3] << endl;
		cout << "InlineQos: " << inlineQos.params.size() << " parameters." << endl;
		cout << "SeqNum: " << writerSN.to64long() << " Payload: enc: " << serializedPayload.encapsulation << " length: " << serializedPayload.length << endl;
	}
}SubmsgData_t;

//!@brief Structure SubmsgHeartbeat_t, contains the information necessary to create a Heartbeat Submessage.
typedef struct{
	SubmessageHeader_t SubmessageHeader;
	bool endiannessFlag;
	bool finalFlag;
	bool livelinessFlag;
	EntityId_t readerId;
	EntityId_t writerId;
	SequenceNumber_t firstSN;
	SequenceNumber_t lastSN;
	Count_t count;
}SubmsgHeartbeat_t;

//!@brief Structure SubmsgAcknack_t, contains the information necessary to create a Acknack Submessage.
typedef struct{
	SubmessageHeader_t SubmessageHeader;
	bool endiannessFlag;
	bool finalFlag;
	EntityId_t readerId;
	EntityId_t writerId;
	SequenceNumberSet_t readerSNState;
	Count_t count;
}SubmsgAcknack_t;

//!@brief Structure SubmsgGap_t, contains the information necessary to create a Gap Submessage.
typedef struct{
	SubmessageHeader_t SubmessageHeader;
	bool endiannessFlag;
	EntityId_t readerId;
	EntityId_t writerId;
	SequenceNumber_t gapStart;
	SequenceNumberSet_t gapList;
}SubmsgGap_t;


//!@brief Structure SubmsgInfoTS_t, contains the information necessary to create a InfoTS Submessage.
typedef struct{
	SubmessageHeader_t SubmessageHeader;
	Time_t timestamp;
}SubmsgInfoTS_t;

///@}


}
}



#endif /* RTPS_MESSAGES_H_ */
