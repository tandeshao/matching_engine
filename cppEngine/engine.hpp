// This file contains declarations for the main Engine class. You will
// need to add declarations to this file as you develop your Engine.

#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <chrono>
#include <unordered_map>
#include <set>
#include "io.hpp"
#include <atomic>
#include <condition_variable>
#include <typeinfo>
#include <shared_mutex>
#include <vector>
#include <list>

class Order
{
private:
	int order_id;
	int execution_id;
	std::string instrument;
	int price;
	int count;
	bool is_sell;
	int64_t add_to_order_book_timestamp;

public:
	Order(int order_id, std::string instrument, int price, int count, bool is_sell)
		: order_id(order_id), execution_id(1),
		  instrument(instrument), price(price), count(count), is_sell(is_sell) {}

	int getOrderId() const { return order_id; }
	int getExecutionId() const { return execution_id; }
	void incrementExecutionId() { execution_id++; }
	void setCount(int count) { this->count = count; }
	std::string getInstrument() const { return instrument; }
	int getPrice() const { return price; }
	int getCount() const { return count; }
	bool getIsSell() const { return is_sell; }
	int64_t getTimestamp() const { return add_to_order_book_timestamp; }
	void setTimestamp(int64_t timestamp) { this->add_to_order_book_timestamp = timestamp; }

	bool operator==(const Order &other) const
	{
		return this->order_id == other.order_id;
	}

	bool operator<(const Order &other) const
	{
		if (this->price == other.price)
			return this->order_id < other.order_id;
		return this->price < other.price;
	}

	// copy constructor. copy all fields.
	Order(const Order &other) : order_id(other.order_id), instrument(other.instrument),
								price(other.price), count(other.count), is_sell(other.is_sell), add_to_order_book_timestamp(other.add_to_order_book_timestamp)
	{
		execution_id = other.execution_id;
	}
};

enum Compare_type
{
	less,
	greater
};

template <typename K, typename V>
class atomic_map
{
public:
	using size_type = typename std::vector<std::list<std::pair<K, V>>>::size_type;

	void insert(const K &key, const V &value)
	{
		size_type index = hash(key);
		std::unique_lock<std::shared_mutex> lock(locks[index]);
		buckets[index].emplace_back(key, value);
		num_elements.store(num_elements.load() + 1);
	}

	V *get(const K &key)
	{
		size_type index = hash(key);
		std::shared_lock<std::shared_mutex> lock(locks[index]);
		for (auto it = buckets[index].begin(); it != buckets[index].end(); it++)
		{
			if (it->first == key)
			{
				return &(it->second);
			}
		}
		lock.unlock();
		insert(key, V());
		return get(key);
	}

	bool contains_key(const K &key)
	{
		size_type index = hash(key);
		std::shared_lock<std::shared_mutex> lock(locks[index]);
		for (auto it = buckets[index].begin(); it != buckets[index].end(); it++)
		{
			if (it->first == key)
			{
				return true;
			}
		}
		return false;
	}

	bool erase(const K &key)
	{
		size_type index = hash(key);
		std::unique_lock<std::shared_mutex> lock(locks[index]);
		for (auto it = buckets[index].begin(); it != buckets[index].end(); it++)
		{
			if (it->first == key)
			{
				buckets[index].erase(it);
				num_elements.store(num_elements.load() - 1);
				return true;
			}
		}
		return false;
	}

private:
	static const int num_buckets = 1000;
	std::vector<std::list<std::pair<K, V>>> buckets{num_buckets};
	std::vector<std::shared_mutex> locks{num_buckets};

	std::atomic<int> num_elements{0};

	size_type hash(const K &key)
	{
		// simple hash function to map keys to bucket index
		return std::hash<K>{}(key) % num_buckets;
	}
};

class StringMutexPair
{
public:
	std::string first;
	std::shared_ptr<std::mutex> second;

	StringMutexPair(const std::string &first, std::shared_ptr<std::mutex> second) : first(first), second(std::move(second)) {}
};

