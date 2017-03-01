// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file StatefulWriter.cpp
 *
 */

#include <fastrtps/rtps/writer/StatefulWriter.h>
#include <fastrtps/rtps/writer/ReaderProxy.h>
#include <fastrtps/rtps/resources/AsyncWriterThread.h>

#include "../participant/RTPSParticipantImpl.h"

#include <fastrtps/rtps/messages/RTPSMessageCreator.h>

#include <fastrtps/utils/TimeConversion.h>

#include <fastrtps/rtps/writer/timedevent/PeriodicHeartbeat.h>
#include <fastrtps/rtps/writer/timedevent/NackSupressionDuration.h>
#include <fastrtps/rtps/writer/timedevent/NackResponseDelay.h>
#include <fastrtps/rtps/writer/timedevent/InitialHeartbeat.h>

#include <fastrtps/rtps/history/WriterHistory.h>

#include <fastrtps/log/Log.h>
#include <fastrtps/utils/TimeConversion.h>

#include <mutex>

using namespace eprosima::fastrtps::rtps;


StatefulWriter::StatefulWriter(RTPSParticipantImpl* pimpl,GUID_t& guid,
        WriterAttributes& att,WriterHistory* hist,WriterListener* listen):
    RTPSWriter(pimpl,guid,att,hist,listen),
    mp_periodicHB(nullptr), m_times(att.times),
    all_acked_mutex_(nullptr), all_acked_(false), all_acked_cond_(nullptr)
{
    m_heartbeatCount = 0;
    if(guid.entityId == c_EntityId_SEDPPubWriter)
        m_HBReaderEntityId = c_EntityId_SEDPPubReader;
    else if(guid.entityId == c_EntityId_SEDPSubWriter)
        m_HBReaderEntityId = c_EntityId_SEDPSubReader;
    else if(guid.entityId == c_EntityId_WriterLiveliness)
        m_HBReaderEntityId= c_EntityId_ReaderLiveliness;
    else
        m_HBReaderEntityId = c_EntityId_Unknown;
    mp_periodicHB = new PeriodicHeartbeat(this,TimeConv::Time_t2MilliSecondsDouble(m_times.heartbeatPeriod));
    all_acked_mutex_ = new std::mutex();
    all_acked_cond_ = new std::condition_variable();
    m_reader_iterator = matched_readers.begin();
}


StatefulWriter::~StatefulWriter()
{
    AsyncWriterThread::removeWriter(*this);

    logInfo(RTPS_WRITER,"StatefulWriter destructor");

    delete all_acked_cond_;
    delete all_acked_mutex_;

    for(std::vector<ReaderProxy*>::iterator it = matched_readers.begin();
            it != matched_readers.end(); ++it)
        (*it)->destroy_timers();

    if(mp_periodicHB !=nullptr)
        delete(mp_periodicHB);

    for(std::vector<ReaderProxy*>::iterator it = matched_readers.begin();
            it!=matched_readers.end();++it)
        delete(*it);
}

/*
 *	CHANGE-RELATED METHODS
 */

