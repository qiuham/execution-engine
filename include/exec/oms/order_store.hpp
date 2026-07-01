#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "exec/oms/order_state_machine.hpp"

namespace exec {

class OrderStore {
public:
    ChildOrder& create_order(ClientOrderId order_id,
                             const BasketId& basket_id,
                             const PositionOrderIntent& intent);

    ChildOrder* find(ClientOrderId order_id);
    const ChildOrder* find(ClientOrderId order_id) const;

    ChildOrder* find_by_venue_order_id(const std::string& venue_order_id);
    const ChildOrder* find_by_venue_order_id(const std::string& venue_order_id) const;

    ChildOrder* find_for_report(const ExecutionReport& report);
    const ChildOrder* find_for_report(const ExecutionReport& report) const;

    void bind_venue_order_id(ClientOrderId order_id, const std::string& venue_order_id);

    const std::vector<ClientOrderId>* order_ids_for_basket(const BasketId& basket_id) const;
    std::vector<ClientOrderId> cancelable_order_ids_for_basket(const BasketId& basket_id) const;
    std::vector<ClientOrderId> cancelable_order_ids() const;
    bool has_cancelable_order_for_basket(const BasketId& basket_id) const;

    std::size_t size() const;

private:
    std::unordered_map<ClientOrderId, ChildOrder> orders_;
    std::unordered_map<std::string, ClientOrderId> venue_to_client_;
    std::unordered_map<BasketId, std::vector<ClientOrderId>> basket_to_orders_;
};

}  // 命名空间 exec
