#include "State.h"

#include "Block.h"
#include "BlockChain.h"
#include "ExtVM.h"
#include "TransactionQueue.h"
#include "DposVote.h"
#include <libdevcore/Assertions.h>
#include <libdevcore/DBFactory.h>
#include <libdevcore/TrieHash.h>
#include <libevm/VMFactory.h>
#include <boost/filesystem.hpp>
#include <boost/timer.hpp>
#include <libdevcore/CommonJS.h>

using namespace std;
using namespace dev;
using namespace dev::brc;
namespace fs = boost::filesystem;

State::State(u256 const& _accountStartNonce, OverlayDB const& _db, BaseState _bs)
  : m_db(_db), m_state(&m_db), m_accountStartNonce(_accountStartNonce)
{
    if (_bs != BaseState::PreExisting)
        // Initialise to the state entailed by the genesis block; this guarantees the trie is built
        // correctly.
        m_state.init();
}

State::State(State const& _s)
  : m_db(_s.m_db),
    m_state(&m_db, _s.m_state.root(), Verification::Skip),
    m_cache(_s.m_cache),
    m_unchangedCacheEntries(_s.m_unchangedCacheEntries),
    m_nonExistingAccountsCache(_s.m_nonExistingAccountsCache),
    m_touched(_s.m_touched),
    m_accountStartNonce(_s.m_accountStartNonce)
{}

OverlayDB State::openDB(fs::path const& _basePath, h256 const& _genesisHash, WithExisting _we)
{
    fs::path path = _basePath.empty() ? db::databasePath() : _basePath;

    if (db::isDiskDatabase() && _we == WithExisting::Kill)
    {
        clog(VerbosityDebug, "statedb") << "Killing state database (WithExisting::Kill).";
        fs::remove_all(path / fs::path("state"));
    }

    path /=
        fs::path(toHex(_genesisHash.ref().cropped(0, 4))) / fs::path(toString(c_databaseVersion));
    if (db::isDiskDatabase())
    {
        fs::create_directories(path);
        DEV_IGNORE_EXCEPTIONS(fs::permissions(path, fs::owner_all));
    }

    try
    {
        std::unique_ptr<db::DatabaseFace> db = db::DBFactory::create(path / fs::path("state"));
        clog(VerbosityTrace, "statedb") << "Opened state DB.";
        return OverlayDB(std::move(db));
    }
    catch (boost::exception const& ex)
    {
        cwarn << boost::diagnostic_information(ex) << '\n';
        if (!db::isDiskDatabase())
            throw;
        else if (fs::space(path / fs::path("state")).available < 1024)
        {
            cwarn << "Not enough available space found on hard drive. Please free some up and then "
                     "re-run. Bailing.";
            BOOST_THROW_EXCEPTION(NotEnoughAvailableSpace());
        }
        else
        {
            cwarn << "Database " << (path / fs::path("state"))
                  << "already open. You appear to have another instance of brcdChain running. "
                     "Bailing.";
            BOOST_THROW_EXCEPTION(DatabaseAlreadyOpen());
        }
    }
}

void State::populateFrom(AccountMap const& _map)
{
    auto it = _map.find(Address("0xffff19f5ada6a28821ce0ed74c605c8c086ceb35"));
    Account a;
    if (it != m_cache.end())
        a = it->second;
    brc::commit(_map, m_state);
    commit(State::CommitBehaviour::KeepEmptyAccounts);
}

u256 const& State::requireAccountStartNonce() const
{
    if (m_accountStartNonce == Invalid256)
        BOOST_THROW_EXCEPTION(InvalidAccountStartNonceInState());
    return m_accountStartNonce;
}

void State::noteAccountStartNonce(u256 const& _actual)
{
    if (m_accountStartNonce == Invalid256)
        m_accountStartNonce = _actual;
    else if (m_accountStartNonce != _actual)
        BOOST_THROW_EXCEPTION(IncorrectAccountStartNonceInState());
}

void State::removeEmptyAccounts()
{
    for (auto& i : m_cache)
        if (i.second.isDirty() && i.second.isEmpty())
            i.second.kill();
}

State& State::operator=(State const& _s)
{
    if (&_s == this)
        return *this;
    m_db = _s.m_db;
    m_state.open(&m_db, _s.m_state.root(), Verification::Skip);
    m_cache = _s.m_cache;
    m_unchangedCacheEntries = _s.m_unchangedCacheEntries;
    m_nonExistingAccountsCache = _s.m_nonExistingAccountsCache;
    m_touched = _s.m_touched;
    m_accountStartNonce = _s.m_accountStartNonce;
    return *this;
}

Account const* State::account(Address const& _a) const
{
    return const_cast<State*>(this)->account(_a);
}

