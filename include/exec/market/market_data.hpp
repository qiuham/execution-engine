#pragma once

#include "exec/core/types.hpp"

namespace exec {

struct BookTop {
    Price bid_price{0};
    Quantity bid_qty{0};
    Price ask_price{0};
    Quantity ask_qty{0};

    bool has_bid() const {
        return bid_price > 0 && bid_qty > 0;
    }

    bool has_ask() const {
        return ask_price > 0 && ask_qty > 0;
    }

    bool has_bbo() const {
        return has_bid() && has_ask() && bid_price <= ask_price;
    }

    Price mid_price() const {
        if (!has_bbo()) {
            return 0;
        }
        return (bid_price + ask_price) / 2;
    }
};

struct QuoteFeatures {
    Price spread{0};
    double imbalance{0.0};
    Price mid_price{0};
};

inline QuoteFeatures make_quote_features(const BookTop& book) {
    QuoteFeatures features;
    if (!book.has_bbo()) {
        return features;
    }

    features.spread = book.ask_price - book.bid_price;
    features.mid_price = book.mid_price();
    const auto depth_sum = static_cast<double>(book.bid_qty + book.ask_qty);
    if (depth_sum > 0.0) {
        features.imbalance = static_cast<double>(book.bid_qty - book.ask_qty) / depth_sum;
    }
    return features;
}

}  // 命名空间 exec
