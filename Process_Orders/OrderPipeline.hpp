#ifndef ORDERPIPELINE_HPP
#define ORDERPIPELINE_HPP

#include <string>
#include <unordered_map>
#include <string_view>
#include <sstream>
#include <vector>

class Book;

enum class OrderType {
    Market, AddLimit, AddMarketLimit, CancelLimit, ModifyLimit,
    AddStop, CancelStop, ModifyStop, AddStopLimit, CancelStopLimit, ModifyStopLimit,
    Unknown
};

struct OrderCmd {
    OrderType type;
    int orderId;
    bool buyOrSell;
    int shares;
    int limitPrice;
    int stopPrice;
    int newShares;
    int newLimit;
    int newLimitPrice;
    int newStopPrice;
};

class OrderPipeline {
private:
    Book* book;

    using OrderFunction = void(OrderPipeline::*)(std::istringstream&);
    std::unordered_map<std::string_view, OrderFunction> orderFunctions;

    void processMarketOrder(std::istringstream& iss);
    void processAddLimitOrder(std::istringstream& iss);
    void processCancelLimitOrder(std::istringstream& iss);
    void processModifyLimitOrder(std::istringstream& iss);
    void processAddStopOrder(std::istringstream& iss);
    void processCancelStopOrder(std::istringstream& iss);
    void processModifyStopOrder(std::istringstream& iss);
    void processAddStopLimitOrder(std::istringstream& iss);
    void processCancelStopLimitOrder(std::istringstream& iss);
    void processModifyStopLimitOrder(std::istringstream& iss);

    void executeOrder(const OrderCmd& cmd);
    OrderType parseOrderType(const std::string& s);

public:
    OrderPipeline(Book* book);

    // Load orders from file into memory (no matching)
    std::vector<OrderCmd> loadOrdersFromFile(const std::string& filename);

    // Process preloaded orders. enableCsv: write order_processing_times.csv
    void processOrdersFromMemory(const std::vector<OrderCmd>& orders, bool enableCsv = false);

    // Legacy: stream from file (kept for compatibility)
    void processOrdersFromFile(const std::string& filename, bool enableCsv = false);
};

#endif