Account* State::account(Address const& _addr)
{
    auto it = m_cache.find(_addr);
    if (it != m_cache.end())
        return &it->second;

    if (m_nonExistingAccountsCache.count(_addr))
        return nullptr;

    // Populate basic info.
    string stateBack = m_state.at(_addr);
    if (stateBack.empty())
    {
        m_nonExistingAccountsCache.insert(_addr);
        return nullptr;
    }

    clearCacheIfTooLarge();

    RLP state(stateBack);

    const bytes _b = state[6].toBytes();
    RLP vote(_b);
    size_t num = vote[0].toInt<size_t>();
    std::unordered_map<Address, u256> _vote;
    for (size_t j=1 ; j <= num; j++)
    {
        std::pair<Address, u256> _pair = vote[j].toPair<Address, u256>();
        _vote.insert(_pair);
    }

    auto i = m_cache.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(_addr),
        std::forward_as_tuple(state[0].toInt<u256>(), state[1].toInt<u256>(), state[2].toHash<h256>(), state[3].toHash<h256>(), state[4].toInt<u256>(), state[5].toInt<u256>(), state[7].toInt<u256>(), state[8].toInt<u256>(), state[9].toInt<u256>(),Account::Unchanged)
    );
    i.first->second.setVoteDate(_vote);

    m_unchangedCacheEntries.push_back(_addr);
    return &i.first->second;
}

void State::clearCacheIfTooLarge() const
{
    // TODO: Find a good magic number
    while (m_unchangedCacheEntries.size() > 1000)
    {
        // Remove a random element
        // FIXME: Do not use random device as the engine. The random device should be only used to
        // seed other engine.
        size_t const randomIndex = std::uniform_int_distribution<size_t>(
            0, m_unchangedCacheEntries.size() - 1)(dev::s_fixedHashEngine);

        Address const addr = m_unchangedCacheEntries[randomIndex];
        swap(m_unchangedCacheEntries[randomIndex], m_unchangedCacheEntries.back());
        m_unchangedCacheEntries.pop_back();

        auto cacheEntry = m_cache.find(addr);
        if (cacheEntry != m_cache.end() && !cacheEntry->second.isDirty())
            m_cache.erase(cacheEntry);
    }
}

void State::commit(CommitBehaviour _commitBehaviour)
{
    if (_commitBehaviour == CommitBehaviour::RemoveEmptyAccounts)
        removeEmptyAccounts();
    m_touched += dev::brc::commit(m_cache, m_state);
    m_changeLog.clear();
    m_cache.clear();
    m_unchangedCacheEntries.clear();
}

unordered_map<Address, u256> State::addresses() const
{
#if BRC_FATDB
    unordered_map<Address, u256> ret;
    for (auto& i : m_cache)
        if (i.second.isAlive())
            ret[i.first] = i.second.balance();
    for (auto const& i : m_state)
        if (m_cache.find(i.first) == m_cache.end())
            ret[i.first] = RLP(i.second)[1].toInt<u256>();
    return ret;
#else
    BOOST_THROW_EXCEPTION(InterfaceNotSupported() << errinfo_interface("State::addresses()"));
#endif
}

std::pair<State::AddressMap, h256> State::addresses(
    h256 const& _beginHash, size_t _maxResults) const
{
    AddressMap addresses;
    h256 nextKey;

#if BRC_FATDB
    for (auto it = m_state.hashedLowerBound(_beginHash); it != m_state.hashedEnd(); ++it)
    {
        auto const address = Address(it.key());
        auto const itCachedAddress = m_cache.find(address);

        // skip if deleted in cache
        if (itCachedAddress != m_cache.end() && itCachedAddress->second.isDirty() &&
            !itCachedAddress->second.isAlive())
            continue;

        // break when _maxResults fetched
        if (addresses.size() == _maxResults)
        {
            nextKey = h256((*it).first);
            break;
        }

        h256 const hashedAddress((*it).first);
        addresses[hashedAddress] = address;
    }
#endif

    // get addresses from cache with hash >= _beginHash (both new and old touched, we can't
    // distinguish them) and order by hash
    AddressMap cacheAddresses;
    for (auto const& addressAndAccount : m_cache)
    {
        auto const& address = addressAndAccount.first;
        auto const addressHash = sha3(address);
        auto const& account = addressAndAccount.second;
        if (account.isDirty() && account.isAlive() && addressHash >= _beginHash)
            cacheAddresses.emplace(addressHash, address);
    }

    // merge addresses from DB and addresses from cache
    addresses.insert(cacheAddresses.begin(), cacheAddresses.end());

    // if some new accounts were created in cache we need to return fewer results
    if (addresses.size() > _maxResults)
    {
        auto itEnd = std::next(addresses.begin(), _maxResults);
        nextKey = itEnd->first;
        addresses.erase(itEnd, addresses.end());
    }

    return {addresses, nextKey};
}


void State::setRoot(h256 const& _r)
{
    m_cache.clear();
    m_unchangedCacheEntries.clear();
    m_nonExistingAccountsCache.clear();
    //  m_touched.clear();
    m_state.setRoot(_r);
}

bool State::addressInUse(Address const& _id) const
{
    return !!account(_id);
}

bool State::accountNonemptyAndExisting(Address const& _address) const
{
    if (Account const* a = account(_address))
        return !a->isEmpty();
    else
        return false;
}

