
#pragma once

template <typename L, typename R>
struct compressed_pair final
{
    L left;
    R right;
};

template <typename L>
struct compressed_pair<L, void> final
{
    L left;
};