// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// An object's items (and a blueprint's start values): item index -> value, kept as a sorted
// vector. It was a std::map; a script reads items all the time (ITEM_VALUE, getItem: 5.6% of a
// 9000-unit battle step went to its two tree lookups, docs/engine/script-profile.md), and an
// object holds a few dozen. Same interface for what the game uses (find, [], at, count, erase,
// iteration) and the same order (by item index), so saves, hashes and walks are unchanged.
// Unlike a map's, a reference or iterator into it is invalidated by adding an item.

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

struct ItemMap {
	using value_type = std::pair<int, float>;
	using container = std::vector<value_type>;
	using iterator = container::iterator;
	using const_iterator = container::const_iterator;

	ItemMap() = default;
	ItemMap(const std::map<int, float>& m) : v(m.begin(), m.end()) {}
	template <typename It> ItemMap(It first, It last) { for (; first != last; ++first) (*this)[first->first] = first->second; }
	operator std::map<int, float>() const { return std::map<int, float>(v.begin(), v.end()); }

	iterator begin() { return v.begin(); }
	iterator end() { return v.end(); }
	const_iterator begin() const { return v.begin(); }
	const_iterator end() const { return v.end(); }
	size_t size() const { return v.size(); }
	bool empty() const { return v.empty(); }
	void clear() { v.clear(); }

	iterator find(int key) {
		auto it = lower(key);
		return it != v.end() && it->first == key ? it : v.end();
	}
	const_iterator find(int key) const {
		auto it = lower(key);
		return it != v.end() && it->first == key ? it : v.end();
	}
	size_t count(int key) const { return find(key) != v.end() ? 1 : 0; }
	bool contains(int key) const { return find(key) != v.end(); }
	float& operator[](int key) {
		auto it = lower(key);
		if (it == v.end() || it->first != key)
			it = v.insert(it, { key, 0.0f });
		return it->second;
	}
	float& at(int key) {
		auto it = find(key);
		if (it == v.end()) throw std::out_of_range("ItemMap::at");
		return it->second;
	}
	const float& at(int key) const {
		auto it = find(key);
		if (it == v.end()) throw std::out_of_range("ItemMap::at");
		return it->second;
	}
	size_t erase(int key) {
		auto it = find(key);
		if (it == v.end()) return 0;
		v.erase(it);
		return 1;
	}
	iterator erase(const_iterator it) { return v.erase(it); }
	std::pair<iterator, bool> insert(const value_type& kv) {
		auto it = lower(kv.first);
		if (it != v.end() && it->first == kv.first) return { it, false };
		return { v.insert(it, kv), true };
	}
	std::pair<iterator, bool> emplace(int key, float value) { return insert({ key, value }); }
	bool operator==(const ItemMap& o) const { return v == o.v; }
	bool operator!=(const ItemMap& o) const { return v != o.v; }

private:
	container v;
	iterator lower(int key) { return std::lower_bound(v.begin(), v.end(), key, [](const value_type& a, int k) { return a.first < k; }); }
	const_iterator lower(int key) const { return std::lower_bound(v.begin(), v.end(), key, [](const value_type& a, int k) { return a.first < k; }); }
};