bool State::addressHasCode(Address const& _id) const
{
    if (auto a = account(_id))
        return a->codeHash() != EmptySHA3;
    else
        return false;
}

u256 State::balance(Address const& _id) const
{
    if (auto a = account(_id))
        return a->balance();
    else
        return 0;
}

u256 State::ballot(Address const& _id) const
{
    if (auto a = account(_id))
    {
        return a->ballot();
    }
    else
        return 0;
}

void State::incNonce(Address const& _addr)
{
    if (Account* a = account(_addr))
    {
        auto oldNonce = a->nonce();
        a->incNonce();
        m_changeLog.emplace_back(_addr, oldNonce);
    }
    else
        // This is possible if a transaction has gas price 0.
        createAccount(_addr, Account(requireAccountStartNonce() + 1, 0));
}

void State::setNonce(Address const& _addr, u256 const& _newNonce)
{
    if (Account* a = account(_addr))
    {
        auto oldNonce = a->nonce();
        a->setNonce(_newNonce);
        m_changeLog.emplace_back(_addr, oldNonce);
    }
    else
        // This is possible when a contract is being created.
        createAccount(_addr, Account(_newNonce, 0));
}

void State::addBalance(Address const& _id, u256 const& _amount)
{
    if (Account* a = account(_id))
    {
        // Log empty account being touched. Empty touched accounts are cleared
        // after the transaction, so this event must be also reverted.
        // We only log the first touch (not dirty yet), and only for empty
        // accounts, as other accounts does not matter.
        // TODO: to save space we can combine this event with Balance by having
        //       Balance and Balance+Touch events.
        if (!a->isDirty() && a->isEmpty())
            m_changeLog.emplace_back(Change::Touch, _id);

        // Increase the account balance. This also is done for value 0 to mark
        // the account as dirty. Dirty account are not removed from the cache
        // and are cleared if empty at the end of the transaction.
        a->addBalance(_amount);
    }
    else
        createAccount(_id, {requireAccountStartNonce(), _amount});

    if (_amount)
        m_changeLog.emplace_back(Change::Balance, _id, _amount);
}

void State::addBallot(Address const& _id, u256 const& _amount)
{
    if (Account* a = account(_id))
    {
        if (!a->isDirty() && a->isEmpty())
            m_changeLog.emplace_back(Change::Touch, _id);
        a->addBallot(_amount);
    }
    else
        BOOST_THROW_EXCEPTION(InvalidAddress() << errinfo_interface("State::addBallot()"));
        //createAccount(_id, {requireAccountStartNonce(), _amount});

    if (_amount)
        m_changeLog.emplace_back(Change::Ballot, _id, _amount);
}

void State::subBalance(Address const& _addr, u256 const& _value)
{
    if (_value == 0)
        return;

    Account* a = account(_addr);
    if (!a || a->balance() < _value)
        // TODO: I expect this never happens.
        BOOST_THROW_EXCEPTION(NotEnoughCash());

    // Fall back to addBalance().
    addBalance(_addr, 0 - _value);
}

void State::subBallot(Address const& _addr, u256 const& _value)
{
    if (_value == 0)
        return;

    Account* a = account(_addr);
    if (!a || a->ballot() < _value)
        BOOST_THROW_EXCEPTION(NotEnoughBallot());

    addBallot(_addr, 0 - _value);
}

void State::setBalance(Address const& _addr, u256 const& _value)
{
    Account* a = account(_addr);
    u256 original = a ? a->balance() : 0;

    // Fall back to addBalance().
    addBalance(_addr, _value - original);
}

// BRC接口实现
u256 State::BRC(Address const& _id) const
{
    if (auto* a = account(_id))
    {
        return a->BRC();
    }
    else
    {
        return 0;
    }
}

void State::addBRC(Address const& _addr, u256 const& _value)
{
    if (Account* a = account(_addr))
    {
        if (!a->isDirty() && a->isEmpty())
            m_changeLog.emplace_back(Change::Touch, _addr);
        a->addBRC(_value);
    }
    else
        createAccount(_addr, {requireAccountStartNonce(), 0,_value});

    if (_value)
        m_changeLog.emplace_back(Change::BRC, _addr, _value);
}

void State::subBRC(Address const& _addr, u256 const& _value)
{
    if (_value == 0)
        return;

    Account* a = account(_addr);
    if (!a || a->BRC() < _value)
        // TODO: I expect this never happens.
        BOOST_THROW_EXCEPTION(NotEnoughCash());

    // Fall back to addBalance().
    addBRC(_addr, 0 - _value);
}

void State::setBRC(Address const& _addr, u256 const& _value)
{
    Account* a = account(_addr);
    u256 original = a ? a->BRC() : 0;

    // Fall back to addBalance().
    addBRC(_addr, _value - original);
}

// FBRC 相关接口实现

u256 State::FBRC(Address const& _id) const
{
    if (auto a = account(_id))
    {
        return a->FBRC();
    } else
	{
        return 0;
	}
}


