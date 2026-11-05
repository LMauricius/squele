# [Note] C++20 pitfalls
- `static_assert(false)` in a class template fires even if the template is never instantiated (until C++23). Make the condition depend on a template parameter.
- After `std::construct_at` into raw storage, read the object through `std::launder`.
- A concept can name a class template that is only forward-declared, as in `std::same_as<typename T::Query, From<T>>`.
