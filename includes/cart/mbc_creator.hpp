#ifndef GBC_MBC_CREATOR_HPP
#define GBC_MBC_CREATOR_HPP

#pragma once
#include <memory>

// Necessary evil to avoid 10 MBC header files and/or circular dependency :(
struct cart;
class Mbc;

std::unique_ptr<Mbc> make_no_mbc(const cart &c);
std::unique_ptr<Mbc> make_mbc1(const cart &c);
std::unique_ptr<Mbc> make_mbc2(const cart &c);
std::unique_ptr<Mbc> make_mbc3(const cart &c);
std::unique_ptr<Mbc> make_mbc5(const cart &c);
std::unique_ptr<Mbc> make_mbc6(const cart &c);
std::unique_ptr<Mbc> make_mbc7(const cart &c);
std::unique_ptr<Mbc> make_mmm01(const cart &c);
std::unique_ptr<Mbc> make_m161(const cart &c);
std::unique_ptr<Mbc> make_wisdom_tree(const cart& c);
std::unique_ptr<Mbc> make_test_mbc();

#endif //GBC_MBC_CREATOR_HPP
