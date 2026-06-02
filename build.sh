#!/bin/bash

# Script to build or run the Limit Order Book project

set -e  # Exit on error

case "$1" in
    build)
        echo "Building project..."
        mkdir -p build
        cd build
        cmake ..
        make
        echo "Build complete!"
        ;;
    run)
        echo "Running LimitOrderBook..."
        cd build
        # Create symlink for initialOrders.txt if it doesn't exist
        if [ ! -e initialOrders.txt ]; then
            ln -sf ../Generate_Orders/initialOrders.txt ./initialOrders.txt
        fi
        ./LimitOrderBook
        ;;
    *)
        echo "Usage: $0 {build|run}"
        echo "  build - Compile the project"
        echo "  run   - Run the executable"
        exit 1
        ;;
esac

