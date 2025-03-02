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
#include "UHDSCPIServer.h"
#include <signal.h>

using namespace std;

vector<string> explode(const string& str, char separator);
string Trim(const string& str);

void help();

void help()
{
	fprintf(stderr,
			"uhdbridge [device options] [general options] [logger options]\n"
			"\n"
			"  [device options:]\n"
			"    --device \"devstring\"        : Connects to UHD device with the specified device argument string.\n"
			"                                    For IP connected SDRs use \"addr=hostname_or_ip\".\n"
			"                                    See Ettus UHD documentation for full details on supported device strings.\n"
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

string g_model;
string g_serial;

Socket g_scpiSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
Socket g_dataSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

#ifdef _WIN32
BOOL WINAPI OnQuit(DWORD signal);
#else
void OnQuit(int signal);
#endif

uhd::usrp::multi_usrp::sptr g_sdr;

//bool g_triggerArmed;

int main(int argc, char* argv[])
{
	//Global settings
	Severity console_verbosity = Severity::NOTICE;

	//Parse command-line arguments
	uint16_t scpi_port = 5025;
	uint16_t waveform_port = 5026;
	string devpath;
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

		else if(s == "--device")
		{
			if(i+1 < argc)
				devpath = argv[++i];
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

	if(devpath.empty())
	{
		help();
		return 0;
	}

	try
	{
		//Try to connect to the SDR
		g_sdr = uhd::usrp::multi_usrp::make(devpath);

		//auto config = g_sdr->get_pp_string();
		//LogDebug("%s\n", config.c_str());

		//Get properties of the SDR
		/*
		auto props = sdr->get_tree();
		uhd::fs_path path("/mboards/0/");
		//auto propnames =  props->list(path);
		LogDebug("name: %s\n", props->access<string>("/mboards/0/name").get().c_str());
		LogDebug("fpgaver: %s\n", props->access<string>("/mboards/0/fpga_version").get().c_str());
		*/

		//Print info about the device
		map<string, string> info = g_sdr->get_usrp_rx_info(0);
		g_model = info["mboard_name"];
		g_serial = info["mboard_serial"];

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

		while(true)
		{
			Socket scpiClient = g_scpiSocket.Accept();
			if(!scpiClient.IsValid())
				break;

			//Create a server object for this connection
			UHDSCPIServer server(scpiClient.Detach());

			//Launch the data-plane thread
			thread dataThread(WaveformServerThread);

			//Process connections on the socket
			server.MainLoop();

			g_waveformThreadQuit = true;
			dataThread.join();
			g_waveformThreadQuit = false;
		}
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

	exit(0);
}

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
