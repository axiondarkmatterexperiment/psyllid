/*
 * egg_writer.cc
 *
 *  Created on: Dec 30, 2015
 *      Author: nsoblath
 */

#include "egg_writer.hh"

#include "butterfly_house.hh"
#include "psyllid_error.hh"

#include "logger.hh"

using midge::stream;

using std::string;
using std::vector;

namespace psyllid
{
    REGISTER_NODE( egg_writer, "egg-writer" );

    LOGGER( plog, "egg_writer" );

    egg_writer::egg_writer() :
            f_file_size_limit_mb()
    {
    }

    egg_writer::~egg_writer()
    {
    }

    void egg_writer::initialize()
    {
        return;
    }

    void egg_writer::execute()
    {
        midge::enum_t t_trig_command = stream::s_none;
        midge::enum_t t_time_command = stream::s_none;

        trigger_flag* t_trig_data = nullptr;
        time_data* t_time_data = nullptr;

        butterfly_house* t_bf_house = butterfly_house::get_instance();
        string t_filename( "psyllid_default_filename.egg" );
        monarch_wrap_ptr t_monarch_ptr;
        stream_wrap_ptr t_swrap_ptr;
        monarch3::M3Record* t_record_ptr;

        uint64_t t_bytes_per_record = PAYLOAD_SIZE;
        unsigned t_stream_no = 0;
        bool t_is_new_event = true;
        //bool t_was_triggered = false;

        while( true )
        {
            t_time_command = in_stream< 0 >().get();
            DEBUG( plog, "Egg writer reading stream 0 (time) at index " << in_stream< 0 >().get_current_index() );

            t_trig_command = in_stream< 1 >().get();
            DEBUG( plog, "Egg writer reading stream 1 (trig) at index " << in_stream< 1 >().get_current_index() );

            if( t_time_command != t_trig_command )
            {
                throw midge::error() << "Time and event stream commands are mismatched: " <<
                        t_time_command << " (time, at stream index " << in_stream< 0 >().get_current_index() << ") vs " <<
                        t_trig_command << " (trigger, at stream index " << in_stream< 0 >().get_current_index() << ")";
            }

            if( t_trig_command == stream::s_stop )
            {
                DEBUG( plog, "Egg writer is stopping" );

                if( t_swrap_ptr )
                {
                    DEBUG( plog, "Finishing stream <" << t_stream_no << ">" );
                    t_monarch_ptr->finish_stream( t_stream_no, true );
                    t_swrap_ptr.reset();
                }

                continue;
            }

            t_time_data = in_stream< 0 >().data();
            t_trig_data = in_stream< 1 >().data();

            if( t_trig_command == stream::s_start )
            {
                DEBUG( plog, "Preparing egg file" );

                //TODO: prepare egg file
                t_filename = "test_file.egg";
                try
                {
                    t_monarch_ptr = t_bf_house->declare_file( t_filename );

                    header_wrap_ptr t_hwrap_ptr = t_monarch_ptr->get_header();
                    // to set:
                    //   run duration
                    //   timestamp
                    //   description

                    vector< unsigned > t_chan_vec;
                    t_stream_no = t_hwrap_ptr->header().AddStream( "Psyllid - ROACH2", 100, PAYLOAD_SIZE / 2, 2, 1, monarch3::sDigitizedUS, 8, monarch3::sBitsAlignedLeft, &t_chan_vec );

                    unsigned i_chan_psyllid = 0; // this is the channel number in mantis, as opposed to the channel number in the monarch file
                    for( std::vector< unsigned >::const_iterator it = t_chan_vec.begin(); it != t_chan_vec.end(); ++it )
                    {
                        t_hwrap_ptr->header().GetChannelHeaders()[ *it ].SetVoltageOffset( 0. );
                        t_hwrap_ptr->header().GetChannelHeaders()[ *it ].SetVoltageRange( 0.5 );
                        t_hwrap_ptr->header().GetChannelHeaders()[ *it ].SetDACGain( 1. );
                        ++i_chan_psyllid;
                    }

                }
                catch( error& e )
                {
                    throw midge::error() << "Unable to prepare the egg file <" << t_filename << ">: " << e.what();
                }

                continue;
            }

            if( t_trig_command == stream::s_run )
            {
                if( ! t_swrap_ptr )
                {
                    DEBUG( plog, "Getting stream <" << t_stream_no << ">" );
                    t_swrap_ptr = t_monarch_ptr->get_stream( t_stream_no );
                    t_record_ptr = t_swrap_ptr->stream().GetStreamRecord();
                }

                uint64_t t_time_id = t_time_data->get_pkt_in_batch();
                uint64_t t_trig_id = t_trig_data->get_id();

                if( t_time_id != t_trig_id )
                {
                    DEBUG( plog, "Mismatch between time id <" << t_time_id << "> and trigger id <" << t_trig_id << ">" );
                    while( t_time_id < t_trig_id )
                    {
                        DEBUG( plog, "Moving time stream forward" );
                        t_time_command = in_stream< 0 >().get();
                        t_time_data = in_stream< 0 >().data();
                        t_time_id = t_time_data->get_pkt_in_batch();
                    }
                    while( t_time_id > t_trig_id )
                    {
                        DEBUG( plog, "Moving trig stream forward" );
                        t_trig_command = in_stream< 1 >().get();
                        t_trig_data = in_stream< 1 >().data();
                        t_trig_id = t_trig_data->get_id();
                    }
                    if( t_time_id != t_trig_id )
                    {
                        throw midge::error() << "Unable to match time and trigger streams";
                    }
                }


                if( t_trig_data->get_flag() )
                {
                    DEBUG( plog, "Triggered packet, id <" << t_trig_data->get_id() << ">" );

                    if( t_is_new_event ) DEBUG( plog, "New event" );
                    memcpy( t_record_ptr->GetData(), t_time_data->get_raw_array(), t_bytes_per_record );
                    t_swrap_ptr->stream().WriteRecord( t_is_new_event );
                    t_is_new_event = false;
                }
                else
                {
                    DEBUG( plog, "Untriggered packet, id <" << t_trig_data->get_id() << ">" );
                    t_is_new_event = true;
                }

                continue;
            }

            if( t_trig_command == stream::s_exit )
            {
                DEBUG( plog, "Egg writer is exiting" );

                if( t_swrap_ptr )
                {
                    DEBUG( plog, "Finishing stream <" << t_stream_no << ">" );
                    t_monarch_ptr->finish_stream( t_stream_no, true );
                    t_swrap_ptr.reset();
                }

                break;
            }
        }

        return;
    }

    void egg_writer::finalize()
    {
        return;
    }


    egg_writer_builder::egg_writer_builder() :
            _node_builder< egg_writer >()
    {
    }

    egg_writer_builder::~egg_writer_builder()
    {
    }

    void egg_writer_builder::apply_config( egg_writer* a_node, const scarab::param_node& a_config )
    {
        a_node->set_file_size_limit_mb( a_config.get_value( "file-size-limit-mb", a_node->get_file_size_limit_mb() ) );
        return;
    }



} /* namespace psyllid */
