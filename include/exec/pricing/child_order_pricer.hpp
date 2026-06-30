#pragma once

#include <string>

#include "exec/market/market_data.hpp"
#include "exec/model/order.hpp"
#include "exec/state/execution_state_view.hpp"

namespace exec {

struct PriceDecision {
    bool accepted{true};
    PositionOrderIntent intent{};
    std::string reason;
    std::string model_name;
    QuoteFeatures features{};
};

class IChildOrderPricer {
public:
    virtual ~IChildOrderPricer() = default;

    virtual PriceDecision price(const PositionOrderIntent& intent, const ExecutionStateView& state) const = 0;
};

class RuleBasedChildOrderPricer final : public IChildOrderPricer {
public:
    PriceDecision price(const PositionOrderIntent& intent, const ExecutionStateView& state) const override;

private:
    PriceDecision price_market_ioc(const PositionOrderIntent& intent,
                                   const ExecutionStateView& state) const;
    PriceDecision price_passive_top_of_book(const PositionOrderIntent& intent,
                                            const ExecutionStateView& state) const;
    PriceDecision price_imbalance_top_of_book(const PositionOrderIntent& intent,
                                              const ExecutionStateView& state) const;
};

}  // 命名空间 exec
