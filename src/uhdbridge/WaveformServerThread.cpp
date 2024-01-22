/***********************************************************************************************************************
*                                                                                                                      *
* uhdbridge                                                                                                            *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg                                                                          *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Waveform data thread (data plane traffic only, no control plane SCPI)
 */
#include "uhdbridge.h"
#include <string.h>

using namespace std;

volatile bool g_waveformThreadQuit = false;

void WaveformServerThread()
{
#ifdef __linux__
	pthread_setname_np(pthread_self(), "WaveformThread");
#endif

	Socket client = g_dataSocket.Accept();
	LogVerbose("Client connected to data plane socket\n");

	if(!client.IsValid())
		return;
	if(!client.DisableNagle())
		LogWarning("Failed to disable Nagle on socket, performance may be poor\n");

	while(!g_waveformThreadQuit)
	{
		//wait if trigger not armed
		if(!g_triggerArmed)
		{
			this_thread::sleep_for(chrono::microseconds(1000));
			continue;
		}

		LogDebug("trigger armed\n");

		auto config = g_sdr->get_pp_string();
		LogDebug("%s\n", config.c_str());

		//Snapshot some variables when we armed the trigger
		bool oneshot = g_triggerOneShot;

		//TODO: check LO lock detect

		//Make the streamer
		//For now, always get fp32 data out and use int16 over the wire
		//For now, only one channel is supported
		uhd::stream_args_t args("fc32", "sc16");
		vector<size_t> channels;
		channels.push_back(0);
		args.channels = channels;
		uhd::rx_streamer::sptr rx = g_sdr->get_rx_stream(args);

		//TODO: play with STREAM_MODE_START_CONTINUOUS
		//For now, grab a constant number of samples each "trigger" then stop (so acquisitions may not be gap-free)
		while(g_triggerArmed)
		{
			LogDebug("starting block\n");

			//Snapshot some values for this block
			size_t blocksize = g_rxBlockSize;
			int64_t rate = g_rxRate;

			//Make RX buffer
			vector<complex<float>> buf(blocksize);

			//Start streaming
			uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
			cmd.num_samps = blocksize;
			cmd.stream_now = true;
			cmd.time_spec = uhd::time_spec_t();
			rx->issue_stream_cmd(cmd);

			//Receive the data
			uhd::rx_metadata_t meta;
			size_t nrx = 0;
			while(true)
			{
				size_t rxsize = rx->recv(&buf.front(), buf.size(), meta, 5.0, false);
				nrx += rxsize;
				bool err = true;

				switch(meta.error_code)
				{
					case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
						LogError("timeout\n");
						break;

					case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
						LogError("overflow\n");
						break;

					case uhd::rx_metadata_t::ERROR_CODE_NONE:
						LogDebug("got %zu samples for total of %zu\n", rxsize, nrx);
						err = false;
						break;

					default:
						LogDebug("unknown error\n");
				}

				if(nrx >= blocksize)
					break;

				if(err)
					break;
			}
			LogDebug("recv done, got %zu of %zu requested samples\n", nrx, blocksize);

			//Send the data out to the client
			//Just the waveform size then the sample data
			uint64_t len = nrx;
			if(!client.SendLooped((uint8_t*)&len, sizeof(len)))
				break;
			if(!client.SendLooped((uint8_t*)&rate, sizeof(len)))
				break;
			if(!client.SendLooped((uint8_t*)&buf[0], nrx * sizeof(complex<float>)))
				break;

			//If one shot, stop
			if(oneshot)
			{
				g_triggerArmed = false;
				break;
			}
		}
	}

	LogDebug("Client disconnected from data plane socket\n");

	//Clean up
}
