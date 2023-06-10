#include <iostream>
#include <thread>

#include "io.hpp"
#include "engine.hpp"

void Engine::accept(ClientConnection connection)
{
	auto thread = std::thread(&Engine::connection_thread, this, std::move(connection));
	thread.detach();
}

void Engine::connection_thread(ClientConnection connection)
{
	std::unordered_map<int, Order> order_id_to_order;

	while (true)
	{
		ClientCommand input{};
		switch (connection.readInput(input))
		{
		case ReadResult::Error:
			SyncCerr{} << "Error reading input" << std::endl;
		case ReadResult::EndOfFile:
			return;
		case ReadResult::Success:
			break;
		}

		switch (input.type)
		{

		case input_cancel:
		{
			this->order_book.delete_order(input.order_id, order_id_to_order);
			break;
		}

		default:
		{
			std::string a(input.instrument);
			Order new_order(input.order_id, input.instrument, input.price, input.count, input.type == input_sell);
			this->order_book.match_order(new_order, order_id_to_order);
			break;
		}
		}
	}
}

void OrderBook::delete_order(int order_id, std::unordered_map<int, Order> &order_id_to_order)
{
	auto it = order_id_to_order.find(order_id);
	if (it == order_id_to_order.end())
	{
		auto output_time = getCurrentTimestamp();
		Output::OrderDeleted(order_id, false, output_time);
		return;
	}

	Order o = it->second;
	order_id_to_order.erase(it);
	bool is_sell = o.getIsSell();
	std::string instrument = o.getInstrument();
	std::lock_guard<std::mutex> lock((is_sell ? *(sell_book_mutex.get(instrument)) : *(buy_book_mutex.get(instrument))));
	auto is_erased = is_sell ? sell_book.get(instrument)->erase(o) : buy_book.get(instrument)->erase(o);

	auto output_time = getCurrentTimestamp();
	Output::OrderDeleted(order_id, is_erased, output_time);
}

void OrderBook::match_order(Order &new_order, std::unordered_map<int, Order> &order_id_to_order)
{

	std::string instrument = new_order.getInstrument();
	bool is_sell = new_order.getIsSell();
	bool opposing_order_type = !is_sell;
	auto &orders = (opposing_order_type ? *sell_book.get(instrument) : *buy_book.get(instrument));
	std::lock_guard<std::mutex> phase_lock((is_sell ? *(buy_book_phase_mutex.get(instrument)) : *(sell_book_phase_mutex.get(instrument))));
	while (new_order.getCount() != 0)
	{
		// necessary to do it before orders.begin() because orders.begin() is undefine behaviour if heap is empty.
		std::unique_lock<std::mutex> opp_lock((is_sell ? *(buy_book_mutex.get(instrument)) : *(sell_book_mutex.get(instrument))));
		if (orders.empty())
		{
			opp_lock.unlock();
			std::scoped_lock scp_lock{*(sell_book_mutex.get(instrument)), *(buy_book_mutex.get(instrument))};
			// Similar to CAS pattern
			if (!orders.empty())
			{
				continue;
			}
			add_order(new_order, order_id_to_order, instrument, is_sell);
			return;
		}

		auto it = orders.begin();
		Order other_order = *it;

		if (
			(new_order.getIsSell() && (other_order.getPrice() < new_order.getPrice())) ||
			(!new_order.getIsSell() && (new_order.getPrice() < other_order.getPrice())))
		{
			opp_lock.unlock();
			std::scoped_lock scp_lock{*(sell_book_mutex.get(instrument)), *(buy_book_mutex.get(instrument))};

			// Similar to CAS pattern
			if (orders.empty() || other_order.getOrderId() != orders.begin()->getOrderId() || other_order.getCount() != orders.begin()->getCount())
			{
				continue;
			}

			add_order(new_order, order_id_to_order, instrument, is_sell);
			return;
		}
		Output::OrderExecuted(other_order.getOrderId(), new_order.getOrderId(), other_order.getExecutionId(),
							  other_order.getPrice(),
							  std::min(new_order.getCount(), other_order.getCount()), getCurrentTimestamp());

		Order &lower_count_order = other_order.getCount() < new_order.getCount() ? other_order : new_order;
		Order &higher_count_order = other_order.getCount() < new_order.getCount() ? new_order : other_order;

		higher_count_order.setCount(higher_count_order.getCount() - lower_count_order.getCount());
		lower_count_order.setCount(0);

		if (higher_count_order == other_order)
		{
			higher_count_order.incrementExecutionId();
		}

		orders.erase(it);
		if (other_order.getCount() != 0)
		{
			// re-insert the order with updated count. Need to do this as elements within std::set is immutable
			orders.insert(other_order);
		}
		opp_lock.unlock();

		if (other_order.getCount() == 0)
		{
			order_id_to_order.erase(other_order.getOrderId());
		}
	}
}

void OrderBook::add_order(Order &new_order, std::unordered_map<int, Order> &order_id_to_order, std::string instrument, bool is_sell)
{
	int64_t timestamp = getCurrentTimestamp();
	new_order.setTimestamp(timestamp);
	order_id_to_order.insert(std::make_pair(new_order.getOrderId(), new_order));

	auto &book = (is_sell ? sell_book : buy_book);
	if (!book.contains_key(instrument))
	{
		auto set = (is_sell ? std::set<Order, OrderComparator>(OrderComparator(Compare_type::less))
							: std::set<Order, OrderComparator>(OrderComparator(Compare_type::greater)));
		book.insert(instrument, set);
	}
	book.get(instrument)->insert(new_order);
	Output::OrderAdded(new_order.getOrderId(),
					   new_order.getInstrument().c_str(), new_order.getPrice(), new_order.getCount(), new_order.getIsSell(), timestamp);
}