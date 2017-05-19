#include "dmap-common/gnuplot-interface.h"

#include <cmath>
#include <string>
#include <vector>
#include <sstream>  // NOLINT

#include <glog/logging.h>

DEFINE_bool(use_gnuplot, true, "Toggle use of gnuplot.");
DEFINE_bool(save_gnuplot, false,
            "Output plots to a .png instead of the screen.");

namespace dmap_common {

GnuplotInterface::GnuplotInterface(bool persist, const std::string& title)
    : title_(title) {
  static int plot_i = 0;
  if (FLAGS_use_gnuplot) {
    if (FLAGS_save_gnuplot) {
      persist = false;
    }
    pipe_ = popen(persist ? "gnuplot --persist" : "gnuplot", "w");
    if (FLAGS_save_gnuplot) {
      operator<<("set term png");
      operator<<("set output \"" + std::to_string(plot_i++) + ".png\"");
    }
  }
  if (title != "") {
    setTitle(title);
  }
}
GnuplotInterface::GnuplotInterface(const std::string& title)
    : GnuplotInterface(true, title) {}
GnuplotInterface::GnuplotInterface() : GnuplotInterface(true, "") {}

GnuplotInterface::~GnuplotInterface() {
  if (FLAGS_use_gnuplot) {
    fclose(pipe_);
  }
}

void GnuplotInterface::operator<<(const std::string& string) {
  if (FLAGS_use_gnuplot) {
    fputs((string + std::string("\n")).c_str(), pipe_);
    fflush(pipe_);
  }
}

void GnuplotInterface::setXLabel(const std::string& label) {
  operator<<("set xlabel \"" + label + "\"");
}

void GnuplotInterface::setYLabel(const std::string& label) {
  operator<<("set ylabel \"" + label + "\"");
}

void GnuplotInterface::setYLabels(const std::string& label_1,
                                  const std::string& label_2) {
  setYLabel(label_1);
  operator<<("set y2label \"" + label_2 + "\"");
  operator<<("set ytics nomirror");
  operator<<("set y2tics");
}

void GnuplotInterface::setTitle(const std::string& title) {
  operator<<("set title \"" + title + "\"");
}

void GnuplotInterface::setTitle() {
  CHECK_NE(title_, "");
  setTitle(title_);
}

void GnuplotInterface::setLegendPosition(const std::string& position) {
  operator<<("set key " + position);
}

}  // namespace dmap_common
