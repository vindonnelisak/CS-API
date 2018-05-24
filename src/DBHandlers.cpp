#include "DBHandlers.h"

// csdb
#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/currency.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>
#include <csdb/storage.h>
#include <csdb/wallet.h>
#include <csdb/csdb.h>

#include "csconnector/csconnector.h"
#include "sha1.hpp"

#include <algorithm>
#include <cassert>


csdb::Storage* DbHandlers::s_storage{nullptr};


void DbHandlers::init(csdb::Storage* m_storage) 
{
  std::string message;

  if(m_storage != nullptr)
	s_storage = m_storage;

  const bool isStorageOpen = s_storage->isOpen();

  if (isStorageOpen) 
  {
     message = "Storage is opened normal";
     csconnector::registerHandler<csconnector::Commands::BalanceGet>(BalanceGet);
     csconnector::registerHandler<csconnector::Commands::TransactionGet>(TransactionGet);
     csconnector::registerHandler<csconnector::Commands::TransactionsGet>(TransactionsGet);
     csconnector::registerHandler<csconnector::Commands::PoolInfoGet>(PoolInfoGet);
     csconnector::registerHandler<csconnector::Commands::PoolTransactionsGet>(PoolTransactionsGet);
     csconnector::registerHandler<csconnector::Commands::PoolListGet>(PoolListGet);
     csconnector::registerHandler<csconnector::Commands::SmartContractGet>(SmartContractGet);
  } 
  else
  {
    message = "Storage is not opedened: " + s_storage->last_error_message();
  }

  std::cout << message << std::endl;
}

void DbHandlers::deinit() {
  csconnector::unregisterHandler<csconnector::Commands::BalanceGet>();
  csconnector::unregisterHandler<csconnector::Commands::TransactionGet>();
  csconnector::unregisterHandler<csconnector::Commands::TransactionsGet>();
  csconnector::unregisterHandler<csconnector::Commands::PoolInfoGet>();
  csconnector::unregisterHandler<csconnector::Commands::PoolTransactionsGet>();
  csconnector::unregisterHandler<csconnector::Commands::PoolListGet>();
}


std::string sha1_make(std::string data){
    SHA1 hash;
    hash.update(data);
    std::string tmp_buff(24, '0');
    tmp_buff += hash.final();
    return tmp_buff;
}


void DbHandlers::BalanceGet(api::BalanceGetResult& _return, const api::Address& address, const api::Currency& currency) {
	assert(s_storage != nullptr);

	unsigned int _target = 0;
	unsigned int _source = 0;
	unsigned int res = 0;

	csdb::Pool curr = s_storage->pool_load(s_storage->last_hash());
	  while (curr.is_valid())
	  {
		  _target += BalanceTarget(address, curr.hash());
		  _source = BalanceSource(address, curr.hash());

		  if (BalanceSource(address, curr.hash(), 1))
		  {
			  res = _target + _source;
			  break;
		  }
		  curr = s_storage->pool_load(curr.previous_hash());
	  }

  _return.amount.integral = res;
  _return.amount.fraction = 0;
  
  
}

void DbHandlers::TransactionGet(api::TransactionGetResult& _return, const api::TransactionId& transactionId) {
  const csdb::TransactionID& tmpTransactionId = csdb::TransactionID::from_string(transactionId);
  const csdb::Transaction&  transaction       = s_storage->transaction(tmpTransactionId);
  _return.found                               = transaction.is_valid();
  if (_return.found) {
      _return.transaction = convertTransaction(transaction);
  }
}




void DbHandlers::TransactionsGet(api::TransactionsGetResult& _return, const api::Address& addressString, const int64_t offset, const int64_t limit) {
    std::string tmp_buff = sha1_make(addressString);
    csdb::Address addr = csdb::Address::from_string(tmp_buff);

   const auto& transactions = s_storage->transactions(addr, limit);
   _return.transactions     = convertTransactions(transactions);
}


void DbHandlers::PoolListGet(api::PoolListGetResult& _return, const int64_t offset, const int64_t limit) {

    csdb::PoolHash offsetedPoolHash = s_storage->last_hash();
    for (int64_t i = 0; i < offset; ++i) {
        offsetedPoolHash   = s_storage->pool_load(offsetedPoolHash).previous_hash();
    }

    for (int64_t i = 0; i < limit; ++i) {
      const api::Pool& apiPool = convertPool(offsetedPoolHash);
      _return.pools.push_back(apiPool);
    }

}


void DbHandlers::PoolInfoGet(api::PoolInfoGetResult& _return, const api::PoolHash& hash, const int64_t index) {
  const csdb::PoolHash poolHash = csdb::PoolHash::from_string(hash);
  const csdb::Pool     pool     = s_storage->pool_load(poolHash);
  _return.isFound               = pool.is_valid();

  if (_return.isFound) {
    _return.pool = convertPool(poolHash);
  }
}




void DbHandlers::PoolTransactionsGet(api::PoolTransactionsGetResult& _return, const api::PoolHash& poolHashString, const int64_t index, const int64_t offset, const int64_t limit) {
  const csdb::PoolHash poolHash = csdb::PoolHash::from_string(poolHashString);
  const csdb::Pool&    pool     = s_storage->pool_load(poolHash);

  if (pool.is_valid()) {
    _return.transactions = extractTransactions(pool, limit, offset);
  }
}

