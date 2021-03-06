#ifndef JSONRPC_CPP_STUB_DEV_RPC_NETFACE_H_
#define JSONRPC_CPP_STUB_DEV_RPC_NETFACE_H_

#include "ModularServer.h"

namespace dev {
    namespace rpc {
        class NetFace : public ServerInterface<NetFace>
        {
            public:
                NetFace()
                {
                    this->bindAndAddMethod(jsonrpc::Procedure("net_version", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING,  NULL), &dev::rpc::NetFace::net_versionI);
                    this->bindAndAddMethod(jsonrpc::Procedure("net_peerCount", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_STRING,  NULL), &dev::rpc::NetFace::net_peerCountI);
                    this->bindAndAddMethod(jsonrpc::Procedure("net_listening", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_BOOLEAN,  NULL), &dev::rpc::NetFace::net_listeningI);
                }

                inline virtual void net_versionI(const Json::Value &request, Json::Value &response)
                {
                    (void)request;
                    response = this->net_version();
                }
                inline virtual void net_peerCountI(const Json::Value &request, Json::Value &response)
                {
                    (void)request;
                    response = this->net_peerCount();
                }
                inline virtual void net_listeningI(const Json::Value &request, Json::Value &response)
                {
                    (void)request;
                    response = this->net_listening();
                }
                virtual std::string net_version() = 0;
                virtual std::string net_peerCount() = 0;
                virtual bool net_listening() = 0;
        };

    }
}
#endif //JSONRPC_CPP_STUB_DEV_RPC_NETFACE_H_
