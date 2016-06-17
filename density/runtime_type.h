
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <type_traits>
#include <limits>
#include <utility>
#include <typeinfo>
#include "density_common.h"

namespace density
{
    /*
        Runtime Type Synopsis

        class runtime_type
        {
            public:

                template <typename COMPLETE_TYPE> static runtime_type make() noexcept;

                runtime_type(const runtime_type &) noexcept;
                runtime_type(runtime_type &&) noexcept;
                ~runtime_type();

                size_t size() const noexcept;
                size_t alignment() const noexcept;

                // optional
                void * copy_construct(void * i_dest_element, const void * i_source_element) const;

                // optional
                void * move_construct_nothrow(void * i_dest_element, void * i_source_element) const noexcept;

                void destroy(void * i_element) const noexcept;
        };

    */

    /* This struct template checks the requirements on a RUNTIME_TYPE. Violations are detected with static_assert. */
    template <typename RUNTIME_TYPE>
        struct RuntimeTypeConceptCheck
    {
		static_assert(std::is_nothrow_default_constructible<RUNTIME_TYPE>::value,
			"The copy constructor of RUNTIME_TYPE must be declared as noexcept");

		static_assert(std::is_nothrow_copy_constructible<RUNTIME_TYPE>::value,
			"The copy constructor of RUNTIME_TYPE must be declared as noexcept");

        static_assert( std::is_nothrow_destructible<RUNTIME_TYPE>::value,
            "The destructor of RUNTIME_TYPE must be noexcept"); // note: destructors are noexcept by default
		
        static_assert(noexcept(std::declval<const RUNTIME_TYPE>().size()),
            "RUNTIME_TYPE::size must be declared as noexcept");

        static_assert(noexcept(std::declval<const RUNTIME_TYPE>().alignment()),
            "RUNTIME_TYPE::alignment must be declared as noexcept");
    };

	namespace type_features
	{
		/** This template is a typelist. Every type of this list is a feature that can be used by a runtime_type.
			The composition of features forms a complete type erasure.
			A Feature is a struct that performs a part of type erasure on a type, and has this form:
			\code
			struct FeatureX
			{
				using type = ...;
				template <typename BASE, typename TYPE> struct Impl
				{
					static const uintptr_t value = ...;
				};
			};
			\endcode
			A feature is a struct (or class) that doesn't depend on the type to erase. The inner type Impl
			depends on the type to erase, and on the base type. All the types to erase are covariant to
			the base type. If the base type is void any type can be erased. \n
			The value of the feature (a static const uintptr_t) in Impl stores a value that is required for
			the feature to do its job on a type: in many cases it is a pointer to a function that do a copy-costruction
			(like in the case of CopyConstruct), or destruction (in the case of Destroy). Anyway it
			may store a value, if it is small enough to fit in a uintptr_t (like for examlpe in case of Size, or 
			Alignment). \n
			The member 'type' is the real type of the value of the feature. The member function runtime_type::get_feature
			casts the value of the feature to this type before returning it. 
			\snippet misc_samples.cpp FeatureList example 1 */
		template <typename... FEATURES> struct FeatureList
		{
			static const size_t size = sizeof...(FEATURES);
		};

		/** This template concatenates two FeatureList. The first template parameter must be a FeatureList. The second
			template argument can be a FeatureList or a feature. FeatureConcat::type is an alias for a FeatureList
			that contains the concatenation al the features in the template arguments.

			\snippet misc_samples.cpp FeatureConcat example 1

			(1) FeatureConcat< FeatureList<FEATURES_1...>, FeatureList<FEATURES_2...> >::type
			(2) FeatureConcat< FeatureList<FEATURES_1...>, FEATURE_2 >::type
		*/
		template <typename...> struct FeatureConcat;
		template <typename... FIRST_FEATURES, typename... SECOND_FEATURES>
			struct FeatureConcat<FeatureList<FIRST_FEATURES...>, FeatureList<SECOND_FEATURES...>>
		{
			using type = FeatureList<FIRST_FEATURES..., SECOND_FEATURES...>;
		};
		template <typename... FIRST_FEATURES, typename SECOND_FEATURE>
			struct FeatureConcat<FeatureList<FIRST_FEATURES...>, SECOND_FEATURE>
		{
			using type = FeatureList<FIRST_FEATURES..., SECOND_FEATURE>;
		};

	} // type_features

