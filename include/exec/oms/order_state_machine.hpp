#pragma once

#include <string>
#include <unordered_set>

#include "exec/core/types.hpp"
#include "exec/model/order.hpp"

namespace exec {

enum class OrderStatus {
    PendingNew,
    New,
    PartiallyFilled,
    PendingCancel,
    CancelRejected,
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
    std::string venue_order_id;
    std::unordered_set<std::string> seen_trade_ids;
    bool open_quantity_released{false};
};

struct ExecutionReport {
    ClientOrderId order_id{0};
    std::string venue_order_id;
    std::string trade_id;
    OrderStatus status{OrderStatus::New};
    Quantity last_qty{0};
    Quantity cumulative_qty{0};
    Price last_price{0};
    std::string text;
};

struct OrderTransitionResult {
    bool accepted{false};
    Quantity fill_qty{0};
    Price fill_price{0};
    bool terminal{false};
    std::string text;
};

inline const char* to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::PendingNew:
            return "PendingNew";
        case OrderStatus::New:
            return "New";
        case OrderStatus::PartiallyFilled:
            return "PartiallyFilled";
        case OrderStatus::PendingCancel:
            return "PendingCancel";
        case OrderStatus::CancelRejected:
            return "CancelRejected";
        case OrderStatus::Filled:
            return "Filled";
        case OrderStatus::Rejected:
            return "Rejected";
        case OrderStatus::Canceled:
            return "Canceled";
    }
    return "Unknown";
}

inline bool is_terminal(OrderStatus status) {
    return status == OrderStatus::Filled || status == OrderStatus::Rejected || status == OrderStatus::Canceled;
}

inline bool is_fill_status(OrderStatus status) {
    return status == OrderStatus::PartiallyFilled || status == OrderStatus::Filled;
}

inline Quantity open_quantity(const ChildOrder& order) {
    const auto open = order.intent.quantity - order.filled_qty;
    return open > 0 ? open : 0;
}

inline bool is_cancelable(const ChildOrder& order) {
    return !is_terminal(order.status) && order.status != OrderStatus::PendingCancel && open_quantity(order) > 0;
}

class OrderStateMachine {
public:
    OrderTransitionResult apply(ChildOrder& order, const ExecutionReport& report) const {
        OrderTransitionResult result;
        result.fill_price = report.last_price;
        result.terminal = is_terminal(order.status);

        if (report.order_id != 0 && report.order_id != order.order_id) {
            result.text = "订单回报 order_id 不匹配，已忽略";
            return result;
        }

        if (!report.venue_order_id.empty()) {
            order.venue_order_id = report.venue_order_id;
        }

        if (is_terminal(order.status)) {
            result.text = "订单已在终态，后续回报仅做幂等忽略";
            return result;
        }

        if (report.status == OrderStatus::New) {
            if (order.status == OrderStatus::PendingNew) {
                order.status = OrderStatus::New;
                result.accepted = true;
            }
            result.text = report.text;
            return result;
        }

        if (report.status == OrderStatus::PendingCancel) {
            order.status = OrderStatus::PendingCancel;
            result.accepted = true;
            result.text = report.text;
            return result;
        }

        if (report.status == OrderStatus::CancelRejected) {
            order.status = order.filled_qty > 0 ? OrderStatus::PartiallyFilled : OrderStatus::New;
            result.text = report.text;
            return result;
        }

        if (is_fill_status(report.status)) {
            if (!report.trade_id.empty()) {
                const auto [unused, inserted] = order.seen_trade_ids.insert(report.trade_id);
                (void)unused;
                if (!inserted) {
                    result.text = "重复成交回报，已忽略";
                    return result;
                }
            }

            const auto before = order.filled_qty;
            const auto remaining = open_quantity(order);
            Quantity incremental = 0;

            if (report.cumulative_qty > 0) {
                auto normalized_cumulative = report.cumulative_qty;
                if (normalized_cumulative > order.intent.quantity) {
                    normalized_cumulative = order.intent.quantity;
                    result.text = "累计成交超过订单数量，已按订单数量截断";
                }
                if (normalized_cumulative > before) {
                    incremental = normalized_cumulative - before;
                }
            } else if (report.last_qty > 0) {
                incremental = report.last_qty;
            }

            if (incremental > remaining) {
                incremental = remaining;
                result.text = "成交增量超过订单剩余量，已按剩余量截断";
            }

            if (incremental > 0) {
                order.filled_qty += incremental;
                result.accepted = true;
                result.fill_qty = incremental;
            }

            if (report.status == OrderStatus::Filled && order.filled_qty < order.intent.quantity &&
                incremental <= 0) {
                result.text = result.text.empty() ? "Filled 回报缺少新增成交数量，已忽略" : result.text;
                return result;
            }

            if (order.filled_qty >= order.intent.quantity || report.status == OrderStatus::Filled) {
                order.status = OrderStatus::Filled;
            } else {
                order.status = OrderStatus::PartiallyFilled;
            }
            result.terminal = is_terminal(order.status);
            if (result.text.empty()) {
                result.text = report.text;
            }
            return result;
        }

        if (report.status == OrderStatus::Rejected) {
            if (order.filled_qty == 0) {
                order.status = OrderStatus::Rejected;
                result.accepted = true;
                result.terminal = true;
            } else {
                result.text = "已有成交的订单收到拒单回报，已忽略";
            }
            if (result.text.empty()) {
                result.text = report.text;
            }
            return result;
        }

        if (report.status == OrderStatus::Canceled) {
            order.status = OrderStatus::Canceled;
            result.accepted = true;
            result.terminal = true;
            result.text = report.text;
            return result;
        }

        result.text = "未知订单状态回报，已忽略";
        return result;
    }
};

}  // 命名空间 exec
