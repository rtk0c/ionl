#pragma once

#include <utility>
#include <variant>

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

template <typename TVariant, typename... Ts>
auto VisitVariantOverloaded(TVariant&& v, Ts&&... cases) {
    return std::visit(Overloaded{ std::forward<Ts>(cases)... }, std::forward<TVariant>(v));
}
