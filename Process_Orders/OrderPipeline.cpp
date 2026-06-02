#include "OrderPipeline.hpp"
#include "../Limit_Order_Book/Book.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <random>
#include <chrono>

OrderPipeline::OrderPipeline(Book* book) : book(book) {
    orderFunctions = {
        {"Market", &OrderPipeline::processMarketOrder},
        {"AddLimit", &OrderPipeline::processAddLimitOrder},
        {"AddMarketLimit", &OrderPipeline::processAddLimitOrder},
        {"CancelLimit", &OrderPipeline::processCancelLimitOrder},
        {"ModifyLimit", &OrderPipeline::processModifyLimitOrder},
        {"AddStop", &OrderPipeline::processAddStopOrder},
        {"CancelStop", &OrderPipeline::processCancelStopOrder},
        {"ModifyStop", &OrderPipeline::processModifyStopOrder},
        {"AddStopLimit", &OrderPipeline::processAddStopLimitOrder},
        {"CancelStopLimit", &OrderPipeline::processCancelStopLimitOrder},
        {"ModifyStopLimit", &OrderPipeline::processModifyStopLimitOrder}
    };
}

OrderType OrderPipeline::parseOrderType(const std::string& s) {
    if (s == "Market") return OrderType::Market;
    if (s == "AddLimit") return OrderType::AddLimit;
    if (s == "AddMarketLimit") return OrderType::AddMarketLimit;
    if (s == "CancelLimit") return OrderType::CancelLimit;
    if (s == "ModifyLimit") return OrderType::ModifyLimit;
    if (s == "AddStop") return OrderType::AddStop;
    if (s == "CancelStop") return OrderType::CancelStop;
    if (s == "ModifyStop") return OrderType::ModifyStop;
    if (s == "AddStopLimit") return OrderType::AddStopLimit;
    if (s == "CancelStopLimit") return OrderType::CancelStopLimit;
    if (s == "ModifyStopLimit") return OrderType::ModifyStopLimit;
    return OrderType::Unknown;
}

std::vector<OrderCmd> OrderPipeline::loadOrdersFromFile(const std::string& filename) {
    std::vector<OrderCmd> orders;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return orders;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string orderTypeStr;
        iss >> orderTypeStr;

        OrderType type = parseOrderType(orderTypeStr);
        OrderCmd cmd{};
        cmd.type = type;

        switch (type) {
            case OrderType::Market:
                iss >> cmd.orderId >> cmd.buyOrSell >> cmd.shares;
                break;
            case OrderType::AddLimit:
            case OrderType::AddMarketLimit:
                iss >> cmd.orderId >> cmd.buyOrSell >> cmd.shares >> cmd.limitPrice;
                break;
            case OrderType::CancelLimit:
            case OrderType::CancelStop:
            case OrderType::CancelStopLimit:
                iss >> cmd.orderId;
                break;
            case OrderType::ModifyLimit:
                iss >> cmd.orderId >> cmd.newShares >> cmd.newLimit;
                break;
            case OrderType::AddStop:
                iss >> cmd.orderId >> cmd.buyOrSell >> cmd.shares >> cmd.stopPrice;
                break;
            case OrderType::ModifyStop:
                iss >> cmd.orderId >> cmd.newShares >> cmd.newStopPrice;
                break;
            case OrderType::AddStopLimit:
                iss >> cmd.orderId >> cmd.buyOrSell >> cmd.shares >> cmd.limitPrice >> cmd.stopPrice;
                break;
            case OrderType::ModifyStopLimit:
                iss >> cmd.orderId >> cmd.newShares >> cmd.newLimitPrice >> cmd.newStopPrice;
                break;
            case OrderType::Unknown:
                std::cerr << "Unknown order type: " << orderTypeStr << std::endl;
                continue;  // skip unknown
        }
        orders.push_back(cmd);
    }
    return orders;
}

void OrderPipeline::executeOrder(const OrderCmd& cmd) {
    if (cmd.type == OrderType::Unknown) return;
    switch (cmd.type) {
        case OrderType::Market:
            book->marketOrder(cmd.orderId, cmd.buyOrSell, cmd.shares);
            break;
        case OrderType::AddLimit:
        case OrderType::AddMarketLimit:
            book->addLimitOrder(cmd.orderId, cmd.buyOrSell, cmd.shares, cmd.limitPrice);
            break;
        case OrderType::CancelLimit:
            book->cancelLimitOrder(cmd.orderId);
            break;
        case OrderType::ModifyLimit:
            book->modifyLimitOrder(cmd.orderId, cmd.newShares, cmd.newLimit);
            break;
        case OrderType::AddStop:
            book->addStopOrder(cmd.orderId, cmd.buyOrSell, cmd.shares, cmd.stopPrice);
            break;
        case OrderType::CancelStop:
            book->cancelStopOrder(cmd.orderId);
            break;
        case OrderType::ModifyStop:
            book->modifyStopOrder(cmd.orderId, cmd.newShares, cmd.newStopPrice);
            break;
        case OrderType::AddStopLimit:
            book->addStopLimitOrder(cmd.orderId, cmd.buyOrSell, cmd.shares, cmd.limitPrice, cmd.stopPrice);
            break;
        case OrderType::CancelStopLimit:
            book->cancelStopLimitOrder(cmd.orderId);
            break;
        case OrderType::ModifyStopLimit:
            book->modifyStopLimitOrder(cmd.orderId, cmd.newShares, cmd.newLimitPrice, cmd.newStopPrice);
            break;
        case OrderType::Unknown:
            break;
    }
}

