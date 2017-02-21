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

/*
 * ResourceSendImpl.cpp
 *
 */

#include "ResourceSendImpl.h"
#include <fastrtps/rtps/common/CDRMessage_t.h>
#include <fastrtps/log/Log.h>

#include <fastrtps/utils/IPFinder.h>

using asio::ip::udp;

namespace eprosima {
namespace fastrtps{
namespace rtps {


static const int MAX_BIND_TRIES = 100;

ResourceSendImpl::ResourceSendImpl() :
										m_useIP4(true),
										m_useIP6(true),
										//m_send_socket_v4(m_send_service),
										//m_send_socket_v6(m_send_service),
										m_bytes_sent(0),
										m_send_next(true),
										mp_mutex(new std::recursive_mutex())
{

}

bool ResourceSendImpl::initSend(RTPSParticipantImpl* /*pimpl*/, const Locator_t& loc, uint32_t sendsockBuffer, bool useIP4, bool useIP6)
{
	m_useIP4 = useIP4;
	m_useIP6 = useIP6;

	std::vector<IPFinder::info_IP> locNames;
	IPFinder::getIPs(&locNames);
	asio::socket_base::send_buffer_size option;
	bool not_bind = true;
	bool initialized = false;
	int bind_tries = 0;
	for (auto ipit = locNames.begin(); ipit != locNames.end(); ++ipit)
	{
		if (ipit->type == IPFinder::IP4 && m_useIP4)
		{
			mv_sendLocator_v4.push_back(loc);
			auto sendLocv4 = mv_sendLocator_v4.back();
			sendLocv4.port = loc.port;
			//OPEN SOCKETS:
			mv_send_socket_v4.push_back(new asio::ip::udp::socket(m_send_service));
			auto sendSocketv4 = mv_send_socket_v4.back();
			sendSocketv4->open(asio::ip::udp::v4());
			sendSocketv4->set_option(asio::socket_base::send_buffer_size(sendsockBuffer));
			bind_tries = 0;
			udp::endpoint send_endpoint;
			while (not_bind && bind_tries < MAX_BIND_TRIES)
			{
				send_endpoint = udp::endpoint(asio::ip::address_v4::from_string(ipit->name), (uint16_t)sendLocv4.port);
				try{
					sendSocketv4->bind(send_endpoint);
					not_bind = false;
				}
				#pragma warning(disable:4101)
				catch (std::system_error const& e)
				{
					logInfo(RTPS_MSG_OUT, "UDPv4 Error binding endpoint: (" << send_endpoint << ")" << " with msg: "<<e.message() );
					sendLocv4.port++;
				}
				++bind_tries;
			}
			if(!not_bind)
			{
				sendSocketv4->get_option(option);
				logInfo(RTPS_MSG_OUT, "UDPv4: " << sendSocketv4->local_endpoint() << "|| State: " << sendSocketv4->is_open() <<
						" || buffer size: " << option.value());
				initialized = true;
			}
			else
			{
				logWarning(RTPS_MSG_OUT,"UDPv4: Maxmimum Number of tries while binding in this interface: "<<send_endpoint)
				mv_sendLocator_v4.erase(mv_sendLocator_v4.end()-1);
				delete(*(mv_send_socket_v4.end()-1));
				mv_send_socket_v4.erase(mv_send_socket_v4.end()-1);
			}
			not_bind = true;

		}
		else if (ipit->type == IPFinder::IP6 && m_useIP6)
		{
			mv_sendLocator_v6.push_back(loc);
			auto sendLocv6 = mv_sendLocator_v6.back();
			sendLocv6.port = loc.port;
			//OPEN SOCKETS:
			mv_send_socket_v6.push_back(new asio::ip::udp::socket(m_send_service));
			auto sendSocketv6 = mv_send_socket_v6.back();
			sendSocketv6->open(asio::ip::udp::v6());
			sendSocketv6->set_option(asio::socket_base::send_buffer_size(sendsockBuffer));
			bind_tries = 0;
			udp::endpoint send_endpoint;
			while (not_bind && bind_tries < MAX_BIND_TRIES)
			{
				asio::ip::address_v6::bytes_type bt;
				for (uint8_t i = 0; i < 16;++i)
					bt[i] = ipit->locator.address[i];
				asio::ip::address_v6 addr = asio::ip::address_v6(bt);
				addr.scope_id(ipit->scope_id);
				send_endpoint = udp::endpoint(addr, (uint16_t)sendLocv6.port);
				//cout << "IP6 ADDRESS: "<< send_endpoint << endl;
				try{
					sendSocketv6->bind(send_endpoint);
					not_bind = false;
				}
                #pragma warning(disable:4101)
				catch (std::system_error const& e)
				{
					logInfo(RTPS_MSG_OUT, "UDPv6 Error binding endpoint: (" << send_endpoint << ")"<< " with msg: "<<e.message() );
					sendLocv6.port++;
				}
				++bind_tries;
			}
			if(!not_bind)
			{
				sendSocketv6->get_option(option);
				logInfo(RTPS_MSG_OUT, "UDPv6: " << sendSocketv6->local_endpoint() << "|| State: " << sendSocketv6->is_open() <<
						" || buffer size: " << option.value());
				initialized = true;
			}
			else
			{
				logWarning(RTPS_MSG_OUT,"UDPv6: Maxmimum Number of tries while binding in this endpoint: "<<send_endpoint);
				mv_sendLocator_v6.erase(mv_sendLocator_v6.end()-1);
				delete(*(mv_send_socket_v6.end()-1));
				mv_send_socket_v6.erase(mv_send_socket_v6.end()-1);
			}
			not_bind = true;
		}
	}

	return initialized;
}


ResourceSendImpl::~ResourceSendImpl()
{
	logInfo(RTPS_MSG_OUT,"");
	for (auto it = mv_send_socket_v4.begin(); it != mv_send_socket_v4.end(); ++it)
		(*it)->close();
	for (auto it = mv_send_socket_v6.begin(); it != mv_send_socket_v6.end(); ++it)
		(*it)->close();
	m_send_service.stop();
	for (auto it = mv_send_socket_v4.begin(); it != mv_send_socket_v4.end(); ++it)
		delete(*it);
	for (auto it = mv_send_socket_v6.begin(); it != mv_send_socket_v6.end(); ++it)
		delete(*it);
	delete(mp_mutex);
}

void ResourceSendImpl::sendSync(CDRMessage_t* msg, const Locator_t& loc)
{
	std::lock_guard<std::recursive_mutex> guard(*this->mp_mutex);
	if(loc.port == 0)
		return;
	if(loc.kind == LOCATOR_KIND_UDPv4 && m_useIP4)
	{
		asio::ip::address_v4::bytes_type addr;
		for(uint8_t i = 0; i < 4; ++i)
			addr[i] = loc.address[12 + i];
        asio::ip::udp::endpoint send_endpoint_v4 = udp::endpoint(asio::ip::address_v4(addr), (uint16_t)loc.port);
		for (auto sockit = mv_send_socket_v4.begin(); sockit != mv_send_socket_v4.end(); ++sockit)
		{
			logInfo(RTPS_MSG_OUT,"UDPv4: " << msg->length << " bytes TO endpoint: " << send_endpoint_v4
					<< " FROM " << (*sockit)->local_endpoint());
			if(send_endpoint_v4.port()>0)
			{
				m_bytes_sent = 0;
				if(m_send_next)
				{
					try {
						m_bytes_sent = (*sockit)->send_to(asio::buffer((void*)msg->buffer, msg->length), send_endpoint_v4);
					}
					catch (const std::exception& error) {
						// Should print the actual error message
						logWarning(RTPS_MSG_OUT, "Error: " << error.what());
					}

				}
				else
				{
					m_send_next = true;
				}
				logInfo (RTPS_MSG_OUT,"SENT " << m_bytes_sent);
			}
			else
				logWarning(RTPS_MSG_OUT,"Port invalid");
		}
	}
	else if(loc.kind == LOCATOR_KIND_UDPv6 && m_useIP6)
	{
		asio::ip::address_v6::bytes_type addr;
		for(uint8_t i = 0; i < 16; i++)
			addr[i] = loc.address[i];
        asio::ip::udp::endpoint send_endpoint_v6 = udp::endpoint(asio::ip::address_v6(addr), (uint16_t)loc.port);
		for (auto sockit = mv_send_socket_v6.begin(); sockit != mv_send_socket_v6.end(); ++sockit)
		{
			logInfo(RTPS_MSG_OUT, "UDPv6: " << msg->length << " bytes TO endpoint: "
					<< send_endpoint_v6 << " FROM " << (*sockit)->local_endpoint());
			if (send_endpoint_v6.port()>0)
			{
				m_bytes_sent = 0;
				if (m_send_next)
				{
					try {
						m_bytes_sent = (*sockit)->send_to(asio::buffer((void*)msg->buffer, msg->length), send_endpoint_v6);
					}
					catch (const std::exception& error) {
						// Should print the actual error message
						logWarning(RTPS_MSG_OUT, "Error: " << error.what());
					}

				}
				else
				{
					m_send_next = true;
				}
				logInfo(RTPS_MSG_OUT, "SENT " << m_bytes_sent);
			}
			else
				logWarning(RTPS_MSG_OUT, "Port invalid");
		}
	}
	else
	{
		logInfo(RTPS_MSG_OUT,"Destination "<< loc << " not valid for this ListenResource");
	}

}

std::recursive_mutex* ResourceSendImpl::getMutex() {return mp_mutex;}

}
} /* namespace rtps */
} /* namespace eprosima */
