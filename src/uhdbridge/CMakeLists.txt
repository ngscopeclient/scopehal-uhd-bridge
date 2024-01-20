###############################################################################
#C++ compilation
add_executable(uhdbridge
	#AseqSCPIServer.cpp
	#WaveformServerThread.cpp
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

