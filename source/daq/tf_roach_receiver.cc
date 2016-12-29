
/*
 * udp_receiver.cc
 *
 *  Created on: Dec 25, 2015
 *      Author: nsoblath
 */

#include "tf_roach_receiver.hh"

#include "psyllid_error.hh"

#include "logger.hh"
#include "param.hh"

#include <thread>
#include <memory>
#include <sys/types.h> // for ssize_t

using midge::stream;

namespace psyllid
{
    REGISTER_NODE_AND_BUILDER( tf_roach_receiver, "tf-roach-receiver" );

    LOGGER( plog, "tf_roach_receiver" );

    tf_roach_receiver::tf_roach_receiver() :
            f_time_length( 10 ),
            f_freq_length( 10 ),
            f_udp_buffer_size( sizeof( roach_packet ) ),
            f_time_sync_tol( 2 ),
            f_start_paused( true ),
            f_paused( true ),
            f_time_session_pkt_counter( 0 ),
            f_freq_session_pkt_counter( 0 )
    {
    }

    tf_roach_receiver::~tf_roach_receiver()
    {
    }

    void tf_roach_receiver::initialize()
    {
        out_buffer< 0 >().initialize( f_time_length );
        out_buffer< 1 >().initialize( f_freq_length );
        return;
    }

