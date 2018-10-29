

#if ETH_JSONRPC && 1

#include <boost/test/unit_test.hpp>
#include <boost/lexical_cast.hpp>
#include <libdevcore/Log.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/CommonJS.h>
#include <libwebthree/WebThree.h>
#include <libethrpc/EthStubServer.h>
#include <libethrpc/CorsHttpServer.h>
#include <jsonrpc/connectors/httpserver.h>
#include <jsonrpc/connectors/httpclient.h>
#include "JsonSpiritHeaders.h"
#include "TestHelper.h"
#include "ethstubclient.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
namespace js = json_spirit;


namespace jsonrpc_tests {

string name = "Ethereum(++) tests";
string dbPath;
dev::WebThreeDirect web3(name, dbPath, true);

auto_ptr<EthStubServer> jsonrpcServer;
auto_ptr<EthStubClient> jsonrpcClient;


struct JsonrpcFixture  {
    JsonrpcFixture()
    {
        cnote << "setup jsonrpc";

        web3.setIdealPeerCount(5);
        web3.ethereum()->setForceMining(true);
        jsonrpcServer = auto_ptr<EthStubServer>(new EthStubServer(new jsonrpc::CorsHttpServer(8080), web3));
        jsonrpcServer->StartListening();
        
        jsonrpcClient = auto_ptr<EthStubClient>(new EthStubClient(new jsonrpc::HttpClient("http://localhost:8080")));
    }
    ~JsonrpcFixture()
    {
        cnote << "teardown jsonrpc";
    }
};
    
BOOST_GLOBAL_FIXTURE(JsonrpcFixture)

BOOST_AUTO_TEST_CASE(jsonrpc_balanceAt)
{
    cnote << "Testing jsonrpc balanceAt...";
    dev::KeyPair key = KeyPair::create();
    auto address = key.address();
    string balance = jsonrpcClient->balanceAt(toJS(address), 0);
    BOOST_CHECK_EQUAL(jsToDecimal(toJS(web3.ethereum()->balanceAt(address))), balance);
}

BOOST_AUTO_TEST_CASE(jsonrpc_block)
{
}

BOOST_AUTO_TEST_CASE(jsonrpc_call)
{
}

BOOST_AUTO_TEST_CASE(jsonrpc_coinbase)
{
    cnote << "Testing jsonrpc coinbase...";
    string coinbase = jsonrpcClient->coinbase();
    BOOST_CHECK_EQUAL(jsToAddress(coinbase), web3.ethereum()->address());
}

BOOST_AUTO_TEST_CASE(jsonrpc_countAt)
{
    cnote << "Testing jsonrpc countAt...";
    dev::KeyPair key = KeyPair::create();
    auto address = key.address();
    double countAt = jsonrpcClient->countAt(toJS(address), 0);
    BOOST_CHECK_EQUAL(countAt, (double)(uint64_t)web3.ethereum()->countAt(address, 0));
}

BOOST_AUTO_TEST_CASE(jsonrpc_defaultBlock)
{
    cnote << "Testing jsonrpc defaultBlock...";
    int defaultBlock = jsonrpcClient->defaultBlock();
    BOOST_CHECK_EQUAL(defaultBlock, web3.ethereum()->getDefault());
}
    
BOOST_AUTO_TEST_CASE(jsonrpc_fromAscii)
{
    cnote << "Testing jsonrpc fromAscii...";
    string testString = "1234567890987654";
    string fromAscii = jsonrpcClient->fromAscii(32, testString);
    BOOST_CHECK_EQUAL(fromAscii, jsFromBinary(testString, 32));

}

BOOST_AUTO_TEST_CASE(jsonrpc_fromFixed)
{
    cnote << "Testing jsonrpc fromFixed...";
    string testString = "1234567890987654";
    double fromFixed = jsonrpcClient->fromFixed(testString);
    BOOST_CHECK_EQUAL(jsFromFixed(testString), fromFixed);
    BOOST_CHECK_EQUAL(testString, jsToFixed(fromFixed));
}

BOOST_AUTO_TEST_CASE(jsonrpc_gasPrice)
{
    cnote << "Testing jsonrpc gasPrice...";
    string gasPrice = jsonrpcClient->gasPrice();
    BOOST_CHECK_EQUAL(gasPrice, toJS(10 * dev::eth::szabo));
}

BOOST_AUTO_TEST_CASE(jsonrpc_isListening)
{
    //TODO
    cnote << "Testing jsonrpc isListening...";
    string testString = "1234567890987654";
}

BOOST_AUTO_TEST_CASE(jsonrpc_isMining)
{
    cnote << "Testing jsonrpc isMining...";

    web3.ethereum()->startMining();
    bool miningOn = jsonrpcClient->isMining();
    BOOST_CHECK_EQUAL(miningOn, web3.ethereum()->isMining());

    web3.ethereum()->stopMining();
    bool miningOff = jsonrpcClient->isMining();
    BOOST_CHECK_EQUAL(miningOff, web3.ethereum()->isMining());
}

BOOST_AUTO_TEST_CASE(jsonrpc_key)
{
    cnote << "Testing jsonrpc key...";
    dev::KeyPair key = KeyPair::create();
    jsonrpcServer->setKeys({key});
    string clientSecret = jsonrpcClient->key();
    jsonrpcServer->setKeys({});
    BOOST_CHECK_EQUAL(jsToSecret(clientSecret), key.secret());
}
    
BOOST_AUTO_TEST_CASE(jsonrpc_keys)
{
    cnote << "Testing jsonrpc keys...";
    std::vector <dev::KeyPair> keys = {KeyPair::create(), KeyPair::create()};
    jsonrpcServer->setKeys(keys);
    Json::Value k = jsonrpcClient->keys();
    jsonrpcServer->setKeys({});
    BOOST_CHECK_EQUAL(k.isArray(), true);
    BOOST_CHECK_EQUAL(k.size(),  keys.size());
    for (unsigned i = 0; i < k.size(); i++)
        BOOST_CHECK_EQUAL(jsToSecret(k[i].asString()) , keys[i].secret());
}

BOOST_AUTO_TEST_CASE(jsonrpc_lll)
{
}

BOOST_AUTO_TEST_CASE(jsonrpc_messages)
{
}

BOOST_AUTO_TEST_CASE(jsonrpc_number)
{
    cnote << "Testing jsonrpc number...";
    int number = jsonrpcClient->number();
    BOOST_CHECK_EQUAL(number, web3.ethereum()->number() + 1);
}

BOOST_AUTO_TEST_CASE(jsonrpc_number2)
{
    cnote << "Testing jsonrpc number2...";
    int number = jsonrpcClient->number();
    BOOST_CHECK_EQUAL(number, web3.ethereum()->number() + 1);
    dev::eth::mine(*(web3.ethereum()), 1);
    int numberAfter = jsonrpcClient->number();
    BOOST_CHECK_EQUAL(number + 1, numberAfter);
    BOOST_CHECK_EQUAL(numberAfter, web3.ethereum()->number() + 1);
}

BOOST_AUTO_TEST_CASE(jsonrpc_peerCount)
{
    cnote << "Testing jsonrpc peerCount...";
    //TODO
}

BOOST_AUTO_TEST_CASE(jsonrpc_secretToAddress)
{
    cnote << "Testing jsonrpc secretToAddress...";
    dev::KeyPair pair = dev::KeyPair::create();
    string address = jsonrpcClient->secretToAddress(toJS(pair.secret()));
    BOOST_CHECK_EQUAL(jsToAddress(address), pair.address());
}

BOOST_AUTO_TEST_CASE(jsonrpc_setListening)
{
    cnote << "Testing jsonrpc setListening...";
    //TODO
}

BOOST_AUTO_TEST_CASE(jsonrpc_setMining)
{
    cnote << "Testing jsonrpc setMining...";

    jsonrpcClient->setMining(true);
    BOOST_CHECK_EQUAL(web3.ethereum()->isMining(), true);

    jsonrpcClient->setMining(false);
    BOOST_CHECK_EQUAL(web3.ethereum()->isMining(), false);
}

BOOST_AUTO_TEST_CASE(jsonrpc_sha3)
{
    cnote << "Testing jsonrpc sha3...";
    string testString = "1234567890987654";
    string sha3 = jsonrpcClient->sha3(testString);
    BOOST_CHECK_EQUAL(jsToFixed<32>(sha3), dev::eth::sha3(jsToBytes(testString)));
}

BOOST_AUTO_TEST_CASE(jsonrpc_stateAt)
{
    cnote << "Testing jsonrpc stateAt...";
    dev::KeyPair key = KeyPair::create();
    auto address = key.address();
    string stateAt = jsonrpcClient->stateAt(toJS(address), 0, "0");
    BOOST_CHECK_EQUAL(toJS(web3.ethereum()->stateAt(address, jsToU256("0"), 0)), stateAt);
}

BOOST_AUTO_TEST_CASE(jsonrpc_toAscii)
{
    cnote << "Testing jsonrpc toAscii...";
    string testString = "1234567890987654";
    string ascii = jsonrpcClient->toAscii(testString);
    BOOST_CHECK_EQUAL(jsToBinary(testString), ascii);
    BOOST_CHECK_EQUAL(testString, jsFromBinary(ascii));     // failing!
}

BOOST_AUTO_TEST_CASE(jsonrpc_toDecimal)
{
    cnote << "Testing jsonrpc toDecimal...";
    string testString = "1234567890987654";
    string decimal = jsonrpcClient->toDecimal(testString);
    BOOST_CHECK_EQUAL(jsToDecimal(testString), decimal);
}

BOOST_AUTO_TEST_CASE(jsonrpc_toFixed)
{
    cnote << "Testing jsonrpc toFixed...";
    double testValue = 123567;
    string fixed = jsonrpcClient->toFixed(testValue);
    BOOST_CHECK_EQUAL(jsToFixed(testValue), fixed);
    BOOST_CHECK_EQUAL(testValue, jsFromFixed(fixed));
}

BOOST_AUTO_TEST_CASE(jsonrpc_transact)
{
    cnote << "Testing jsonrpc transact...";
    dev::KeyPair key = KeyPair::create();
    auto address = key.address();
    auto receiver = KeyPair::create();
    
    web3.ethereum()->setAddress(address);
    dev::eth::mine(*(web3.ethereum()), 1);
    auto balance = web3.ethereum()->balanceAt(address, 0);
    BOOST_REQUIRE(balance > 0);
    auto txAmount = balance / 2u;
    auto gasPrice = 10 * dev::eth::szabo;
    auto gas = dev::eth::c_txGas;
    
    Json::Value t;
    t["from"] = toJS(key.secret());
    t["value"] = jsToDecimal(toJS(txAmount));
    t["to"] = toJS(receiver.address());
    t["data"] = toJS(bytes());
    t["gas"] = toJS(gas);
    t["gasPrice"] = toJS(gasPrice);
    
    jsonrpcClient->transact(t);
    
    dev::eth::mine(*(web3.ethereum()), 1);
    auto balance2 = web3.ethereum()->balanceAt(receiver.address());
    auto messages = jsonrpcClient->messages(Json::Value());
    BOOST_REQUIRE(balance2 > 0);
    BOOST_CHECK_EQUAL(txAmount, balance2);
    BOOST_CHECK_EQUAL(txAmount, jsToU256(messages[0u]["value"].asString()));
}

BOOST_AUTO_TEST_CASE(jsonrpc_transaction)
{
    // TODO! not working?
//    auto messages = jsonrpcClient->messages(Json::Value());
//    auto transactionNumber = messages[0u]["path"][0u].asInt();
//    auto transactionBlock = messages[0u]["block"].asString();
//    Json::Value p = jsonrpcClient->transaction(transactionNumber, transactionBlock);
}

BOOST_AUTO_TEST_CASE(jsonrpc_uncle)
{
}

BOOST_AUTO_TEST_CASE(jsonrpc_watch)
{

}




}

#endif


