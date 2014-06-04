/*************************************************************************
 * Copyright (c) 2014 eProsima. All rights reserved.
 *
 * This copy of eProsima RTPS is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/**
 * @file ThroughputTypes.cpp
 *
 *  Created on: Jun 4, 2014
 *      Author: Gonzalo Rodriguez Canosa
 *      email:  gonzalorodriguez@eprosima.com
 *              grcanosa@gmail.com  	
 */

#include "ThroughputTypes.h"


//Funciones de serializacion y deserializacion para el ejemplo
bool LatencyDataType::serialize(void*data,SerializedPayload_t* payload)
{
	LatencyType* lt = (LatencyType*)data;
	*(uint32_t*)payload->data = lt->seqnum;
	*(uint32_t*)(payload->data+4) = (uint32_t)lt->data.size();
	std::copy(lt->data.begin(),lt->data.end(),payload->data+8);
	payload->length = 8+lt->data.size();
	return true;
}

bool LatencyDataType::deserialize(SerializedPayload_t* payload,void * data)
{
	LatencyType* lt = (LatencyType*)data;
	lt->seqnum = *(uint32_t*)payload->data;
	uint32_t siz = *(uint32_t*)(payload->data+4);
	std::copy(payload->data+8,payload->data+8+siz,lt->data.begin());
	return true;
}


bool ThroughputDataType::serialize(void*data,SerializedPayload_t* payload)
{
	ThroughputCommandType* t = (ThroughputCommandType*)data;
	*(ThroughputCommandType::Command*)payload->data = t->m_command;
	payload->length = 4;
	return true;
}
bool ThroughputDataType::deserialize(SerializedPayload_t* payload,void * data)
{
	ThroughputCommandType* t = (ThroughputCommandType*)data;
	 t->m_command = *(ThroughputCommandType::Command*)payload->data;
	return true;
}


