/*************************************************************************
 * Copyright (c) 2013 eProsima. All rights reserved.
 *
 * This copy of FastCdr is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/*
 * StatelessWriter.cpp
 *
 *  Created on: Feb 25, 2014
 *      Author: Gonzalo Rodriguez Canosa
 *      email:  gonzalorodriguez@eprosima.com
 *              grcanosa@gmail.com
 */

#include "eprosimartps/writer/StatelessWriter.h"
#include "eprosimartps/writer/ReaderLocator.h"
#include "eprosimartps/dds/ParameterList.h"

namespace eprosima {
namespace rtps {



StatelessWriter::StatelessWriter(const WriterParams_t* param,uint32_t payload_size):
		RTPSWriter(param->historySize,payload_size)
{
	m_pushMode = param->pushMode;
	//writer_cache.changes.reserve(param->historySize);


	m_stateType = STATELESS;
	//locator lists:
	unicastLocatorList = param->unicastLocatorList;
	multicastLocatorList = param->multicastLocatorList;
	topicKind = param->topicKind;
	m_topicName = param->topicName;
	m_topicDataType = param->topicDataType;
}




StatelessWriter::~StatelessWriter()
{
	// TODO Auto-generated destructor stub
	pDebugInfo("StatelessWriter destructor"<<endl;);
}

bool StatelessWriter::reader_locator_add(ReaderLocator& a_locator) {

	for(std::vector<ReaderLocator>::iterator rit=reader_locator.begin();rit!=reader_locator.end();++rit){
		if(rit->locator == a_locator.locator)
			return false;
	}

	for(std::vector<CacheChange_t*>::iterator it = m_writer_cache.m_changes.begin();
			it!=m_writer_cache.m_changes.end();++it){
		a_locator.unsent_changes.push_back((*it));
	}
	reader_locator.push_back(a_locator);
	return true;
}

bool StatelessWriter::reader_locator_remove(Locator_t& locator) {
	std::vector<ReaderLocator>::iterator it;
	for(it=reader_locator.begin();it!=reader_locator.end();++it){
		if(it->locator == locator){
			reader_locator.erase(it);
			return true;
		}
	}
	return false;
}

void StatelessWriter::unsent_changes_reset() {


	for(std::vector<ReaderLocator>::iterator rit=reader_locator.begin();rit!=reader_locator.end();++rit){
		rit->unsent_changes.clear();
		for(std::vector<CacheChange_t*>::iterator cit=m_writer_cache.m_changes.begin();
				cit!=m_writer_cache.m_changes.end();++cit){
			rit->unsent_changes.push_back((*cit));
		}
	}
	unsent_changes_not_empty();
}

bool sort_cacheChanges (CacheChange_t* c1,CacheChange_t* c2)
{
	return(c1->sequenceNumber.to64long() < c2->sequenceNumber.to64long());
}

void StatelessWriter::unsent_change_add(CacheChange_t* cptr)
{
	if(!reader_locator.empty())
	{
		for(std::vector<ReaderLocator>::iterator rit=reader_locator.begin();rit!=reader_locator.end();++rit)
		{
			rit->unsent_changes.push_back(cptr);

			if(m_pushMode)
			{
//				std::sort(rit->unsent_changes.begin(),rit->unsent_changes.end(),sort_cacheChanges);
				RTPSMessageGroup::send_Changes_AsData(&m_cdrmessages,(RTPSWriter*)this,
						&rit->unsent_changes,&rit->locator,rit->expectsInlineQos,c_EntityId_Unknown);
				rit->unsent_changes.clear();
			}
			else
			{
					SequenceNumber_t first,last;
				m_writer_cache.get_seq_num_min(&first,NULL);
				m_writer_cache.get_seq_num_max(&last,NULL);
				m_heartbeatCount++;
				CDRMessage::initCDRMsg(&m_cdrmessages.m_rtpsmsg_fullmsg);
				RTPSMessageCreator::addMessageHeartbeat(&m_cdrmessages.m_rtpsmsg_fullmsg,m_guid.guidPrefix,
										ENTITYID_UNKNOWN,m_guid.entityId,first,last,m_heartbeatCount,true,false);
				mp_send_thr->sendSync(&m_cdrmessages.m_rtpsmsg_fullmsg,&rit->locator);
				rit->unsent_changes.clear();
			}
		}

	}
	else
	{
		pWarning( "No reader locator to add change" << std::endl);

	}

}

void StatelessWriter::unsent_changes_not_empty()
{

	for(std::vector<ReaderLocator>::iterator rit=reader_locator.begin();rit!=reader_locator.end();++rit)
	{
		if(m_pushMode)
		{
			//	std::sort(rit->unsent_changes.begin(),rit->unsent_changes.end(),sort_cacheChanges);
			RTPSMessageGroup::send_Changes_AsData(&m_cdrmessages,(RTPSWriter*)this,
					&rit->unsent_changes,&rit->locator,rit->expectsInlineQos,c_EntityId_Unknown);
			rit->unsent_changes.clear();
		}
		else
		{
			SequenceNumber_t first,last;
			m_writer_cache.get_seq_num_min(&first,NULL);
			m_writer_cache.get_seq_num_max(&last,NULL);
			m_heartbeatCount++;
			CDRMessage::initCDRMsg(&m_cdrmessages.m_rtpsmsg_fullmsg);
			RTPSMessageCreator::addMessageHeartbeat(&m_cdrmessages.m_rtpsmsg_fullmsg,m_guid.guidPrefix,
					ENTITYID_UNKNOWN,m_guid.entityId,first,last,m_heartbeatCount,true,false);
			mp_send_thr->sendSync(&m_cdrmessages.m_rtpsmsg_fullmsg,&rit->locator);
			rit->unsent_changes.clear();
		}

	}
	pDebugInfo ( "Finish sending unsent changes" << endl);
}


} /* namespace rtps */
} /* namespace eprosima */