	namespace detail
	{
		/* size_t invoke_hash(const TYPE & i_object) - Computes the hash of an object.
			- If a the call hash_func(i_object) is legal, it is used to compute the hash. This function
				should be defined in the namespace that contains TYPE (it will use ADL). If such function exits,
				its return type must be size_t.
			- Otherwise std::hash<TYPE> is used to compute the hash
		see http://stackoverflow.com/questions/257288/is-it-possible-to-write-a-c-template-to-check-for-a-functions-existence */
		template <typename> struct sfinae_true : std::true_type { };
		template <typename TYPE> static sfinae_true<decltype(hash_func(std::declval<TYPE>()))> has_hash_func_impl(int);
		template <typename TYPE> static std::false_type has_hash_func_impl(long);
		template <typename TYPE> inline size_t invoke_hash_func_impl(const TYPE & i_object, std::true_type)
		{
			static_assert(std::is_same< decltype(hash_func(i_object)), size_t >::value,
				"if the hash_func() exits for this type, then it must return a size_t");
			return hash_func(i_object);
		}
		template <typename TYPE> inline size_t invoke_hash_func_impl(const TYPE & i_object, std::false_type)
		{
			return std::hash<TYPE>()(i_object);
		}
		template <typename TYPE>
		inline size_t invoke_hash(const TYPE & i_object)
		{
			return invoke_hash_func_impl(i_object, decltype(has_hash_func_impl<TYPE>(0))());
		}

		/* DERIVED * down_cast<DERIVED*>(BASE *i_base_ptr) - down cast from a base class to a derived, assuming
			that the cast is legal. A static_cast is used if it is possible. Otherwise, if a virtual base is
			involved, dynamic_cast is used. */
		template <typename DERIVED, typename BASE> static sfinae_true<decltype(
			static_cast<DERIVED>(std::declval<BASE>()))> can_static_cast_impl(int);
		template <typename DERIVED, typename BASE> static std::false_type can_static_cast_impl(long);
		template <typename DERIVED, typename BASE>
			inline DERIVED down_cast_impl(BASE i_base_ptr, std::true_type)
		{
			return static_cast<DERIVED>(i_base_ptr);
		}
		template <typename DERIVED, typename BASE>
			inline DERIVED down_cast_impl(BASE i_base_ptr, std::false_type)
		{
			return dynamic_cast<DERIVED>(i_base_ptr);
		}
		template <typename DERIVED, typename BASE>
			inline DERIVED down_cast(BASE i_base_ptr)
		{
			static_assert(std::is_pointer<DERIVED>::value, "DERIVED must be a pointer");
			static_assert(std::is_pointer<BASE>::value, "BASE must be a pointer");

			using BaseNaked = typename std::decay<typename std::remove_pointer<BASE>::type>::type;
			using DerivedNaked = typename std::decay<typename std::remove_pointer<DERIVED>::type>::type;
			static_assert(std::is_same< BaseNaked, void >::value || std::is_same< BaseNaked, DerivedNaked >::value ||
				std::is_base_of< BaseNaked, DerivedNaked >::value, "*BASE must be void, the same or a base a base of *DERIVED");

			return down_cast_impl<DERIVED>(i_base_ptr, decltype(can_static_cast_impl<DERIVED, BASE>(0))());
		}
				
		/* FeatureTable<TYPE, FeatureList<FEATURES...> >::s_table is a static array of all the FEATURES::s_value */
		template <typename BASE, typename TYPE, typename FEATURE_LIST> struct FeatureTable;
		template <typename BASE, typename TYPE, typename... FEATURES>
			struct FeatureTable< BASE, TYPE, type_features::FeatureList<FEATURES...> >
		{
			static void * const s_table[sizeof...(FEATURES)];
		};
		template <typename BASE, typename TYPE, typename... FEATURES>
			void * const FeatureTable< BASE, TYPE, type_features::FeatureList<FEATURES...> >::s_table[sizeof...(FEATURES)]
				= { reinterpret_cast<void *>( FEATURES::template Impl<BASE, TYPE>::value )... };