void State::addFBRC(Address const& _addr, u256 const& _value)
{
    if (Account* a = account(_addr))
    {
        if (!a->isDirty() && a->isEmpty())
            m_changeLog.emplace_back(Change::Touch, _addr);
        a->addFBRC(_value);
    }

    if (_value)
        m_changeLog.emplace_back(Change::FBRC, _addr, _value);
}

void State::subFBRC(Address const& _addr, u256 const& _value)
{
    if (_value == 0)
        return;

    Account* a = account(_addr);
    if (!a || a->FBRC() < _value)
        // TODO: I expect this never happens.
        BOOST_THROW_EXCEPTION(NotEnoughCash());

    // Fall back to addBalance().
    addFBRC(_addr, 0 - _value);
}


//FBalance接口实现
u256 State::FBalance(Address const& _id) const
{
    if (auto a = account(_id))
    {
        return a->FBalance();
    }
    else
    {
        return 0;
    }
}


void State::addFBalance(Address const& _addr, u256 const& _value)
{
    if (Account* a = account(_addr))
    {
        if (!a->isDirty() && a->isEmpty())
            m_changeLog.emplace_back(Change::Touch, _addr);
        a->addFBalance(_value);
    }

    if (_value)
        m_changeLog.emplace_back(Change::FBalance, _addr, _value);
}

void State::subFBalance(Address const& _addr, u256 const& _value)
{
    if (_value == 0)
        return;

    Account* a = account(_addr);
    if (!a || a->FBalance() < _value)
        // TODO: I expect this never happens.
        BOOST_THROW_EXCEPTION(NotEnoughCash());

    // Fall back to addBalance().
    addFBalance(_addr, 0 - _value);
}


    //交易挂单接口
void State::brcPendingOrder(Address const& _addr, u256 const& _value, size_t _pendingOrderPrice,
    h256 _pendingOrderHash, size_t _pendingOrderType)
{
    u256 _nowTime = utcTimeMilliSec();
    if (_pendingOrderType == dev::brc::PendingOrderEnum::EBuyBrcPendingOrder)
    {
        subBalance(_addr, _value * _pendingOrderPrice);
        addFBalance(_addr, _value * _pendingOrderPrice);
    }
    else if (_pendingOrderType == dev::brc::PendingOrderEnum::ESellBrcPendingOrder)
    {
        subBRC(_addr, _value);
        addFBRC(_addr, _value);
    }
    //交易所挂单
}

void State::fuelPendingOrder(Address const& _addr, u256 const& _value, size_t _pendingOrderPrice,
    h256 _pendingOrderHash, size_t _pendingOrderType)
{
    u256 _nowTime = utcTimeMilliSec();
	if (_pendingOrderType == dev::brc::PendingOrderEnum::EBuyFuelPendingOrder)
    {
        subBRC(_addr, _value * _pendingOrderPrice);
        addFBRC(_addr, _value * _pendingOrderPrice);
    }
    else if (_pendingOrderType == dev::brc::PendingOrderEnum::ESellFuelPendingOrder)
    {
        subBalance(_addr, _value);
        addFBalance(_addr, _value);
    }
    //交易所挂单
}


void State::cancelPendingOrder(
    Address const& _addr, u256 const& _value, size_t _pendingOrderType, h256 _pendingOrderHash)
{
    if (_pendingOrderType == dev::brc::PendingOrderEnum::EBuyBrcPendingOrder)
    {
    }
    else if (_pendingOrderType == dev::brc::PendingOrderEnum::ESellBrcPendingOrder)
    {
    }
    else if (_pendingOrderType == dev::brc::PendingOrderEnum::EBuyFuelPendingOrder)
    {
    }
    else if (_pendingOrderType == dev::brc::PendingOrderEnum::ESellFuelPendingOrder)
    {
    }

    //取消交易所挂单
    subFBRC(_addr, _value);
    addBRC(_addr, _value);
}

void State::createContract(Address const& _address)
{
    createAccount(_address, {requireAccountStartNonce(), 0});
}

void State::createAccount(Address const& _address, Account const&& _account)
{
    assert(!addressInUse(_address) && "Account already exists");
    m_cache[_address] = std::move(_account);
    m_nonExistingAccountsCache.erase(_address);
    m_changeLog.emplace_back(Change::Create, _address);
}

void State::kill(Address _addr)
{
    if (auto a = account(_addr))
        a->kill();
    // If the account is not in the db, nothing to kill.
}

u256 State::getNonce(Address const& _addr) const
{
    if (auto a = account(_addr))
        return a->nonce();
    else
        return m_accountStartNonce;
}

u256 State::storage(Address const& _id, u256 const& _key) const
{
    if (Account const* a = account(_id))
        return a->storageValue(_key, m_db);
    else
        return 0;
}

void State::setStorage(Address const& _contract, u256 const& _key, u256 const& _value)
{
    m_changeLog.emplace_back(_contract, _key, storage(_contract, _key));
    m_cache[_contract].setStorage(_key, _value);
}

