########
# data #
########

set( headers
    freq_data.hh
    id_range_event.hh
    memory_block.hh
    roach_packet.hh
    time_data.hh
    trigger_flag.hh
)

set( sources
    freq_data.cc
    id_range_event.cc
    memory_block.cc
    roach_packet.cc
    time_data.cc
    trigger_flag.cc
)

set( dependencies
    PsyllidUtility
)

###########
# library #
###########

midge_library( PsyllidData sources dependencies )
pbuilder_install_headers( ${headers} )


