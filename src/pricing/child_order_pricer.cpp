#include "exec/pricing/child_order_pricer.hpp"

#include <algorithm>
#include <utility>

namespace exec {

namespace {

Price side_price_for_market(const PositionOrderIntent& intent,
                            const ExecutionStateView& state,
                            const BookTop& book) {
    if (intent.side == TradeSide::Buy && book.has_ask()) {
        return book.ask_price;
    }
    if (intent.side == TradeSide::Sell && book.has_bid()) {
        return book.bid_price;
    }
    return state.mark_price(intent.instrument_id);
}

Price reservation_price_for(const PositionOrderIntent& intent,
                            const ExecutionStateView& state,
                            const BookTop& book,
                            Price quote_price) {
    if (intent.side == TradeSide::Sell) {
        return quote_price > 0 ? quote_price : side_price_for_market(intent, state, book);
    }

    const auto market_price = side_price_for_market(intent, state, book);
    if (quote_price <= 0) {
        return market_price;
    }
    if (market_price <= 0) {
        return quote_price;
    }
    return std::max(quote_price, market_price);
}

PriceDecision accepted(PositionOrderIntent intent,
                       std::string model_name,
                       QuoteFeatures features = {}) {
    return PriceDecision{
        .accepted = true,
        .intent = std::move(intent),
        .model_name = std::move(model_name),
        .features = features,
    };
}

PriceDecision rejected(PositionOrderIntent intent, std::string reason, std::string model_name) {
    return PriceDecision{
        .accepted = false,
        .intent = std::move(intent),
        .reason = std::move(reason),
        .model_name = std::move(model_name),
    };
}

}  // 匿名命名空间

PriceDecision RuleBasedChildOrderPricer::price(const PositionOrderIntent& intent,
                                               const ExecutionStateView& state) const {
    if (intent.order.limit_price.has_value()) {
        auto priced = intent;
        if (*priced.order.limit_price <= 0) {
            return rejected(std::move(priced), "limit_price 必须为正", "explicit-limit");
        }
        if (priced.order.reservation_price.has_value() && *priced.order.reservation_price <= 0) {
            return rejected(std::move(priced), "reservation_price 必须为正", "explicit-limit");
        }
        if (!priced.order.reservation_price.has_value()) {
            priced.order.reservation_price = priced.order.limit_price;
        }
        return accepted(std::move(priced), "explicit-limit");
    }

    switch (intent.style.price_model_id) {
        case PriceModelId::MarketIoc:
            return price_market_ioc(intent, state);
        case PriceModelId::PassiveTopOfBook:
            return price_passive_top_of_book(intent, state);
        case PriceModelId::ImbalanceTopOfBook:
            return price_imbalance_top_of_book(intent, state);
        case PriceModelId::External:
            return rejected(intent, "External 报价模型需要注入自定义 IChildOrderPricer", "external");
    }

    return rejected(intent, "未知价格模型", "unknown");
}

PriceDecision RuleBasedChildOrderPricer::price_market_ioc(const PositionOrderIntent& intent,
                                                          const ExecutionStateView& state) const {
    if (intent.order.type == OrderType::Limit) {
        return rejected(intent, "限价单缺少 limit_price", "market-ioc");
    }

    auto priced = intent;
    const auto book = state.book_top(intent.instrument_id);
    priced.order.type = OrderType::Market;
    priced.order.tif = TimeInForce::Ioc;
    priced.order.post_only = false;
    priced.order.reservation_price = reservation_price_for(priced, state, book, 0);
    return accepted(std::move(priced), "market-ioc");
}

PriceDecision RuleBasedChildOrderPricer::price_passive_top_of_book(const PositionOrderIntent& intent,
                                                                   const ExecutionStateView& state) const {
    const auto book = state.book_top(intent.instrument_id);
    if (!book.has_bbo()) {
        return rejected(intent, "PassiveTopOfBook 需要有效盘口 bid/ask", "passive-top-of-book");
    }

    auto priced = intent;
    const auto quote_price = intent.side == TradeSide::Buy ? book.bid_price : book.ask_price;
    priced.order.type = OrderType::Limit;
    priced.order.tif = TimeInForce::Gtc;
    priced.order.limit_price = quote_price;
    priced.order.reservation_price = reservation_price_for(priced, state, book, quote_price);
    priced.order.post_only = true;
    return accepted(std::move(priced), "passive-top-of-book", make_quote_features(book));
}

PriceDecision RuleBasedChildOrderPricer::price_imbalance_top_of_book(const PositionOrderIntent& intent,
                                                                     const ExecutionStateView& state) const {
    const auto book = state.book_top(intent.instrument_id);
    if (!book.has_bbo()) {
        return rejected(intent, "ImbalanceTopOfBook 需要有效盘口 bid/ask", "imbalance-top-of-book");
    }

    const auto features = make_quote_features(book);
    const auto quote_price = features.imbalance > 0.0 ? book.ask_price : book.bid_price;

    auto priced = intent;
    priced.order.type = OrderType::Limit;
    priced.order.tif = TimeInForce::Gtc;
    priced.order.limit_price = quote_price;
    priced.order.reservation_price = reservation_price_for(priced, state, book, quote_price);
    priced.order.post_only = false;
    return accepted(std::move(priced), "imbalance-top-of-book", features);
}

}  // 命名空间 exec
