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
 * @file ListenResourceImpl.cpp
 *
 */

#include "ListenResourceImpl.h"
#include <fastrtps/rtps/resources/ListenResource.h>
#include <fastrtps/rtps/messages/MessageReceiver.h>
#include "../participant/RTPSParticipantImpl.h"

#include <functional>
#include <thread>
#include <system_error>

#include <fastrtps/utils/IPFinder.h>
#include <fastrtps/log/Log.h>

#define IDSTRING "(ID:"<<this->mp_listenResource->m_ID<<") "<<

using asio::ip::udp;

namespace eprosima {
namespace fastrtps{
namespace rtps {


typedef std::vector<RTPSWriter*>::iterator Wit;
typedef std::vector<RTPSReader*>::iterator Rit;

ListenResourceImpl::MessageReceiver(ListenResource* LR):
																								mp_RTPSParticipantImpl(nullptr),
																								mp_listenResource(LR),
																								mp_thread(nullptr),
																								m_listen_socket(m_io_service),
                                                                                                runningAsync_(false), stopped_(false)

{

}

ListenResourceImpl::~ListenResourceImpl()
{
	if(mp_thread !=nullptr)
	{
        std::unique_lock<std::mutex> lock(mutex_);
        if(runningAsync_)
            cond_.wait(lock);

		logInfo(RTPS_MSG_IN,IDSTRING"Removing listening thread " << mp_thread->get_id() <<" socket: "
				<<m_listen_socket.local_endpoint() <<  " locators: " << mv_listenLoc);

        m_listen_socket.cancel();
		m_listen_socket.close();

		m_io_service.stop();
        stopped_ = true;
        lock.unlock();

		logInfo(RTPS_MSG_IN,"Joining with thread");
		mp_thread->join();

		delete(mp_thread);
		logInfo(RTPS_MSG_IN,"Listening thread closed succesfully");
	}
}

bool ListenResourceImpl::isListeningTo(const Locator_t& loc)
{
	if(IsAddressDefined(loc))
	{
		LocatorList_t locList = mv_listenLoc;
		return locList.contains(loc);
	}
	else
	{
		if(loc.port == mv_listenLoc.begin()->port)
			return true;
	}
	return false;
}



void ListenResourceImpl::newCDRMessage(const std::error_code& err, std::size_t msg_size)
{

	if(!err)
	{
        std::unique_lock<std::mutex> lock(mutex_);
        if(stopped_)
            return;
        runningAsync_ = true;
        lock.unlock();

		mp_listenResource->setMsgRecMsgLength((uint32_t)msg_size);

		if(msg_size == 0)
			return;
		try{
			logInfo(RTPS_MSG_IN,IDSTRING mp_listenResource->mp_receiver->m_rec_msg.length
					<< " bytes FROM: " << m_sender_endpoint << " TO: " << m_listen_socket.local_endpoint());

			//Get address into Locator
			m_senderLocator.port = m_sender_endpoint.port();
			LOCATOR_ADDRESS_INVALID(m_senderLocator.address);
			if(m_sender_endpoint.address().is_v4())
			{
				for(int i=0;i<4;i++)
				{
					m_senderLocator.address[i+12] = m_sender_endpoint.address().to_v4().to_bytes()[i];
				}
			}
			else
			{
				for(int i=0;i<16;i++)
				{
					m_senderLocator.address[i] = m_sender_endpoint.address().to_v6().to_bytes()[i];
				}
			}
		}
		catch(std::system_error const& e)
		{
			logError(RTPS_MSG_IN,"Error: " << e.message());
            lock.lock();
            runningAsync_ = false;
            lock.unlock();
            cond_.notify_one();
			this->putToListen();
			return;
		}
        
		try
		{
			mp_listenResource->mp_receiver->processCDRMsg(mp_RTPSParticipantImpl->getGuid().guidPrefix,
					&m_senderLocator,
					&mp_listenResource->mp_receiver->m_rec_msg);
		}
		catch(int e)
		{
			logError(RTPS_MSG_IN,IDSTRING"Error processing message: " << e);

		}

		logInfo(RTPS_MSG_IN,IDSTRING " Message of size "<< mp_listenResource->mp_receiver->m_rec_msg.length <<" processed" );
        lock.lock();
        runningAsync_ = false;
        lock.unlock();
        cond_.notify_one();
		this->putToListen();
	}
	else if(err == asio::error::operation_aborted)
	{
		logInfo(RTPS_MSG_IN,IDSTRING"Operation in listening socket aborted...");
		return;
	}
	else
	{
		//CDRMessage_t msg;
		logInfo(RTPS_MSG_IN,IDSTRING"Msg processed, Socket async receive put again to listen ");
		this->putToListen();
	}
}

// NOTE This version allways listen in ANY.
void ListenResourceImpl::getLocatorAddresses(Locator_t& loc, bool isMulti)
{

    logInfo(RTPS_MSG_IN,"Defined Locator IP with 0s (listen to all interfaces), listening to all interfaces");
    LocatorList_t myIP;

    if(loc.kind == LOCATOR_KIND_UDPv4)
    {
        IPFinder::getIP4Address(&myIP);
        m_listen_endpoint.address(asio::ip::address_v4());
    }
    else if(loc.kind == LOCATOR_KIND_UDPv6)
    {
        IPFinder::getIP6Address(&myIP);
        m_listen_endpoint.address(asio::ip::address_v6());
    }

    if(!isMulti)
    {
        for(auto lit = myIP.begin();lit!= myIP.end();++lit)
        {
            lit->port = loc.port;
            mv_listenLoc.push_back(*lit);
        }
    }
    else
    {
        mv_listenLoc.push_back(loc);
    }

	m_listen_endpoint.port((uint16_t)loc.port);
}


bool ListenResourceImpl::init_thread(RTPSParticipantImpl* pimpl,Locator_t& loc, uint32_t listenSocketSize, bool isMulti, bool isFixed)
{
	this->mp_RTPSParticipantImpl = pimpl;
	if(loc.kind == LOCATOR_KIND_INVALID)
		return false;

	getLocatorAddresses(loc, isMulti);
	logInfo(RTPS_MSG_IN,"Initializing in : "<<mv_listenLoc);
	std::asio::ip::address multiaddress;
	//OPEN THE SOCKET:
	m_listen_socket.open(m_listen_endpoint.protocol());
	m_listen_socket.set_option(asio::socket_base::receive_buffer_size(listenSocketSize));
	if(isMulti)
	{
		m_listen_socket.set_option( asio::ip::udp::socket::reuse_address( true ) );
		m_listen_socket.set_option( asio::ip::multicast::enable_loopback( true ) );

        if(loc.kind == LOCATOR_KIND_UDPv4)
        {
            multiaddress = asio::ip::address_v4::from_string(loc.to_IP4_string().c_str());
        }
        else if(loc.kind == LOCATOR_KIND_UDPv6)
        {
			asio::ip::address_v6::bytes_type bt;
			for (uint8_t i = 0; i < 16; ++i)
				bt[i] = loc.address[i];
            multiaddress = asio::ip::address_v6(bt);
        }
	}

	if(isFixed)
	{
		try
		{
			m_listen_socket.bind(m_listen_endpoint);
		}
		catch (std::system_error const& e)
		{
			logError(RTPS_MSG_IN,"Error: " << e.message() << " : " << m_listen_endpoint);
			return false;
		}
	}
	else
	{
		bool binded = false;
		for(uint8_t i = 0; i < 100; ++i) //TODO make it configurable by user.
		{
			m_listen_endpoint.port(m_listen_endpoint.port()+i);
			try
			{
				m_listen_socket.bind(m_listen_endpoint);
				binded = true;
				break;
			}
			catch(std::system_error const& )
			{
				logInfo(RTPS_MSG_IN,"Tried port "<< m_listen_endpoint.port() << ", trying next...");
			}
		}

		if(!binded)
		{
			logError(RTPS_MSG_IN,"Tried 100 ports and none was working, last tried: "<< m_listen_endpoint);
			return false;
		}
		else
		{
			for(auto lit = mv_listenLoc.begin();lit!=mv_listenLoc.end();++lit)
				lit->port = m_listen_endpoint.port();
		}
	}

	asio::socket_base::receive_buffer_size option;
	m_listen_socket.get_option(option);
	logInfo(RTPS_MSG_IN,"Created: " << m_listen_endpoint<< " || Listen buffer size: " << option.value());
	if(isMulti)
	{
		joinMulticastGroup(multiaddress);
	}
	this->putToListen();
	mp_thread = new std::thread(&ListenResourceImpl::run_io_service,this);
	mp_RTPSParticipantImpl->ResourceSemaphoreWait();
	return true;
}

void ListenResourceImpl::joinMulticastGroup(asio::ip::address& addr)
{
	logInfo(RTPS_MSG_IN,"Joining group: "<<mv_listenLoc);
	try
	{
		LocatorList_t loclist;
		if(m_listen_endpoint.address().is_v4())
		{
			IPFinder::getIP4Address(&loclist);
			for(LocatorListIterator it=loclist.begin();it!=loclist.end();++it)
				m_listen_socket.set_option( asio::ip::multicast::join_group(addr.to_v4(),
						asio::ip::address_v4::from_string(it->to_IP4_string())) );
		}
		else if(m_listen_endpoint.address().is_v6())
		{
			IPFinder::getIP6Address(&loclist);
			int index = 0;
			for(LocatorListIterator it=loclist.begin();it!=loclist.end();++it)
			{
				//			asio::ip::address_v6::bytes_type bt;
				//			for (uint8_t i = 0; i < 16;++i)
				//				bt[i] = it->address[i];
				m_listen_socket.set_option(
						asio::ip::multicast::join_group(
								addr.to_v6(),index
						));
				++index;
			}
		}
	}
	catch(std::system_error const& e)
	{
		logError(RTPS_MSG_IN,"Error: "<< e.message());
	}
}

void ListenResourceImpl::putToListen()
{
	CDRMessage::initCDRMsg(&mp_listenResource->mp_receiver->m_rec_msg);
	try
	{
		m_listen_socket.async_receive_from(
				asio::buffer((void*)mp_listenResource->mp_receiver->m_rec_msg.buffer,
						mp_listenResource->mp_receiver->m_rec_msg.max_size),
						m_sender_endpoint,
						std::bind(&ListenResourceImpl::newCDRMessage, this,
								std::placeholders::_1,
								std::placeholders::_2));
	}
	catch(std::system_error const& e)
	{
		logError(RTPS_MSG_IN,"Error: "<< e.message());
	}
}

void ListenResourceImpl::run_io_service()
{
	try
	{
		logInfo(RTPS_MSG_IN,"Thread: " << std::this_thread::get_id() << " listening in IP: " << m_listen_socket.local_endpoint()) ;

		mp_RTPSParticipantImpl->ResourceSemaphorePost();

		this->m_io_service.run();
	}
	catch(std::system_error const& e)
	{
		logError(RTPS_MSG_IN,"Error: "<<e.message());
	}
}
}
}
}
