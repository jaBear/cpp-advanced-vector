#pragma once
#include <algorithm>
#include <iterator>
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <exception>
#include <memory>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }
    
    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }
    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (rhs.buffer_ != buffer_) {
            Swap(rhs);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    
    Vector() = default;
    
    explicit Vector(size_t size)
            : data_(size)
            , size_(size)
        {
            std::uninitialized_value_construct_n(data_.GetAddress(), size);
        }
    
    Vector(const Vector& other)
            : data_(other.size_)
            , size_(other.size_)  //
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }
    
    Vector(Vector&& other)
    {
        data_ = std::move(other.data_);
        size_ = std::exchange(other.size_, 0);
    }
    
    Vector& operator=(const Vector& rhs) {
            if (this != &rhs) {
                if (rhs.size_ > data_.Capacity()) {
                    Vector rhs_copy(rhs);
                    Swap(rhs_copy);
                    /* Применить copy-and-swap */
                } else {
                    CopyElementsFromVector(rhs);
                    /* Скопировать элементы из rhs, создав при необходимости новые
                       или удалив существующие */
                }
            }
            return *this;
        }
    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }
    
    using iterator = T*;
    using const_iterator = const T*;
    
    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }
    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }
    
    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }
    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::forward<T>(value));
    }
    
    void CopyElementsFromVector (const Vector& rhs) {
        size_t copy_elem = rhs.size_ < size_ ? rhs.size_ : size_;
        auto end = std::copy_n(rhs.data_.GetAddress(), copy_elem, data_.GetAddress());
        if (rhs.size_ < size_) {
            std::destroy_n(end, size_ - rhs.size_);
        } else {
            std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, end);
        }
        size_ = rhs.size_;
    }
    
    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= begin() && pos <= end());
        size_t index = static_cast<size_t>(pos - begin());
        auto dist = std::distance(cbegin(), pos);
        auto non_const_iter = begin() + dist;
        
        if (size_ == Capacity()) {
            size_t new_size = 1;
            if (size_ > 0) {
                new_size = size_ * 2;
            }
            RawMemory<T> new_data(new_size);
            non_const_iter = new_data.GetAddress() + dist;
            new(new_data.GetAddress() + dist) T(std::forward<Args>(args)...);
            
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(data_.GetAddress(), dist, new_data.GetAddress());
                std::uninitialized_move_n(data_.GetAddress() + dist, size_ - dist, new_data.GetAddress() + dist + 1);
            } else {
                std::uninitialized_copy_n(data_.GetAddress(), dist, new_data.GetAddress());
                std::uninitialized_copy_n(data_.GetAddress() + dist, size_ - dist,  new_data.GetAddress());
            }
            
            data_.Swap(new_data);
            DestroyN(new_data.GetAddress(), size_);

        } else if (end() == pos) {
            return &EmplaceBack(std::forward<Args>(args)...);
        } else {
            T temp(std::forward<Args>(args)...);
            new (end()) T(std::move(data_[size_ - 1]));

            std::move_backward(begin() + index, end() - 1, end());
            data_[dist] = (std::move(temp));
        }
        ++size_;

        return non_const_iter;
    }
    
    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ == Capacity()) {
            Emplace(end(), std::forward<Args>(args)...);
            return  *(data_.GetAddress() + size_ - 1);
        } else {
            new(data_.GetAddress() + size_) T(std::forward<Args>(args)...);
            ++size_;
        }
        return *(data_.GetAddress() + size_ - 1);
    }
    
    void Resize(size_t new_size) {
        if (new_size == size_)
            return;
        
        size_t size_dif = size_ > new_size ? size_ - new_size : new_size - size_;

        if (size_ > new_size) {
            DestroyN(data_ + new_size, size_dif);
        } else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_ + size_, size_dif);
        }
        
        size_ = new_size;
        
    }
    
    void PushBack(const T& value) {
        EmplaceBack(value);
    }
    void PushBack(T&& value) {
        EmplaceBack(std::forward<T>(value));
    }
    
    void PopBack() {
        if (size_ > 0) {
            std::destroy_at(data_.GetAddress() + size_ - 1);
            --size_;
        }
    }
    
    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                        std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        data_.Swap(new_data);
        DestroyN(new_data.GetAddress(), size_);
    }
    
    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }
    
    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }
    
    iterator Erase(const_iterator pos) {
        assert(pos >= begin() && pos < end());
        size_t index = static_cast<size_t>(pos - begin());
        std::move(begin() + index + 1, end(), begin() + index);
        std::destroy_at(end());
        --size_;
        return begin() + index;
    } /*noexcept(std::is_nothrow_move_assignable_v<T>)*/;
    
    ~Vector() {
        DestroyN(data_.GetAddress(), size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    static void DestroyN(T* buf, size_t n) noexcept {
        std::destroy_n(buf, n);
    }
    
};