		/* IndexOfFeature<0, TARGET_FEATURE, FeatureList<FEATURES...> >::value is the index of TARGET_FEATURE in FeatureList<FEATURES...>,
			or FeatureList<FEATURES...>::size if TARGET_FEATURE is not present */
		template <size_t START_INDEX, typename TARGET_FEATURE, typename FEATURE_LIST>
			struct IndexOfFeature;
		template <size_t START_INDEX, typename TARGET_FEATURE, typename... OTHER_FEATURES>
			struct IndexOfFeature<START_INDEX, TARGET_FEATURE, type_features::FeatureList<OTHER_FEATURES...> >
		{
			static const size_t value = START_INDEX;
		};
		template <size_t START_INDEX, typename TARGET_FEATURE, typename FIRST_FEATURE, typename... OTHER_FEATURES>
			struct IndexOfFeature<START_INDEX, TARGET_FEATURE, type_features::FeatureList<FIRST_FEATURE, OTHER_FEATURES...> >
		{
			static const size_t value = std::is_same<TARGET_FEATURE, FIRST_FEATURE>::value ?
				START_INDEX : IndexOfFeature<START_INDEX + 1, TARGET_FEATURE, type_features::FeatureList<OTHER_FEATURES...> >::value;
		};

		// CopyConstructImpl<COMPLETE_TYPE>::invoke
		template <typename COMPLETE_TYPE, bool CAN = std::is_copy_constructible<COMPLETE_TYPE>::value> struct CopyConstructImpl;
		template <typename COMPLETE_TYPE> struct CopyConstructImpl<COMPLETE_TYPE, true>
		{
			static void * invoke(void * i_first, void * i_second) DENSITY_NOEXCEPT
			{
				return new (i_first) COMPLETE_TYPE(*static_cast<const COMPLETE_TYPE*>(i_second));
			}
		};
		template <typename COMPLETE_TYPE> struct CopyConstructImpl<COMPLETE_TYPE, false>
		{
			static void * invoke(void *, void *)
			{
				return throw std::exception("copy-construction not supported");
			}
		};

		// MoveConstructImpl<COMPLETE_TYPE>::invoke
		template <typename COMPLETE_TYPE, bool CAN = std::is_nothrow_move_constructible<COMPLETE_TYPE>::value > struct MoveConstructImpl;
		template <typename COMPLETE_TYPE> struct MoveConstructImpl<COMPLETE_TYPE, true>
		{
			static void * invoke(void * i_first, void * i_second) DENSITY_NOEXCEPT
			{
				return new (i_first) COMPLETE_TYPE(std::move(*static_cast<COMPLETE_TYPE*>(i_second)));
			}
		};
		template <typename COMPLETE_TYPE> struct MoveConstructImpl<COMPLETE_TYPE, false>
		{
			static void * invoke(void *, void *)
			{
				throw std::exception("move-construction not supported");
			}
		};

	} // namespace detail

	namespace type_features
	{
		/** This feature stores the size in the table of the type */
		struct Size
		{
			using type = uintptr_t;

			template <typename BASE, typename TYPE> struct Impl
			{
				static_assert(sizeof(TYPE) < std::numeric_limits<uintptr_t>::max() / 4,
					"Type with size >= 1/4 of the address space are not supported");
				// constraining the size of types allows to reduce the runtime checks to detect pointer arithmetic overflow
				static const uintptr_t value = sizeof(TYPE);
			};
		};

		/** This feature stores the alignment in the table of the type */
		struct Alignment
		{
			using type = uintptr_t;

			template <typename BASE, typename TYPE> struct Impl
			{
				static_assert(alignof(TYPE) < std::numeric_limits<uintptr_t>::max() / 4,
					"Type with alignment >= 1/4 of the address space are not supported");
				// constraining the alignment of types allows to reduce the runtime checks to detect pointer arithmetic overflow
				static const uintptr_t value = alignof(TYPE);
			};
		};

		/** This feature computes the hash of an object
			- If a the call hash_func(i_object) is legal, it is used to compute the hash. This function
				should be defined in the namespace that contains TYPE (it will use ADL). If such function exits,
				its return type must be size_t.
			- Otherwise std::hash<TYPE> is used to compute the hash */
		struct Hash
		{
			using type = size_t(*) (const void * i_source);

