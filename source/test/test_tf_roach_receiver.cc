/*
 * test_tf_roach_receiver.cc
 *
 *  Created on: Dec 28, 2015
 *      Author: nsoblath
 *
 *  Suggested UDP client: roach_simulator.go
 *
 *  Usage: > test_tf_roach_receiver [options]
 *
 *  Parameters:
 *    - type: (string) server type name; options are "socket" (default) and "fpa"
 *    - interface: (string) network interface name; this is only needed if using the FPA server; default is "eth1"
 */


#include "psyllid_error.hh"
#include "packet_receiver_socket.hh"
#include "terminator.hh"
#include "tf_roach_receiver.hh"

#include "diptera.hh"

#include "configurator.hh"
#include "logger.hh"
#include "param.hh"

#include <signal.h>

using namespace psyllid;

LOGGER( plog, "test_tf_roach_receiver" );

scarab::cancelable* f_cancelable = nullptr;

void cancel( int )
{
    LINFO( plog, "Attempting to cancel" );
    if( f_cancelable != nullptr ) f_cancelable->cancel();
    return;
}

int main( int argc, char** argv )
{
    try
    {
        scarab::param_node t_default_config;
        t_default_config.add( "ip", new scarab::param_value( "127.0.0.1" ) );
        t_default_config.add( "port", new scarab::param_value( 23530 ) );

        scarab::configurator t_configurator( argc, argv, &t_default_config );

        std::string t_ip( t_configurator.get< std::string >( "ip" ) );
        unsigned t_port = t_configurator.get< unsigned >( "port" );

        LINFO( plog, "Creating and configuring nodes" );

        midge::diptera* t_root = new midge::diptera();

        packet_receiver_socket* t_pck_rec = new packet_receiver_socket();
        t_pck_rec->set_name( "pck_rec" );
        t_pck_rec->set_length( 10 );
        t_pck_rec->set_port( t_port );
        t_pck_rec->ip() = t_ip;
        t_root->add( t_pck_rec );

        tf_roach_receiver* t_tfr_rec = new tf_roach_receiver();
        t_tfr_rec->set_name( "tfr_rec" );
        t_tfr_rec->set_time_length( 10 );
        t_tfr_rec->set_start_paused( false );
        t_root->add( t_tfr_rec );

        terminator_time_data* t_term_t = new terminator_time_data();
        t_term_t->set_name( "term_t" );
        t_root->add( t_term_t );

        terminator_freq_data* t_term_f = new terminator_freq_data();
        t_term_f->set_name( "term_f" );
        t_root->add( t_term_f );

        LINFO( plog, "Connecting nodes" );

        t_root->join( "pck_rec.out_0:tfr_rec.in_0" );
        t_root->join( "tfr_rec.out_0:term_t.in_0" );
        t_root->join( "tfr_rec.out_1:term_f.in_0" );

        LINFO( plog, "Exit with ctrl-c" );

        // set up signal handling for canceling with ctrl-c
        f_cancelable = t_pck_rec;
        signal( SIGINT, cancel );

        LINFO( plog, "Executing" );

        t_root->run( "pck_rec:tfr_rec:term_t:term_f" );

        LINFO( plog, "Execution complete" );

        // un-setup signal handling
        f_cancelable = nullptr;

        delete t_root;

        return 0;
    }
    catch( std::exception& e )
    {
        LERROR( plog, "Exception caught: " << e.what() );
        return -1;
    }

}
