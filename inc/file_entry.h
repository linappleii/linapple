#pragma once

#include <cstdint>
#include <stdint.h>
#include <string>


struct file_entry_t {
  std::string name;
  enum { UP, DIR, FILE } type;
  std::uintmax_t size;

  bool operator < (const file_entry_t& that) const {
    if (this->type < that.type) return true;
    if (this->type > that.type) return false;
    return strcasecmp(this->name.c_str(), that.name.c_str()) < 0;
  }

  bool is_dir_type() const {
    return type == UP || type == DIR;
  }

  std::string type_or_size_as_string() const {
    if (type_size_cache.size() > 0)
      return type_size_cache;

    return fill_type_size_cache();
  }

  file_entry_t(const std::string& name0, decltype(type) type0, std::uintmax_t size0) :
    name(name0), type(type0), size(size0)
  {}


private:
  mutable std::string type_size_cache;

  std::string fill_type_size_cache() const {
    switch (type) {
    case UP: return type_size_cache = "<UP>";
    case DIR: return type_size_cache = "<DIR>";
    case FILE: {
      std::uintmax_t size = this->size;
      std::string suffix = "";

      if (1000 > size) {
      } else if (1000000 > size) {
        size /= 1000;
        suffix = "K";
      } else if (1000000000 > size) {
        size /= 1000000;
        suffix = "M";
      } else {
        size /= 1000000000;
        suffix = "G";
      }

      return type_size_cache = to_string(size) + suffix;
    }

    default:
      throw "Bug!";
    }
  }
};



// end