			template <typename BASE, typename TYPE> struct Impl
			{
				static const uintptr_t value;

				static size_t invoke(const void * i_source)
				{
					auto const base_ptr = static_cast<const BASE*>(i_source);
					return detail::invoke_hash(*detail::down_cast<const TYPE*>(base_ptr));
				}
			};
		};

		template <typename BASE, typename TYPE>
		const uintptr_t Hash::Impl<BASE, TYPE>::value = reinterpret_cast<uintptr_t>(invoke);

		/** This feature stores a pointer to a std::type_info associated to a type in the table of the type */
		struct RTTI
		{
			using type = const std::type_info*;

			template <typename BASE, typename TYPE> struct Impl
			{
				static const uintptr_t value;
			};
		};
		template <typename BASE, typename TYPE>
		const uintptr_t RTTI::Impl<BASE, TYPE>::value = reinterpret_cast<uintptr_t>(&typeid(TYPE));

		/** This feature stores a pointer to the default constructor in the table of the type.
			The effect of the feature is default-constructing an object of type TYPE, and returning a pointer (of type BASE) to it.
				@param i_complete_dest pointer to the storage in which the TYPE must be constructed.
				@return pointer to a subobject (of type BASE) of the new object. */
		struct DefaultConstruct
		{
			using type = void * (*) (void * i_complete_dest);

			template <typename BASE, typename TYPE> struct Impl
			{
				static void * invoke(void * i_complete_dest)
				{
					BASE * const base_result = new(i_complete_dest) TYPE();
					return base_result;
				}

				static const uintptr_t value;
			};
		};
		template <typename TYPE, typename BASE>
		const uintptr_t DefaultConstruct::Impl<TYPE, BASE>::value = reinterpret_cast<uintptr_t>(invoke);

		/** This feature stores a pointer to the copy constructor in the table of the type.
			The effect of the feature is copy-constructing an object of type TYPE, and returning a pointer (of type BASE) to it.
				@param i_complete_dest pointer to the storage in which the TYPE must be constructed.
				@param i_base_source pointer to a subobject (of type BASE) of an object whose complete
					type is TYPE, that is to be used as source of the copy.
				@return pointer to a subobject (of type BASE) of the new object. */
		struct CopyConstruct
		{
			using type = void * (*) (void * i_complete_dest, const void * i_base_source);

			template <typename BASE, typename TYPE> struct Impl
			{
				static void * invoke(void * i_complete_dest, const void * i_base_source)
				{
					auto const base_source = static_cast<const BASE*>(i_base_source);
					// DENSITY_ASSERT( dynamic_cast<const TYPE*>(base_source) != nullptr ); to do: implement a detail::IsInstanceOf
					BASE * const base_result = new(i_complete_dest) TYPE(*detail::down_cast<const TYPE*>(base_source));
					return base_result;
				}

				static const uintptr_t value;
			};
		};
		template <typename TYPE, typename BASE>
		const uintptr_t CopyConstruct::Impl<TYPE, BASE>::value = reinterpret_cast<uintptr_t>(invoke);

		/** This feature stores a pointer to the move constructor in the table of the type.
			The effect of this feature move-constructing an object of type TYPE, and returning a pointer (of type BASE) to it.
				@param i_complete_dest pointer to the storage in which the TYPE must be constructed.
				@param i_base_source pointer to a subobject (of type BASE) of an object whose complete
					type is TYPE, that is to be used as source of the move.
				@return pointer to a subobject (of type BASE) of the new object.
		*/
		struct MoveConstruct
		{
			using type = void * (*)(void * i_dest, void * i_source);

			template <typename BASE, typename TYPE> struct Impl
			{
				/*  */
				static void * invoke(void * i_complete_dest, void * i_base_source) noexcept
				{
					BASE * base_source = static_cast<BASE*>(i_base_source);
					// DENSITY_ASSERT(dynamic_cast<const TYPE*>(base_source) != nullptr); to do: implement a detail::IsInstanceOf
					BASE * base_result = new(i_complete_dest) TYPE(std::move(*detail::down_cast<TYPE*>(base_source)));
					return base_result;
				}
				static const uintptr_t value;
			};
		};
		template <typename TYPE, typename BASE>
		const uintptr_t MoveConstruct::Impl<TYPE, BASE>::value = reinterpret_cast<uintptr_t>(invoke);