u256 State::originalStorageValue(Address const& _contract, u256 const& _key) const
{
    if (Account const* a = account(_contract))
        return a->originalStorageValue(_key, m_db);
    else
        return 0;
}

void State::clearStorage(Address const& _contract)
{
    h256 const& oldHash{m_cache[_contract].baseRoot()};
    if (oldHash == EmptyTrie)
        return;
    m_changeLog.emplace_back(Change::StorageRoot, _contract, oldHash);
    m_cache[_contract].clearStorage();
}

map<h256, pair<u256, u256>> State::storage(Address const& _id) const
{
#if BRC_FATDB
    map<h256, pair<u256, u256>> ret;

    if (Account const* a = account(_id))
    {
        // Pull out all values from trie storage.
        if (h256 root = a->baseRoot())
        {
            SecureTrieDB<h256, OverlayDB> memdb(
                const_cast<OverlayDB*>(&m_db), root);  // promise we won't alter the overlay! :)

            for (auto it = memdb.hashedBegin(); it != memdb.hashedEnd(); ++it)
            {
                h256 const hashedKey((*it).first);
                u256 const key = h256(it.key());
                u256 const value = RLP((*it).second).toInt<u256>();
                ret[hashedKey] = make_pair(key, value);
            }
        }

        // Then merge cached storage over the top.
        for (auto const& i : a->storageOverlay())
        {
            h256 const key = i.first;
            h256 const hashedKey = sha3(key);
            if (i.second)
                ret[hashedKey] = i;
            else
                ret.erase(hashedKey);
        }
    }
    return ret;
#else
    (void)_id;
    BOOST_THROW_EXCEPTION(
        InterfaceNotSupported() << errinfo_interface("State::storage(Address const& _id)"));
#endif
}

h256 State::storageRoot(Address const& _id) const
{
    string s = m_state.at(_id);
    if (s.size())
    {
        RLP r(s);
        return r[2].toHash<h256>();
    }
    return EmptyTrie;
}

bytes const& State::code(Address const& _addr) const
{
    Account const* a = account(_addr);
    if (!a || a->codeHash() == EmptySHA3)
        return NullBytes;

    if (a->code().empty())
    {
        // Load the code from the backend.
        Account* mutableAccount = const_cast<Account*>(a);
        mutableAccount->noteCode(m_db.lookup(a->codeHash()));
        CodeSizeCache::instance().store(a->codeHash(), a->code().size());
    }

    return a->code();
}

void State::setCode(Address const& _address, bytes&& _code)
{
    m_changeLog.emplace_back(_address, code(_address));
    m_cache[_address].setCode(std::move(_code));
}

h256 State::codeHash(Address const& _a) const
{
    if (Account const* a = account(_a))
        return a->codeHash();
    else
        return EmptySHA3;
}

size_t State::codeSize(Address const& _a) const
{
    if (Account const* a = account(_a))
    {
        if (a->hasNewCode())
            return a->code().size();
        auto& codeSizeCache = CodeSizeCache::instance();
        h256 codeHash = a->codeHash();
        if (codeSizeCache.contains(codeHash))
            return codeSizeCache.get(codeHash);
        else
        {
            size_t size = code(_a).size();
            codeSizeCache.store(codeHash, size);
            return size;
        }
    }
    else
        return 0;
}

size_t State::savepoint() const
{
    return m_changeLog.size();
}

void State::rollback(size_t _savepoint)
{
    while (_savepoint != m_changeLog.size())
    {
        auto& change = m_changeLog.back();
        auto& account = m_cache[change.address];
		std::cout << BrcYellow "rollback: " << " change.kind" << (size_t)change.kind << " change.value:" << change.value<< BrcReset << std::endl;
        // Public State API cannot be used here because it will add another
        // change log entry.
        switch (change.kind)
        {
        case Change::Storage:
            account.setStorage(change.key, change.value);
            break;
        case Change::StorageRoot:
            account.setStorageRoot(change.value);
            break;
        case Change::Balance:
            account.addBalance(0 - change.value);
            break;
        case Change::BRC:
            account.addBRC(0 - change.value);
            break;
        case Change::Nonce:
            account.setNonce(change.value);
            break;
        case Change::Create:
            m_cache.erase(change.address);
            break;
        case Change::Code:
            account.setCode(std::move(change.oldCode));
            break;
        case Change::Touch:
            account.untouch();
            m_unchangedCacheEntries.emplace_back(change.address);
            break;
        case Change::Ballot:
            account.addBallot(0 - change.value);
            break;
        case Change::Poll:
            account.addPoll(0 - change.value);
            break;   
        case Change::Vote:
            account.addVote(change.vote);
            break;
        case Change::SysVoteData:
            account.manageSysVote(change.sysVotedate.first, !change.sysVotedate.second, 0);
            break;
        case Change::FBRC:
            account.addFBRC(0 - change.value);
			break;
		case Change::FBalance:
            account.addFBalance(0 - change.value);
            break;
        break;
        }
        m_changeLog.pop_back();
    }
}

