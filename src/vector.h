
#pragma once

#include <exception>
#include <cassert>
#include <memory>
#include <iterator>
#include <algorithm>

#ifdef _MSC_VER
#define FCV_NOEXCEPT _NOEXCEPT
#else
#define FCV_NOEXCEPT noexcept
#endif

template<
	typename _Ty,
	typename _Alloc = std::allocator<_Ty>
>
class fixed_capacity_vector
{
public:
	typedef _Ty value_type;
	typedef _Alloc allocator_type;
	typedef unsigned int size_type;
	typedef value_type* iterator;
	typedef const value_type* const_iterator;
	typedef std::reverse_iterator<iterator> reverse_iterator;
	typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

	explicit fixed_capacity_vector(size_type _capacity, const allocator_type& allocator = allocator_type())
		: capacity_(0), allocator_(allocator), buffer_(nullptr), size_(0)
	{
		_alloc(_capacity);
	}

	fixed_capacity_vector(const fixed_capacity_vector& other)
		: capacity_(0), buffer_(nullptr), size_(0)
		, allocator_(std::allocator_traits<allocator_type>::select_on_container_copy_construction(other.allocator_)) 
	{
		_alloc(other.capacity());
		_copy_construct(other.size(), other.buffer_);
	}

	fixed_capacity_vector(fixed_capacity_vector&& other) FCV_NOEXCEPT
		// [allocator.requirements] requires allocator move not to throw
		: capacity_(0), buffer_(nullptr), size_(0), allocator_(std::move(other.allocator_))
	{
		_swap_state(*this, other);
	}

	fixed_capacity_vector(size_type capacity, const std::initializer_list<value_type>& il,
		const allocator_type& allocator = allocator_type())
		: capacity_(0), allocator_(allocator), buffer_(nullptr), size_(0)
	{
		if(il.size() > capacity)
			throw std::length_error("size of initializer_list exceeds capacity of fixed_capacity_vector");

		using std::begin;
		_alloc(capacity);
		_copy_construct(il.size(), begin(il));
	}

	fixed_capacity_vector& operator=(const fixed_capacity_vector& other)
	{
		if(this != &other)
		{
			if(std::allocator_traits<allocator_type>::propagate_on_container_copy_assignment::value
				&& allocator_ != other.allocator_)
			{
				clear();
				_free();
				allocator_ = other.allocator_;
				_alloc(other.capacity());
			}
			else if(capacity_ != other.capacity())
			{
				clear();
				_free();
				_alloc(other.capacity());
			}

			if(size() >= other.size())
			{
				resize(other.size());
				_copy_assign(other.size(), buffer_, other.buffer_);
			}
			else
			{
				_copy_assign(size(), buffer_, other.buffer_);
				_copy_construct(other.size() - size(), other.buffer_ + size());
			}
		}
		return *this;
	}

	fixed_capacity_vector& operator=(fixed_capacity_vector&& other)
	{
		if(this != &other)
		{
			clear();
			_free();

			if(std::allocator_traits<allocator_type>::propagate_on_container_move_assignment::value
				&& allocator_ != other.allocator_)
			{
				allocator_ = std::move(other.allocator_);
			}

			assert(!size_ && !capacity_ && !buffer_);
			_swap_state(*this, other);
		}
		return *this;
	}

	fixed_capacity_vector& operator=(const std::initializer_list<value_type>& il)
	{
		if(capacity() < il.size())
			throw std::length_error("size of initializer_list exceeds capacity of fixed_capacity_vector");

		using std::begin;
		if(il.size() <= size())
		{
			resize(il.size());
			_copy_assign(il.size(), buffer_, begin(il));
		}
		else
		{
			_copy_assign(size(), buffer_, begin(il));
			_copy_construct(il.size() - size(), begin(il) + size());
		}
		return *this;
	}

