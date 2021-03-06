/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/usr/testcore/kernel/msgtest.H $                           */
/*                                                                        */
/* OpenPOWER HostBoot Project                                             */
/*                                                                        */
/* COPYRIGHT International Business Machines Corp. 2012,2014              */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */

/** @file msgtest.H
 *  @brief Testcases for the messaging subsystem of the kernel.
 */

#include <cxxtest/TestSuite.H>
#include <sys/task.h>
#include <sys/msg.h>

class MessagingTest : public CxxTest::TestSuite
{
    public:

        /** Test sync-messaging interfaces */
        void testSync()
        {
            msg_q_t client = msg_q_create(),
                    server = msg_q_create();

            msg_t* sync_msg = NULL;
            int rc = 0;

            tid_t server_task = task_create(MessagingTest::processMsg,
                                            server);

            // Send a message with the normal sync messaging interface.
            sync_msg = msg_allocate();
            sync_msg->type = ECHO;
            sync_msg->data[0] = 1;
            rc = msg_sendrecv(server, sync_msg);
            if (rc)
            {
                TS_FAIL("Failure to send message (msg_sendrecv).");
            }
            else if (sync_msg->data[0] != (2 << 1))
            {
                TS_FAIL("Message response doesn't match expected value for "
                        "sync message.  %d", sync_msg->data[0]);
            }
            msg_free(sync_msg);

            // Send a message with the non-blocking sync message interface.
            barrier_t barrier;
            barrier_init(&barrier, 2);
            sync_msg = msg_allocate();
            sync_msg->type = ECHO;
            sync_msg->data[0] = 2;
            sync_msg->data[1] = reinterpret_cast<uint64_t>(&barrier);
            rc = msg_sendrecv_noblk(server, sync_msg, client);
            barrier_wait(&barrier);
            sync_msg = msg_wait(client);
            if (rc)
            {
                TS_FAIL("Failure to send message (msg_sendrecv_noblk)");
            }
            else if (sync_msg->data[0] != (2 << 2))
            {
                TS_FAIL("Message response doesn't match expected value for "
                        "no-blk sync message.  %d", sync_msg->data[0]);
            }
            msg_free(sync_msg);
            barrier_destroy(&barrier);

            // Shutdown the child thread.
            msg_t* shutdown_msg = msg_allocate();
            shutdown_msg->type = SHUTDOWN;
            msg_send(server, shutdown_msg);

            task_wait_tid(server_task, NULL, NULL);

        };

    private:

        enum msg_types { SHUTDOWN, ECHO };

        static void* processMsg(void* _msgQ)
        {
            msg_q_t msgQ = static_cast<msg_q_t>(_msgQ);

            while(msg_t* msg = msg_wait(msgQ))
            {
                switch (msg->type)
                {
                    case SHUTDOWN: // Shutdown.
                        msg_free(msg);
                        return NULL;

                    case ECHO:
                        if (msg->data[1])
                        {
                            barrier_wait(
                                reinterpret_cast<barrier_t*>(msg->data[1]));
                        }
                        msg->data[0] = (2 << msg->data[0]);
                        msg_respond(msgQ, msg);
                        break;
                }
            }
            return NULL;
        }
};
