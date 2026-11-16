#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace recall {
struct MathPoint { double x,y; };
struct MathResult {
    std::wstring value, info, error;
    std::vector<MathPoint> points;
    double xmin=-10,xmax=10,ymin=-10,ymax=10;
};
double calculate(std::wstring_view expression,double x=0);
std::wstring math_number(double value);
MathResult math_query(std::wstring_view expression,bool graph=false);
}