class atomic_map_mutex
{
public:
	using size_type = typename std::vector<std::list<StringMutexPair>>::size_type;

	void insert(const std::string &key, const std::shared_ptr<std::mutex> &value)
	{
		size_type index = hash(key);
		std::unique_lock<std::shared_mutex> lock(locks[index]);
		StringMutexPair pair(key, std::move(value));
		buckets[index].push_back(pair);
		num_elements.store(num_elements.load() + 1);
	}

	std::shared_ptr<std::mutex> get(const std::string &key)
	{
		size_type index = hash(key);
		std::shared_lock<std::shared_mutex> lock(locks[index]);
		for (auto it = buckets[index].begin(); it != buckets[index].end(); it++)
		{
			if (it->first == key)
			{
				return it->second;
			}
		}
		lock.unlock();
		std::shared_ptr<std::mutex> mutexPtr = std::make_shared<std::mutex>();
		insert(key, mutexPtr);
		return mutexPtr;
	}

	bool contains_key(const std::string &key)
	{
		size_type index = hash(key);
		std::shared_lock<std::shared_mutex> lock(locks[index]);
		for (auto it = buckets[index].begin(); it != buckets[index].end(); it++)
		{
			if (it->first == key)
			{
				return true;
			}
		}
		return false;
	}

	bool erase(const std::string &key)
	{
		size_type index = hash(key);
		std::unique_lock<std::shared_mutex> lock(locks[index]);
		for (auto it = buckets[index].begin(); it != buckets[index].end(); it++)
		{
			if (it->first == key)
			{
				buckets[index].erase(it);
				num_elements.store(num_elements.load() - 1);
				return true;
			}
		}
		return false;
	}

private:
	static const int num_buckets = 1000;
	std::vector<std::list<StringMutexPair>> buckets{num_buckets};
	std::vector<std::shared_mutex> locks{num_buckets};

	std::atomic<int> num_elements{0};

	size_type hash(const std::string &key)
	{
		// simple hash function to map keys to bucket index
		return std::hash<std::string>{}(key) % num_buckets;
	}
};

struct OrderComparator
{
	Compare_type cmp_type;
	OrderComparator(Compare_type t = Compare_type::less) : cmp_type(t) {}

	// sort by best price, tiebreak timestamp then order_id.
	bool operator()(const Order &lhs, const Order &rhs) const
	{
		if (lhs.getPrice() != rhs.getPrice())
			return (cmp_type == less ? lhs.getPrice() < rhs.getPrice() : lhs.getPrice() > rhs.getPrice());
		if (lhs.getTimestamp() != rhs.getTimestamp())
			return lhs.getTimestamp() < rhs.getTimestamp();
		return lhs.getOrderId() < rhs.getOrderId();
	}
};

class OrderBook
{
public:
	void delete_order(int order_id, std::unordered_map<int, Order> &order_id_to_order);
	void match_order(Order &new_order, std::unordered_map<int, Order> &order_id_to_order);

private:
	// synchornization primitives for order book
	atomic_map_mutex buy_book_mutex;
	atomic_map_mutex sell_book_mutex;

	// for phase 2 one buy one sell
	atomic_map_mutex buy_book_phase_mutex;
	atomic_map_mutex sell_book_phase_mutex;

	// sort by best price (asc), tiebreak timestamp then order_id.
	atomic_map<std::string, std::set<Order, OrderComparator>> sell_book;
	// sort by best price (desc), tiebreak timestamp then order_id.
	atomic_map<std::string, std::set<Order, OrderComparator>> buy_book;

	void add_order(Order &order, std::unordered_map<int, Order> &order_id_to_order, std::string instrument, bool is_sell);
};

struct Engine
{
public:
	void accept(ClientConnection conn);

private:
	OrderBook order_book;

	void connection_thread(ClientConnection conn);
};

inline std::chrono::microseconds::rep getCurrentTimestamp() noexcept
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
};

#endif
