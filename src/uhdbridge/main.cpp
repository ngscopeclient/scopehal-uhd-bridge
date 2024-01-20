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
	@brief Program entry point
 */

#include "uhdbridge.h"
//#include "AseqSCPIServer.h"
#include <signal.h>

using namespace std;

vector<string> explode(const string& str, char separator);
string Trim(const string& str);

void help();

void help()
{
	fprintf(stderr,
			"uhdbridge [general options] [logger options]\n"
			"\n"
			"  [general options]:\n"
			"    --help                        : this message...\n"
			"    --scpi-port port              : specifies the SCPI control plane port (default 5025)\n"
			"    --waveform-port port          : specifies the binary waveform data port (default 5026)\n"
			"\n"
			"  [logger options]:\n"
			"    levels: ERROR, WARNING, NOTICE, VERBOSE, DEBUG\n"
			"    --quiet|-q                    : reduce logging level by one step\n"
			"    --verbose                     : set logging level to VERBOSE\n"
			"    --debug                       : set logging level to DEBUG\n"
			"    --trace <classname>|          : name of class with tracing messages. (Only relevant when logging level is DEBUG.)\n"
			"            <classname::function>\n"
			"    --logfile|-l <filename>       : output log messages to file\n"
			"    --logfile-lines|-L <filename> : output log messages to file, with line buffering\n"
			"    --stdout-only                 : writes errors/warnings to stdout instead of stderr\n"
		   );
}

/*
string g_model;
string g_serial;
//string g_fwver;
*/
Socket g_scpiSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
Socket g_dataSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

#ifdef _WIN32
BOOL WINAPI OnQuit(DWORD signal);
#else
void OnQuit(int signal);
#endif
/*
uintptr_t g_hDevice = 0;

int g_numPixels = 3653;
bool g_triggerArmed;

void ReadCalData();

//Wavelengths, in nm, of each spectral bin
vector<float> g_wavelengths;

//sensor flatness cal
vector<float> g_sensorResponse;

//abs irradiance cal
vector<float> g_absResponse;
float g_absCal = 1;*/

int main(int argc, char* argv[])
{
	//Global settings
	Severity console_verbosity = Severity::NOTICE;

	//Parse command-line arguments
	uint16_t scpi_port = 5025;
	uint16_t waveform_port = 5026;
	for(int i=1; i<argc; i++)
	{
		string s(argv[i]);

		//Let the logger eat its args first
		if(ParseLoggerArguments(i, argc, argv, console_verbosity))
			continue;

		if(s == "--help")
		{
			help();
			return 0;
		}

		else if(s == "--scpi-port")
		{
			if(i+1 < argc)
				scpi_port = atoi(argv[++i]);
		}

		else if(s == "--waveform-port")
		{
			if(i+1 < argc)
				waveform_port = atoi(argv[++i]);
		}

		else
		{
			fprintf(stderr, "Unrecognized command-line argument \"%s\", use --help\n", s.c_str());
			return 1;
		}
	}

	//Set up logging
	g_log_sinks.emplace(g_log_sinks.begin(), new ColoredSTDLogSink(console_verbosity));

	try
	{
		//Try to connect to the SDR
		uhd::usrp::multi_usrp::sptr sdr = uhd::usrp::multi_usrp::make(string("addr=10.2.4.16"));

		//Set reference clock
		sdr->set_clock_source("internal");

		//Select sub device
		sdr->set_rx_subdev_spec(string("A:A"));

		//Set sample rate to 15 Msps (seems to be stable?)
		sdr->set_rx_rate(15e6);

		//Set center frequency to 2.415 GHz
		uhd::tune_request_t tune(2415 * 1e6);
		sdr->set_rx_freq(tune);

		//TODO: gain?
		sdr->set_rx_gain(20);

		//RX bandwidth is 15 MHz
		sdr->set_rx_bandwidth(15e6);

		//Select antenna to use
		sdr->set_rx_antenna("TX/RX");

		//Get configuration
		auto config = sdr->get_pp_string();
		LogDebug("%s\n", config.c_str());

		//Print info about the device
		map<string, string> info = sdr->get_usrp_rx_info(0);
		LogDebug("Vendor: (no API)\n");
		LogDebug("Model:  %s\n", info["mboard_name"].c_str());
		LogDebug("Serial: %s\n", info["mboard_serial"].c_str());

		//TODO: check LO lock detect

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//Make RX streamer
		uint64_t blocksize = 15e6;	//1 second
		uhd::stream_args_t args("fc32", "sc16");
		vector<size_t> channels;
		channels.push_back(0);
		args.channels = channels;
		uhd::rx_streamer::sptr rx = sdr->get_rx_stream(args);

		//Make RX buffer
		vector<complex<float>> buf(blocksize);

		//TODO: play with STREAM_MODE_START_CONTINUOUS

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
			size_t rxsize = rx->recv(&buf.front(), buf.size(), meta, 3.0, false);
			nrx += rxsize;

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
					break;

				default:
					LogDebug("unknown error\n");
			}

			if(nrx >= blocksize)
				break;
		}

		//Write to disk
		FILE* fp = fopen("/tmp/test.complex", "wb");
		fwrite(&buf[0], sizeof(complex<float>), blocksize, fp);
		fclose(fp);

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//Set up signal handlers
	#ifdef _WIN32
		SetConsoleCtrlHandler(OnQuit, TRUE);
	#else
		signal(SIGINT, OnQuit);
		signal(SIGPIPE, SIG_IGN);
	#endif

		//Configure the data plane socket
		g_dataSocket.Bind(waveform_port);
		g_dataSocket.Listen();

		//Launch the control plane socket server
		g_scpiSocket.Bind(scpi_port);
		g_scpiSocket.Listen();
		LogDebug("Ready\n");

		/*
		while(true)
		{
			Socket scpiClient = g_scpiSocket.Accept();
			if(!scpiClient.IsValid())
				break;

			//Create a server object for this connection
			AseqSCPIServer server(scpiClient.Detach());

			//Launch the data-plane thread
			thread dataThread(WaveformServerThread);

			//Process connections on the socket
			server.MainLoop();

			g_waveformThreadQuit = true;
			dataThread.join();
			g_waveformThreadQuit = false;
		}
		*/
	}
	catch(uhd::exception& ex)
	{
		LogError("UHD exception: %s\n", ex.what());
	}

	OnQuit(SIGQUIT);
	return 0;
}

