/*
 * daq_control.cc
 *
 *  Created on: Jan 22, 2016
 *      Author: nsoblath
 */

#include "daq_control.hh"

namespace psyllid
{

    daq_control::daq_control() :
            cancelable(),
            f_is_running( false ),
            f_daq_worker()
    {
    }

    daq_control::~daq_control()
    {
    }

    void daq_control::do_run()
    {
        if( f_canceled )
        {
            throw error() << "daq_control has been canceled";
        }

        if( f_is_running.load() )
        {
            throw run_error() << "Run already in progress";
        }

        std::unique_lock< std::mutex > t_lock( f_worker_mutex );

        if( f_daq_worker )
        {
            throw run_error() << "Run may be in the process of starting or stoping already (daq_worker exists)";
        }

        f_daq_worker.reset( new daq_worker() );

        t_lock.unlock();

        f_is_running.store( true );

        midge::thread t_worker_thread;
        t_worker_thread.start( f_daq_worker.get(), &daq_worker::execute );

        t_worker_thread.join();

        f_is_running.store( false );

        f_daq_worker.reset();

        return;
    }

    void daq_control::do_cancellation()
    {
        std::unique_lock< std::mutex > t_lock( f_worker_mutex );
        if( f_daq_worker )
        {
            f_daq_worker->cancel();
        }
        return;
    }

} /* namespace psyllid */
