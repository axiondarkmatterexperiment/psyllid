/*
 * freq_data.cc
 *
 *  Created on: Dec 28, 2015
 *      Author: nsoblath
 */

#include "freq_data.hh"

namespace psyllid
{

    freq_data::freq_data() :
            f_id( 0 ),
            f_array( new vector< midge::real_t >() )
    {
    }

    freq_data::~freq_data()
    {
    }

} /* namespace psyllid */