void StatefulWriter::unsent_change_added_to_history(CacheChange_t* change)
{
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);

    //TODO Think about when set liveliness assertion when writer is asynchronous.
    this->setLivelinessAsserted(true);

    if(!matched_readers.empty())
    {
        if(!isAsync())
        {
            LocatorList_t unilocList;
            LocatorList_t multilocList;
            bool expectsInlineQos = false;

            for(auto it = matched_readers.begin(); it != matched_readers.end(); ++it)
            {
                ChangeForReader_t changeForReader(change);

                // TODO(Ricardo) Study next case: Not push mode, writer reiable and reader besteffort.
                if(m_pushMode)
                {
                    if((*it)->m_att.endpoint.reliabilityKind == RELIABLE)
                        changeForReader.setStatus(UNDERWAY);
                    else
                        changeForReader.setStatus(ACKNOWLEDGED);
                }
                else
                    changeForReader.setStatus(UNACKNOWLEDGED);

                (*it)->mp_mutex->lock();
                changeForReader.setRelevance((*it)->rtps_is_relevant(change));
                (*it)->addChange(changeForReader);
                unilocList.push_back((*it)->m_att.endpoint.unicastLocatorList);
                multilocList.push_back((*it)->m_att.endpoint.multicastLocatorList);
                expectsInlineQos |= (*it)->m_att.expectsInlineQos;
                (*it)->mp_mutex->unlock();

                if((*it)->mp_nackSupression != nullptr) // It is reliable
                    (*it)->mp_nackSupression->restart_timer();
            }

            std::vector<CacheChangeForGroup_t> changes_to_send;
            changes_to_send.push_back(CacheChangeForGroup_t(change));

            uint32_t bytesSent = RTPSMessageGroup::send_Changes_AsData(&m_cdrmessages, (RTPSWriter*)this,
                    changes_to_send, c_GuidPrefix_Unknown, c_EntityId_Unknown, unilocList,
                    multilocList, expectsInlineQos);

            if(bytesSent == 0 || changes_to_send.size() > 0)
                logError(RTPS_WRITER, "Error sending change " << change->sequenceNumber);

            this->mp_periodicHB->restart_timer();
        }
        else
        {
            for(auto it = matched_readers.begin(); it != matched_readers.end(); ++it)
            {
                ChangeForReader_t changeForReader(change);

                if(m_pushMode)
                    changeForReader.setStatus(UNSENT);
                else
                    changeForReader.setStatus(UNACKNOWLEDGED);

                std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);
                changeForReader.setRelevance((*it)->rtps_is_relevant(change));
                (*it)->addChange(changeForReader);
            }
        }
    }
    else
    {
        logInfo(RTPS_WRITER,"No reader proxy to add change.");
    }
}


bool StatefulWriter::change_removed_by_history(CacheChange_t* a_change)
{
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);
    logInfo(RTPS_WRITER,"Change "<< a_change->sequenceNumber << " to be removed.");

	// Invalidate CacheChange pointer in ReaderProxies.
    for(std::vector<ReaderProxy*>::iterator it = this->matched_readers.begin();
            it!=this->matched_readers.end();++it)
    {
        (*it)->setNotValid(a_change);
    }

    return true;
}

bool StatefulWriter::wrap_around_readers()
{
    // We loop the reader iterator around until
    // we reach the wrapping point

    if (m_readers_to_walk == 0)
        return false;

    m_readers_to_walk--;
    m_reader_iterator++;
    if (m_reader_iterator == matched_readers.end())
        m_reader_iterator = matched_readers.begin();

    return true;
}

