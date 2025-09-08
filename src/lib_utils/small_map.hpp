#pragma once

#include <cstddef> // size_t
#include <vector>

// A fast-compiling, low LOC, replacement for std::map.
// Uses linear search for lookup, should be OK for small maps.
// Don't use with a huge element count.
template<typename Key, typename Value>
struct SmallMap {
  struct Pair {
    Key key;
    Value value;
  };

  struct Iterator {
    SmallMap *parent;
    int idx;

    bool operator==(const Iterator &other) const { return idx == other.idx; }

    bool operator!=(const Iterator &other) const { return idx != other.idx; }

    void operator++() { idx++; }

    Pair &operator*() { return parent->pairs[idx]; }
  };

  std::vector<Pair> pairs;

  Value &operator[](Key key) {
    auto i = find(key);
    if(i != end())
      return (*i).value;

    pairs.push_back({key, {}});
    return pairs[pairs.size() - 1].value;
  }

  const Value &operator[](Key key) const { return const_cast<SmallMap<Key, Value> *>(this)->operator[](key); }

  Iterator find(Key key) {
    for(int i = 0; i < (int)pairs.size(); ++i)
      if(key == pairs[i].key)
        return {this, i};

    return end();
  }

  Iterator find(Key key) const {
    for(int i = 0; i < (int)pairs.size(); ++i)
      if(key == pairs[i].key)
        return {const_cast<SmallMap<Key, Value> *>(this), i};

    return end();
  }

  void erase(Iterator i) { pairs.erase(pairs.begin() + i.idx); }

  Iterator begin() const { return {const_cast<SmallMap<Key, Value> *>(this), 0}; }

  Iterator end() const { return {const_cast<SmallMap<Key, Value> *>(this), (int)pairs.size()}; }

  void clear() { pairs.clear(); }

  size_t size() const { return pairs.size(); }
};
