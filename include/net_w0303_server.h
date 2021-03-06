#pragma once

//glog
#include <glog/logging.h>

#include <boost/algorithm/string/trim.hpp>

#include "../include/net.h"
#include "../include/net_w0303_common.h"

#include "../include/data.h"

class IPCServer : public yf::net::server_interface<CustomMsgTypes>
{
public:
    IPCServer(uint16_t nPort) : yf::net::server_interface<CustomMsgTypes>(nPort)
    {

    }

//Arm Methods
//
//Getter
public:
    // Status
    //
    // Mission Status
    yf::data::common::MissionStatus GetArmMissionStatus()
    {
        std::unique_lock<std::mutex> ul_arm_status(mux_arm_Blocking);        // create a unique lock, not blocking now.
        cv_arm_Blocking.wait(ul_arm_status);
        return arm_mission_status;
    }

    // Connection Status
    yf::data::common::ConnectionStatus GetArmConnectionStatus()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return arm_connection_status;
    }

    //
    std::string GetArmLatestNetMsg()
    {
        if(!arm_net_recv_msgs.empty())
        {
            return arm_net_recv_msgs.back();
        }
        else
        {
            std::cerr << "Not msg from Arm!" << std::endl;
        }
    }

    std::deque<std::string> GetArmAllNetMsg()
    {
        if(!arm_net_recv_msgs.empty())
        {
            return arm_net_recv_msgs;
        }
        else
        {
            std::cerr << "Not msg from Arm!" << std::endl;
        }
    }

    std::shared_ptr<yf::net::connection<CustomMsgTypes>> GetArmClient()
    {
        return client_arm;
    }

    uint32_t GetArmConnectionId()
    {
        return arm_connection_id;
    }

    void WaitConnectionFromArm()
    {
        while(arm_connection_id == 0)
        {
            std::unique_lock<std::mutex> ul(mux_Blocking);        // create a unique lock, not blocking now.
            cv_Blocking.wait(ul);                                  // blocking now! wait for "ul" to be notified.
        }
    }

//IPC2 Methods
//
//
public:

    std::shared_ptr<yf::net::connection<CustomMsgTypes>> GetIPC2Client()
    {
        return client_ipc2;
    }

    uint32_t GetIPC2ConnectionId()
    {
        return ipc2_connection_id;
    }

    void WaitConnectionFromIPC2()
    {
        while(ipc2_connection_id == 0)
        {
            std::unique_lock<std::mutex> ul(mux_Blocking);        // create a unique lock, not blocking now.
            cv_Blocking.wait(ul);                                  // blocking now! wait for "ul" to be notified.
        }
    }

protected:

    std::mutex mux_Blocking;
    std::condition_variable cv_Blocking;

    //For Arm
    std::mutex mux_arm_Blocking;
    std::condition_variable cv_arm_Blocking;

    uint32_t arm_connection_id{};
    std::shared_ptr<yf::net::connection<CustomMsgTypes>> client_arm;

    // todo: manage the size of queue. --- (done1/2)
    std::string pre_recv_msg;
    std::deque<std::string> arm_net_recv_msgs;

    yf::data::common::ConnectionStatus arm_connection_status;            // arm network connection status
    yf::data::common::MissionStatus    arm_mission_status;
    yf::data::common::ConnectionStatus arm_ln_status;             // arm listen node connection status

    //for ipc2
    std::shared_ptr<yf::net::connection<CustomMsgTypes>> client_ipc2;
    uint32_t ipc2_connection_id{};
    yf::data::common::ConnectionStatus ipc2_net_status;            // ipc2 network connection status
    yf::data::common::MissionStatus    ipc2_mission_status;