api::SmartContract DbHandlers::convertStringToContract(const std::string& data) {
	api::SmartContract smartContract;

	smartContract.sourceCode = "test_source_code";
	smartContract.byteCode = "test_byte_code";
	smartContract.hashState = "test_hash_state";

	return smartContract;
}


void DbHandlers::SmartContractGet(api::SmartContractGetResult& _return, const api::Address& address) {
  assert(s_storage != nullptr);


    std::string tmp_buff = sha1_make(address);
    csdb::Address addr = csdb::Address::from_string(tmp_buff);

  const csdb::Transaction transaction = s_storage->get_last_by_source(addr);

  //Test database
  csdb::UserField smart_contract_field = transaction.user_field(0);
  std::string smart_contract_data = smart_contract_field.value<std::string>();

  assert(smart_contract_data != "");
  _return.smartContract = convertStringToContract(smart_contract_data);

}



api::Transactions DbHandlers::convertTransactions(const std::vector<csdb::Transaction>& transactions) {
  api::Transactions result;
  //reserve vs resize
  result.resize(transactions.size());
  std::transform(transactions.begin(), transactions.end(), result.begin(), convertTransaction);
  return result;
}

api::Transaction DbHandlers::convertTransaction(const csdb::Transaction& transaction) {
  api::Transaction    result;
  const csdb::Amount& amount     = transaction.amount();
  const csdb::Currency& currency = transaction.currency();
  const csdb::Address& target    = transaction.target();
  const csdb::TransactionID& id  = transaction.id();
  const csdb::Address& address   = transaction.source();

  result.amount                  = convertAmount(amount);
  result.currency                = currency.to_string();
  result.innerId                 = id.to_string();
  result.source                  = address.to_string();
  result.target                  = target.to_string();

  return result;
}

api::Amount DbHandlers::convertAmount(const csdb::Amount& amount) {
  api::Amount result;
  result.integral = amount.integral();
  result.fraction = amount.fraction();
  assert(result.fraction >= 0);
  return result;
}



api::Pool DbHandlers::convertPool(const csdb::PoolHash& poolHash) {
  api::Pool         result;
  const csdb::Pool& pool = s_storage->pool_load(poolHash);
  if (pool.is_valid()) {
    result.hash       = poolHash.to_string();
    result.poolNumber = pool.sequence();
    assert(result.poolNumber >= 0);
    result.prevHash          = pool.previous_hash().to_string();
//    result.time              = pool.user_field(); // заменить когда появится дополнительное поле
    result.transactionsCount = pool.transactions_count();
    assert(result.transactionsCount >= 0);
  }
  return result;
}

api::Transactions DbHandlers::extractTransactions(const csdb::Pool& pool, int64_t limit, const int64_t offset) {
  int64_t transactionsCount = pool.transactions_count();
  assert(transactionsCount >= 0);

  if (offset > transactionsCount) {
    return api::Transactions{}; // если запрашиваемые транзакций выходят за пределы пула возвращаем пустой результат
  }
  api::Transactions result;
  transactionsCount -= offset; // мы можем отдать все транзакции в пуле за вычетом смещения

  if (limit > transactionsCount)
    limit = transactionsCount; // лимит уменьшается до реального количества транзакций которые можно отдать

  for (int64_t index = offset; index < (offset+limit); ++index) {
    const csdb::Transaction transaction = pool.transaction(index);
    result.push_back(convertTransaction(transaction));
  }
  return result;
}


//Utils
unsigned int DbHandlers::BalanceTarget(api::Address address, csdb::PoolHash pool_hash)
{
    unsigned int summ = 0;

    std::string tmp_buff = sha1_make(address);
    csdb::Address addr = csdb::Address::from_string(tmp_buff);

	csdb::Pool curr = s_storage->pool_load(pool_hash);

	if (curr.is_valid())
	{
		const auto& t = curr.get_last_by_source(addr);
		for (unsigned int i = 0; i < curr.transactions_count(); i++)
		{
			csdb::Transaction tr = curr.transaction(i);
			if (tr.is_valid())
			{
				summ += tr.amount().integral();
			}
		}
	}

	return summ;
}

unsigned int DbHandlers::BalanceSource(api::Address address, csdb::PoolHash pool_hash)
{
    unsigned int summ = 0;

    std::string tmp_buff = sha1_make(address);
    csdb::Address addr = csdb::Address::from_string(tmp_buff);
	csdb::Pool curr = s_storage->pool_load(pool_hash);


	if (curr.is_valid())
	{
		const auto& t = curr.get_last_by_source(addr);
		for (unsigned int i = 0; i < curr.transactions_count(); i++)
		{
			csdb::Transaction tr = curr.transaction(i);
			if (tr.is_valid())
			{
				summ = tr.balance().integral();
			}
		}
	}

	return summ;
}


bool DbHandlers::BalanceSource(api::Address address, csdb::PoolHash pool_hash, bool flag)
{
    std::string tmp_buff = sha1_make(address);
    csdb::Address addr = csdb::Address::from_string(tmp_buff);

	csdb::Pool curr = s_storage->pool_load(pool_hash);

	unsigned int summ = 0;

	if (curr.is_valid())
	{
		const auto& t = curr.get_last_by_source(addr);
		for (unsigned int i = 0; i < curr.transactions_count(); i++)
		{
			csdb::Transaction tr = curr.transaction(i);
			if (tr.is_valid())
			{
				summ = tr.balance().integral();
				return true;
			}
		}
	}

	return false;
}




