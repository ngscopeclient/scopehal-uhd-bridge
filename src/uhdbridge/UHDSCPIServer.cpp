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
	@brief SCPI server. Control plane traffic only, no waveform data.

	SCPI commands supported:

		*IDN?
			Returns a standard SCPI instrument identification string

		REFCLK [internal|external]
			Sets the reference clock for the instrument

		RXGAIN [dB]
			Sets receiver gain

		RXBW [Hz]
			Sets receiver bandwidth

		RXFREQ [Hz]
			Sets receiver center frequency
 */

#include "uhdbridge.h"
#include "UHDSCPIServer.h"
#include <string.h>
#include <math.h>

#define __USE_MINGW_ANSI_STDIO 1 // Required for MSYS2 mingw64 to support format "%z" ...

using namespace std;

mutex g_mutex;

bool g_triggerOneShot = false;

size_t g_rxBlockSize = 0;
int64_t g_centerFrequency = 0;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

UHDSCPIServer::UHDSCPIServer(ZSOCKET sock)
	: BridgeSCPIServer(sock)
{
	//Select sub device (TODO: expose this somehow)
	g_sdr->set_rx_subdev_spec(string("A:A"));

	//Select antenna to use (TODO: expose this somehow)
	g_sdr->set_rx_antenna("TX/RX");
}