#ifdef _WIN32
BOOL WINAPI OnQuit(DWORD signal)
{
	(void)signal;
#else
void OnQuit(int /*signal*/)
{
#endif
	LogNotice("Shutting down...\n");

	//disconnectDeviceContext(&g_hDevice);

	exit(0);
}

/*
void ReadCalData()
{
	//Read calibration data
	LogDebug("Reading calibration data...\n");
	LogIndenter li;
	const int ncal = 97264;	//TODO: is this always the same size?
	char buf[ncal+1];
	int err;
	if(0 != (err = readFlash((uint8_t*)buf, 0, ncal, &g_hDevice)))
	{
		LogError("failed to read cal data, code %d\n", err);
		exit(1);
	}
	buf[ncal] = '\0';

	//Parse the text into lines
	string sbuf(buf);
	auto lines = explode(sbuf, '\n');
	LogDebug("Found %zu lines of data\n", lines.size());

	//First line: model c.[Y|N] serial
	auto firstFields = explode(lines[0], ' ');
	g_model = Trim(firstFields[0]);
	g_serial = Trim(firstFields[2]);
	LogDebug("Spectrometer is model %s, serial %s\n", g_model.c_str(), g_serial.c_str());
	bool hasAbsCal = (firstFields[1] == "c.Y");
	if(hasAbsCal)
		LogDebug("Absolute cal data present\n");

	//Starting at line 13 (one based, per docs) of the file we have 3653 spectral bins worth of wavelength data
	for(int i=0; i<g_numPixels; i++)
		g_wavelengths.push_back(atof(lines[i+12].c_str()));
	LogDebug("First pixel is %.3f nm\n", g_wavelengths[0]);
	LogDebug("Last pixel is %.3f nm\n", g_wavelengths[g_numPixels-1]);

	//Skip a blank line

	//Read the sensor response normalization data
	for(int i=0; i<g_numPixels; i++)
		g_sensorResponse.push_back(atof(lines[i+13+g_numPixels].c_str()));
	LogDebug("First pixel norm coeff is %.3f\n", g_sensorResponse[0]);
	LogDebug("Mid pixel norm coeff is %.3f\n", g_sensorResponse[2365]);
	LogDebug("Last pixel norm coeff is %.3f\n", g_sensorResponse[g_numPixels-1]);

	//Read absolute irradiance data, if present
	g_absCal = atof(lines[1].c_str());
	for(int i=0; i<g_numPixels; i++)
		g_absResponse.push_back(atof(lines[i+13+2*g_numPixels].c_str()));
}
*/
/**
	@brief Splits a string up into an array separated by delimiters
 */
vector<string> explode(const string& str, char separator)
{
	vector<string> ret;
	string tmp;
	for(auto c : str)
	{
		if(c == separator)
		{
			if(!tmp.empty())
				ret.push_back(tmp);
			tmp = "";
		}
		else
			tmp += c;
	}
	if(!tmp.empty())
		ret.push_back(tmp);
	return ret;
}

/**
	@brief Removes whitespace from the start and end of a string
 */
string Trim(const string& str)
{
	string ret;
	string tmp;

	//Skip leading spaces
	size_t i=0;
	for(; i<str.length() && isspace(str[i]); i++)
	{}

	//Read non-space stuff
	for(; i<str.length(); i++)
	{
		//Non-space
		char c = str[i];
		if(!isspace(c))
		{
			ret = ret + tmp + c;
			tmp = "";
		}

		//Space. Save it, only append if we have non-space after
		else
			tmp += c;
	}

	return ret;
}