std::pair<ExecutionResult, TransactionReceipt> State::execute(EnvInfo const& _envInfo,
    SealEngineFace const& _sealEngine, Transaction const& _t, Permanence _p, OnOpFunc const& _onOp)
{
    // Create and initialize the executive. This will throw fairly cheaply and quickly if the
    // transaction is bad in any way.
    Executive e(*this, _envInfo, _sealEngine);
    ExecutionResult res;
    e.setResultRecipient(res);

    auto onOp = _onOp;
#if BRC_VMTRACE
    if (!onOp)
        onOp = e.simpleTrace();
#endif
    u256 const startGasUsed = _envInfo.gasUsed();
    bool const statusCode = executeTransaction(e, _t, onOp);

    bool removeEmptyAccounts = false;
    switch (_p)
    {
    case Permanence::Reverted:
        m_cache.clear();
        break;
    case Permanence::Committed:
        removeEmptyAccounts = _envInfo.number() >= _sealEngine.chainParams().EIP158ForkBlock;
        commit(removeEmptyAccounts ? State::CommitBehaviour::RemoveEmptyAccounts :
                                     State::CommitBehaviour::KeepEmptyAccounts);
        break;
    case Permanence::Uncommitted:
        break;
    }

    TransactionReceipt const receipt =
        _envInfo.number() >= _sealEngine.chainParams().byzantiumForkBlock ?
            TransactionReceipt(statusCode, startGasUsed + e.gasUsed(), e.logs()) :
            TransactionReceipt(rootHash(), startGasUsed + e.gasUsed(), e.logs());
    return make_pair(res, receipt);
}

void State::executeBlockTransactions(Block const& _block, unsigned _txCount,
    LastBlockHashesFace const& _lastHashes, SealEngineFace const& _sealEngine)
{
    u256 gasUsed = 0;
    for (unsigned i = 0; i < _txCount; ++i)
    {
        EnvInfo envInfo(_block.info(), _lastHashes, gasUsed);

        Executive e(*this, envInfo, _sealEngine);
        executeTransaction(e, _block.pending()[i], OnOpFunc());

        gasUsed += e.gasUsed();
    }
}

/// @returns true when normally halted; false when exceptionally halted; throws when internal VM
/// exception occurred.
bool State::executeTransaction(Executive& _e, Transaction const& _t, OnOpFunc const& _onOp)
{
    size_t const savept = savepoint();
    try
    {
        _e.initialize(_t);

        if (!_e.execute())
            _e.go(_onOp);
        return _e.finalize();
    }
    catch (Exception const&)
    {
        rollback(savept);
        throw;
    }
}

u256 dev::brc::State::poll(Address const& _addr) const
{
    if(auto a = account(_addr))
        return a->poll();
    else
        return 0;
}

void dev::brc::State::addPoll(Address const & _addr, u256 const & _value)
{

    if(Account* a = account(_addr))
    {
        a->addBalance(_value);
    }
    else
        BOOST_THROW_EXCEPTION(InvalidAddressAddr() << errinfo_interface("State::addPoll()"));

    if(_value)
        m_changeLog.emplace_back(Change::Poll, _addr, _value);
}


void dev::brc::State::subPoll(Address const& _addr, u256 const& _value)
{
    if(_value == 0)
        return;
    Account* a = account(_addr);
    if(!a || a->poll() < _value)
        BOOST_THROW_EXCEPTION(NotEnoughPoll());
    addPoll(_addr, 0 - _value);
}



std::string dev::brc::State::accoutMessage(Address const& _addr)
{
	std::string _str ="";
	Json::Value jv;
	if(auto a = account(_addr))
	{
		/*_str << "Address:" << _addr << " | balance:" << a->balance()
			<< " | ballot:" << a->ballot()
			<< " | poll:" << a->poll()
			<< " | "*/
		jv["Address"] = toJS(_addr);
		jv["balance"] = toJS(a->balance());
		jv["ballot"] = toJS(a->ballot());
		jv["poll"] = toJS(a->poll());
		jv["nonce"] = toJS(a->nonce());
		jv["BRC"] = toJS(a->BRC());
		jv["vote"] = toJS(a->BRC());
		Json::Value _array;
        for (auto val : a->voteData())
        {
			Json::Value _v;
			_v["Adress"] = toJS(val.first);
			_v["vote_num"] = toJS(val.second);
			_array.append(_v);
        }
		jv["vote"] = _array;

		return jv.toStyledString();
	}
    return _str; 
}

dev::u256 dev::brc::State::voteAll(Address const& _id) const
{
    if(auto a = account(_id))
        return a->voteAll();
    else
        return 0;
}


dev::u256 dev::brc::State::voteAdress(Address const& _id, Address const& _recivedAddr) const
{
    if(auto a = account(_id))
        return a->vote(_recivedAddr);
    else
        return 0;
}