	void swap(fixed_capacity_vector& other) FCV_NOEXCEPT
	{
		// [allocator.requirements] requires allocator assign not to throw
		if(this != &other)
		{
			using std::swap;
			if(std::allocator_traits<allocator_type>::propagate_on_container_swap::value)
				swap(allocator_, other.allocator_);
			_swap_state(*this, other);
		}
	}
		
	~fixed_capacity_vector() FCV_NOEXCEPT
	{
		clear();
		_free();
	}

	size_type capacity() const FCV_NOEXCEPT 
	{
		return capacity_;
	}
		
	size_type size() const FCV_NOEXCEPT
	{
		return size_;
	}

	bool empty() const FCV_NOEXCEPT
	{
		return size() == 0;
	}

	size_type max_size() const FCV_NOEXCEPT
	{
		return std::allocator_traits<allocator_type>::max_size(allocator_);
	}
		
	iterator begin() FCV_NOEXCEPT
	{
		return buffer_;
	}

	const_iterator begin() const FCV_NOEXCEPT
	{
		return cbegin();
	}

	iterator end() FCV_NOEXCEPT
	{
		return (buffer_ + size());
	}

	const_iterator end() const FCV_NOEXCEPT
	{
		return cend();
	}

	const_iterator cbegin() const FCV_NOEXCEPT
	{
		return buffer_;
	}

	const_iterator cend() const FCV_NOEXCEPT
	{
		return (buffer_ + size());
	}
	
	reverse_iterator rbegin() FCV_NOEXCEPT
	{
		return _make_reverse_iter(end());
	}

	const_reverse_iterator rbegin() const FCV_NOEXCEPT
	{
		return crbegin();
	}

	reverse_iterator rend() FCV_NOEXCEPT
	{
		return _make_reverse_iter(begin());
	}

	const_reverse_iterator rend() const FCV_NOEXCEPT
	{
		return crend();
	}

	const_reverse_iterator crbegin() const FCV_NOEXCEPT
	{
		return _make_reverse_iter(end());
	}

	const_reverse_iterator crend() const FCV_NOEXCEPT
	{
		return _make_reverse_iter(begin());
	}

	void resize(size_type _size, const value_type& value = value_type())
	{
		if(_size > capacity_)
			throw std::length_error("size exceeds capacity of fixed_capacity_vector");
		
		// shrink if new size is < current size
		for(; size() > _size; )
			pop_back();

		// grow if new size is > current size
		for(; size_ < _size; ++size_) // increment size after construction because constructors may throw
			_emplace(buffer_ + size_, value);
	}
		
	void push_back(const value_type& value)
	{
		if(size_ == capacity_)
			throw std::length_error("fixed_capacity_vector out of capacity");

		_emplace(buffer_ + size_, value);
		// modify size after copy to be consistent if copy-constructor throws
		++size_;
	}

	void push_back(value_type&& value)
	{
		if(size_ == capacity_)
			throw std::length_error("fixed_capacity_vector out of capacity");

		_emplace(buffer_ + size_, std::move(value));
		// modify size after move to be consistent if move-constructor throws
		++size_;
	}

	void pop_back()
	{
		assert(!empty() && "pop_back() called on empty vector");
		if(!empty())
		{
			// destructors should not throw but in case one does, we still stay
			// in a consistent state if we decrement size first
 			--size_;
 			_destroy(buffer_ + size_);
		}
	}

	iterator insert(const_iterator pos, const value_type& value)
	{
		if(size() == capacity())
			throw std::length_error("fixed_capacity_vector out of capacity");

		_check_iterator(pos, true);
		push_back(value);

		auto p = const_cast<iterator>(pos);
		std::rotate(rbegin(), rbegin() + 1, _make_reverse_iter(p));
		return p;
	}

	iterator insert(const_iterator pos, value_type&& value)
	{
		if(size() == capacity())
			throw std::length_error("fixed_capacity_vector out of capacity");

		_check_iterator(pos, true);
		push_back(std::move(value));

		auto p = const_cast<iterator>(pos);
		std::rotate(rbegin(), rbegin() + 1, _make_reverse_iter(p));
		return p;	
	}

