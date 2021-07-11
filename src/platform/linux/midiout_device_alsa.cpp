/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#include "midiout_device_alsa.h"
#include "output.h"
#include "system.h"

AlsaMidiOutDevice::AlsaMidiOutDevice() {
	int status = snd_seq_open(&midi_out, "default", SND_SEQ_OPEN_DUPLEX, 0);
	if (status < 0) {
		Output::Debug("ALSA: snd_seq_open failed: {}", snd_strerror(status));
		return;
	}

	snd_seq_client_info_t* client_info;
	snd_seq_port_info_t* port_info;
	snd_seq_client_info_alloca(&client_info);
	snd_seq_port_info_alloca(&port_info);

	// TODO: This simply enumerates all devices and connects to the last matching one
	// There should be a way to configure this
	// The last is usually timidity++ or fluidsynth, so this works
	std::string dst_client_name;
	std::string dst_port_name;
	bool candidate_found = false;

	snd_seq_client_info_set_client(client_info, -1);
	while (snd_seq_query_next_client(midi_out, client_info) == 0) {
		const char* client_name = snd_seq_client_info_get_name(client_info);
		int dst_client_candidate = snd_seq_client_info_get_client(client_info);
		snd_seq_port_info_set_client(port_info, dst_client_candidate);
		snd_seq_port_info_set_port(port_info, -1);

		while (snd_seq_query_next_port(midi_out, port_info) == 0) {
			unsigned int port_caps = snd_seq_port_info_get_capability(port_info);
			unsigned int port_type = snd_seq_port_info_get_type(port_info);
			const int type = SND_SEQ_PORT_TYPE_MIDI_GENERIC;
			const int cap = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;

			if ((port_type & type) == type && (port_caps & cap) == cap)	{
				// This is a suitable client
				dst_client = dst_client_candidate;
				dst_client_name = client_name;
				dst_port = snd_seq_port_info_get_port(port_info);
				dst_port_name = snd_seq_port_info_get_name(port_info);
				candidate_found = true;
			}
		}
	}

	if (!candidate_found) {
		Output::Debug("ALSA: No suitable client found");
		return;
	}

	Output::Debug("ALSA: Using client {}:{}:{}", dst_client, dst_port_name, dst_port);

	status = snd_seq_create_simple_port(midi_out, "Harmony",
		SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_APPLICATION);
	if (status < 0) {
		Output::Debug("ALSA: snd_seq_create_simple_port failed: {}", snd_strerror(status));
		return;
	}

	snd_seq_set_client_name(midi_out, GAME_TITLE);

	status = snd_seq_connect_to(midi_out, 0, dst_client, dst_port);
	if (status < 0) {
		Output::Debug("ALSA: snd_seq_connect_to failed: {}", snd_strerror(status));
		return;
	}

	queue = snd_seq_alloc_named_queue(midi_out, GAME_TITLE);
	if (queue < 0) {
		Output::Debug("ALSA: snd_seq_connect_to failed: {}", snd_strerror(queue));
		return;
	}

	status = snd_seq_start_queue(midi_out, queue, nullptr);
	if (status < 0) {
		Output::Debug("ALSA: snd_seq_connect_to failed: {}", snd_strerror(status));
		return;
	}

	works = true;
}

AlsaMidiOutDevice::~AlsaMidiOutDevice() {
	if (midi_out) {
		snd_seq_close(midi_out);
		midi_out = nullptr;
	}
}

void AlsaMidiOutDevice::SendMidiMessage(uint32_t message) {
	snd_seq_event_t evt = {};
	evt.source.port = 0;
	evt.queue = queue;
	evt.dest.client = dst_client;
	evt.dest.port = dst_port;

	unsigned int event = message & 0xF0;
	unsigned int channel = message & 0x0F;
	unsigned int param1 = (message >> 8) & 0x7F;
	unsigned int param2 = (message >> 16) & 0x7F;

	switch (event) {
		case MidiEvent_NoteOff:
			evt.type = SND_SEQ_EVENT_NOTEOFF;
			evt.data.note.channel  = channel;
			evt.data.note.note = param1;
			break;
		case MidiEvent_NoteOn:
			evt.type = SND_SEQ_EVENT_NOTEON;
			evt.data.note.channel = channel;
			evt.data.note.note = param1;
			evt.data.note.velocity = param2;
			break;
		case MidiEvent_KeyPressure:
			evt.type = SND_SEQ_EVENT_KEYPRESS;
			evt.data.note.channel = channel;
			evt.data.note.note = param1;
			evt.data.note.velocity = param2;
			break;
		case MidiEvent_Controller:
			evt.type = SND_SEQ_EVENT_CONTROLLER;
			evt.data.control.channel = channel;
			evt.data.control.param = param1;
			evt.data.control.value = param2;
			break;
		case MidiEvent_ProgramChange:
			evt.type = SND_SEQ_EVENT_PGMCHANGE;
			evt.data.control.channel = channel;
			evt.data.control.value = param1;
			break;
		case MidiEvent_ChannelPressure:
			evt.type = SND_SEQ_EVENT_CHANPRESS;
			evt.data.control.channel = channel;
			evt.data.control.value = param1;
			break;
		case MidiEvent_PitchBend:
			evt.type = SND_SEQ_EVENT_PITCHBEND;
			evt.data.control.channel = channel;
			evt.data.control.value = ((param2 & 0x7F) << 7) | (param1 & 0x7F);
			break;
		default:
			break;
	}

	int status = snd_seq_event_output_direct(midi_out, &evt);
	if (status < 0) {
		Output::Debug("ALSA snd_seq_event_output_direct failed: {}", snd_strerror(status));
	}
}

void AlsaMidiOutDevice::SendSysExMessage(const void* data, size_t size) {
	snd_seq_event_t evt = {};
	evt.source.port = 0;
	evt.queue = queue;
	evt.dest.client = dst_client;
	evt.dest.port = dst_port;
	evt.type = SND_SEQ_EVENT_SYSEX;

	evt.flags |= SND_SEQ_EVENT_LENGTH_VARIABLE;
	evt.data.ext.ptr = const_cast<void*>(data);
	evt.data.ext.len = size;

	int status = snd_seq_event_output_direct(midi_out, &evt);
	if (status < 0) {
		Output::Debug("ALSA SysEx snd_seq_event_output_direct failed: {}", snd_strerror(status));
	}
}

void AlsaMidiOutDevice::SendMidiReset() {
	unsigned char gm_reset[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
	SendSysExMessage(gm_reset, sizeof(gm_reset));
}

std::string AlsaMidiOutDevice::GetName() {
	return "ALSA Midi";
}

bool AlsaMidiOutDevice::IsInitialized() const {
	return works;
}