		/** This feature invokes a function object. The template parameter must be a callable type. */
		template <typename> struct FeatureInvoke;
		template <typename RET, typename... PARAMS>
		struct FeatureInvoke<RET(PARAMS...)>
		{
			using type = RET(*)(void * i_base_dest, PARAMS... i_params);

			template <typename BASE, typename TYPE> struct Impl
			{
				static RET invoke(void * i_base_dest, PARAMS... i_params)
				{
					const auto base_dest = static_cast<BASE*>(i_base_dest);
					return (*detail::down_cast<TYPE*>(base_dest))(std::forward<PARAMS>(i_params)...);
				}
				static const uintptr_t value;
			};
		};
		template <typename RET, typename... PARAMS>
		template <typename TYPE, typename BASE>
		const uintptr_t FeatureInvoke<RET(PARAMS...)>::Impl<TYPE, BASE>::value = reinterpret_cast<uintptr_t>(invoke);

		/** This feature invokes and destroys a function object. The template parameter must be a callable type. */
		template <typename> struct FeatureInvokeDestroy;
		template <typename RET, typename... PARAMS>
		struct FeatureInvokeDestroy<RET(PARAMS...)>
		{
			using type = RET(*)(void * i_base_dest, PARAMS... i_params);

			template <typename BASE, typename TYPE> struct Impl
			{
				static RET invoke_and_destroy(void * i_base_dest, PARAMS... i_params)
				{
					return invoke_and_destroy_impl(i_base_dest, std::is_void<RET>(), std::forward<PARAMS>(i_params)...);
				}
				static const uintptr_t value;

			private:
				static RET invoke_and_destroy_impl(void * i_base_dest, std::false_type, PARAMS... i_params)
				{
					const auto base_dest = static_cast<BASE*>(i_base_dest);
					auto && result = (*detail::down_cast<TYPE*>(base_dest))(std::forward<PARAMS>(i_params)...);
					detail::down_cast<TYPE*>(base_dest)->TYPE::~TYPE();
					return result;
				}
				static void invoke_and_destroy_impl(void * i_base_dest, std::true_type, PARAMS... i_params)
				{
					const auto base_dest = static_cast<BASE*>(i_base_dest);
					(*detail::down_cast<TYPE*>(base_dest))(std::forward<PARAMS>(i_params)...);
					detail::down_cast<TYPE*>(base_dest)->TYPE::~TYPE();
				}
			};
		};
		template <typename RET, typename... PARAMS>
		template <typename TYPE, typename BASE>
		const uintptr_t FeatureInvokeDestroy<RET(PARAMS...)>::Impl<TYPE, BASE>::value = reinterpret_cast<uintptr_t>(invoke_and_destroy);

		/** This feature stores a pointer to the destructor of a type.
				@param i_base_dest pointer to the object to be destroyed. */
		struct Destroy
		{
			using type = void(*)(void * i_dest);

			template <typename BASE, typename TYPE> struct Impl
			{
				static void invoke(void * i_base_dest) noexcept
				{
					const auto base_dest = static_cast<BASE*>(i_base_dest);
					detail::down_cast<TYPE*>(base_dest)->TYPE::~TYPE();
				}
				static const uintptr_t value;
			};
		};
		template <typename TYPE, typename BASE>
		const uintptr_t Destroy::Impl<TYPE, BASE>::value = reinterpret_cast<uintptr_t>(invoke);

	} // namespace type_features

	namespace detail
	{
		/* GetDefaultFeatures<TYPE>::type. Implementation of default_type_features */
		template <typename TYPE,
			bool CAN_COPY = std::is_copy_constructible<TYPE>::value || std::is_same<void, TYPE>::value,
			bool CAN_MOVE = std::is_nothrow_move_constructible<TYPE>::value || std::is_same<void, TYPE>::value
		> struct GetDefaultFeatures;