	iterator erase(const_iterator pos)
	{
		_check_iterator(pos, false);

		auto p = const_cast<iterator>(pos);
		std::rotate(p, p + 1, end());
		pop_back();

		return p;	
	}

	void clear()
	{
		for(; size(); )
			pop_back();
	}

	value_type& front()
	{
		assert(!empty() && "calling front() on empty container has undefined behavior");
		return buffer_[0];
	}

	const value_type& front() const
	{
		assert(!empty() && "calling front() on empty container has undefined behavior");
		return buffer_[0];		
	}

	value_type& back()
	{		
		assert(!empty() && "calling back() on empty container has undefined behavior");
		return buffer_[size() - 1];
	}

	const value_type& back() const
	{
		assert(!empty() && "calling back() on empty container has undefined behavior");
		return buffer_[size() - 1];	
	}

	value_type& at(size_type index)
	{
		assert(index < size_);
		return *(buffer_ + index);
	}

	const value_type& at(size_type index) const
	{
		assert(index < size_);
		return *(buffer_ + index);
	}

	value_type& operator[](size_type index)
	{
		return at(index);
	}

	const value_type& operator[](size_type index) const
	{
		return at(index);
	}
	
	value_type* data() FCV_NOEXCEPT
	{
		return buffer_;
	}

	const value_type* data() const FCV_NOEXCEPT
	{
		return buffer_;
	}
	
private:
	enum { _req_destruction = !std::is_scalar<value_type>::value };

	void _alloc(size_type _capacity)
	{
		assert(!buffer_);
		buffer_ = _capacity ? std::allocator_traits<allocator_type>::allocate(allocator_, _capacity) : nullptr;
		capacity_ = _capacity;
	}

	void _free()
	{
		if(capacity_)
		{
			assert(buffer_);
			std::allocator_traits<allocator_type>::deallocate(allocator_, buffer_, capacity_);
			capacity_ = 0;
			buffer_ = nullptr;
		}
	}

	void _copy_assign(size_type n, value_type* dst, const value_type* src)
	{
		for(; n > 0; --n)
			*dst++ = *src++;
	}

	void _copy_construct(size_type n, const value_type* src)
	{
		assert(!buffer_ == !n);
		assert(size() + n <= capacity());
		for(value_type* p = buffer_ + size(); n>0; --n, ++size_)
			_emplace(p++, *src++);
	}

	template<
		typename... _TyArgs
	>
	void _emplace(value_type* dst, _TyArgs&&... args)
	{
		assert(buffer_);
		assert(dst < (buffer_ + capacity_));
		std::allocator_traits<allocator_type>::construct(allocator_, dst, std::forward<_TyArgs>(args)...);
	}

	void _destroy(value_type* dst)
	{
		assert(dst);
		if(_req_destruction)
			std::allocator_traits<allocator_type>::destroy(allocator_, dst);
	}

	void _swap_state(fixed_capacity_vector& lhs, fixed_capacity_vector& rhs) FCV_NOEXCEPT
	{
		std::swap(lhs.size_, rhs.size_);
		std::swap(lhs.capacity_, rhs.capacity_);
		std::swap(lhs.buffer_, rhs.buffer_);
	}

	void _check_iterator(const_iterator iter, bool include_end) const
	{
		assert(iter >= begin() && (include_end ? (iter <= end()) : (iter < end()))
			&& "iterator not valid");
	}

	template<typename _Iter>
	std::reverse_iterator<_Iter> _make_reverse_iter(_Iter i) const FCV_NOEXCEPT
	{
		return std::reverse_iterator<_Iter>(i);
	}

private:
	value_type* buffer_;
	size_type size_;
	size_type capacity_;
	allocator_type allocator_;
};


template<
	typename _Ty,
	typename _Alloc
>
void swap(fixed_capacity_vector<_Ty, _Alloc>& lhs, fixed_capacity_vector<_Ty, _Alloc>& rhs) FCV_NOEXCEPT
{
	lhs.swap(rhs);
}