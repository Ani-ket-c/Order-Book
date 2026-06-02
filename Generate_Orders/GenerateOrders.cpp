#include "GenerateOrders.hpp"
#include "../Limit_Order_Book/Book.hpp"
#include "../Limit_Order_Book/Limit.hpp"
#include "../Limit_Order_Book/Order.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <random>
#include <algorithm>
#include <functional>
#include <numeric>

GenerateOrders::GenerateOrders(Book* _book) : book(_book), gen(rd()) {}

void GenerateOrders::market()
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::uniform_int_distribution<> buyOrSellDist(0, 1);

    int shares = sharesDist(gen);
    bool buyOrSell = buyOrSellDist(gen);

    book->marketOrder(orderId, buyOrSell, shares);
    orderId ++;
}

void GenerateOrders::addLimit()
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::normal_distribution<> limitPriceDist(300, 50);
    std::uniform_int_distribution<> buyOrSellDist(0, 1);


    int shares = sharesDist(gen);
    bool buyOrSell = buyOrSellDist(gen);
    int limitPrice;

    if (buyOrSell)
    {
        do {
            limitPrice = limitPriceDist(gen);
        } while (limitPrice >= book->getLowestSell()->getLimitPrice());  
    } else {
        do {
            limitPrice = limitPriceDist(gen);
        } while (limitPrice <= book->getHighestBuy()->getLimitPrice());  
    }

    book->addLimitOrder(orderId, buyOrSell, shares, limitPrice);
    orderId ++;
}

void GenerateOrders::cancelLimit()
{
    Order* order = book->getRandomOrder(0, gen);

    if (order == nullptr)
    {
        addLimit();
        return;
    }
    int orderId = order->getOrderId();
    book->cancelLimitOrder(orderId);
}

void GenerateOrders::modifyLimit()
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::normal_distribution<> limitPriceDist(book->getHighestBuy()->getLimitPrice(), 50);

    int shares = sharesDist(gen);

    Order* order = book->getRandomOrder(0, gen);

    if (order == nullptr)
    {
        addLimit();
        return;
    }
    int orderId = order->getOrderId();
    bool buyOrSell = order->getBuyOrSell();
    int limitPrice;
    if (buyOrSell)
    {
        do {
            limitPrice = limitPriceDist(gen);
        } while (limitPrice >= book->getLowestSell()->getLimitPrice());  
    } else {
        do {
            limitPrice = limitPriceDist(gen);
        } while (limitPrice <= book->getHighestBuy()->getLimitPrice());  
    }
    book->modifyLimitOrder(orderId, shares, limitPrice);
}

void GenerateOrders::addLimitMarket()
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::uniform_int_distribution<> buyOrSellDist(0, 1);
    std::normal_distribution<> limitPriceDist(book->getHighestBuy()->getLimitPrice(), 50);

    int shares = sharesDist(gen);
    int limitPrice;
    bool buyOrSell = buyOrSellDist(gen);

    if (buyOrSell)
    {
        limitPrice = book->getLowestSell()->getLimitPrice() + 1;
    } else {
        limitPrice = book->getHighestBuy()->getLimitPrice() - 1;
    }
    
    book->addLimitOrder(orderId, buyOrSell, shares, limitPrice);
    orderId ++;
}

void GenerateOrders::addStop()
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::normal_distribution<> stopPriceDist(book->getHighestBuy()->getLimitPrice(), 50);
    std::uniform_int_distribution<> buyOrSellDist(0, 1);


    int shares = sharesDist(gen);
    bool buyOrSell = buyOrSellDist(gen);

    int stopPrice;
    if (buyOrSell)
    {
        do {
            stopPrice = stopPriceDist(gen);
        } while (stopPrice <= book->getLowestSell()->getLimitPrice());  
    } else {
        do {
            stopPrice = stopPriceDist(gen);
        } while (stopPrice >= book->getHighestBuy()->getLimitPrice());  
    }

    book->addStopOrder(orderId, buyOrSell, shares, stopPrice);
    orderId ++;
}

void GenerateOrders::cancelStop()
{
    Order* order = book->getRandomOrder(1, gen);

    if (order == nullptr)
    {
        addStop();
        return;
    }
    int orderId = order->getOrderId();
    book->cancelStopOrder(orderId);
}

void GenerateOrders::modifyStop()
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::normal_distribution<> stopPriceDist(book->getHighestBuy()->getLimitPrice(), 50);

    int shares = sharesDist(gen);

    Order* order = book->getRandomOrder(1, gen);

    if (order == nullptr)
    {
        addStop();
        return;
    }
    int orderId = order->getOrderId();
    bool buyOrSell = order->getBuyOrSell();
    int stopPrice;
    if (buyOrSell)
    {
        do {
            stopPrice = stopPriceDist(gen);
        } while (stopPrice <= book->getLowestSell()->getLimitPrice());  
    } else {
        do {
            stopPrice = stopPriceDist(gen);
        } while (stopPrice >= book->getHighestBuy()->getLimitPrice());  
    }
    book->modifyStopOrder(orderId, shares, stopPrice);
}

void GenerateOrders::addStopLimit()
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::normal_distribution<> stopPriceDist(book->getHighestBuy()->getLimitPrice(), 50);
    std::uniform_int_distribution<> limitPriceDist(1, 5);
    std::uniform_int_distribution<> buyOrSellDist(0, 1);


    int shares = sharesDist(gen);
    bool buyOrSell = buyOrSellDist(gen);

    int stopPrice;
    int limitPrice;
    if (buyOrSell)
    {
        do {
            stopPrice = stopPriceDist(gen);
        } while (stopPrice <= book->getLowestSell()->getLimitPrice());  
        limitPrice = stopPrice + limitPriceDist(gen);
    } else {
        do {
            stopPrice = stopPriceDist(gen);
        } while (stopPrice >= book->getHighestBuy()->getLimitPrice());
        limitPrice = stopPrice - limitPriceDist(gen);
    }

    book->addStopLimitOrder(orderId, buyOrSell, shares, limitPrice, stopPrice);
    orderId ++;
}

