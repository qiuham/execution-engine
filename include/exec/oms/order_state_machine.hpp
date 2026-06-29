#pragma once

#include "exec/core/types.hpp"
#include "exec/model/order.hpp"

namespace exec {

enum class OrderStatus {
    PendingNew,
    New,
    PartiallyFilled,
    Filled,
    Rejected,
    Canceled,
};

struct ChildOrder {
    ClientOrderId order_id{0};
    BasketId basket_id;
    PositionOrderIntent intent{};
    OrderStatus status{OrderStatus::PendingNew};
    Quantity filled_qty{0};
};

struct ExecutionReport {
    ClientOrderId order_id{0};
    OrderStatus status{OrderStatus::New};
    Quantity last_qty{0};
    Quantity cumulative_qty{0};
    Price last_price{0};
    std::string text;
};

class OrderStateMachine {
public:
    void apply(ChildOrder& order, const ExecutionReport& report) const {
        order.status = report.status;
        order.filled_qty = report.cumulative_qty;
    }
};

}  // namespace exec
