/*
 * udp_receiver.cc
 *
 *  Created on: Dec 25, 2015
 *      Author: nsoblath
 */

#include "tf_roach_receiver.hh"

#include "fast_packet_acq.hh"
#include "psyllid_error.hh"
#include "udp_server_socket.hh"

#ifdef __linux__
#include "udp_server_fpa.hh"
#endif

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
            f_server_config( nullptr ),
            f_paused( false ),
            f_server( nullptr )
    {
    }

    tf_roach_receiver::~tf_roach_receiver()
    {
        delete f_server_config;
    }

    void tf_roach_receiver::initialize()
    {
        std::string t_server_type( "socket" );
        if( f_server_config != nullptr ) t_server_type = f_server_config->get_value( "type", t_server_type );
        LDEBUG( plog, "Initializing tf_roach_receiver with server type <" << t_server_type << ">" );
        try
        {
            if( t_server_type == "socket" )
            {
                f_server.reset( new udp_server_socket( f_server_config ) );
                static_cast< udp_server_socket* >( f_server.get() )->initialize_packet_wrapper( f_udp_buffer_size );
            }
#ifdef __linux__
            else if( t_server_type == "fpa" )
            {
                //fast_packet_acq* t_fpa = dynamic_cast< fast_packet_acq* >( node_ptr( f_server_config->get_value( "fpa", "eth1" ) ) );
                f_server.reset( new udp_server_fpa( f_server_config ) );
            }
#endif
            else
            {
                throw error() << "[udp_receiver] Unknown server type: " << t_server_type;
            }
        }
        catch( error& e )
        {
            throw;
        }

        out_buffer< 0 >().initialize( f_time_length );
        out_buffer< 1 >().initialize( f_freq_length );
        return;
    }

    void tf_roach_receiver::execute()
    {
        LDEBUG( plog, "Executing the TF ROACH receiver" );

        f_server->activate();

        time_data* t_time_data = nullptr;
        freq_data* t_freq_data = nullptr;

        std::thread* t_server_thread = nullptr;

        try
        {
            //LDEBUG( plog, "Server is listening" );

            std::unique_ptr< char[] > t_buffer_ptr( new char[ f_udp_buffer_size ] );

            //out_stream< 0 >().set( stream::s_start );
            //out_stream< 1 >().set( stream::s_start );
            f_paused = true;
            bool t_unpausing = false;

            uint32_t t_last_packet_time = 0;
            uint64_t t_time_session_pkt_counter = 0;
            uint64_t t_freq_session_pkt_counter = 0;

            uint64_t t_time_batch_pkt = 0;
            uint64_t t_freq_batch_pkt = 0;

            ssize_t t_size_received = 0;

            // start the server running
            LDEBUG( plog, "Launching asynchronous server thread" );
            t_server_thread = new std::thread( &udp_server::execute, f_server.get() );
            //f_server->execute();

            LINFO( plog, "Starting main loop; waiting for packets" );
            while( ! f_canceled.load() )
            {
                t_size_received = 0;

                if( f_paused )
                {
                    midge::instruction t_instruction = midge::instruction::none;
                    if( have_instruction() ) t_instruction = use_instruction();
                    if( t_instruction == midge::instruction::pause )
                    {
                        LDEBUG( plog, "Received pause instruction while paused" );
                        t_unpausing = false;
                    }
                    else if( t_instruction == midge::instruction::resume || t_unpausing )
                    {
                        LDEBUG( plog, "UDP receiver resuming" );
                        t_unpausing = true;
                        f_server->reset_read();
                        if( std::abs( time( nullptr ) - t_last_packet_time ) > f_time_sync_tol )
                        {
                            LINFO( plog, "Waiting to synchronize with the client (psyllid time - last packet time: |" << time(nullptr) << " - " << t_last_packet_time << "| > " << f_time_sync_tol << ")" );
                            // after this, we'll go around the loop again, and end up below where we're waiting for a few milliseconds
                        }
                        else
                        {
                            out_stream< 0 >().data()->set_unix_time( t_last_packet_time );
                            out_stream< 0 >().data()->set_pkt_in_session( 0 );
                            out_stream< 1 >().data()->set_pkt_in_session( 0 );
                            out_stream< 0 >().set( stream::s_start );
                            out_stream< 1 >().set( stream::s_start );
                            t_time_session_pkt_counter = 0;
                            t_freq_session_pkt_counter = 0;
                            t_unpausing = false;
                            f_paused = false;
                        }
                    }
                    else
                    {
                        // if we've reached here, we're waiting for psyllid to synchronize with the incoming packets
                        // wait for a second, then continue the loop
                        //LDEBUG( plog, "Waiting for instruction" );
                        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
                        continue;
                    }
                }
                else
                {
                    if( have_instruction() && use_instruction() == midge::instruction::pause )
                    {
                        LDEBUG( plog, "UDP receiver pausing" );
                        out_stream< 0 >().set( stream::s_stop );
                        out_stream< 1 >().set( stream::s_stop );
                        f_paused = true;
                    }
                }

                if( (out_stream< 0 >().get() == stream::s_stop) ||
                        (out_stream< 1 >().get() == stream::s_stop) ||
                        (false /* some other condition for stopping */) )
                {
                    LWARN( plog, "Output stream(s) have stop condition" );
                    break;
                }

                LDEBUG( plog, "Waiting for UDP packets" );

                // inner loop over packet-receive timeouts
                while( t_size_received <= 0 && ! f_canceled.load() )
                {
                    //t_size_received = f_server->recv( t_buffer_ptr.get(), f_udp_buffer_size, 0 );
                    t_size_received = f_server->get_next_packet( t_buffer_ptr.get(), f_udp_buffer_size );
                }

                if( f_canceled.load() )
                {
                    break;
                }

                if( t_size_received > 0 )
                {
                    byteswap_inplace( reinterpret_cast< raw_roach_packet* >( t_buffer_ptr.get() ) );
                    roach_packet* t_roach_packet = reinterpret_cast< roach_packet* >( t_buffer_ptr.get() );

                    if( f_paused )
                    {
                        t_last_packet_time = t_roach_packet->f_unix_time;
                        continue;
                    }

                    // debug purposes only
#ifndef NDEBUG
                    raw_roach_packet* t_raw_packet = reinterpret_cast< raw_roach_packet* >( t_buffer_ptr.get() );
                    LDEBUG( plog, "Raw packet header: " << std::hex << t_raw_packet->f_word_0 << ", " << t_raw_packet->f_word_1 << ", " << t_raw_packet->f_word_2 << ", " << t_raw_packet->f_word_3 );
#endif

                    if( t_roach_packet->f_freq_not_time )
                    {
                        // packet is frequency data

                        t_freq_batch_pkt = t_roach_packet->f_pkt_in_batch;
                        //id_match_sanity_check( t_time_batch_pkt, t_freq_batch_pkt, t_time_session_pkt_counter, t_freq_session_pkt_counter );

                        t_freq_data = out_stream< 1 >().data();
                        t_freq_data->set_pkt_in_session( t_freq_session_pkt_counter++ );
                        ::memcpy( &t_freq_data->packet(), t_roach_packet, f_udp_buffer_size );

                        LDEBUG( plog, "Frequency data received (" << t_size_received << " bytes):  chan = " << t_freq_data->get_digital_id() <<
                               "  time = " << t_freq_data->get_unix_time() <<
                               "  id = " << t_freq_data->get_pkt_in_session() <<
                               "  freqNotTime = " << t_freq_data->get_freq_not_time() <<
                               "  bin 0 [0] = " << (unsigned)t_freq_data->get_array()[ 0 ][ 0 ] );
                        LDEBUG( plog, "Frequency data written to stream index <" << out_stream< 1 >().get_current_index() << ">" );

                        out_stream< 1 >().set( stream::s_run );
                    }
                    else
                    {
                        // packet is time data

                        t_time_batch_pkt = t_roach_packet->f_pkt_in_batch;
                        //id_match_sanity_check( t_time_batch_pkt, t_freq_batch_pkt, t_time_session_pkt_counter, t_freq_session_pkt_counter );

                        t_time_data = out_stream< 0 >().data();
                        t_time_data->set_pkt_in_session( t_time_session_pkt_counter++ );
                        ::memcpy( &t_time_data->packet(), t_roach_packet, f_udp_buffer_size );

                        LDEBUG( plog, "Time data received (" << t_size_received << " bytes):  chan = " << t_time_data->get_digital_id() <<
                               "  time = " << t_time_data->get_unix_time() <<
                               "  id = " << t_time_data->get_pkt_in_session() <<
                               "  freqNotTime = " << t_time_data->get_freq_not_time() <<
                               "  bin 0 [0] = " << (unsigned)t_time_data->get_array()[ 0 ][ 0 ] );
                        LDEBUG( plog, "Time data written to stream index <" << out_stream< 1 >().get_current_index() << ">" );

                        out_stream< 0 >().set( stream::s_run );
                    }

                    continue;
                }
                else
                {
                    LDEBUG( plog, "No message received & no error present" );
                    continue;
                }
            }

            LINFO( plog, "UDP receiver is exiting" );

            LDEBUG( plog, "Canceling packet server" );
            f_server->cancel();
            t_server_thread->join();
            delete t_server_thread;

            // normal exit condition
            LDEBUG( plog, "Stopping output streams" );
            out_stream< 0 >().set( stream::s_stop );
            out_stream< 1 >().set( stream::s_stop );

            LDEBUG( plog, "Exiting output streams" );
            out_stream< 0 >().set( stream::s_exit );
            out_stream< 1 >().set( stream::s_exit );

            return;
        }
        catch( midge::error& e )
        {
            LERROR( plog, "Midge exception caught: " << e.what() );

            LDEBUG( plog, "Canceling packet server" );
            f_server->cancel();
            t_server_thread->join();
            delete t_server_thread;

            LDEBUG( plog, "Stopping output streams" );
            out_stream< 0 >().set( stream::s_stop );
            out_stream< 1 >().set( stream::s_stop );

            LDEBUG( plog, "Exiting output streams" );
            out_stream< 0 >().set( stream::s_exit );
            out_stream< 1 >().set( stream::s_exit );

            return;
        }
        catch( error& e )
        {
            LERROR( plog, "Psyllid exception caught: " << e.what() );

            LDEBUG( plog, "Canceling packet server" );
            f_server->cancel();
            t_server_thread->join();
            delete t_server_thread;

            LDEBUG( plog, "Stopping output streams" );
            out_stream< 0 >().set( stream::s_stop );
            out_stream< 1 >().set( stream::s_stop );

            LDEBUG( plog, "Exiting output streams" );
            out_stream< 0 >().set( stream::s_exit );
            out_stream< 1 >().set( stream::s_exit );

            return;
        }

        // control should not reach here
        LERROR( plog, "Control should not reach this point" );
        return;
    }

    void tf_roach_receiver::finalize()
    {
        out_buffer< 0 >().finalize();
        out_buffer< 1 >().finalize();
        return;
    }

    void tf_roach_receiver::do_cancellation()
    {
        LDEBUG( plog, "Canceling tf_roach_receiver" );
        f_server->cancel();
        return;
    }

    void tf_roach_receiver::id_match_sanity_check( uint64_t a_time_batch_pkt, uint64_t a_freq_batch_pkt, uint64_t a_time_session_pkt, uint64_t a_freq_session_pkt )
    {
        if( a_time_batch_pkt == a_freq_batch_pkt )
        {
            if( a_time_session_pkt != a_freq_session_pkt )
            {
                LERROR( plog, "Packet ID mismatch:\n" <<
                        "\tTime batch packet: " << a_time_batch_pkt << '\n' <<
                        "\tFreq batch packet: " << a_freq_batch_pkt << '\n' <<
                        "\tTime session packet: " << a_time_session_pkt << '\n' <<
                        "\tFreq session packet: " << a_freq_session_pkt );
            }
        }
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
        const scarab::param_node* t_server_config = a_config.node_at( "server" );
        if( t_server_config == nullptr )
        {
            LDEBUG( plog, "No server config" );
            a_node->set_server_config( nullptr );
        }
        else
        {
            LDEBUG( plog, "Saving server config:\n" << *t_server_config );
            a_node->set_server_config( new scarab::param_node( *t_server_config ) );
        }
        return;
    }


} /* namespace psyllid */
