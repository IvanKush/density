
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include "heterogeneous_queue.h"

namespace density
{
    template < typename FUNCTION > class function_queue;

    /** Queue of callable objects (or function object).

        Every element in the queue is a type-erased callable object (like a std::function). Elements that can be added to
        the queue include:
            - lambda expressions
            - classes overloading the function call operator
            - the result a std::bind

        This container is similar to a std::deque of std::function objects, but has more specialized storage strategy:
        the state of all the callable object is stored tightly and linearly in the memory.

        The template argument FUNCTION is the function type used as signature for the function object contained in the queue.
        For example:

        \code{.cpp}
            function_queue<void()> queue_1;
            queue_1.push([]() { std::cout << "hello!" << std::endl; });
            queue_1.consume_front();

            function_queue<int(double, double)> queue_2;
            queue_2.push([](double a, double b) { return static_cast<int>(a + b); });
            std::cout << queue_2.consume_front(40., 2.) << std::endl;
        \endcode

        small_function_queue internally uses a void heterogeneous_queue (a queue that can contain elements of any type).
        function_queue never moves or reallocates its elements, and has both better performances and better behavior
        respect to small_function_queue when the number of elements is not small.

        \n<b> Thread safeness</b>: None. The user is responsible to avoid race conditions.
        \n<b>Exception safeness</b>: Any function of function_queue is noexcept or provides the strong exception guarantee.

        There is not a constant time function that gives the number of elements in a function_queue in constant time,
        but std::distance will do (in linear time). Anyway function_queue::mem_size, function_queue::mem_capacity and
        function_queue::empty work in constant time.
        Insertion is allowed only at the end (with the methods function_queue::push or function_queue::emplace).
        Removal is allowed only at the begin (with the methods function_queue::pop or function_queue::consume). */
    template < typename RET_VAL, typename... PARAMS >
        class function_queue<RET_VAL (PARAMS...)> final
    {
    public:

        using value_type = RET_VAL(PARAMS...);
        using features = type_features::feature_list<
            type_features::size, type_features::alignment,
            type_features::copy_construct, type_features::move_construct,
            type_features::destroy, typename type_features::invoke<value_type>, typename type_features::invoke_destroy<value_type> >;
        using underlying_queue = heterogeneous_queue<void, void_allocator, runtime_type<void, features > >;

        /** Adds a new function at the end of queue.
            @param i_source object to be used as source to construct of new element.
                - If this argument is an l-value, the new element copy-constructed (and the source object is left unchanged).
                - If this argument is an r-value, the new element move-constructed (and the source object will have an undefined but valid content).

            \n\b Requires:
                - ELEMENT_COMPLETE_TYPE must be a calable object that has RET_VAL as return type and PARAMS... as parameters
                - the type ELEMENT_COMPLETE_TYPE must copy constructible (in case of l-value) or move constructible (in case of r-value)

            \n<b> Effects on iterators </b>: all the iterators are invalidated
            \n\b Throws: unspecified
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no visible side effects).
            \n\b Complexity: constant amortized (a reallocation may be required). */
        template <typename ELEMENT_COMPLETE_TYPE>
            void push(ELEMENT_COMPLETE_TYPE && i_source)
        {
            m_queue.push(std::forward<ELEMENT_COMPLETE_TYPE>(i_source));
        }

        /** Invokes the first function object of the queue
            If the queue is empty, the behavior is undefined.
                @param i_params... parameters to be passed to the function object
                @return the value returned by the function object

            \n<b> Effects on iterators </b>: no iterator is invalidated
            \n\b Throws: anything that the function object invocation throws
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no visible side effects).
            \n\b Complexity: constant. */
        RET_VAL invoke_front(PARAMS... i_params) const
        {
            auto first = m_queue.begin();
            return first.complete_type().template get_feature<typename type_features::invoke<value_type>>()(
                first.element(), std::forward<PARAMS>(i_params)...);
        }

        /** Invokes the first function object of the queue and then deletes it from the queue.
            This function is equivalent to a call to invoke_front followed by a call to pop, but has better performance.
            If the queue is empty, the behavior is undefined.
                @param i_params... parameters to be passed to the function object
                @return the value returned by the function object.

            \n<b> Effects on iterators </b>: all the iterators are invalidated
            \n\b Throws: anything that the function object invocation throws
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no visible side effects).
            \n\b Complexity: constant. */
        RET_VAL consume_front(PARAMS... i_params)
        {
            return m_queue.manual_consume([&i_params...](const runtime_type<void, features > & i_complete_type, void * i_element) {
                return i_complete_type.template get_feature<typename type_features::invoke_destroy<value_type>>()(
                    i_element, std::forward<PARAMS>(i_params)...);
            } );
        }

        /** Deletes the first function object in the queue.
            \n<b> Effects on iterators </b>: all the iterators are invalidated
            \n\b Throws: nothing
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no visible side effects).
            \n\b Complexity: constant */
        void pop() noexcept
        {
            m_queue.pop();
        }

        /** Deletes all the function objects in the queue.
            \n<b> Effects on iterators </b>: all the iterators are invalidated
            \n\b Throws: nothing
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no visible side effects).
            \n\b Complexity: linear. */
        void clear() noexcept
        {
            m_queue.clear();
        }

        /** Returns whether this container is empty */
        bool empty() noexcept
        {
            return m_queue.empty();
        }

        /** Returns the capacity in bytes of this queue, that is the size of the memory buffer used to store the elements.
            \remark There is no way to predict if the next push\emplace will cause a reallocation.

            \n\b Throws: nothing
            \n\b Complexity: constant */
        size_t mem_capacity() const noexcept
        {
            return m_queue.mem_capacity();
        }

        /** Returns the used size in bytes.

            \n\b Throws: nothing
            \n\b Complexity: constant */
        size_t mem_size() const noexcept
        {
            return m_queue.mem_size();
        }

        /** Returns the free size in bytes. This is equivalent to function_queue::mem_capacity decreased by function_queue::mem_size.

            \n\b Throws: nothing
            \n\b Complexity: constant */
        size_t mem_free() const noexcept
        {
            return m_queue.mem_free();
        }

        typename underlying_queue::iterator begin() noexcept { return m_queue.begin(); }
        typename underlying_queue::iterator end() noexcept { return m_queue.end(); }

        typename underlying_queue::const_iterator cbegin() noexcept { return m_queue.cbegin(); }
        typename underlying_queue::const_iterator cend() noexcept { return m_queue.cend(); }

        typename underlying_queue::const_iterator begin() const noexcept { return m_queue.cbegin(); }
        typename underlying_queue::const_iterator end() const noexcept { return m_queue.cend(); }

    private:
        underlying_queue m_queue;
    };

} // namespace density
