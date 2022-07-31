#pragma once

#include <variant>

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

template <typename TVariant, typename... Ts>
auto VisitVariantOverloaded(TVariant v, Ts... cases) {
    return std::visit(Overloaded{ cases... }, v);
}
