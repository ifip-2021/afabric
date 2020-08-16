add_library(libapps OBJECT
        app.cc
        app.h
        app-pias.cc
        app-pias.h
        app-chunks.h
        app-chunks.cc
        bitops.h
        pbc.cc
        pbc.h
        ping.cc
        ping.h
        rtp.cc
        rtp.h
        telnet.cc
        telnet.h
        udp.cc
        udp.h
        vat.h
        vatrcvr.cc
        worm.cc
        worm.h
        )
target_include_directories(libapps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(libapps PUBLIC libcommon libtools librouting)
target_compile_features(libapps PRIVATE cxx_std_17)
if(WITH_SCTP)
    target_sources(libapps PRIVATE
            sctp_app1.cc
            sctp_app1.h
        )
endif()