void dev::brc::State::addVote(Address const& _id, Address const& _recivedAddr, u256 _value)
{
    //此为投票接口  没有投票人地址失败   投票人票数不足 失败
    Account* a = account(_id);
    Account *rec_a = account(_recivedAddr);
    if(a && rec_a)
    {
        // 一个原子操作
        //扣票
        if(a->ballot() < _value)
            BOOST_THROW_EXCEPTION(NotEnoughBallot() << errinfo_interface("State::addvote()"));
        a->addBallot(0 - _value);
        //加票
        rec_a->addPoll(_value);
        //添加记录
        a->addVote(std::make_pair(_recivedAddr, _value));
    }
    else
        BOOST_THROW_EXCEPTION(InvalidAddressAddr() << errinfo_interface("State::addvote()"));

    if(_value)
    {
        m_changeLog.emplace_back( _id, std::make_pair(_recivedAddr, _value) );
        m_changeLog.emplace_back(Change::Ballot, _id, 0- _value);
        m_changeLog.emplace_back(Change::Poll, _id, _value);
    }
}


void dev::brc::State::subVote(Address const& _id, Address const& _recivedAddr, u256 _value)
{
    //撤销投票
    Account *rec_a = account(_recivedAddr);
    Account* a = account(_id);
    if(a && rec_a )
    {
        // 验证投票将记录
        if(a->vote(_recivedAddr) < _value )
            BOOST_THROW_EXCEPTION(NotEnoughVoteLog() << errinfo_interface("State::subVote()"));
        a->addVote(std::make_pair(_recivedAddr, 0 - _value));
        a->addBallot(_value);
        if(rec_a->poll() < _value)
            _value = rec_a->poll();
        rec_a->addPoll(0 - _value);
    }                 
    else
        BOOST_THROW_EXCEPTION(InvalidAddressAddr() << errinfo_interface("State::subVote()"));

    if(_value)
    {
        m_changeLog.emplace_back(_id, std::make_pair(_recivedAddr, 0 - _value));
        m_changeLog.emplace_back(Change::Ballot, _id, _value);
        m_changeLog.emplace_back(Change::Poll, _id, 0 - _value);
    }
}                                           


std::unordered_map<dev::Address, dev::u256> dev::brc::State::voteDate(Address const& _id) const
{
    if(auto a = account(_id))
        return a->voteData();
	else
	{
		return std::unordered_map<Address, u256>();
	}
}


void dev::brc::State::addSysVoteDate(Address const& _sysAddress, Address const& _id)
{
    Account *sysAddr = account(_sysAddress);
    Account* a = account(_id);
    if(!sysAddr)
	{
		createAccount(_sysAddress, { requireAccountStartNonce(), 0 });
		sysAddr = account(_sysAddress);
	}
    if(!a)
        BOOST_THROW_EXCEPTION(InvalidAddressAddr() << errinfo_interface("State::addSysVoteDate()"));
    sysAddr->manageSysVote(_id, true, 0);
    m_changeLog.emplace_back(_sysAddress, std::make_pair(_id, true));
}


void dev::brc::State::subSysVoteDate(Address const& _sysAddress, Address const& _id)
{
    Account *sysAddr = account(_sysAddress);
    Account* a = account(_id);
    if(!sysAddr)
        BOOST_THROW_EXCEPTION(InvalidSysAddress() << errinfo_interface("State::subSysVoteDate()"));
    if(!a)
        BOOST_THROW_EXCEPTION(InvalidAddressAddr() << errinfo_interface("State::subSysVoteDate()"));
    sysAddr->manageSysVote(_id, false, 0);
    m_changeLog.emplace_back(_sysAddress, std::make_pair(_id, false));
}


void dev::brc::State::transferBallotBuy(Address const& _from, Address const& _to, u256 const& _value)
{
	subBRC(_from, _value*BALLOTPRICE);
	addBRC(_to, _value*BALLOTPRICE);
	addBallot(_from, _value);
}

void dev::brc::State::transferBallotSell(Address const& _from, Address const& _to, u256 const& _value)
{
	subBallot(_from, _value);
	addBRC(_from, _value*BALLOTPRICE);
	subBRC(_to, BALLOTPRICE);
}

