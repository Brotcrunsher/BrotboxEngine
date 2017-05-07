#pragma once

#include "DataType.h"
#include "UtilTest.h"
#include "List.h"
#include <iostream>
#include "STLCapsule.h"

namespace bbe {
	class StackAllocatorDestructor {
	private:
		const void* m_data;
		void(*destructor)(const void*);
	public:
		template<class T>
		explicit StackAllocatorDestructor(const T& data) noexcept :
			m_data(bbe::addressOf(data)) {
			destructor = [](const void* lambdaData) {
				auto originalType = static_cast<const T*>(lambdaData);
				originalType->~T();
			};
		}

		void operator () () noexcept {
			destructor(m_data);
		}
	};

	template <typename T>
	class StackAllocatorMarker {
	public:
		T* m_markerValue;
		size_t m_destructorHandle;
		StackAllocatorMarker(T* markerValue, size_t destructorHandle) :
			m_markerValue(markerValue), m_destructorHandle(destructorHandle) {

		}
	};

	template <typename T = byte, typename Allocator = STLAllocator<T>>
	class StackAllocator {
	public:
		typedef typename T                                           value_type;
		typedef typename T*                                          pointer;
		typedef typename const T*                                    const_pointer;
		typedef typename T&                                          reference;
		typedef typename const T&                                    const_reference;
		typedef typename size_t                                      size_type;
		typedef typename std::pointer_traits<T*>::difference_type    difference_type;
		typedef typename std::pointer_traits<T*>::rebind<const void> const_void_pointer;
	private:
		static const size_t STACKALLOCATORDEFAULSIZE = 1024;
		T* m_data = nullptr;
		T* m_head = nullptr;
		size_t m_size = 0;

		Allocator* m_parentAllocator = nullptr;
		bool m_needsToDeleteParentAllocator = false;
		
		List<StackAllocatorDestructor> destructors; //TODO change to own container type
	public:
		explicit StackAllocator(size_t size = STACKALLOCATORDEFAULSIZE, Allocator* parentAllocator = nullptr)
			: m_size(size), m_parentAllocator(parentAllocator) 
		{
			if (m_parentAllocator == nullptr) {
				m_parentAllocator = new Allocator();
				m_needsToDeleteParentAllocator = true;
			}
			m_data = m_parentAllocator->allocate(m_size);
			m_head = m_data;

			memset(m_data, 0, m_size);
		}

		~StackAllocator() {
			if (m_data != m_head) {
				//TODO add further error handling
				debugBreak();
			}
			if (m_data != nullptr && m_parentAllocator != nullptr) {
				m_parentAllocator->deallocate(m_data, m_size);
			}
			if (m_needsToDeleteParentAllocator) {
				delete m_parentAllocator;
			}
			m_data = nullptr;
			m_head = nullptr;
		}

		StackAllocator(const StackAllocator&  other) = delete; //Copy Constructor
		StackAllocator(const StackAllocator&& other) = delete; //Move Constructor
		StackAllocator& operator=(const StackAllocator&  other) = delete; //Copy Assignment
		StackAllocator& operator=(StackAllocator&& other) = delete; //Move Assignment

		template <typename U, typename... arguments>
		U* allocateObject(size_t amountOfObjects = 1, arguments&&... args) {
			T* allocationLocation = (T*)nextMultiple((size_t)alignof(T), (size_t)m_head);
			T* newHeadPointer = allocationLocation + amountOfObjects * sizeof(U);
			if (newHeadPointer <= m_data + m_size) {
				U* returnPointer = reinterpret_cast<U*>(allocationLocation);
				m_head = newHeadPointer;
				for (size_t i = 0; i < amountOfObjects; i++) {
					U* object = new (bbe::addressOf(returnPointer[i])) U(std::forward<arguments>(args)...);
					destructors.pushBack(StackAllocatorDestructor(*object));
				}
				return returnPointer;
			}
			else {
				//TODO add additional errorhandling
				return nullptr;
			}
		}


		void* allocate(size_t amountOfBytes, size_t alignment = 1)
		{
			T* allocationLocation = nextMultiple(alignment, m_head);
			T* newHeadPointer = allocationLocation + amountOfBytes;
			if (newHeadPointer <= m_data + m_size) {
				m_head = newHeadPointer;
				return allocationLocation;
			}
			else {
				//TODO add additional errorhandling
				return nullptr;
			}
		}

		StackAllocatorMarker<T> getMarker() {
			return StackAllocatorMarker<T>(m_head, destructors.getLength());
		}
		
		void deallocateToMarker(StackAllocatorMarker<T> sam, bool callDestructors = true) {
			m_head = sam.m_markerValue;
			if (callDestructors) {
				while (destructors.getLength() > sam.m_destructorHandle) {
					destructors.last()();
					destructors.popBack();
				}
			}
			else {
				while (destructors.getLength() > sam.m_destructorHandle) {
					destructors.popBack();
				}
			}
		}

		void deallocateAll(bool callDestructors = true) {
			//TODO call Destructors
			m_head = m_data;
			if (callDestructors) {
				while (destructors.size() > 0) {
					StackAllocatorDestructor sad = destructors.back();
					sad();
					destructors.pop_back();
				}
			}
			else {
				while (destructors.size() > 0) {
					destructors.pop_back();
				}
			}
		}

	};
	

}