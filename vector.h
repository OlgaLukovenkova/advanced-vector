#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;

    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept {
		Deallocate(buffer_);
		buffer_ = std::exchange(other.buffer_, nullptr);
		capacity_ = std::exchange(other.capacity_, 0);
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
		Deallocate(buffer_);
        buffer_ = std::exchange(rhs.buffer_, nullptr);
        capacity_ = std::exchange(rhs.capacity_, 0);
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // allocates raw memory for n items and returns the pointer
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // frees the raw memory that was allocated using Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_ + size_;
    }

    const_iterator begin() const noexcept {
        return const_cast<Vector&>(*this).begin();
    }

    const_iterator end() const noexcept {
        return const_cast<Vector&>(*this).end();
    }

    const_iterator cbegin() const noexcept {
        return static_cast<const_iterator>(begin());
    }

    const_iterator cend() const noexcept {
        return static_cast<const_iterator>(end());
    }

    Vector() = default;

    Vector(size_t size)
        : data_(size)
        , size_(size) {
        std::uninitialized_value_construct_n(begin(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, begin());
    }

    Vector(Vector&& other) noexcept
        : data_(std::move(other.data_))
        , size_(other.size_) {
        other.size_ = 0;
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) { //copy and swap
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
            else {
                std::copy_n(rhs.begin(), std::min(size_, rhs.size_), begin());

                if (rhs.size_ < size_) {
                    std::destroy_n(begin() + rhs.size_, size_ - rhs.size_);
                }
                else {
                    std::uninitialized_copy_n(rhs.begin() + size_, rhs.size_ - size_, end());
                }

                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        data_ = std::move(rhs.data_);
        size_ = std::exchange(rhs.size_, 0);
        return *this;
    }

    ~Vector() noexcept {
        std::destroy_n(begin(), size_);
    }

    void Swap(Vector& other) noexcept {
        std::swap(size_, other.size_);
        data_.Swap(other.data_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    void Reserve(size_t capacity) {
        if (capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(capacity);
        CopyOrMoveToNewBuffer(begin(), new_data.GetAddress(), size_);
        std::destroy_n(begin(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(begin() + new_size, size_ - new_size);
        }
        else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(end(), new_size - size_);
        }
        size_ = new_size;
    }

    void PushBack(const T& value) {
        [[maybe_unused]] T& res = EmplaceBack(value);
    }

    void PushBack(T&& value) {
        [[maybe_unused]] T& res = EmplaceBack(std::move(value));
    }

    void PopBack() noexcept {
        data_[size_ - 1].~T();
        --size_;
    }

    template <typename... Args>
    [[nodiscard]] T& EmplaceBack(Args&&... args) {

        if (size_ == data_.Capacity()) {
            return *ReallocateMemoryAddingNewElement(size_, std::forward<Args>(args)...);
        }

        T* ptr = new (data_ + size_) T(std::forward<Args>(args)...);
        ++size_;
        return *ptr;
    }

    [[nodiscard]] iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    [[nodiscard]] iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    template <typename... Args>
    [[nodiscard]] iterator Emplace(const_iterator pos, Args&&... args) {
        size_t index = pos - begin();

        if (index == size_) {
            return &EmplaceBack(std::forward<Args>(args)...);
        }

        if (size_ == data_.Capacity()) {
            return ReallocateMemoryAddingNewElement(index, std::forward<Args>(args)...);
        }

        T temp_value(std::forward<Args>(args)...);
        new (end()) T(std::move(data_[size_ - 1]));
        std::move_backward(begin() + index, end() - 1, end());
        data_[index] = std::move(temp_value);
        ++size_;
        return begin() + index;
    }

    [[nodiscard]] iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        size_t index = pos - begin();
        std::move(begin() + index + 1, end(), begin() + index);
        data_[size_ - 1].~T();
        --size_;
        return begin() + index;
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    static void CopyOrMoveToNewBuffer(T* from, T* to, size_t number) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(from, number, to);
        }
        else {
            std::uninitialized_copy_n(from, number, to);
        }
    }

    size_t CalcNewCapacity() const noexcept {
        return size_ == 0 ? 1 : size_ * 2;
    }

    template <typename... Args>
    T* ReallocateMemoryAddingNewElement(size_t index, Args&&... args) {
        RawMemory<T> new_data(CalcNewCapacity());
        T* ptr = new (new_data + index) T(std::forward<Args>(args)...);
        try {
            CopyOrMoveToNewBuffer(begin(), new_data.GetAddress(), index);
            std::destroy_n(begin(), index);
        }
        catch (...) {
            ptr->~T();
            throw;
        }

        if (index < size_) {
            try {
                CopyOrMoveToNewBuffer(begin() + index, new_data.GetAddress() + index + 1, size_ - index);
                std::destroy_n(begin() + index, size_ - index);
            }
            catch (...) {
                std::destroy_n(new_data.GetAddress(), index + 1);
                throw;
            }
        }
        data_.Swap(new_data);
        ++size_;
        return ptr;
    }
};