void GenerateOrders::cancelStopLimit()
{
    Order* order = book->getRandomOrder(2, gen);

    if (order == nullptr)
    {
        addStopLimit();
        return;
    }
    int orderId = order->getOrderId();
    book->cancelStopLimitOrder(orderId);
}

void GenerateOrders::modifyStopLimit()
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::normal_distribution<> stopPriceDist(book->getHighestBuy()->getLimitPrice(), 50);
    std::uniform_int_distribution<> limitPriceDist(1, 5);

    int shares = sharesDist(gen);

    Order* order = book->getRandomOrder(2, gen);

    if (order == nullptr)
    {
        addStopLimit();
        return;
    }
    int orderId = order->getOrderId();
    bool buyOrSell = order->getBuyOrSell();
    int stopPrice;
    int limitPrice;
    if (buyOrSell)
    {
        do {
            stopPrice = stopPriceDist(gen);
        } while (stopPrice <= book->getLowestSell()->getLimitPrice());  
        limitPrice = stopPrice + limitPriceDist(gen);
    } else {
        do {
            stopPrice = stopPriceDist(gen);
        } while (stopPrice >= book->getHighestBuy()->getLimitPrice());  
        limitPrice = stopPrice - limitPriceDist(gen);
    }
    book->modifyStopLimitOrder(orderId, shares, limitPrice, stopPrice);
}

void GenerateOrders::createOrders(int numberOfOrders)
{
    std::uniform_real_distribution<> dis(0.0, 1.0);

    // Define the probabilities and actions
    std::vector<double> probabilities = {0.025, 0, 0.195, 0.295, 0.025, 0, 0.12, 0.12, 0, 0.12, 0.12};
    // std::vector<double> probabilities = {0.05, 0, 0.2, 0.3, 0, 0, 0.15, 0.15, 0, 0, 0.15};
    std::vector<std::function<void()>> actions = {
        std::bind(&GenerateOrders::market, this),
        std::bind(&GenerateOrders::addLimit, this),
        std::bind(&GenerateOrders::cancelLimit, this),
        std::bind(&GenerateOrders::modifyLimit, this),
        std::bind(&GenerateOrders::addLimitMarket, this),
        std::bind(&GenerateOrders::addStop, this),
        std::bind(&GenerateOrders::cancelStop, this),
        std::bind(&GenerateOrders::modifyStop, this),
        std::bind(&GenerateOrders::addStopLimit, this),
        std::bind(&GenerateOrders::cancelStopLimit, this),
        std::bind(&GenerateOrders::modifyStopLimit, this),
    };

    // Calculate the cumulative probabilities
    std::partial_sum(probabilities.begin(), probabilities.end(), probabilities.begin());

    for (size_t i = 1; i < numberOfOrders + 1; i++)
    {
        // Generate a random number between 0 and 1 
        double randNum = dis(gen);

        // Use std::lower_bound to find the first element greater than randNum
        auto it = std::lower_bound(probabilities.begin(), probabilities.end(), randNum);

        // Calculate the index of the selected action
        int selectedAction = std::distance(probabilities.begin(), it);

        // Perform the selected action
        if (selectedAction < probabilities.size()) {
            actions[selectedAction]();

            // if (i%100000 == 0)
            // {
            //     std::cout << "-------------------------------------" << std::endl;
            //     std::cout << "Number of orders done: " << i << std::endl;
                
            //     std::cout << "Highest Stop Sell: " << book->getHighestStopSell()->getLimitPrice() << ", Lowest Stop Buy: " << book->getLowestStopBuy()->getLimitPrice() << std::endl;
            //     std::cout << "Lowest Sell: " << book->getLowestSell()->getLimitPrice() << ", Highest Buy: " << book->getHighestBuy()->getLimitPrice() << std::endl;
            //     book->printOrderBook();
            // }
            
        } else {
            std::cerr << "Error: No action selected!" << std::endl;
        }
    }
}

void GenerateOrders::createInitialOrders(int numberOfOrders, int centreOfBook)
{
    std::uniform_int_distribution<> sharesDist(1, 1000);
    std::normal_distribution<> limitPriceDist(centreOfBook, 50);

    for (int order = 1; order <= numberOfOrders; ++order) {
        int shares = sharesDist(gen);
        int limitPrice = limitPriceDist(gen);
        bool buyOrSell = limitPrice < centreOfBook;
        book->addLimitOrder(order, buyOrSell, shares, limitPrice);
    }

    std::uniform_int_distribution<> stopLimitPriceDist(1, 5);
    std::uniform_int_distribution<> stopOrStopLimitDist(0, 1);

    for (int order = numberOfOrders + 1; order <= numberOfOrders * 1.1; ++order) {
        int shares = sharesDist(gen);
        int stopPrice = limitPriceDist(gen);
        bool buyOrSell = stopPrice > centreOfBook;
        bool stopOrStopLimit = stopOrStopLimitDist(gen);

        if (stopOrStopLimit) {
            book->addStopOrder(order, buyOrSell, shares, stopPrice);
        } else {
            int limitPrice = buyOrSell ? stopPrice + stopLimitPriceDist(gen) : stopPrice - stopLimitPriceDist(gen);
            book->addStopLimitOrder(order, buyOrSell, shares, limitPrice, stopPrice);
        }
    }
}