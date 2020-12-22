#ifndef DY_NET_BUFFER_H
#define DY_NET_BUFFER_H

#include <string.h>

#include <cstddef>
#include <algorithm>
#include <functional>

namespace dy
{
namespace utility
{
class buffer
{
public:
    using buff_type = std::string;
    using size_type = buff_type::size_type;

    enum constant : size_type
    {
        max_pack_size  = 4 * 1024 * 1024,   //  4M
        initial_size   = 32 * 1024,         // 32K
        per_alloc_size = 32 * 1024,         // 32K
    };

    buffer()
    {
        container_.resize(constant::initial_size);
        size_ = 0;
        offset_ = 0;
    }

    buffer(const char* data, const size_type& length)
    {
        container_.resize(constant::initial_size > length ? constant::initial_size : length);
        memcpy(const_cast<char*>(container_.data()), data, length);
        size_ = length;
        offset_ = 0;
    }

    virtual ~buffer()
    {
        // nothing
    }

    const char* data() const
    {
        return container_.data() + offset_;
    }

    const size_type& size() const
    {
        return size_;
    }

    bool empty() const
    {
        return size_ == 0;
    }

    void push_cache(const size_type& size)
    {
        size_ += size;
    }

    void pop_cache(const size_type& size)
    {
        offset_ += size;
        size_ -= size;
    }

    void clear()
    {
        offset_ = 0;
        size_ = 0;
    }

    char* writable_buff()
    {
        if (container_.size() <= offset_ + size_)
        {
            // 偏移量大于一次申请长度时移动否则扩容
            if (offset_ >= constant::per_alloc_size)
            {
                move2head();
            }
            else
            {
                expand();
            }
        }
        return const_cast<char*>(container_.data() + offset_ + size_);
    }

    size_type writable_size()
    {
        return container_.size() - offset_ - size_;
    }

    void move2head()
    {
        // 将当前有效数据移动到data_起始位置
        memmove(const_cast<char*>(container_.data()), this->data(), size_);
        // 重置偏移量
        offset_ = 0;
    }

protected:
    void expand()
    {
        container_.resize(container_.size() + constant::per_alloc_size);
    }
    void shrink()
    {
        move2head();
        container_.resize(std::max<size_type>(constant::initial_size, (size_ + constant::per_alloc_size - 1) / constant::per_alloc_size * constant::per_alloc_size));
    }

protected:
    buff_type container_;   // 数据容器
    size_type offset_;      // 有效数据位置
    size_type size_;        // 有效数据长度
};


} // namespace utility
} // namespace dy

#endif