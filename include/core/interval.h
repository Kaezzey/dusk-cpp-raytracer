#ifndef INTERVAL_H
#define INTERVAL_H

class interval {
  public:
    double min, max;

    //constructors
    interval() : min(+infinity), max(-infinity) {} 

    //initialise with specific min and max
    interval(double min, double max) : min(min), max(max) {}

    double size() const {
        return max - min;
    }

    bool contains(double x) const {
        return min <= x && x <= max;
    }

    bool surrounds(double x) const {
        return min < x && x < max;
    }

    double clamp(double x) const {
        if (x < min) return min;
        if (x > max) return max;
        return x;
    }

    //static constant intervals
    static const interval empty, universe;
};

//static constant intervals
const interval interval::empty    = interval(+infinity, -infinity);
const interval interval::universe = interval(-infinity, +infinity);

#endif