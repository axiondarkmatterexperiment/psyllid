/*
 * batch_executor.hh
 *
 * Created on: April 12, 2018
 *     Author: laroque
 */

#ifndef PSYLLID_BATCH_EXECUTOR_HH_
#define PSYLLID_BATCH_EXECUTOR_HH_

#include "cancelable.hh"
#include "param.hh"

namespace psyllid
{
    /*!
     @class batch_executor
     @author B. H. LaRoque

     @brief A class sequentially execute a list of actions, equivalent to a sequence of dripline requests

     @details

     ... tbd ...

    */

    // forward declarations
    class request_receiver;

    class batch_executor : public scarab::cancelable
    {
        public:
            batch_executor();
            batch_executor( const scarab::param_node& a_master_config, std::shared_ptr<psyllid::request_receiver> a_request_receiver );
            virtual ~batch_executor();

            void execute();

        private:
            scarab::param_array f_actions_array;
            std::shared_ptr<request_receiver> f_request_receiver;

            virtual void do_cancellation();

    };

} /* namespace psyllid */

#endif /*PSYLLID_BATCH_EXECUTOR_HH_*/
