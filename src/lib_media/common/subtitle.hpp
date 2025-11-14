#pragma once

#include "lib_modules/core/database.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace Modules {

struct Page {
  int64_t showTimestamp = 0;
  int64_t hideTimestamp = 0;

  struct Style {
    std::string color = "#ffffff";
    std::string bgColor = "#000000c2"; // TTML BasicDE: black with an opacity of 76% and a transparency of 24%

    // BasicDE: To leave space between the text in adjacent rows, the font size for
    //          double height is not 200% but 160%. The value of 125% for tts:lineHeight
    //          defines a line-height that is 125% of the computed font-size
    // WebVTT:  ignored.
    bool doubleHeight = false;

    std::string fontFamily = "monospaceSansSerif"; // TTML BasicDE: "Verdana,Arial,Tiresias"
    std::string fontSize = "100%"; // TTML BasicDE: 160%
    std::string lineHeight = "100%"; // TTML BasicDE: 125%
    std::string textAlign = "center";

    void merge(const Style &style) {
      Page::Style defaultStyle;
      if(style.bgColor != defaultStyle.bgColor)
        bgColor = style.bgColor;
      if(style.color != defaultStyle.color)
        color = style.color;
      if(style.doubleHeight != defaultStyle.doubleHeight)
        doubleHeight = style.doubleHeight;
      if(style.fontFamily != defaultStyle.fontFamily)
        fontFamily = style.fontFamily;
      if(style.fontSize != defaultStyle.fontSize)
        fontSize = style.fontSize;
      if(style.lineHeight != defaultStyle.lineHeight)
        lineHeight = style.lineHeight;
      if(style.textAlign != defaultStyle.textAlign)
        textAlign = style.textAlign;
    }
  };

  struct Region {
    // default values inherited from teletext
    int row = 24; // last line
    int col = 0;

    // values inherited from TTML tts:origin and tts:extent and used to compute row and col
    double originX = 0, originY = 0, extentX = 0, extentY = 0;

    std::string displayAlign = "after";
  };

  struct Line {
    std::string text;
    Region region{};
    Style style{};
  };
  std::vector<Line> lines;

  int numCols = 40, numRows = 25;

  std::string toString() const {
    std::string r;

    for(auto &ss : lines) {
      // TODO: add more to trace than text: r += format("row=%s col=%s text:\n", ss.region.row, ss.region.col);
      r += ss.text;
      if(&ss != &lines.back())
        r += "\n";
    }

    return r;
  }
};

struct DataSubtitle : DataBase {
  DataSubtitle(size_t size) {
    if(size > 0)
      throw std::runtime_error("Forbidden operation. DataSubtitle requested size must be 0.");
  }
  std::shared_ptr<DataBase> clone() const override {
    auto dataSub = std::make_shared<DataSubtitle>(0);
    std::shared_ptr<DataBase> clone = dataSub;
    DataBase::clone(this, clone.get());
    dataSub->page = page;
    return clone;
  }
  Page page;
};

}
