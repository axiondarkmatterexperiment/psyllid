/*
 * control_access.cc
 *
 *  Created on: Feb 23, 2016
 *      Author: nsoblath
 */

#include "../control/control_access.hh"

#include "daq_control.hh"

namespace psyllid
{

    control_access::control_access( std::shared_ptr< daq_control > a_daq_control ) :
            f_daq_control( a_daq_control )
    {
    }

    control_access::~control_access()
    {
    }

    void control_access::set_daq_control( std::shared_ptr< daq_control > a_daq_control )
    {
        f_daq_control = a_daq_control;
    }

} /* namespace psyllid */
