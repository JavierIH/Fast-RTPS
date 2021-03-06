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
 * @file Participant.cpp
 *
 */

#include <fastrtps/participant/Participant.h>

#include "ParticipantImpl.h"

namespace eprosima {
namespace fastrtps {

Participant::Participant():
		mp_impl(nullptr)
{

}

Participant::~Participant() {
	// TODO Auto-generated destructor stub
}

const GUID_t& Participant::getGuid() const
{
	return mp_impl->getGuid();
}

const ParticipantAttributes& Participant::getAttributes()
{
	return mp_impl->getAttributes();
}

bool Participant::newRemoteEndpointDiscovered(const GUID_t& partguid, uint16_t endpointId,
	EndpointKind_t kind)
{
	return mp_impl->newRemoteEndpointDiscovered(partguid, endpointId, kind);
}

std::pair<StatefulReader*,StatefulReader*> Participant::getEDPReaders(){
	std::pair<StatefulReader *,StatefulReader*> buffer;

	return mp_impl->getEDPReaders();
}

std::vector<std::string> Participant::getParticipantNames(){
  return mp_impl->getParticipantNames();
}

int Participant::get_no_publishers(char *target_topic){
	return mp_impl->get_no_publishers(target_topic);
}

int Participant::get_no_subscribers(char *target_topic){
	return mp_impl->get_no_subscribers(target_topic);
}

} /* namespace pubsub */
} /* namespace eprosima */
