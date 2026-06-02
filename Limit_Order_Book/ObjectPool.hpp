#ifndef OBJECTPOOL_HPP
#define OBJECTPOOL_HPP

#include <vector>
#include <cstddef>

template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t blockSize = 4096) : blockSize_(blockSize) {
        grow();
    }

    ~ObjectPool() {
        for (char* block : blocks_) {
            delete[] block;
        }
    }

    template<typename... Args>
    T* alloc(Args&&... args) {
        if (freeList_.empty()) {
            grow();
        }
        T* obj = freeList_.back();
        freeList_.pop_back();
        new (obj) T(std::forward<Args>(args)...);
        return obj;
    }

    void free(T* obj) {
        obj->~T();
        freeList_.push_back(obj);
    }

private:
    void grow() {
        char* block = new char[blockSize_ * sizeof(T)];
        blocks_.push_back(block);
        for (size_t i = 0; i < blockSize_; ++i) {
            freeList_.push_back(reinterpret_cast<T*>(block + i * sizeof(T)));
        }
    }

    size_t blockSize_;
    std::vector<char*> blocks_;
    std::vector<T*> freeList_;
};

#endif