size_t StatefulWriter::send_any_unsent_changes()
{
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);
    size_t number_of_changes_sent = 0;

    m_readers_to_walk = matched_readers.size();
    // The reader proxy vector is walked in a different order each time
    // to prevent persistent prioritization of a single reader
    while(wrap_around_readers())
    {
        std::lock_guard<std::recursive_mutex> rguard(*(*m_reader_iterator)->mp_mutex);

        std::vector<const ChangeForReader_t*> ch_vec = (*m_reader_iterator)->get_unsent_changes();

        std::vector<CacheChangeForGroup_t> relevant_changes;
        std::vector<SequenceNumber_t> not_relevant_changes;

        for(auto cit = ch_vec.begin(); cit != ch_vec.end(); ++cit)
        {
            //cout << "EXPECTSINLINE: "<< (*m_reader_iterator)->m_att.expectsInlineQos<< endl;
            if((*cit)->isRelevant() && (*cit)->isValid())
            {
                relevant_changes.emplace_back(**cit);
            }
            else
            {
                not_relevant_changes.push_back((*cit)->getSequenceNumber());
                (*m_reader_iterator)->set_change_to_status((*cit)->getChange(), UNDERWAY);
            }
        }

        // Clear all relevant changes through the local controllers first
        for (auto& controller : m_controllers)
            (*controller)(relevant_changes);

        // Clear all relevant changes through the parent controllers
        for (auto& controller : mp_RTPSParticipant->getFlowControllers())
            (*controller)(relevant_changes);

        // Those that remain are set to UNDERWAY or their unsent sets updated
        for (auto& change : relevant_changes)
        {
            if (change.isFragmented() && !change.getFragmentsClearedForSending().isSetEmpty())
                (*m_reader_iterator)->mark_fragments_as_sent_for_change(change.getChange(), change.getFragmentsClearedForSending());
            else
            {
                if((*m_reader_iterator)->m_att.endpoint.reliabilityKind == RELIABLE)
                    (*m_reader_iterator)->set_change_to_status(change.getChange(), UNDERWAY);
                else
                    (*m_reader_iterator)->set_change_to_status(change.getChange(), ACKNOWLEDGED);
            }
        }

        // And controllers are notified about the changes being sent
        for (const auto& change : relevant_changes)
            FlowController::NotifyControllersChangeSent(&change);

        if(m_pushMode)
        {
            if(!relevant_changes.empty())
            {
                number_of_changes_sent += relevant_changes.size();
                uint32_t bytesSent = 0;
                do
                {
                    bytesSent =  RTPSMessageGroup::send_Changes_AsData(&m_cdrmessages, (RTPSWriter*)this,
                            relevant_changes,
                            (*m_reader_iterator)->m_att.guid.guidPrefix,
                            (*m_reader_iterator)->m_att.guid.entityId,
                            (*m_reader_iterator)->m_att.endpoint.unicastLocatorList,
                            (*m_reader_iterator)->m_att.endpoint.multicastLocatorList,
                            (*m_reader_iterator)->m_att.expectsInlineQos);
                } while(bytesSent > 0 && relevant_changes.size() > 0);
            }
            if(!not_relevant_changes.empty())
                RTPSMessageGroup::send_Changes_AsGap(&m_cdrmessages,(RTPSWriter*)this,
                        &not_relevant_changes,
                        (*m_reader_iterator)->m_att.guid.guidPrefix,
                        (*m_reader_iterator)->m_att.guid.entityId,
                        &(*m_reader_iterator)->m_att.endpoint.unicastLocatorList,
                        &(*m_reader_iterator)->m_att.endpoint.multicastLocatorList);

            if((*m_reader_iterator)->m_att.endpoint.reliabilityKind == RELIABLE)
            {
                this->mp_periodicHB->restart_timer();
            }

            if((*m_reader_iterator)->mp_nackSupression != nullptr) // It is reliable
                (*m_reader_iterator)->mp_nackSupression->restart_timer();
        }
        else
        {
            SequenceNumber_t firstSeq = this->get_seq_num_min();
            SequenceNumber_t lastSeq = this->get_seq_num_max();

            if(firstSeq != c_SequenceNumber_Unknown && lastSeq != c_SequenceNumber_Unknown && lastSeq >= firstSeq)
            {
                this->incrementHBCount();
                CDRMessage::initCDRMsg(&m_cdrmessages.m_rtpsmsg_fullmsg);
				// TODO(Ricardo) This is a StatefulWriter in Reliable. Hast the FinalFlag be true? Check.
                RTPSMessageCreator::addMessageHeartbeat(&m_cdrmessages.m_rtpsmsg_fullmsg,m_guid.guidPrefix,
                        m_HBReaderEntityId, m_guid.entityId, firstSeq, lastSeq, m_heartbeatCount, true, false);
                std::vector<Locator_t>::iterator lit;
                for(lit = (*m_reader_iterator)->m_att.endpoint.unicastLocatorList.begin();lit!=(*m_reader_iterator)->m_att.endpoint.unicastLocatorList.end();++lit)
                    getRTPSParticipant()->sendSync(&m_cdrmessages.m_rtpsmsg_fullmsg,(Endpoint *)this,(*lit));
                for(lit = (*m_reader_iterator)->m_att.endpoint.multicastLocatorList.begin();lit!=(*m_reader_iterator)->m_att.endpoint.multicastLocatorList.end();++lit)
                    getRTPSParticipant()->sendSync(&m_cdrmessages.m_rtpsmsg_fullmsg,(Endpoint *)this,(*lit));
            }
        }
    }

    logInfo(RTPS_WRITER, "Finish sending unsent changes");
    return number_of_changes_sent;
}