void OrderPipeline::processOrdersFromMemory(const std::vector<OrderCmd>& orders, bool enableCsv) {
    std::ofstream csvFile;
    if (enableCsv) {
        csvFile.open("order_processing_times.csv", std::ios::trunc);
        if (!csvFile.is_open()) {
            std::cerr << "Error opening CSV file for writing." << std::endl;
        }
    }

    const char* typeNames[] = {"Market", "AddLimit", "AddMarketLimit", "CancelLimit", "ModifyLimit",
        "AddStop", "CancelStop", "ModifyStop", "AddStopLimit", "CancelStopLimit", "ModifyStopLimit", "Unknown"};

    for (const auto& cmd : orders) {
        if (cmd.type == OrderType::Unknown) continue;
        if (enableCsv && csvFile.is_open()) {
            auto start = std::chrono::steady_clock::now();
            executeOrder(cmd);
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            int typeIdx = static_cast<int>(cmd.type);
            if (cmd.type == OrderType::AddLimit || cmd.type == OrderType::AddMarketLimit) {
                csvFile << typeNames[typeIdx] << "," << duration.count() << ",0," << book->AVLTreeBalanceCount << "\n";
            } else {
                csvFile << typeNames[typeIdx] << "," << duration.count() << "," << book->executedOrdersCount << "," << book->AVLTreeBalanceCount << "\n";
            }
        } else {
            executeOrder(cmd);
        }
    }

    if (enableCsv && csvFile.is_open()) {
        csvFile.close();
    }
}

void OrderPipeline::processOrdersFromFile(const std::string& filename, bool enableCsv) {
    auto orders = loadOrdersFromFile(filename);
    if (!orders.empty()) {
        processOrdersFromMemory(orders, enableCsv);
    }
}

void OrderPipeline::processMarketOrder(std::istringstream& iss) {
    int orderId, shares;
    bool buyOrSell;
    iss >> orderId >> buyOrSell >> shares;
    book->marketOrder(orderId, buyOrSell, shares);
}

void OrderPipeline::processAddLimitOrder(std::istringstream& iss) {
    int orderId, shares, limitPrice;
    bool buyOrSell;
    iss >> orderId >> buyOrSell >> shares >> limitPrice;
    book->addLimitOrder(orderId, buyOrSell, shares, limitPrice);
}

void OrderPipeline::processCancelLimitOrder(std::istringstream& iss) {
    int orderId;
    iss >> orderId;
    book->cancelLimitOrder(orderId);
}

void OrderPipeline::processModifyLimitOrder(std::istringstream& iss) {
    int orderId, newShares, newLimit;
    iss >> orderId >> newShares >> newLimit;
    book->modifyLimitOrder(orderId, newShares, newLimit);
}

void OrderPipeline::processAddStopOrder(std::istringstream& iss) {
    int orderId, shares, stopPrice;
    bool buyOrSell;
    iss >> orderId >> buyOrSell >> shares >> stopPrice;
    book->addStopOrder(orderId, buyOrSell, shares, stopPrice);
}

void OrderPipeline::processCancelStopOrder(std::istringstream& iss) {
    int orderId;
    iss >> orderId;
    book->cancelStopOrder(orderId);
}

void OrderPipeline::processModifyStopOrder(std::istringstream& iss) {
    int orderId, newShares, newStopPrice;
    iss >> orderId >> newShares >> newStopPrice;
    book->modifyStopOrder(orderId, newShares, newStopPrice);
}

void OrderPipeline::processAddStopLimitOrder(std::istringstream& iss) {
    int orderId, shares, limitPrice, stopPrice;
    bool buyOrSell;
    iss >> orderId >> buyOrSell >> shares >> limitPrice >> stopPrice;
    book->addStopLimitOrder(orderId, buyOrSell, shares, limitPrice, stopPrice);
}

void OrderPipeline::processCancelStopLimitOrder(std::istringstream& iss) {
    int orderId;
    iss >> orderId;
    book->cancelStopLimitOrder(orderId);
}

void OrderPipeline::processModifyStopLimitOrder(std::istringstream& iss) {
    int orderId, newShares, newLimitPrice, newStopPrice;
    iss >> orderId >> newShares >> newLimitPrice >> newStopPrice;
    book->modifyStopLimitOrder(orderId, newShares, newLimitPrice, newStopPrice);
}