std::ostream& dev::brc::operator<<(std::ostream& _out, State const& _s)
{
    _out << "--- " << _s.rootHash() << std::endl;
    std::set<Address> d;
    std::set<Address> dtr;
    auto trie = SecureTrieDB<Address, OverlayDB>(const_cast<OverlayDB*>(&_s.m_db), _s.rootHash());
    for (auto i : trie)
        d.insert(i.first), dtr.insert(i.first);
    for (auto i : _s.m_cache)
        d.insert(i.first);

    for (auto i : d)
    {
        auto it = _s.m_cache.find(i);
        Account* cache = it != _s.m_cache.end() ? &it->second : nullptr;
        string rlpString = dtr.count(i) ? trie.at(i) : "";
        RLP r(rlpString);
        assert(cache || r);

        if (cache && !cache->isAlive())
            _out << "XXX  " << i << std::endl;
        else
        {
            string lead = (cache ? r ? " *   " : " +   " : "     ");
            if (cache && r && cache->nonce() == r[0].toInt<u256>() &&
                cache->balance() == r[1].toInt<u256>())
                lead = " .   ";

            stringstream contout;

            if ((cache && cache->codeHash() == EmptySHA3) ||
                (!cache && r && (h256)r[3] != EmptySHA3))
            {
                std::map<u256, u256> mem;
                std::set<u256> back;
                std::set<u256> delta;
                std::set<u256> cached;
                if (r)
                {
                    SecureTrieDB<h256, OverlayDB> memdb(const_cast<OverlayDB*>(&_s.m_db),
                        r[2].toHash<h256>());  // promise we won't alter the overlay! :)
                    for (auto const& j : memdb)
                        mem[j.first] = RLP(j.second).toInt<u256>(), back.insert(j.first);
                }
                if (cache)
                    for (auto const& j : cache->storageOverlay())
                    {
                        if ((!mem.count(j.first) && j.second) ||
                            (mem.count(j.first) && mem.at(j.first) != j.second))
                            mem[j.first] = j.second, delta.insert(j.first);
                        else if (j.second)
                            cached.insert(j.first);
                    }
                if (!delta.empty())
                    lead = (lead == " .   ") ? "*.*  " : "***  ";

                contout << " @:";
                if (!delta.empty())
                    contout << "???";
                else
                    contout << r[2].toHash<h256>();
                if (cache && cache->hasNewCode())
                    contout << " $" << toHex(cache->code());
                else
                    contout << " $" << (cache ? cache->codeHash() : r[3].toHash<h256>());

                for (auto const& j : mem)
                    if (j.second)
                        contout << std::endl
                                << (delta.count(j.first) ?
                                           back.count(j.first) ? " *     " : " +     " :
                                           cached.count(j.first) ? " .     " : "       ")
                                << std::hex << nouppercase << std::setw(64) << j.first << ": "
                                << std::setw(0) << j.second;
                    else
                        contout << std::endl
                                << "XXX    " << std::hex << nouppercase << std::setw(64) << j.first
                                << "";
            }
            else
                contout << " [SIMPLE]";
            _out << lead << i << ": " << std::dec << (cache ? cache->nonce() : r[0].toInt<u256>())
                 << " #:" << (cache ? cache->balance() : r[1].toInt<u256>()) << contout.str()
                 << std::endl;
        }
    }
    return _out;
}

State& dev::brc::createIntermediateState(
    State& o_s, Block const& _block, unsigned _txIndex, BlockChain const& _bc)
{
    o_s = _block.state();
    u256 const rootHash = _block.stateRootBeforeTx(_txIndex);
    if (rootHash)
        o_s.setRoot(rootHash);
    else
    {
        o_s.setRoot(_block.stateRootBeforeTx(0));
        o_s.executeBlockTransactions(_block, _txIndex, _bc.lastBlockHashes(), *_bc.sealEngine());
    }
    return o_s;
}

template <class DB>
AddressHash dev::brc::commit(AccountMap const& _cache, SecureTrieDB<Address, DB>& _state)
{
    AddressHash ret;
    for (auto const& i : _cache)
        if (i.second.isDirty())
        {
            if (!i.second.isAlive())
                _state.remove(i.first);
            else
            {
                RLPStream s(10);
                s << i.second.nonce() << i.second.balance();
                if (i.second.storageOverlay().empty())
                {
                    assert(i.second.baseRoot());
                    s.append(i.second.baseRoot());
                }
                else
                {
                    SecureTrieDB<h256, DB> storageDB(_state.db(), i.second.baseRoot());
                    for (auto const& j : i.second.storageOverlay())
                        if (j.second)
                            storageDB.insert(j.first, rlp(j.second));
                        else
                            storageDB.remove(j.first);
                    assert(storageDB.root());
                    s.append(storageDB.root());
                }

                if (i.second.hasNewCode())
                {
                    h256 ch = i.second.codeHash();
                    // Store the size of the code
                    CodeSizeCache::instance().store(ch, i.second.code().size());
                    _state.db()->insert(ch, &i.second.code());
                    s << ch;
                }
                else
                    s << i.second.codeHash();
                s << i.second.ballot();
                s << i.second.poll();
                { 
                    RLPStream _s;
                    size_t num = i.second.voteData().size();
                    _s.appendList(num + 1);
                    _s << num;
                    for(auto val : i.second.voteData())
                    {
                        _s.append<Address, u256>(std::make_pair(val.first, val.second));
                    }
                    s << _s.out();
                }
                s << i.second.BRC();
                s << i.second.FBRC();
				s << i.second.FBalance();
                _state.insert(i.first, &s.out());
            }
            ret.insert(i.first);
        }
    return ret;
}


template AddressHash dev::brc::commit<OverlayDB>(
    AccountMap const& _cache, SecureTrieDB<Address, OverlayDB>& _state);
template AddressHash dev::brc::commit<StateCacheDB>(
    AccountMap const& _cache, SecureTrieDB<Address, StateCacheDB>& _state);