/*
 *	MATCHED_READER-RELATED METHODS
 */
bool StatefulWriter::matched_reader_add(RemoteReaderAttributes& rdata)
{
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);
    if(rdata.guid == c_Guid_Unknown)
    {
        logError(RTPS_WRITER,"Reliable Writer need GUID_t of matched readers");
        return false;
    }

    // Check if it is already matched.
    for(std::vector<ReaderProxy*>::iterator it=matched_readers.begin();it!=matched_readers.end();++it)
    {
        std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);
        if((*it)->m_att.guid == rdata.guid)
        {
            logInfo(RTPS_WRITER, "Attempting to add existing reader" << endl);
            return false;
        }
    }

    ReaderProxy* rp = new ReaderProxy(rdata,m_times,this);
    std::vector<SequenceNumber_t> not_relevant_changes;

    for(std::vector<CacheChange_t*>::iterator cit = mp_history->changesBegin();
            cit != mp_history->changesEnd(); ++cit)
    {
        ChangeForReader_t changeForReader(*cit);

        if(rp->m_att.endpoint.durabilityKind >= TRANSIENT_LOCAL && this->getAttributes()->durabilityKind == TRANSIENT_LOCAL)
        {
            changeForReader.setRelevance(rp->rtps_is_relevant(*cit));
            if(!rp->rtps_is_relevant(*cit))
                not_relevant_changes.push_back(changeForReader.getSequenceNumber());
        }
        else
        {
            changeForReader.setRelevance(false);
            not_relevant_changes.push_back(changeForReader.getSequenceNumber());
        }

        changeForReader.setStatus(UNACKNOWLEDGED);
        rp->addChange(changeForReader);
    }

    // Send a initial heartbeat
    if(rp->mp_initialHeartbeat != nullptr) // It is reliable
        rp->mp_initialHeartbeat->restart_timer();

    // Send Gap
    if(!not_relevant_changes.empty())
        RTPSMessageGroup::send_Changes_AsGap(&m_cdrmessages, (RTPSWriter*)this,
                &not_relevant_changes,
                rp->m_att.guid.guidPrefix,
                rp->m_att.guid.entityId,
                &rp->m_att.endpoint.unicastLocatorList,
                &rp->m_att.endpoint.multicastLocatorList);

    // Always activate heartbeat period. We need a confirmation of the reader.
    // The state has to be updated.
    this->mp_periodicHB->restart_timer();


    matched_readers.push_back(rp);
    // Invalidate persistent iterator
    m_reader_iterator = matched_readers.begin();

    logInfo(RTPS_WRITER, "Reader Proxy "<< rp->m_att.guid<< " added to " << this->m_guid.entityId << " with "
            <<rp->m_att.endpoint.unicastLocatorList.size()<<"(u)-"
            <<rp->m_att.endpoint.multicastLocatorList.size()<<"(m) locators");

    return true;
}

bool StatefulWriter::matched_reader_remove(RemoteReaderAttributes& rdata)
{
    ReaderProxy *rproxy = nullptr;
    std::unique_lock<std::recursive_mutex> lock(*mp_mutex);

    for(std::vector<ReaderProxy*>::iterator it=matched_readers.begin();it!=matched_readers.end();++it)
    {
        std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);
        if((*it)->m_att.guid == rdata.guid)
        {
            logInfo(RTPS_WRITER, "Reader Proxy removed: " << (*it)->m_att.guid);
            rproxy = *it;
            matched_readers.erase(it);
            // Invalidate persistent iterator
            m_reader_iterator = matched_readers.begin();

            if(matched_readers.size()==0)
                this->mp_periodicHB->cancel_timer();

            break;
        }
    }

    lock.unlock();

    if(rproxy != nullptr)
    {
        delete rproxy;

        if(this->getAttributes()->durabilityKind == VOLATILE)
            clean_history();

        return true;
    }

    logInfo(RTPS_HISTORY,"Reader Proxy doesn't exist in this writer");
    return false;
}

