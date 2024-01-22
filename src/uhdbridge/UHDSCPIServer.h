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
	@brief Declaration of UHDSCPIServer
 */

#ifndef UHDSCPIServer_h
#define UHDSCPIServer_h

#include "../../lib/scpi-server-tools/BridgeSCPIServer.h"

/**
	@brief SCPI server for managing control plane traffic to a single client
 */
class UHDSCPIServer : public BridgeSCPIServer
{
public:
	UHDSCPIServer(ZSOCKET sock);
	virtual ~UHDSCPIServer();

protected:
	virtual std::string GetMake() override;
	virtual std::string GetModel() override;
	virtual std::string GetSerial() override;
	virtual std::string GetFirmwareVersion() override;
	virtual size_t GetAnalogChannelCount() override;
	virtual std::vector<size_t> GetSampleRates() override;
	virtual std::vector<size_t> GetSampleDepths() override;

	virtual bool OnCommand(
		const std::string& line,
		const std::string& subject,
		const std::string& cmd,
		const std::vector<std::string>& args) override;

	virtual bool OnQuery(
		const std::string& line,
		const std::string& subject,
		const std::string& cmd) override;

	virtual bool GetChannelID(const std::string& subject, size_t& id_out) override;
	virtual ChannelType GetChannelType(size_t channel) override;

	//Command methods
	virtual void AcquisitionStart(bool oneShot = false) override;
	virtual void AcquisitionForceTrigger() override;
	virtual void AcquisitionStop() override;
	virtual void SetChannelEnabled(size_t chIndex, bool enabled) override;
	virtual void SetAnalogCoupling(size_t chIndex, const std::string& coupling) override;
	virtual void SetAnalogRange(size_t chIndex, double range_V) override;
	virtual void SetAnalogOffset(size_t chIndex, double offset_V) override;
	virtual void SetDigitalThreshold(size_t chIndex, double threshold_V) override;
	virtual void SetDigitalHysteresis(size_t chIndex, double hysteresis) override;

	virtual void SetSampleRate(uint64_t rate_hz) override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual void SetTriggerDelay(uint64_t delay_fs) override;
	virtual void SetTriggerSource(size_t chIndex) override;
	virtual void SetTriggerLevel(double level_V) override;
	virtual void SetTriggerTypeEdge() override;
	virtual void SetEdgeTriggerEdge(const std::string& edge) override;
	virtual bool IsTriggerArmed() override;
};

#endif