protected:

    virtual bool OnClientConnect(std::shared_ptr<yf::net::connection<CustomMsgTypes>> client)
    {
        // verify which device!
        yf::net::message<CustomMsgTypes> msg;
        std::string str_verify = "which device";
        msg.body.resize(str_verify.size());
        msg.body.assign(str_verify.begin(),str_verify.end());

        client->SendRawMsg(msg);
        return true;
    }

    // Called when a client appears to have disconnected
    virtual void OnClientDisconnect(std::shared_ptr<yf::net::connection<CustomMsgTypes>> client)
    {
        // Clear arm connection id.
        if(client->GetID() == arm_connection_id)
        {
            // clear connection id, client ptr, arm net status, arm mission status.
            arm_connection_id = 0;
            client_arm = nullptr;
            arm_connection_status = yf::data::common::ConnectionStatus::Disconnected;
            arm_mission_status = yf::data::common::MissionStatus::Error;
            std::cout << "Removing Arm. client [" << client->GetID() << "]\n";
            LOG(INFO) << "Removing Arm. client [" << client->GetID() << "]";
        }

        // Clear ipc2 connection id.
        if(client->GetID() == ipc2_connection_id)
        {
            ipc2_connection_id = 0;
            client_ipc2 = nullptr;
            ipc2_net_status = yf::data::common::ConnectionStatus::Disconnected;
            ipc2_mission_status = yf::data::common::MissionStatus::Error;
            std::cout << "Removing IPC2. client [" << client->GetID() << "]\n";
            LOG(INFO) << "Removing IPC2. client [" << client->GetID() << "]";
        }
    }

    // Called when a message arrives
    virtual void OnMessage(std::shared_ptr<yf::net::connection<CustomMsgTypes>> client, yf::net::message<CustomMsgTypes>& msg)
    {
        // Verify each connection with sever.
        //
        // 1. get latest msg

        std::string latest_msg(msg.body.begin(), msg.body.end());
        boost::trim_right(latest_msg);

        // 2. classify each device based on what they return
        auto index_arm     = latest_msg.find("TM5-900");
        auto index_ipc2    = latest_msg.find("IPC2");

        // todo: fine tune the connection verify step. IPC can only accept Arm and IPC2.
        // For first time connection. ipc_server will collect each connection, because (arm_connection_id == 0)
        // (1) ipc_server will collect the client_id.
        // (2) and then ipc_server will return client to our arm_client, ipc2_client for further manipulation.
        if(this->arm_connection_id == 0)
        {
            if (index_arm != std::string::npos)
            {
                // todo: not clear information about whether the connection are robust and stable or not.
                //  (1) Need to check by experience.
                // Assign client id to each device.
                //
                this->arm_connection_id = client->GetID();
                client_arm = client;
                arm_connection_status = yf::data::common::ConnectionStatus::Connected;

                // Notify WaitForConnectionFromArm()
                std::unique_lock<std::mutex> ul(mux_Blocking);
                cv_Blocking.notify_one();
            }
        }

        if(this->ipc2_connection_id == 0)
        {
            if (index_ipc2 != std::string::npos)
            {
                this->ipc2_connection_id = client->GetID();
                client_ipc2 = client;
                ipc2_net_status = yf::data::common::ConnectionStatus::Connected;

                std::unique_lock<std::mutex> ul(mux_Blocking);
                cv_Blocking.notify_one();
            }
        }

        // Todo: Msg for Arm, Update Arm Status.
        //  1. preserve latest no repeated Network msg in Q --- arm_net_recv_msgs
        //  2. Parse received msg
        //  3. Update the arm status...
        if(this->arm_connection_id == client->GetID())
        {
            // 1.
            if(pre_recv_msg != latest_msg)
            {
                //  push back to msg deque
                arm_net_recv_msgs.push_back(latest_msg);

                pre_recv_msg = latest_msg;

                LOG(INFO) << "[IPC1 <--- Arm]: " << latest_msg ;
            }

            // 2.
            auto index_error    = latest_msg.find("Arm Error");
            auto index_running  = latest_msg.find("Arm Running");
            auto index_idle     = latest_msg.find("Arm Idle");
            auto index_pause    = latest_msg.find("Arm Pause");
            auto index_finish   = latest_msg.find("Arm Finish");

            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // wait 50 ms

            // 3.
            //
            // for error status
            if (index_error != std::string::npos)
            {
                //Update Arm Network Status
                arm_mission_status = yf::data::common::MissionStatus::Error;
                std::unique_lock<std::mutex> ul_arm_status(mux_arm_Blocking);
                cv_arm_Blocking.notify_one();
                std::cout << "IPC know Error command!" << std::endl;
            }
            // for running status
            else
            if (index_running != std::string::npos)
            {
                // update listen node connection status to IPC1
                auto index_EnterListenNode  = latest_msg.find("EnterListenNode");

                if (index_EnterListenNode != std::string::npos)
                {
                    arm_ln_status = yf::data::common::ConnectionStatus::Connected;
                }

                //update arm network status
                arm_mission_status = yf::data::common::MissionStatus::Running;
                std::unique_lock<std::mutex> ul_arm_status(mux_arm_Blocking);
                cv_arm_Blocking.notify_one();
                std::cout << "IPC know robot is running! " << std::endl;
            }
            // for idle status
            else
            if (index_idle != std::string::npos)
            {
                //update arm network status
                arm_mission_status = yf::data::common::MissionStatus::Idle;
                std::unique_lock<std::mutex> ul_arm_status(mux_arm_Blocking);
                cv_arm_Blocking.notify_one();
                std::cout << "IPC know robot is idle! " << std::endl;
            }
            // for pause status
            else
            if  (index_pause != std::string::npos)
            {
                //update arm network status
                arm_mission_status = yf::data::common::MissionStatus::Pause;
                std::unique_lock<std::mutex> ul_arm_status(mux_arm_Blocking);
                cv_arm_Blocking.notify_one();
                std::cout << "IPC know robot is pause! " << std::endl;

            }
            // for finish status
            else
            if(index_finish != std::string::npos)
            {
                // update listen node connection status to IPC1
                auto index_EnterListenNode  = latest_msg.find("EnterListenNode");

                if (index_EnterListenNode != std::string::npos)
                {
                    arm_ln_status = yf::data::common::ConnectionStatus::Disconnected;
                }

                //update arm network status
                arm_mission_status = yf::data::common::MissionStatus::Finish;
                std::unique_lock<std::mutex> ul_arm_status(mux_arm_Blocking);
                cv_arm_Blocking.notify_one();
                std::cout << "IPC know robot task has finished! " << std::endl;
            }
        }
    }
};