bool StatefulWriter::matched_reader_is_matched(RemoteReaderAttributes& rdata)
{
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);
    for(std::vector<ReaderProxy*>::iterator it=matched_readers.begin();it!=matched_readers.end();++it)
    {
        std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);
        if((*it)->m_att.guid == rdata.guid)
        {
            return true;
        }
    }
    return false;
}

bool StatefulWriter::matched_reader_lookup(GUID_t& readerGuid,ReaderProxy** RP)
{
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);
    std::vector<ReaderProxy*>::iterator it;
    for(it=matched_readers.begin();it!=matched_readers.end();++it)
    {
        std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);
        if((*it)->m_att.guid == readerGuid)
        {
            *RP = *it;
            return true;
        }
    }
    return false;
}

bool StatefulWriter::is_acked_by_all(CacheChange_t* change)
{
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);

    if(change->writerGUID != this->getGuid())
    {
        logWarning(RTPS_WRITER,"The given change is not from this Writer");
        return false;
    }

    for(auto it = matched_readers.begin(); it!=matched_readers.end(); ++it)
    {
        if(!(*it)->change_is_acked(change->sequenceNumber))
        {
            logInfo(RTPS_WRITER, "Change " << change->sequenceNumber << " not acked." << endl);
            return false;
        }
    }
    return true;
}

bool StatefulWriter::wait_for_all_acked(const Duration_t& max_wait)
{
    std::unique_lock<std::recursive_mutex> lock(*mp_mutex);
    std::unique_lock<std::mutex> all_lock(*all_acked_mutex_);

    all_acked_ = true;

    for(auto it = matched_readers.begin(); it != matched_readers.end(); ++it)
    {
        std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);
        if((*it)->countChangesForReader() > 0)
        {
            all_acked_ = false;
            break;
        }
    }
    lock.unlock();

    if(!all_acked_)
    {
        std::chrono::microseconds max_w(::TimeConv::Time_t2MicroSecondsInt64(max_wait));
        if(all_acked_cond_->wait_for(all_lock, max_w)  == std::cv_status::no_timeout)
            all_acked_ = true;
    }

    return all_acked_;
}

void StatefulWriter::check_for_all_acked()
{
    std::unique_lock<std::recursive_mutex> lock(*mp_mutex);
    std::unique_lock<std::mutex> all_lock(*all_acked_mutex_);

    all_acked_ = true;

    for(auto it = matched_readers.begin(); it != matched_readers.end(); ++it)
    {
        std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);
        if((*it)->countChangesForReader() > 0)
        {
            all_acked_ = false;
            break;
        }
    }
    lock.unlock();

    if(all_acked_)
    {
        all_lock.unlock();
        all_acked_cond_->notify_all();
    }
}

bool StatefulWriter::clean_history(unsigned int max)
{
    logInfo(RTPS_WRITER, "Starting process clean_history for writer " << getGuid());
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);
    std::vector<CacheChange_t*> ackca;
    bool limit = (max != 0);

    for(std::vector<CacheChange_t*>::iterator cit = mp_history->changesBegin();
            cit != mp_history->changesEnd() && (!limit || ackca.size() < max); ++cit)
    {
        bool acknowledge = true;

        for(std::vector<ReaderProxy*>::iterator it = matched_readers.begin(); it != matched_readers.end(); ++it)
        {
            if(!(*it)->change_is_acked((*cit)->sequenceNumber))
            {
                acknowledge = false;
                break;
            }
        }

        if(acknowledge)
            ackca.push_back(*cit);
    }

    for(std::vector<CacheChange_t*>::iterator cit = ackca.begin();
            cit != ackca.end(); ++cit)
    {
        mp_history->remove_change_g(*cit);
    }

    return (ackca.size() > 0);
}