    void tf_roach_receiver::execute( midge::diptera* a_midge )
    {
        try
        {
            LDEBUG( plog, "Executing the TF ROACH receiver" );

            midge::enum_t t_in_command = stream::s_none;

            memory_block* t_memory_block = nullptr;
            time_data* t_time_data = nullptr;
            freq_data* t_freq_data = nullptr;

            //LDEBUG( plog, "Server is listening" );

            std::unique_ptr< char[] > t_buffer_ptr( new char[ f_udp_buffer_size ] );

            // start out as configured (default is paused)
            //out_stream< 0 >().set( stream::s_start );
            //out_stream< 1 >().set( stream::s_start );
            f_paused = f_start_paused;

            //uint64_t t_time_batch_pkt = 0;
            //uint64_t t_freq_batch_pkt = 0;

            size_t t_pkt_size = 0;

            if( ! f_start_paused )
            {
                LDEBUG( plog, "TF ROACH receiver starting unpaused" );
                out_stream< 0 >().data()->set_pkt_in_session( 0 );
                out_stream< 1 >().data()->set_pkt_in_session( 0 );
                if( ! out_stream< 0 >().set( stream::s_start ) ) return;
                if( ! out_stream< 1 >().set( stream::s_start ) ) return;
                f_time_session_pkt_counter = 0;
                f_freq_session_pkt_counter = 0;
            }

            LINFO( plog, "Starting main loop; waiting for packets" );
            while( ! is_canceled() )
            {
                // check if we've received a pause or unpause instruction
                try
                {
                    check_instruction();
                }
                catch( error& e )
                {
                    LERROR( plog, e.what() );
                    break;
                }

                t_in_command = in_stream< 0 >().get();
                if( t_in_command == stream::s_none ) continue;
                if( t_in_command == stream::s_error ) break;

                LTRACE( plog, "TF ROACH receiver reading stream 0 at index " << in_stream< 0 >().get_current_index() );

                // check for any commands from upstream
                if( t_in_command == stream::s_exit )
                {
                    LDEBUG( plog, "TF ROACH receiver is exiting" );
                    // Output streams are stopped here because even though this is controlled by the pause/unpause commands,
                    // receiving a stop command from upstream overrides that.
                    if( ! out_stream< 0 >().set( stream::s_exit ) ) break;
                    out_stream< 1 >().set( stream::s_exit );
                    break;
                }

                if( t_in_command == stream::s_stop )
                {
                    LDEBUG( plog, "TF ROACH receiver is stopping" );
                    // Output streams are stopped here because even though this is controlled by the pause/unpause commands,
                    // receiving a stop command from upstream overrides that.
                    if( ! out_stream< 0 >().set( stream::s_stop ) ) break;
                    if( ! out_stream< 1 >().set( stream::s_stop ) ) break;
                    continue;
                }

                if( t_in_command == stream::s_start )
                {
                    LDEBUG( plog, "TF ROACH receiver is starting" );
                    // Output streams are not started here because this is controled by the pause/unpause commands
                    continue;
                }

                if( is_canceled() )
                {
                    break;
                }

                try
                {
                    check_instruction();
                }
                catch( error& e )
                {
                    LERROR( plog, e.what() );
                    break;
                }

                // do nothing with input data if paused
                if( f_paused ) continue;

                if( t_in_command == stream::s_run )
                {
                    t_memory_block = in_stream< 0 >().data();

                    if( is_canceled() )
                    {
                        break;
                    }

                    t_pkt_size = t_memory_block->get_n_bytes_used();
                    LTRACE( plog, "Handling packet at stream index <" << in_stream< 0 >().get_current_index() << ">; size =  " << t_pkt_size << " bytes; block address = " << (void*)t_memory_block->block() );
                    if( t_pkt_size != f_udp_buffer_size )
                    {
                        LWARN( plog, "Improper packet size; packet may be malformed: received " << t_memory_block->get_n_bytes_used() << " bytes; expected " << f_udp_buffer_size << " bytes" );
                        if( t_pkt_size == 0 ) continue;
                    }

                    byteswap_inplace( reinterpret_cast< raw_roach_packet* >( t_memory_block->block() ) );
                    roach_packet* t_roach_packet = reinterpret_cast< roach_packet* >( t_memory_block->block() );

                    // debug purposes only
#ifndef NDEBUG
                    raw_roach_packet* t_raw_packet = reinterpret_cast< raw_roach_packet* >( t_memory_block->block() );
                    LTRACE( plog, "Raw packet header: " << std::hex << t_raw_packet->f_word_0 << ", " << t_raw_packet->f_word_1 << ", " << t_raw_packet->f_word_2 << ", " << t_raw_packet->f_word_3 );
#endif

                    if( t_roach_packet->f_freq_not_time )
                    {
                        // packet is frequency data
                        //t_freq_batch_pkt = t_roach_packet->f_pkt_in_batch;

                        t_freq_data = out_stream< 1 >().data();
                        t_freq_data->set_pkt_in_session( f_freq_session_pkt_counter++ );
                        ::memcpy( &t_freq_data->packet(), t_roach_packet, t_pkt_size );

                        LTRACE( plog, "Frequency data received (" << t_pkt_size << " bytes):  chan = " << t_freq_data->get_digital_id() <<
                               "  time = " << t_freq_data->get_unix_time() <<
                               "  pkt_session = " << t_freq_data->get_pkt_in_session() <<
                               "  pkt_batch = " << t_roach_packet->f_pkt_in_batch <<
                               "  freqNotTime = " << t_freq_data->get_freq_not_time() <<
                               "  first 8 bins: " << (int)t_freq_data->get_array()[ 0 ][ 0 ]  << ", " << (int)t_freq_data->get_array()[ 0 ][ 1 ] << " -- " << (int)t_freq_data->get_array()[ 1 ][ 0 ] << ", " << (int)t_freq_data->get_array()[ 1 ][ 1 ] << " -- " << (int)t_freq_data->get_array()[ 2 ][ 0 ] << ", " << (int)t_freq_data->get_array()[ 2 ][ 1 ] << " -- " << (int)t_freq_data->get_array()[ 3 ][ 0 ] << ", " << (int)t_freq_data->get_array()[ 3 ][ 1 ]);
                        LTRACE( plog, "Frequency data written to stream index <" << out_stream< 1 >().get_current_index() << ">" );
                        if( ! out_stream< 1 >().set( stream::s_run ) )
                        {
                            LERROR( plog, "Exiting due to stream error" );
                            break;
                        }
                    }
                    else
                    {
                        // packet is time data
                        //t_time_batch_pkt = t_roach_packet->f_pkt_in_batch;

                        t_time_data = out_stream< 0 >().data();
                        t_time_data->set_pkt_in_session( f_time_session_pkt_counter++ );
                        ::memcpy( &t_time_data->packet(), t_roach_packet, t_pkt_size );

                        LTRACE( plog, "Time data received (" << t_pkt_size << " bytes):  chan = " << t_time_data->get_digital_id() <<
                               "  time = " << t_time_data->get_unix_time() <<
                               "  pkt_session = " << t_time_data->get_pkt_in_session() <<
                               "  pkt_batch = " << t_roach_packet->f_pkt_in_batch <<
                               "  freqNotTime = " << t_time_data->get_freq_not_time() <<
                               "  first 8 bins: " << (int)t_time_data->get_array()[ 0 ][ 0 ]  << ", " << (int)t_time_data->get_array()[ 0 ][ 1 ] << " -- " << (int)t_time_data->get_array()[ 1 ][ 0 ] << ", " << (int)t_time_data->get_array()[ 1 ][ 1 ] << " -- " << (int)t_time_data->get_array()[ 2 ][ 0 ] << ", " << (int)t_time_data->get_array()[ 2 ][ 1 ] << " -- " << (int)t_time_data->get_array()[ 3 ][ 0 ] << ", " << (int)t_time_data->get_array()[ 3 ][ 1 ]);
                        LTRACE( plog, "Time data written to stream index <" << out_stream< 1 >().get_current_index() << ">" );
                        if( ! out_stream< 0 >().set( stream::s_run ) )
                        {
                            LERROR( plog, "Exiting due to stream error" );
                            break;
                        }
                    }

                    //if( t_time_batch_pkt == t_freq_batch_pkt )
                    //{
                    //    LTRACE( plog, "Time and frequency batch IDs match: " << t_time_batch_pkt );
                    //    LTRACE( plog, "Frequency data written to stream index <" << out_stream< 1 >().get_current_index() << ">" );
                    //    out_stream< 1 >().set( stream::s_run );
                    //    LTRACE( plog, "Time data written to stream index <" << out_stream< 1 >().get_current_index() << ">" );
                    //    out_stream< 0 >().set( stream::s_run );
                   // }
                } // if block for run command
            } // main while loop

            LINFO( plog, "TF ROACH receiver is exiting" );

            // normal exit condition
            LDEBUG( plog, "Stopping output streams" );
            if( ! out_stream< 0 >().set( stream::s_stop ) ) return;
            if( ! out_stream< 1 >().set( stream::s_stop ) ) return;

            LDEBUG( plog, "Exiting output streams" );
            if( ! out_stream< 0 >().set( stream::s_exit ) ) return;
            out_stream< 1 >().set( stream::s_exit );

            return;
        }
        catch(...)
        {
            if( a_midge ) a_midge->throw_ex( std::current_exception() );
            else throw;
        }
    }