		template <typename TYPE> struct GetDefaultFeatures<TYPE, false, false >
		{
			using type = type_features::FeatureList<type_features::Size, type_features::Alignment, type_features::RTTI, type_features::Destroy>;
		};
		template <typename TYPE> struct GetDefaultFeatures<TYPE, true, false >
		{
			using type = type_features::FeatureList<type_features::Size, type_features::Alignment, type_features::RTTI, type_features::Destroy,
				type_features::CopyConstruct>;
		};
		template <typename TYPE> struct GetDefaultFeatures<TYPE, false, true >
		{
			using type = type_features::FeatureList<type_features::Size, type_features::Alignment, type_features::RTTI, type_features::Destroy,
				type_features::MoveConstruct>;
		};
		template <typename TYPE> struct GetDefaultFeatures<TYPE, true, true >
		{
			using type = type_features::FeatureList<type_features::Size, type_features::Alignment, type_features::RTTI, type_features::Destroy,
				type_features::MoveConstruct, type_features::CopyConstruct>;
		};		

	} // namespace detail

	namespace type_features
	{

		/** \class default_type_features
			This type alias template gives a FeatureList for a given type.
				@tparam BASE_TYPE type to use to select the features to include. \n


			- The result FeatureList always includes: Size, Alignment, RTTI, Destroy.\n
			- If BASE_TYPE is copy-constructible, CopyConstruct is included
			- If BASE_TYPE is nothrow move-constructible, MoveConstruct is included
			
			Note: A FeatureList does not depend on a type. The template argument is used only to decide which features to include.
		*/
		#ifndef DOXYGEN_DOC_GENERATION
			template <typename BASE_TYPE>
				using default_type_features = typename detail::GetDefaultFeatures<BASE_TYPE>;
		#else
		template <typename BASE_TYPE>
			struct default_type_features
			{
				using type = default_type_features_t<BASE_TYPE>;
			};
		#endif
		template <typename BASE_TYPE>
			using default_type_features_t = typename default_type_features<BASE_TYPE>::type;
			
	} // namespace type_features

    /** Class template that performs type-erasure.
            @tparam BASE type to which all type-erased types are covariant. If it is void, any type can be type-erased.
    */
    template <typename BASE = void, typename FEATURE_LIST = typename type_features::default_type_features_t<BASE> >
        class runtime_type
    {
    public:

        runtime_type() = default;
        runtime_type(runtime_type && ) DENSITY_NOEXCEPT = default;
        runtime_type(const runtime_type &) DENSITY_NOEXCEPT = default;
		runtime_type & operator = (runtime_type &&) DENSITY_NOEXCEPT = default;
		runtime_type & operator = (const runtime_type &) DENSITY_NOEXCEPT = default;

        template <typename TYPE>
            static runtime_type make()
        {
            return runtime_type(detail::FeatureTable<BASE, TYPE, FEATURE_LIST>::s_table);
        }

        size_t size() const DENSITY_NOEXCEPT
        {
            return get_feature<type_features::Size>();
        }

        size_t alignment() const DENSITY_NOEXCEPT
        {
            return get_feature<type_features::Alignment>();
        }

        void * default_construct(void * i_dest) const
        {
            return get_feature<type_features::DefaultConstruct>()(i_dest);
        }

        void * copy_construct(void * i_dest, const void * i_source) const
        {
            return get_feature<type_features::CopyConstruct>()(i_dest, i_source);
        }

        void * move_construct_nothrow(void * i_dest, void * i_source) const DENSITY_NOEXCEPT
        {
            return get_feature<type_features::MoveConstruct>()(i_dest, i_source);
        }

        void destroy(void * i_dest) const noexcept
        {
            get_feature<type_features::Destroy>()(i_dest);
        }

        const std::type_info & type_info() const noexcept
        {
            return *get_feature<type_features::RTTI>();
        }

		/** Returns the feature matching the specified type, if present. If the feature is not present, a static_assert fails */
        template <typename FEATURE>
            typename FEATURE::type get_feature() const noexcept
        {
            const size_t feature_index = detail::IndexOfFeature<0, FEATURE, FEATURE_LIST>::value;
            static_assert(feature_index < FEATURE_LIST::size, "feature not available in FEATURE_LIST");
            return reinterpret_cast<typename FEATURE::type>(m_table[feature_index]);
        }

    private:
        runtime_type(void * const * i_table) : m_table(i_table) { }

    private:
        void * const * m_table;
    };
}