UHDSCPIServer::~UHDSCPIServer()
{
	LogVerbose("Client disconnected\n");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command parsing

bool UHDSCPIServer::OnQuery(
	const string& line,
	const string& subject,
	const string& cmd)
{
	if(BridgeSCPIServer::OnQuery(line, subject, cmd))
		return true;
	/*
	else if(cmd == "POINTS")
		SendReply(to_string(g_numPixels));
	*/
	else
	{
		LogDebug("Unrecognized query received: %s\n", line.c_str());
	}
	return false;
}

string UHDSCPIServer::GetMake()
{
	if(g_model.find("ANT") == 0)
		return "Microphase";
	return "Ettus Research";
}

string UHDSCPIServer::GetModel()
{
	return g_model;
}

string UHDSCPIServer::GetSerial()
{
	return g_serial;
}

string UHDSCPIServer::GetFirmwareVersion()
{
	return "1.0";
}

size_t UHDSCPIServer::GetAnalogChannelCount()
{
	//TODO: support additional channels
	return 1;
}

vector<size_t> UHDSCPIServer::GetSampleRates()
{
	vector<size_t> rates;

	//List of possible sample rates is probably going to be super long!
	//Do at least 500 kHz steps to keep the dropdown sane
	auto range = g_sdr->get_rx_rates();
	float step = range.step();
	float minstep = 500000;
	if(step < minstep)
		step *= ceil(minstep / step);

	for(float f = range.stop(); f >= range.start(); f -= step)
	{
		//TODO: Round the rate as close as possible to a nice round number
		rates.push_back(f);
	}

	reverse(rates.begin(), rates.end());

	return rates;
}

vector<size_t> UHDSCPIServer::GetSampleDepths()
{
	vector<size_t> depths;

	//UHD doesn't seem to have a cap on max memory size.
	//Fill with a bunch of reasonable buffer sizes.
	const int64_t k = 1000;
	const int64_t m = k * k;
	depths.push_back(10 * k);
	depths.push_back(20 * k);
	depths.push_back(50 * k);
	depths.push_back(100 * k);
	depths.push_back(200 * k);
	depths.push_back(500 * k);
	depths.push_back(1 * m);
	depths.push_back(2 * m);
	depths.push_back(5 * m);
	depths.push_back(10 * m);
	depths.push_back(20 * m);
	depths.push_back(50 * m);
	depths.push_back(100 * m);


	return depths;
}

bool UHDSCPIServer::OnCommand(
	const string& line,
	const string& subject,
	const string& cmd,
	const vector<string>& args)
{
	if(BridgeSCPIServer::OnCommand(line, subject, cmd, args))
		return true;

	else if(cmd == "REFCLK")
	{
		LogDebug("set refclk\n");
		lock_guard<mutex> lock(g_mutex);
		g_sdr->set_clock_source(args[0]);
	}

	//TODO: support >1 channel
	else if(cmd == "RXGAIN")
	{
		lock_guard<mutex> lock(g_mutex);

		double requested = stod(args[0]);
		g_sdr->set_rx_gain(requested);
		auto actual = g_sdr->get_rx_gain();

		LogDebug("set rx gain: requested %.1f dB, got %.1f dB\n", requested, actual);
	}
	else if(cmd == "RXBW")
	{
		lock_guard<mutex> lock(g_mutex);

		double requested = stod(args[0]);
		g_sdr->set_rx_bandwidth(requested);
		auto actual = g_sdr->get_rx_bandwidth();

		LogDebug("set rx bandwidth: requested %.1f MHz, got %.1f MHz\n", requested*1e-6, actual*1e-6);
	}
	else if(cmd == "RXFREQ")
	{
		lock_guard<mutex> lock(g_mutex);

		double requested = stod(args[0]);
		uhd::tune_request_t tune(requested);
		g_sdr->set_rx_freq(tune);
		auto actual = g_sdr->get_rx_freq();

		g_centerFrequency = actual;

		LogDebug("set rx frequency: requested %.1f MHz, got %.1f MHz\n", requested*1e-6, actual*1e-6);
	}

	else
		LogError("Unrecognized command %s\n", line.c_str());

	return true;
}

bool UHDSCPIServer::GetChannelID(const string& /*subject*/, size_t& id_out)
{
	id_out = 0;
	return true;
}

BridgeSCPIServer::ChannelType UHDSCPIServer::GetChannelType(size_t /*channel*/)
{
	return CH_ANALOG;
}

void UHDSCPIServer::AcquisitionStart(bool oneShot)
{
	/*g_triggerArmed = true;
	g_triggerOneShot = oneShot;*/
}

void UHDSCPIServer::AcquisitionForceTrigger()
{
	//g_triggerArmed = true;
}

void UHDSCPIServer::AcquisitionStop()
{
	//g_triggerArmed = false;
}

void UHDSCPIServer::SetChannelEnabled(size_t /*chIndex*/, bool /*enabled*/)
{
}

void UHDSCPIServer::SetAnalogCoupling(size_t /*chIndex*/, const std::string& /*coupling*/)
{
}

void UHDSCPIServer::SetAnalogRange(size_t /*chIndex*/, double /*range_V*/)
{
}

void UHDSCPIServer::SetAnalogOffset(size_t /*chIndex*/, double /*offset_V*/)
{
}

void UHDSCPIServer::SetDigitalThreshold(size_t /*chIndex*/, double /*threshold_V*/)
{
}

void UHDSCPIServer::SetDigitalHysteresis(size_t /*chIndex*/, double /*hysteresis*/)
{
}

void UHDSCPIServer::SetSampleRate(uint64_t rate_hz)
{
	g_sdr->set_rx_rate(rate_hz);

	auto actual = g_sdr->get_rx_rate();
	LogDebug("set rx bandwidth: requested %.2f Msps, got %.2f Msps\n", rate_hz*1e-6, actual*1e-6);
}

void UHDSCPIServer::SetSampleDepth(uint64_t depth)
{
	g_rxBlockSize = depth;
}

void UHDSCPIServer::SetTriggerDelay(uint64_t /*delay_fs*/)
{
}

void UHDSCPIServer::SetTriggerSource(size_t /*chIndex*/)
{
}

void UHDSCPIServer::SetTriggerLevel(double /*level_V*/)
{
}

void UHDSCPIServer::SetTriggerTypeEdge()
{
	//all triggers are edge, nothing to do here until we start supporting other trigger types
}

bool UHDSCPIServer::IsTriggerArmed()
{
	//return g_triggerArmed;
	return true;
}

void UHDSCPIServer::SetEdgeTriggerEdge(const string& /*edge*/)
{

}