    void tf_roach_receiver::finalize()
    {
        out_buffer< 0 >().finalize();
        out_buffer< 1 >().finalize();
        return;
    }

    void tf_roach_receiver::check_instruction()
    {
        if( f_paused )
        {
            if( have_instruction() && use_instruction() == midge::instruction::resume )
            {
                LDEBUG( plog, "TF ROACH receiver resuming" );
                out_stream< 0 >().data()->set_pkt_in_session( 0 );
                out_stream< 1 >().data()->set_pkt_in_session( 0 );
                if( ! out_stream< 0 >().set( stream::s_start ) ) throw error() << "Stream 0 error while starting";
                if( ! out_stream< 1 >().set( stream::s_start ) ) throw error() << "Stream 1 error while starting";
                f_time_session_pkt_counter = 0;
                f_freq_session_pkt_counter = 0;
                f_paused = false;
            }
        }
        else
        {
            if( have_instruction() && use_instruction() == midge::instruction::pause )
            {
                LDEBUG( plog, "TF ROACH receiver pausing" );
                if( ! out_stream< 0 >().set( stream::s_stop ) ) throw error() << "Stream 0 error while stopping";
                if( ! out_stream< 1 >().set( stream::s_stop ) ) throw error() << "Stream 1 error while stopping";
                f_paused = true;
            }
        }

        return;
    }

    void tf_roach_receiver::do_cancellation()
    {
        return;
    }


    tf_roach_receiver_builder::tf_roach_receiver_builder() :
            _node_builder< tf_roach_receiver >()
    {
    }

    tf_roach_receiver_builder::~tf_roach_receiver_builder()
    {
    }

    void tf_roach_receiver_builder::apply_config( tf_roach_receiver* a_node, const scarab::param_node& a_config )
    {
        LDEBUG( plog, "Configuring tf_roach_receiver with:\n" << a_config );
        a_node->set_time_length( a_config.get_value( "time-length", a_node->get_time_length() ) );
        a_node->set_freq_length( a_config.get_value( "freq-length", a_node->get_freq_length() ) );
        a_node->set_udp_buffer_size( a_config.get_value( "udp-buffer-size", a_node->get_udp_buffer_size() ) );
        a_node->set_time_sync_tol( a_config.get_value( "time-sync-tol", a_node->get_time_sync_tol() ) );
        a_node->set_start_paused( a_config.get_value( "start-paused", a_node->get_start_paused() ) );
        return;
    }


} /* namespace psyllid */