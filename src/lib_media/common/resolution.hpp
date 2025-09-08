#pragma once

struct Resolution {
  Resolution(int w = 0, int h = 0)
      : width(w)
      , height(h) {}
  bool operator==(Resolution const &other) const { return width == other.width && height == other.height; }
  bool operator!=(Resolution const &other) const { return !(*this == other); }
  Resolution operator/(int div) const { return Resolution(this->width / div, this->height / div); }
  int width = 0, height = 0;
};
