#include "exec/oms/order_store.hpp"

#include <utility>

namespace exec {

ChildOrder& OrderStore::create_order(ClientOrderId order_id,
                                     const BasketId& basket_id,
                                     const PositionOrderIntent& intent) {
    ChildOrder order;
    order.order_id = order_id;
    order.basket_id = basket_id;
    order.intent = intent;

    auto [it, inserted] = orders_.insert_or_assign(order_id, std::move(order));
    if (inserted) {
        basket_to_orders_[basket_id].push_back(order_id);
    }
    return it->second;
}

ChildOrder* OrderStore::find(ClientOrderId order_id) {
    const auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return nullptr;
    }
    return &it->second;
}

const ChildOrder* OrderStore::find(ClientOrderId order_id) const {
    const auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return nullptr;
    }
    return &it->second;
}

ChildOrder* OrderStore::find_by_venue_order_id(const std::string& venue_order_id) {
    const auto it = venue_to_client_.find(venue_order_id);
    if (it == venue_to_client_.end()) {
        return nullptr;
    }
    return find(it->second);
}

const ChildOrder* OrderStore::find_by_venue_order_id(const std::string& venue_order_id) const {
    const auto it = venue_to_client_.find(venue_order_id);
    if (it == venue_to_client_.end()) {
        return nullptr;
    }
    return find(it->second);
}

ChildOrder* OrderStore::find_for_report(const ExecutionReport& report) {
    if (report.order_id != 0) {
        return find(report.order_id);
    }
    if (!report.venue_order_id.empty()) {
        return find_by_venue_order_id(report.venue_order_id);
    }
    return nullptr;
}

const ChildOrder* OrderStore::find_for_report(const ExecutionReport& report) const {
    if (report.order_id != 0) {
        return find(report.order_id);
    }
    if (!report.venue_order_id.empty()) {
        return find_by_venue_order_id(report.venue_order_id);
    }
    return nullptr;
}

void OrderStore::bind_venue_order_id(ClientOrderId order_id, const std::string& venue_order_id) {
    if (venue_order_id.empty()) {
        return;
    }
    venue_to_client_[venue_order_id] = order_id;
}

const std::vector<ClientOrderId>* OrderStore::order_ids_for_basket(const BasketId& basket_id) const {
    const auto it = basket_to_orders_.find(basket_id);
    if (it == basket_to_orders_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<ClientOrderId> OrderStore::cancelable_order_ids_for_basket(const BasketId& basket_id) const {
    std::vector<ClientOrderId> result;
    const auto* ids = order_ids_for_basket(basket_id);
    if (ids == nullptr) {
        return result;
    }

    for (const auto order_id : *ids) {
        const auto* order = find(order_id);
        if (order != nullptr && is_cancelable(*order)) {
            result.push_back(order_id);
        }
    }
    return result;
}

std::vector<ClientOrderId> OrderStore::cancelable_order_ids() const {
    std::vector<ClientOrderId> result;
    result.reserve(orders_.size());
    for (const auto& [order_id, order] : orders_) {
        if (is_cancelable(order)) {
            result.push_back(order_id);
        }
    }
    return result;
}

bool OrderStore::has_cancelable_order_for_basket(const BasketId& basket_id) const {
    return !cancelable_order_ids_for_basket(basket_id).empty();
}

std::size_t OrderStore::size() const {
    return orders_.size();
}

}  // 命名空间 exec
