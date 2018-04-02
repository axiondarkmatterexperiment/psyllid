/*
 * frequency_transform.hh
 *
 *  Created on: March 28, 2018
 *      Author: laroque
 */

#ifndef PSYLLID_FREQUENCY_TRANSFORM_HH_
#define PSYLLID_FREQUENCY_TRANSFORM_HH_

#include "freq_data.hh"
//#include "memory_block.hh"
#include "node_builder.hh"
#include "time_data.hh"

#include "transformer.hh"
#include "shared_cancel.hh"

#include <fftw3.h>
//#include <memory>

namespace scarab
{
    class param_node;
}

namespace psyllid
{
    /*!
     @class frequency_transform
     @author B. H. LaRoque

     @brief A transformer to receive time data, compute an FFT, and distribute as time and frequency ROACH packets.

     @details

     Parameter setting is not thread-safe.  Executing is thread-safe.

     Node type: "frequency-transform"

     Available configuration values:
     - "time-length": uint -- The size of the output time-data buffer
     - "freq-length": uint -- The size of the output frequency-data buffer
     - "start-paused": bool -- Whether to start execution paused and wait for an unpause command

     Input Stream:
     - 0: time_data

     Output Streams:
     - 0: time_data
     - 1: freq_data
    */
    class frequency_transform : public midge::_transformer< frequency_transform, typelist_1( time_data ), typelist_2( time_data, freq_data ) >
    {
        private:
            typedef std::map< std::string, unsigned > TransformFlagMap;

        public:
            frequency_transform();
            virtual ~frequency_transform();

        public:
            mv_accessible( uint64_t, time_length );
            mv_accessible( uint64_t, freq_length );
            mv_accessible( unsigned, fft_size ); // I really wish I could get this from the egg header
            mv_accessible( bool, start_paused );
            mv_accessible( std::string, transform_flag );
            mv_accessible( bool, use_wisdom );
            mv_accessible( std::string, wisdom_filename );
            //TODO all of these mv need to be added to the configuration interface class

        public:
            virtual void initialize();
            virtual void execute( midge::diptera* a_midge = nullptr );
            virtual void finalize();

        private:
            TransformFlagMap f_transform_flag_map;
            fftw_complex* f_fftw_input;
            fftw_complex* f_fftw_output;
            fftw_plan f_fftw_plan;

            bool f_paused;

            uint64_t f_time_session_pkt_counter;
            uint64_t f_freq_session_pkt_counter;
        private:
            void setup_internal_maps();

    };

    class frequency_transform_binding : public _node_binding< frequency_transform, frequency_transform_binding >
    {
        public:
            frequency_transform_binding();
            virtual ~frequency_transform_binding();

        private:
            virtual void do_apply_config( frequency_transform* a_node, const scarab::param_node& a_config ) const;
            virtual void do_dump_config( const frequency_transform* a_node, scarab::param_node& a_config ) const;

            //virtual bool do_run_command( frequency_transform* a_node, const std::string& a_cmd, const scarab::param_node& ) const;
    };

} /* namespace psyllid */

#endif /* PSYLLID_FREQUENCY_TRANSFORM_HH_ */
