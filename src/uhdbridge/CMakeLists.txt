###############################################################################
#C++ compilation
add_executable(uhdbridge
	UHDSCPIServer.cpp
	WaveformServerThread.cpp
	main.cpp
)

###############################################################################
#Linker settings
target_link_libraries(uhdbridge
	xptools
	log
	scpi-server-tools
	uhd
	)