/*
 * PARAMETER_RELATED METHODS
 */
void StatefulWriter::updateAttributes(WriterAttributes& att)
{
    this->updateTimes(att.times);
}

void StatefulWriter::updateTimes(WriterTimes& times)
{
    std::lock_guard<std::recursive_mutex> guard(*mp_mutex);
    if(m_times.heartbeatPeriod != times.heartbeatPeriod)
    {
        this->mp_periodicHB->update_interval(times.heartbeatPeriod);
    }
    if(m_times.nackResponseDelay != times.nackResponseDelay)
    {
        for(std::vector<ReaderProxy*>::iterator it = this->matched_readers.begin();
                it!=this->matched_readers.end();++it)
        {
            std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);

            if((*it)->mp_nackResponse != nullptr) // It is reliable
                (*it)->mp_nackResponse->update_interval(times.nackResponseDelay);
        }
    }
    if(m_times.nackSupressionDuration != times.nackSupressionDuration)
    {
        for(std::vector<ReaderProxy*>::iterator it = this->matched_readers.begin();
                it!=this->matched_readers.end();++it)
        {
            std::lock_guard<std::recursive_mutex> rguard(*(*it)->mp_mutex);

            if((*it)->mp_nackSupression != nullptr) // It is reliable
                (*it)->mp_nackSupression->update_interval(times.nackSupressionDuration);
        }
    }
    m_times = times;
}

void StatefulWriter::add_flow_controller(std::unique_ptr<FlowController> controller)
{
    m_controllers.push_back(std::move(controller));
}

SequenceNumber_t StatefulWriter::next_sequence_number() const
{
	return mp_history->next_sequence_number();
}

void StatefulWriter::send_heartbeat_to(ReaderProxy& remoteReaderProxy)
{
    SequenceNumber_t firstSeq = this->get_seq_num_min();
    SequenceNumber_t lastSeq = this->get_seq_num_max();

    if(firstSeq == c_SequenceNumber_Unknown || lastSeq == c_SequenceNumber_Unknown)
    {
        firstSeq = mp_history->next_sequence_number();
        lastSeq = SequenceNumber_t(0, 0);
    }
    else
    {
        (void)firstSeq;
        assert(firstSeq <= lastSeq);
    }

    this->incrementHBCount();
    CDRMessage::initCDRMsg(&m_cdrmessages.m_rtpsmsg_fullmsg);
    // FinalFlag is always false because this is a StatefulWriter in Reliable.
    RTPSMessageCreator::addMessageHeartbeat(&m_cdrmessages.m_rtpsmsg_fullmsg, m_guid.guidPrefix, remoteReaderProxy.m_att.guid.guidPrefix,
            remoteReaderProxy.m_att.guid.entityId, m_guid.entityId,
            firstSeq, lastSeq, m_heartbeatCount, false, false);
    logInfo(RTPS_WRITER, m_guid.entityId << " Sending Heartbeat (" << firstSeq << " - " << lastSeq << ")");
    for (auto lit = remoteReaderProxy.m_att.endpoint.multicastLocatorList.begin(); lit != remoteReaderProxy.m_att.endpoint.multicastLocatorList.end(); ++lit)
        mp_RTPSParticipant->sendSync(&m_cdrmessages.m_rtpsmsg_fullmsg, (Endpoint *)this, (*lit));
    for (auto lit = remoteReaderProxy.m_att.endpoint.unicastLocatorList.begin(); lit != remoteReaderProxy.m_att.endpoint.unicastLocatorList.end(); ++lit)
        mp_RTPSParticipant->sendSync(&m_cdrmessages.m_rtpsmsg_fullmsg, (Endpoint *)this, (*lit));
}
